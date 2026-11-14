// Excitebots: Trick Racing (RX3E01) side of Network Play. Copied into the runtime as
// runtime/src/netplay/ by the build scripts, next to netplay_runtime.cpp. Everything here is idle
// unless the launcher started the game for Network Play (NetplayRuntime::Active()).
//
// Game facts used here (all RX3E01, r13 = 0x805A7FE0, r2 = 0x805ACAE0):
// - Every race runs through 0x802DA160(param): param +0 mode flags, +4 contest id, +0x10 the
//   other players' list. Menu races call it through the thunk 0x802DA830; -direct_to_track runs
//   GameDoEve 0x802DA940. Both are reimplemented below to see each race start and end.
// - Contests: table 0x804DBE70 (0x50 bytes each, count r13-0x6600): +0 id, +4 eve, +0xC kind
//   (0 = race, 0xE = poker race, others are minigames), +0x18 name ("Silver_Finland").
// - Race director: state r13-0x52F8 (1 = before the green light), world time of the green light
//   r13-0x52F4 (float), race clock r13-0x551C. Held at the line by keeping the green light a
//   second ahead of the clock.
// - Racers: table r13-0x531C (0x9C bytes each, count r13-0x5328); +0x08 place. The player's car
//   index is *(r13-0x4D84) + 0x64 (0x80282560(0)). Bots' bodies: array 0x805829C8, one per
//   car index, the same index as the racer table and the score manager: vtable 0x804D3E10, rotation 3x3 at +0x04, position at +0x28, and
//   four limbs at +0x100 + 0xC0*k, each with its own rotation (+0x18) and position (+0x3C).
// - Stars: score manager *(r13-0x5580): per car 0x20 bytes from +0x08, four categories of
//   {int stars, float flash}. The HUD shows the sum.
// - Rank targets: *(r13-0x5750) holds S, A, B, C, D (int) and the passing grade at +0x14.
// - The race loop calls 0x803C6D40 (begin drawing) after the simulation step and before the
//   race is drawn: reimplemented so that other players' bots are posed there.

#include "netplay_runtime.h"

#include "abi_bridge.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace eb_netplay_detail {

constexpr uint32_t kR13 = 0x805A7FE0u;
constexpr uint32_t kR2 = 0x805ACAE0u;
constexpr uint32_t kDeityState = kR13 - 0x52F8;
constexpr uint32_t kGreenTime = kR13 - 0x52F4;
constexpr uint32_t kRaceClock = kR13 - 0x551C;
constexpr uint32_t kRacerTable = kR13 - 0x531C;
constexpr uint32_t kRacerCount = kR13 - 0x5328;
constexpr uint32_t kScoreManager = kR13 - 0x5580;
constexpr uint32_t kGradeSchedule = kR13 - 0x5750;
constexpr uint32_t kContestCount = kR13 - 0x6600;
constexpr uint32_t kContestTable = 0x804DBE70u;
constexpr uint32_t kBodies = 0x805829C8u;
constexpr uint32_t kBodyVtable = 0x804D3E10u;
constexpr uint32_t kRacerStride = 0x9C;

bool IsGuestPointer(uint32_t p) { return p >= 0x80000000u && p < 0x817FF000u && (p & 3) == 0; }
uint32_t R32(uint32_t a) { return Memory::Read32(a); }
float RF(uint32_t a) { return Memory::ReadFloat32(a); }
void WF(uint32_t a, float v) { Memory::WriteFloat32(a, v); }

uint32_t Call(CpuContext* ctx, uint32_t fn) {
    InvokeIndirectCpu(fn, ctx);
    return ctx->gpr[3];
}

std::string ReadString(uint32_t addr, size_t max = 40) {
    std::string s;
    for (size_t i = 0; i < max; ++i) {
        const char c = char(Memory::Read8(addr + uint32_t(i)));
        if (!c) break;
        s += c;
    }
    return s;
}

double NowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---- Race identity -----------------------------------------------------------------------------

struct Pick {
    uint32_t contestId = 0;
    uint32_t kind = 0;
    bool known = false;
    std::string name;   // "Silver_Finland"
};

Pick LookUp(uint32_t contestId) {
    Pick p;
    p.contestId = contestId;
    const int count = int(R32(kContestCount));
    for (int i = 0; i < count && i < 256; ++i) {
        const uint32_t c = kContestTable + uint32_t(i) * 0x50;
        if (R32(c) != contestId) continue;
        p.known = true;
        p.kind = R32(c + 0x0C);
        p.name = ReadString(c + 0x18);
        break;
    }
    return p;
}

std::string KeyOf(const Pick& p) { return "eb/" + (p.name.empty() ? std::to_string(p.contestId) : p.name); }

std::string TitleOf(const Pick& p) {
    std::string t = p.name.empty() ? "Race " + std::to_string(p.contestId) : p.name;
    for (auto& c : t) if (c == '_') c = ' ';
    return t;
}

// Network Play shares the main races only: no poker races, minigames or Excite School.
bool Shareable(const Pick& p) {
    if (!p.known) return true;   // a race the table doesn't list: treat it as a main race
    return p.kind == 0 && p.name.rfind("School", 0) != 0;
}

// ---- State --------------------------------------------------------------------------------------

struct Pose {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float p[3] = {};
};

struct Ghost {
    bool has = false;
    float lastTime = -1;
    double receivedAt = 0;
    netplay::VehicleState state;
};

struct EbState {
    bool inRace = false;   // a shared race is running
    bool holding = false;
    int countedScore = 0;
    bool scoreInit = false;
    bool gridCaptured = false;
    std::array<Pose, netplay::kRaceSlots> grid{};   // racer i's start spot
    uint32_t scaledObject = 0;
    int32_t originalThresholds[5] = {};
    bool finishSent = false;
    uint8_t lastPlace = 0;     // the player's place, kept each frame for the finish report
    float lastClock = 0;
    std::array<Ghost, netplay::kRaceSlots> ghosts{};
    std::array<Pose, netplay::kRaceSlots> lastSent{};
    std::array<double, netplay::kRaceSlots> lastSentAt{};
    int mySlot = 0;
    bool announcePending = false;
    std::string announceKey, announceTitle;
    double announceSince = 0;
} g_eb;

int RacerCount() {
    const int n = int(R32(kRacerCount));
    return IsGuestPointer(R32(kRacerTable)) && n > 0 && n <= netplay::kRaceSlots ? n : 0;
}

uint32_t RacerEntry(int racer) { return R32(kRacerTable) + uint32_t(racer) * kRacerStride; }

// The car the player drives on this PC: local player 0's car index (0x80282560(0)).
int HumanRacer() {
    const uint32_t players = R32(kR13 - 0x4D84);
    if (!IsGuestPointer(players)) return -1;
    const int car = int(R32(players + 0x64));
    return car >= 0 && car < RacerCount() ? car : -1;
}

uint32_t RacerBody(int racer) {
    if (racer < 0 || racer >= RacerCount()) return 0;
    const uint32_t body = R32(kBodies + 4 * uint32_t(racer));
    return IsGuestPointer(body) && R32(body) == kBodyVtable ? body : 0;
}

// Race slots map onto racers the same way on every PC: slot k is the k-th racer in the order
// "player first, then the CPUs in racer order". My slot takes the player's racer here.
int RacerForSlot(int slot) {
    const int human = HumanRacer();
    const int n = RacerCount();
    if (human < 0 || slot < 0 || slot >= n) return -1;
    std::array<int, netplay::kRaceSlots> order{};
    int k = 0;
    order[k++] = human;
    for (int r = 0; r < n; ++r) if (r != human) order[k++] = r;
    // As in Excite Truck: my slot is the player, the slots before mine shift up by one.
    const int idx = slot == g_eb.mySlot ? 0 : slot < g_eb.mySlot ? slot + 1 : slot;
    return idx < n ? order[idx] : -1;
}

// The racer the starter's PC has in this slot (where everyone in that slot starts).
int GridRacerForSlot(int slot) {
    const int human = HumanRacer();
    const int n = RacerCount();
    if (human < 0 || slot < 0 || slot >= n) return -1;
    if (slot == 0) return human;
    int k = 0;
    for (int r = 0; r < n; ++r) {
        if (r == human) continue;
        if (++k == slot) return r;
    }
    return -1;
}

Pose ReadPose(uint32_t body) {
    Pose p;
    for (int i = 0; i < 9; ++i) p.m[i] = RF(body + 0x04 + 4 * i);
    for (int i = 0; i < 3; ++i) p.p[i] = RF(body + 0x28 + 4 * i);
    return p;
}

void MatrixToQuat(const float* m, float* q) {
    const float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q[3] = 0.25f * s; q[0] = (m[7] - m[5]) / s; q[1] = (m[2] - m[6]) / s; q[2] = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2;
        q[3] = (m[7] - m[5]) / s; q[0] = 0.25f * s; q[1] = (m[1] + m[3]) / s; q[2] = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2;
        q[3] = (m[2] - m[6]) / s; q[0] = (m[1] + m[3]) / s; q[1] = 0.25f * s; q[2] = (m[5] + m[7]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2;
        q[3] = (m[3] - m[1]) / s; q[0] = (m[2] + m[6]) / s; q[1] = (m[5] + m[7]) / s; q[2] = 0.25f * s;
    }
}

void QuatToMatrix(const float* qin, float* m) {
    float q[4] = {qin[0], qin[1], qin[2], qin[3]};
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-6f) { const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; std::memcpy(m, id, sizeof id); return; }
    for (auto& v : q) v /= n;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - z * w);     m[2] = 2 * (x * z + y * w);
    m[3] = 2 * (x * y + z * w);     m[4] = 1 - 2 * (x * x + z * z); m[5] = 2 * (y * z - x * w);
    m[6] = 2 * (x * z - y * w);     m[7] = 2 * (y * z + x * w);     m[8] = 1 - 2 * (x * x + y * y);
}

// Moves a bot: the body, and its four limbs with it (rotation and position both follow the
// change from the body's current pose to the target: X' = T * L^-1 * X).
void WritePose(uint32_t body, const Pose& target) {
    const Pose local = ReadPose(body);
    for (int k = 0; k < 4; ++k) {
        const uint32_t limb = body + 0x100 + 0xC0 * uint32_t(k);
        float rot[9], d[3], r[3];
        for (int i = 0; i < 9; ++i) rot[i] = RF(limb + 0x18 + 4 * i);
        for (int i = 0; i < 3; ++i) d[i] = RF(limb + 0x3C + 4 * i) - local.p[i];
        // Position: r = Lt * d, p' = T * r + pt.
        for (int i = 0; i < 3; ++i) r[i] = local.m[0 + i] * d[0] + local.m[3 + i] * d[1] + local.m[6 + i] * d[2];
        for (int i = 0; i < 3; ++i) {
            WF(limb + 0x3C + 4 * i, target.m[3 * i] * r[0] + target.m[3 * i + 1] * r[1] + target.m[3 * i + 2] * r[2] + target.p[i]);
        }
        // Rotation: R' = T * Lt * R.
        float a[9];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                float s = 0;
                for (int t = 0; t < 3; ++t) s += local.m[3 * t + i] * rot[3 * t + j];
                a[3 * i + j] = s;
            }
        }
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                float s = 0;
                for (int t = 0; t < 3; ++t) s += target.m[3 * i + t] * a[3 * t + j];
                WF(limb + 0x18 + 4 * uint32_t(3 * i + j), s);
            }
        }
    }
    for (int i = 0; i < 9; ++i) WF(body + 0x04 + 4 * i, target.m[i]);
    for (int i = 0; i < 3; ++i) WF(body + 0x28 + 4 * i, target.p[i]);
}

// Pose another player's bot (or a CPU run on the starter's PC) where their PC says it is.
// Called after the simulation step and before drawing.
void ApplyGhosts() {
    if (!g_eb.inRace) return;
    // Everyone starts on their slot's spot: a joiner moves to the spot the starter's PC gives
    // that slot.
    if (g_eb.gridCaptured && g_eb.mySlot > 0 && R32(kDeityState) == 1) {
        const int spot = GridRacerForSlot(g_eb.mySlot);
        if (const uint32_t me = RacerBody(HumanRacer()); me && spot >= 0) WritePose(me, g_eb.grid[spot]);
    }
    const double now = NowSeconds();
    for (int slot = 0; slot < netplay::kRaceSlots; ++slot) {
        const Ghost& gh = g_eb.ghosts[slot];
        if (!gh.has) continue;
        const uint32_t body = RacerBody(RacerForSlot(slot));
        if (!body) continue;
        const float ahead = float(std::clamp(now - gh.receivedAt, 0.0, 0.25));
        Pose target;
        QuatToMatrix(gh.state.rot, target.m);
        for (int i = 0; i < 3; ++i) target.p[i] = gh.state.pos[i] + gh.state.vel[i] * ahead;
        WritePose(body, target);
    }
}

// The player's stars: score slot +0x08, four categories {int, float}.
uint32_t ScoreBlock() {
    const uint32_t mgr = R32(kScoreManager);
    const int human = HumanRacer();
    if (!IsGuestPointer(mgr) || human < 0) return 0;
    return mgr + uint32_t(human) * 0x20;
}

int ReadScore(uint32_t block) {
    int s = 0;
    for (int k = 0; k < 4; ++k) s += int(R32(block + 0x08 + 8 * uint32_t(k)));
    return s;
}

void RestoreThresholds() {
    if (g_eb.scaledObject && R32(kGradeSchedule) == g_eb.scaledObject) {
        for (int i = 0; i < 5; ++i) Memory::Write32(g_eb.scaledObject + 4 * i, uint32_t(g_eb.originalThresholds[i]));
    }
    g_eb.scaledObject = 0;
}

void ResetRaceState() {
    RestoreThresholds();
    g_eb.holding = false;
    g_eb.scoreInit = false;
    g_eb.finishSent = false;
    g_eb.lastPlace = 0;
    g_eb.ghosts = {};
    g_eb.gridCaptured = false;
    g_eb.lastSentAt = {};
}

// ---- Bridge -------------------------------------------------------------------------------------

struct EbBridge final : NetplayRuntime::Bridge {
    bool CanJoinNow() override { return !g_eb.inRace; }
    bool CupUnlocked(const std::string&) override { return true; }
    bool JoinsByView() override { return false; }
    void BeginRace(const std::string&) override {}
    void CurrentVehicle(uint32_t& vehicle, uint32_t& variant) override {
        vehicle = 0;
        variant = 0;
    }
    void RaceFrame(netplay::Session& s) override {
        if (!g_eb.inRace || RacerCount() == 0) return;
        const netplay::RaceView& race = s.Race();
        for (const auto& m : race.members) if (m.member == s.MyId()) g_eb.mySlot = m.slot;
        if (!g_eb.gridCaptured && R32(kDeityState) == 1) {
            const int n = RacerCount();
            bool all = true;
            for (int r = 0; r < n && all; ++r) {
                const uint32_t b = RacerBody(r);
                all = b != 0;
                if (b) g_eb.grid[r] = ReadPose(b);
            }
            g_eb.gridCaptured = all;
        }

        // Rank targets x players, once the field is final.
        if (race.phase >= netplay::RaceView::AtLine && !g_eb.scaledObject && race.Players() > 1) {
            const uint32_t obj = R32(kGradeSchedule);
            if (IsGuestPointer(obj)) {
                for (int i = 0; i < 5; ++i) {
                    const int32_t v = int32_t(R32(obj + 4 * i));
                    g_eb.originalThresholds[i] = v;
                    if (v > 0) Memory::Write32(obj + 4 * i, uint32_t(netplay::ScaleRequirement(v, race.Players())));
                }
                g_eb.scaledObject = obj;
            }
        }

        // My stars go to the others; theirs are added to my count and shown.
        if (const uint32_t block = ScoreBlock()) {
            // Testing: WC_NET_TEST_STARS=1 gives the player a star every 10 seconds of racing.
            static const bool testStars = std::getenv("WC_NET_TEST_STARS") != nullptr;
            static double nextTestStar = 0;
            if (testStars && race.phase >= netplay::RaceView::Racing && NowSeconds() > nextTestStar) {
                if (nextTestStar > 0) Memory::Write32(block + 0x08, R32(block + 0x08) + 1);
                nextTestStar = NowSeconds() + 10.0;
            }
            const int score = ReadScore(block);
            if (!g_eb.scoreInit || score < g_eb.countedScore) {
                g_eb.countedScore = score;
                g_eb.scoreInit = true;
            }
            const int earned = score - g_eb.countedScore;
            if (earned >= 1 && race.phase >= netplay::RaceView::Racing) {
                s.EarnedStars(earned, 0, "");
                g_eb.countedScore += earned;
                std::fprintf(stderr, "[netplay] earned %d stars (score %d)\n", earned, score);
            }
            netplay::StarEvent ev;
            while (s.PollStars(ev)) {
                if (ev.amount <= 0 || ev.member == s.MyId()) continue;
                Memory::Write32(block + 0x08, R32(block + 0x08) + uint32_t(ev.amount));
                WF(block + 0x0C, RF(kR2 - 0x6690));   // the flash the game starts on a score change
                g_eb.countedScore += ev.amount;
                std::fprintf(stderr, "[netplay] +%d stars from %s (score %d)\n", ev.amount, ev.name.c_str(), ReadScore(block));
                NetplayRuntime::ShowToast("+" + std::to_string(ev.amount) + (ev.amount == 1 ? " star  " : " stars  ") + ev.name,
                                          s.ColorOf(ev.member));
            }
        }

        // Place and race time, reported when the race ends (the racer table is gone by then).
        if (const int human = HumanRacer(); human >= 0 && race.phase == netplay::RaceView::Racing) {
            const uint32_t place = R32(RacerEntry(human) + 0x08);
            if (place >= 1 && place <= 6) g_eb.lastPlace = uint8_t(place);
            g_eb.lastClock = RF(kRaceClock);
        }

        // Vehicle states: mine, plus the CPUs when I started the race.
        if (race.phase >= netplay::RaceView::AtLine) {
            std::vector<netplay::VehicleState> out;
            const double now = NowSeconds();
            for (int slot = 0; slot < netplay::kRaceSlots; ++slot) {
                const bool mine = s.SlotIsMine(uint8_t(slot));
                bool cpu = s.RunsCpus();
                for (const auto& m : race.members) cpu &= m.slot != slot;
                if (!mine && !cpu) continue;
                const uint32_t body = RacerBody(RacerForSlot(slot));
                if (!body) continue;
                const Pose p = ReadPose(body);
                netplay::VehicleState st;
                st.slot = uint8_t(slot);
                st.time = float(now);
                std::memcpy(st.pos, p.p, sizeof st.pos);
                MatrixToQuat(p.m, st.rot);
                const double dt = now - g_eb.lastSentAt[slot];
                if (g_eb.lastSentAt[slot] > 0 && dt > 0.001 && dt < 1.0) {
                    for (int i = 0; i < 3; ++i) st.vel[i] = float((p.p[i] - g_eb.lastSent[slot].p[i]) / dt);
                }
                out.push_back(st);
            }
            s.SendStates(out);
            for (const auto& st : out) {
                g_eb.lastSent[st.slot] = ReadPose(RacerBody(RacerForSlot(st.slot)));
                g_eb.lastSentAt[st.slot] = now;
            }
            static const bool debug = std::getenv("WC_NET_DEBUG") != nullptr;
            static double lastLog = 0;
            if (debug && now - lastLog > 2.0) {
                lastLog = now;
                const int human = HumanRacer();
                for (int r = 0; r < RacerCount(); ++r) {
                    if (const uint32_t b = RacerBody(r)) {
                        const Pose p = ReadPose(b);
                        std::fprintf(stderr, "[netplay] racer %d%s at %.1f %.1f %.1f place %u\n", r, r == human ? " (me)" : "",
                                     p.p[0], p.p[1], p.p[2], R32(RacerEntry(r) + 0x08));
                    }
                }
            }
            for (int slot = 0; slot < netplay::kRaceSlots; ++slot) {
                Ghost& gh = g_eb.ghosts[slot];
                const auto& remote = s.Remote(uint8_t(slot));
                if (!s.SlotIsRemote(uint8_t(slot)) || !remote) { gh.has = false; continue; }
                if (!gh.has || remote->time != gh.lastTime) {
                    gh.state = *remote;
                    gh.lastTime = remote->time;
                    gh.receivedAt = now;
                    gh.has = true;
                }
            }
        }
    }
};

EbBridge g_bridge;

bool Registered() {
    static const bool done = [] {
        NetplayRuntime::Register(&g_bridge);
        return true;
    }();
    return done;
}

bool InRoom() {
    netplay::Session* s = NetplayRuntime::Session();
    return s && s->GetStatus() == netplay::Session::Status::InRoom;
}

void AnnounceIfReady() {
    if (!g_eb.announcePending || !InRoom()) return;
    g_eb.announcePending = false;
    NetplayRuntime::LocalRaceChosen(g_eb.announceKey, g_eb.announceTitle, "");
}

void RaceStarting(uint32_t param) {
    if (!NetplayRuntime::Active()) return;
    Registered();
    const Pick pick = LookUp(R32(param + 4));
    std::fprintf(stderr, "[netplay] race start id %u (%s) kind %u flags %x%s\n", pick.contestId, pick.name.c_str(), pick.kind,
                 R32(param), Shareable(pick) ? "" : " - not shared");
    if (!Shareable(pick)) return;
    ResetRaceState();
    g_eb.inRace = true;
    g_eb.announceKey = KeyOf(pick);
    g_eb.announceTitle = TitleOf(pick);
    g_eb.announcePending = true;
    g_eb.announceSince = NowSeconds();
    AnnounceIfReady();
}

void RaceEnded() {
    if (!g_eb.inRace) return;
    g_eb.inRace = false;
    g_eb.announcePending = false;
    if (netplay::Session* s = NetplayRuntime::Session(); s && g_eb.lastPlace && !g_eb.finishSent) {
        s->Finished(g_eb.lastPlace, g_eb.lastClock);
        g_eb.finishSent = true;
    }
    ResetRaceState();
    NetplayRuntime::LocalRaceEnded();
}

} // namespace eb_netplay_detail

namespace EbNetplay {
using namespace eb_netplay_detail;

// Once a presented frame, after NetplayRuntime::Frame(): keeps the countdown at the start line
// until everyone in the race is there, and reports the finish.
void Tick() {
    if (!NetplayRuntime::Active()) return;
    Registered();
    if (!g_eb.inRace) return;
    const bool beforeGreen = R32(kDeityState) == 1 && RacerCount() > 0;
    static bool lastBefore = false;
    if (beforeGreen != lastBefore) {
        lastBefore = beforeGreen;
        std::fprintf(stderr, "[netplay] before green: %d (green %.2f, clock %.2f)\n", int(beforeGreen), RF(kGreenTime), RF(kRaceClock));
    }
    if (!beforeGreen) {
        g_eb.holding = false;
        return;
    }
    AnnounceIfReady();
    const bool connecting = g_eb.announcePending && NowSeconds() - g_eb.announceSince < 20.0;
    if (connecting || NetplayRuntime::HoldAtLine()) {
        g_eb.holding = true;
        const float later = RF(kRaceClock) + 1.0f;
        if (RF(kGreenTime) < later) WF(kGreenTime, later);
    } else {
        g_eb.holding = false;
    }
}

} // namespace EbNetplay

namespace eb_netplay_detail {

// Thunk to the race runner used by the menus (0x802DA830: b 0x802DA160).
extern "C" void EB_RaceThunk_802da830(CpuContext* ctx)
{
    const uint32_t param = ctx->gpr[3];
    RaceStarting(param);
    ctx->gpr[3] = param;
    Call(ctx, 0x802DA160u);
    RaceEnded();
}
PPC_NATIVE_OVERRIDE_VOID(8E2DA830, EB_RaceThunk_802da830, (CpuContext* ctx), (ctx));

// GameDoEve (0x802DA940, -direct_to_track): runs contest r4 of eve r3, as the game has it
// without its log line.
extern "C" void EB_GameDoEve_802da940(CpuContext* ctx)
{
    const uint32_t eve = ctx->gpr[3], contest = ctx->gpr[4];
    const uint32_t oldSp = ctx->gpr[1];
    const uint32_t sp = oldSp - 0x1D0;
    Memory::Write32(sp, oldSp);
    ctx->gpr[1] = sp;

    const uint32_t param = sp + 8;
    for (uint32_t i = 0; i < 0x14; i += 4) Memory::Write32(param + i, 0);
    ctx->gpr[3] = eve;
    ctx->gpr[4] = contest;
    const uint32_t c = Call(ctx, 0x800193E0u);
    Memory::Write32(param + 4, R32(c));
    Memory::Write32(param + 8, 1);
    const int32_t players = int32_t(R32(kR13 - 0x6B30));
    if (players > 1) {
        const uint32_t list = sp + 0x1C;
        Memory::Write32(list + 0x1A0, uint32_t(players));
        for (int32_t i = 0; i < players; ++i) {
            Call(ctx, 0x80016F50u);
            const uint32_t who = Call(ctx, 0x800174D0u);
            ctx->gpr[3] = 0;
            const uint32_t r = Call(ctx, 0x8000EB90u);
            ctx->gpr[3] = list + 0x34 * uint32_t(i);
            ctx->gpr[4] = r;
            ctx->gpr[5] = 0;
            ctx->gpr[6] = who;
            ctx->gpr[7] = uint32_t(i);
            Call(ctx, 0x80375730u);
        }
        Memory::Write32(param + 0x10, list);
    }
    RaceStarting(param);
    ctx->gpr[3] = param;
    Call(ctx, 0x802DA160u);
    RaceEnded();
    ctx->gpr[1] = oldSp;
}
PPC_NATIVE_OVERRIDE_VOID(8E2DA940, EB_GameDoEve_802da940, (CpuContext* ctx), (ctx));

// Begin drawing (0x803C6D40), called by the race loop after the simulation step: other players'
// bots are posed first.
extern "C" void EB_BeginDraw_803c6d40(CpuContext* ctx)
{
    ApplyGhosts();
    const uint32_t oldSp = ctx->gpr[1];
    const uint32_t sp = oldSp - 0x10;
    Memory::Write32(sp, oldSp);
    ctx->gpr[1] = sp;
    Call(ctx, 0x803E6EA0u);
    Memory::Write32(kR13 - 0x3AC4, 1);
    Call(ctx, 0x803CD070u);
    Call(ctx, 0x803CF720u);
    ctx->gpr[1] = oldSp;
}
PPC_NATIVE_OVERRIDE_VOID(8E3C6D40, EB_BeginDraw_803c6d40, (CpuContext* ctx), (ctx));

} // namespace eb_netplay_detail
