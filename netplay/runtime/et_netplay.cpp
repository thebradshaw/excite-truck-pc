// Excite Truck (REXE01) side of Network Play. Copied into the runtime as runtime/src/netplay/ by
// the build scripts, next to netplay_runtime.cpp. Everything here is idle unless the launcher
// started the game for Network Play (NetplayRuntime::Active()).
//
// Game facts used here (all REXE01, r13 = 0x80560900):
// - Excite Race runs each main race through 0x8027E024(buf, contest id, extra) -> 0x8023B9EC(eve,
//   contest index, buf, extra, 0); -direct_to_track races come through 0x8023BEB8. Both are
//   reimplemented below to see each race start and end. A race is known by its contest id.
// - Race director ("deity"): state r13-0x2258 (1 = before the green light), countdown clock
//   r13-0x2254 (float, world time of the green light; the world clock is r13-0x2538). Held at
//   the line by keeping the green light a second ahead of the world clock.
// - Racers: table r13-0x227C (0x80 bytes each, count r13-0x2288); entry +0x5C -> object whose
//   +0x26C is the car's scene object + 0x540. Racer 0 is the player.
// - Car scene object: rotation 3x3 at +0x04, position at +0x28 (a copy of both at +0x4A0),
//   four wheel positions at +0x2BBC/+0x2C7C/+0x2D3C/+0x2DFC.
// - Race render 0x8000B2AC (called once a frame by the race loop after the simulation step):
//   reimplemented so that other players' trucks are posed after physics and before drawing.
// - Stars: score manager *(r13-0x2698); per-car entries at +0x9E4, 0xC0 apart, count at +0xFE4;
//   entry +0x04 = 1 for the player, +0x14 and +0x18 = stars (float).
// - Rank targets: *(r13-0x2644) holds the race's star thresholds S, A, B, C, D (int) and the
//   passing grade at +0x14. Network Play multiplies the thresholds by the players in the race.
// - Results: Awards object *(0x8055E884): +0x2A8 place (0 until pushed), +0x2B8 race time.

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

namespace et_netplay_detail {

constexpr uint32_t kR13 = 0x80560900u;
constexpr uint32_t kDeityState = kR13 - 0x2258;
constexpr uint32_t kDeityClock = kR13 - 0x2254;   // world time of the green flag
constexpr uint32_t kWorldTime = kR13 - 0x2538;
constexpr uint32_t kRacerTable = kR13 - 0x227C;
constexpr uint32_t kRacerCount = kR13 - 0x2288;
constexpr uint32_t kScoreManager = kR13 - 0x2698;
constexpr uint32_t kGradeSchedule = kR13 - 0x2644;
constexpr uint32_t kAwardsPointer = 0x8055E884u;

constexpr uint32_t kWheelOffsets[4] = {0x2BBC, 0x2C7C, 0x2D3C, 0x2DFC};

bool IsGuestPointer(uint32_t p) { return p >= 0x80000000u && p < 0x817FF000u && (p & 3) == 0; }
uint32_t R32(uint32_t a) { return Memory::Read32(a); }
float RF(uint32_t a) { return Memory::ReadFloat32(a); }
void WF(uint32_t a, float v) { Memory::WriteFloat32(a, v); }

uint32_t Call(CpuContext* ctx, uint32_t fn) {
    InvokeIndirectCpu(fn, ctx);
    return ctx->gpr[3];
}

std::string ReadString(uint32_t addr, size_t max = 32) {
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
    uint32_t contestId = 0;   // the contest's id (contest +0), the same on every copy of the disc
    int32_t eve = -1, contest = 0;
};

std::string KeyOf(const Pick& p) { return "et/" + std::to_string(p.contestId); }

std::string TitleOf(CpuContext* ctx, const Pick& p) {
    std::string name;
    if (ctx && p.eve >= 0) {
        const uint32_t saved3 = ctx->gpr[3];
        ctx->gpr[3] = uint32_t(p.eve);
        const uint32_t eve = Call(ctx, 0x80012820u);
        ctx->gpr[3] = saved3;
        if (IsGuestPointer(eve)) name = ReadString(eve + 8);
    }
    if (name.empty()) name = "Race";
    for (auto& c : name) if (c == '_') c = ' ';
    name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
    return name + " " + std::to_string(p.contest + 1);
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

struct EtState {
    bool inRace = false;         // between race start and race end
    uint32_t truck = 0, paint = 0;
    // Start-line hold.
    bool holding = false;
    // Stars.
    float countedScore = 0;
    bool scoreInit = false;
    // Start grid, as loaded (racer i's spot).
    bool gridCaptured = false;
    std::array<Pose, netplay::kRaceSlots> grid{};
    // Rank targets.
    uint32_t scaledObject = 0;
    int32_t originalThresholds[5] = {};
    // Finish.
    bool finishSent = false;
    // States.
    std::array<Ghost, netplay::kRaceSlots> ghosts{};
    std::array<Pose, netplay::kRaceSlots> lastSent{};
    std::array<double, netplay::kRaceSlots> lastSentAt{};
    int mySlot = 0;
    // A race picked before the room was joined (a -direct_to_track start) is shared once it is.
    bool announcePending = false;
    std::string announceKey, announceTitle;
    double announceSince = 0;
} g_et;

uint32_t RacerScene(int racer) {
    const uint32_t table = R32(kRacerTable);
    const int count = int(R32(kRacerCount));
    if (!IsGuestPointer(table) || racer < 0 || racer >= count || count > 8) return 0;
    const uint32_t obj = R32(table + uint32_t(racer) * 0x80 + 0x5C);
    if (!IsGuestPointer(obj)) return 0;
    const uint32_t link = R32(obj + 0x26C);
    if (!IsGuestPointer(link)) return 0;
    const uint32_t scene = link - 0x540;
    return R32(scene) == 0x8033A300u ? scene : 0;   // car scene object vtable
}

// Racer on this PC for a race slot: my slot is racer 0, the other slots fill racers 1..5 in order.
int RacerForSlot(int slot) {
    if (slot == g_et.mySlot) return 0;
    return slot < g_et.mySlot ? slot + 1 : slot;
}

Pose ReadPose(uint32_t scene) {
    Pose p;
    for (int i = 0; i < 9; ++i) p.m[i] = RF(scene + 0x04 + 4 * i);
    for (int i = 0; i < 3; ++i) p.p[i] = RF(scene + 0x28 + 4 * i);
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

// Moves a car: body pose, and the wheels with it (w' = Rt * Rl^T * (w - pl) + pt).
void WritePose(uint32_t scene, const Pose& target) {
    const Pose local = ReadPose(scene);
    for (uint32_t off : kWheelOffsets) {
        float d[3], r[3], o[3];
        for (int i = 0; i < 3; ++i) d[i] = RF(scene + off + 4 * i) - local.p[i];
        for (int i = 0; i < 3; ++i) r[i] = local.m[0 + i] * d[0] + local.m[3 + i] * d[1] + local.m[6 + i] * d[2];
        for (int i = 0; i < 3; ++i) o[i] = target.m[3 * i] * r[0] + target.m[3 * i + 1] * r[1] + target.m[3 * i + 2] * r[2];
        for (int i = 0; i < 3; ++i) WF(scene + off + 4 * i, o[i] + target.p[i]);
    }
    for (uint32_t base : {0x04u, 0x4A0u}) {
        for (int i = 0; i < 9; ++i) WF(scene + base + 4 * i, target.m[i]);
        for (int i = 0; i < 3; ++i) WF(scene + base + 0x24 + 4 * i, target.p[i]);
    }
}

// Name tags: the race camera keeps its axes at 0x804BE0B0 (three rows: screen x, up, forward) and
// its position at 0x804BE0D4. The race projection's vertical focal length is about 1.6.
constexpr uint32_t kCameraPos = 0x804BE0D4u;

float TagFocal() {
    static const float f = [] {
        const char* e = std::getenv("WC_NET_TAG_FOCAL");
        return e && *e ? float(std::atof(e)) : 1.6f;
    }();
    return f;
}

void UpdateTag(int slot, const float* pos, bool show) {
    if (!show) {
        NetplayRuntime::SetNameTag(uint8_t(slot), 0, 0, 1, false);
        return;
    }
    float axes[9], cam[3], d[3];
    for (int i = 0; i < 9; ++i) axes[i] = RF(kCameraPos - 0x24 + 4 * i);
    for (int i = 0; i < 3; ++i) cam[i] = RF(kCameraPos + 4 * i);
    for (int i = 0; i < 3; ++i) d[i] = pos[i] - cam[i];
    d[1] += 2.5f;   // above the truck
    const float x = axes[0] * d[0] + axes[1] * d[1] + axes[2] * d[2];
    const float y = axes[3] * d[0] + axes[4] * d[1] + axes[5] * d[2];
    const float depth = axes[6] * d[0] + axes[7] * d[1] + axes[8] * d[2];
    const bool visible = depth > 2.0f && depth < 400.0f;
    NetplayRuntime::SetNameTag(uint8_t(slot), visible ? x / depth : 0, visible ? y / depth : 0, TagFocal(), visible);
}

// Pose another player's truck (or a CPU run on the starter's PC) where their PC says it is.
// Called after the simulation step and before drawing, so physics never sees it.
void ApplyGhosts() {
    if (!g_et.inRace) return;
    // Everyone starts on their own slot's grid spot: on every PC the player is racer 0, which
    // sits on the pole, so a joiner moves to the spot of the racer their slot pushed off it.
    if (g_et.gridCaptured && g_et.mySlot > 0 && g_et.mySlot < netplay::kRaceSlots && R32(kDeityState) == 1) {
        if (const uint32_t me = RacerScene(0)) WritePose(me, g_et.grid[g_et.mySlot]);
    }
    const double now = NowSeconds();
    for (int slot = 0; slot < netplay::kRaceSlots; ++slot) {
        const Ghost& gh = g_et.ghosts[slot];
        const uint32_t scene = gh.has ? RacerScene(RacerForSlot(slot)) : 0;
        if (!scene) {
            UpdateTag(slot, nullptr, false);
            continue;
        }
        const float ahead = float(std::clamp(now - gh.receivedAt, 0.0, 0.25));
        Pose target;
        QuatToMatrix(gh.state.rot, target.m);
        for (int i = 0; i < 3; ++i) target.p[i] = gh.state.pos[i] + gh.state.vel[i] * ahead;
        WritePose(scene, target);
        UpdateTag(slot, target.p, true);
    }
}

uint32_t ScoreEntry(bool player, int index = 0) {
    const uint32_t mgr = R32(kScoreManager);
    if (!IsGuestPointer(mgr)) return 0;
    const int count = int(R32(mgr + 0xFE4));
    if (count <= 0 || count > 8) return 0;
    for (int i = 0; i < count; ++i) {
        const uint32_t e = mgr + 0x9E4 + uint32_t(i) * 0xC0;
        if (player ? R32(e + 4) == 1 : i == index) return e;
    }
    return 0;
}

void RestoreThresholds() {
    if (g_et.scaledObject && R32(kGradeSchedule) == g_et.scaledObject) {
        for (int i = 0; i < 5; ++i) Memory::Write32(g_et.scaledObject + 4 * i, uint32_t(g_et.originalThresholds[i]));
    }
    g_et.scaledObject = 0;
}

void ResetRaceState() {
    RestoreThresholds();
    g_et.holding = false;
    g_et.scoreInit = false;
    g_et.finishSent = false;
    g_et.ghosts = {};
    g_et.gridCaptured = false;
    g_et.lastSentAt = {};
}

// ---- Bridge -------------------------------------------------------------------------------------

struct EtBridge final : NetplayRuntime::Bridge {
    bool CanJoinNow() override { return !g_et.inRace; }
    bool CupUnlocked(const std::string&) override {
        // The course menu only offers what the save has unlocked; joining an invite skips it,
        // so the save check belongs here. Until the save's unlock table is mapped, allow it.
        return true;
    }
    // Joining is done by picking the same race in Excite Race while the invite is open.
    bool JoinsByView() override { return false; }
    void BeginRace(const std::string&) override {}
    void CurrentVehicle(uint32_t& vehicle, uint32_t& variant) override {
        vehicle = g_et.truck;
        variant = g_et.paint;
    }
    void RaceFrame(netplay::Session& s) override {
        if (!g_et.inRace) return;
        const netplay::RaceView& race = s.Race();
        for (const auto& m : race.members) if (m.member == s.MyId()) g_et.mySlot = m.slot;
        if (!g_et.gridCaptured && R32(kDeityState) == 1) {
            bool all = true;
            for (int r = 0; r < netplay::kRaceSlots && all; ++r) {
                const uint32_t sc = RacerScene(r);
                all = sc != 0;
                if (sc) g_et.grid[r] = ReadPose(sc);
            }
            g_et.gridCaptured = all;
        }

        // Rank targets x players, once the field is final.
        if (race.phase >= netplay::RaceView::AtLine && !g_et.scaledObject && race.Players() > 1) {
            const uint32_t obj = R32(kGradeSchedule);
            if (IsGuestPointer(obj)) {
                for (int i = 0; i < 5; ++i) {
                    const int32_t v = int32_t(R32(obj + 4 * i));
                    g_et.originalThresholds[i] = v;
                    if (v > 0) Memory::Write32(obj + 4 * i, uint32_t(netplay::ScaleRequirement(v, race.Players())));
                }
                g_et.scaledObject = obj;
            }
        }

        // My stars go to the others; theirs are added to my count and shown.
        if (const uint32_t e = ScoreEntry(true)) {
            const float score = RF(e + 0x14);
            if (!g_et.scoreInit || score + 0.5f < g_et.countedScore) {
                g_et.countedScore = score;   // race (re)started
                g_et.scoreInit = true;
            }
            const int earned = int(score - g_et.countedScore);
            if (earned >= 1 && race.phase >= netplay::RaceView::Racing) {
                s.EarnedStars(earned, 0, "");
                g_et.countedScore += float(earned);
            }
            netplay::StarEvent ev;
            while (s.PollStars(ev)) {
                if (ev.amount <= 0 || ev.member == s.MyId()) continue;
                WF(e + 0x14, RF(e + 0x14) + float(ev.amount));
                WF(e + 0x18, RF(e + 0x18) + float(ev.amount));
                g_et.countedScore += float(ev.amount);
                NetplayRuntime::ShowToast("+" + std::to_string(ev.amount) + (ev.amount == 1 ? " star  " : " stars  ") + ev.name,
                                          s.ColorOf(ev.member));
            }
        }

        // Finish: the results push my place.
        if (!g_et.finishSent && race.phase == netplay::RaceView::Racing) {
            const uint32_t awards = R32(kAwardsPointer);
            if (IsGuestPointer(awards)) {
                const uint32_t place = R32(awards + 0x2A8);
                if (place >= 1 && place <= 6) {
                    s.Finished(uint8_t(place), RF(awards + 0x2B8));
                    g_et.finishSent = true;
                }
            }
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
                const uint32_t scene = RacerScene(RacerForSlot(slot));
                if (!scene) continue;
                const Pose p = ReadPose(scene);
                netplay::VehicleState st;
                st.slot = uint8_t(slot);
                st.time = float(now);
                std::memcpy(st.pos, p.p, sizeof st.pos);
                MatrixToQuat(p.m, st.rot);
                const double dt = now - g_et.lastSentAt[slot];
                if (g_et.lastSentAt[slot] > 0 && dt > 0.001 && dt < 1.0) {
                    for (int i = 0; i < 3; ++i) st.vel[i] = float((p.p[i] - g_et.lastSent[slot].p[i]) / dt);
                }
                out.push_back(st);
            }
            s.SendStates(out);   // sends at 20 Hz, drops the calls in between
            static const bool debug = std::getenv("WC_NET_DEBUG") != nullptr;
            static double lastLog = 0;
            if (debug && now - lastLog > 2.0) {
                lastLog = now;
                for (int r = 0; r < 6; ++r) {
                    if (const uint32_t sc = RacerScene(r)) {
                        const Pose p = ReadPose(sc);
                        std::fprintf(stderr, "[netplay] racer %d at %.1f %.1f %.1f\n", r, p.p[0], p.p[1], p.p[2]);
                    }
                }
                for (int slot = 0; slot < 6; ++slot) {
                    if (g_et.ghosts[slot].has) {
                        const auto& st = g_et.ghosts[slot].state;
                        std::fprintf(stderr, "[netplay] ghost slot %d -> racer %d at %.1f %.1f %.1f\n", slot, RacerForSlot(slot), st.pos[0], st.pos[1], st.pos[2]);
                    }
                }
            }
            for (const auto& st : out) {
                g_et.lastSent[st.slot] = ReadPose(RacerScene(RacerForSlot(st.slot)));
                g_et.lastSentAt[st.slot] = now;
            }
            for (int slot = 0; slot < netplay::kRaceSlots; ++slot) {
                Ghost& gh = g_et.ghosts[slot];
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

EtBridge g_bridge;

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
    if (!g_et.announcePending || !InRoom()) return;
    g_et.announcePending = false;
    NetplayRuntime::LocalRaceChosen(g_et.announceKey, g_et.announceTitle, "");
}

void RaceStarting(CpuContext* ctx, const Pick& pick) {
    if (!NetplayRuntime::Active()) return;
    Registered();
    ResetRaceState();
    g_et.inRace = true;
    g_et.announceKey = KeyOf(pick);
    g_et.announceTitle = TitleOf(ctx, pick);
    g_et.announcePending = true;
    g_et.announceSince = NowSeconds();
    std::fprintf(stderr, "[netplay] race start %s (%s)\n", g_et.announceKey.c_str(), g_et.announceTitle.c_str());
    AnnounceIfReady();
}

void RaceEnded() {
    if (!g_et.inRace) return;
    g_et.inRace = false;
    g_et.announcePending = false;
    ResetRaceState();
    NetplayRuntime::LocalRaceEnded();
}

} // namespace et_netplay_detail

namespace EtNetplay {
using namespace et_netplay_detail;


// Once a presented frame, after NetplayRuntime::Frame(): keeps the countdown at the start line
// until everyone in the race is there.
void Tick() {
    if (!NetplayRuntime::Active()) return;
    Registered();
    if (!g_et.inRace) return;
    const bool beforeGreen = R32(kDeityState) == 1 && IsGuestPointer(R32(kRacerTable));
    static bool lastBefore = false;
    if (beforeGreen != lastBefore) {
        lastBefore = beforeGreen;
        std::fprintf(stderr, "[netplay] before green: %d (clock %.2f)\n", int(beforeGreen), RF(kDeityClock));
    }
    if (!beforeGreen) {
        g_et.holding = false;
        return;
    }
    AnnounceIfReady();
    // Still connecting: wait at the line for up to 20 seconds, then race solo.
    const bool connecting = g_et.announcePending && NowSeconds() - g_et.announceSince < 20.0;
    if (connecting || NetplayRuntime::HoldAtLine()) {
        // The green flag waves when the world clock passes kDeityClock: keep it a second ahead.
        g_et.holding = true;
        const float later = RF(kWorldTime) + 1.0f;
        if (RF(kDeityClock) < later) WF(kDeityClock, later);
    } else {
        g_et.holding = false;
    }
}

} // namespace EtNetplay

namespace et_netplay_detail {

// Excite Race's race runner (0x8027E024, called by the Excite Race front end 0x8027E0C8 once a
// race and truck are picked): finds the contest's eve and index, then runs it through 0x8023BF4C
// -> 0x8023B9EC(eve, index, buf, extra, 0). Reimplemented to see each main race start and end;
// Challenge races (0x8026029C) don't come through here, so they stay solo.
extern "C" void ET_ExciteRaceRun_8027e024(CpuContext* ctx)
{
    const uint32_t buf = ctx->gpr[3], contestId = ctx->gpr[4], extra = ctx->gpr[5];
    const uint32_t oldSp = ctx->gpr[1];
    const uint32_t sp = oldSp - 0x30;
    Memory::Write32(sp, oldSp);
    ctx->gpr[1] = sp;

    ctx->gpr[3] = contestId;
    const uint32_t eve = R32(Call(ctx, 0x8001285Cu) + 4);
    ctx->gpr[3] = eve;
    const int32_t count = int32_t(R32(Call(ctx, 0x80012820u) + 0x70));
    int32_t index = -1;
    for (int32_t i = count - 1; i >= 0; --i) {
        ctx->gpr[3] = eve;
        ctx->gpr[4] = uint32_t(i);
        if (R32(Call(ctx, 0x80012870u)) == contestId) { index = i; break; }
    }

    Pick pick;
    pick.contestId = contestId;
    pick.eve = int32_t(eve);
    pick.contest = index;
    RaceStarting(ctx, pick);
    ctx->gpr[3] = eve;
    ctx->gpr[4] = uint32_t(index);
    ctx->gpr[5] = buf;
    ctx->gpr[6] = extra;
    ctx->gpr[7] = 0;
    Call(ctx, 0x8023B9ECu);
    RaceEnded();
    ctx->gpr[1] = oldSp;
}
PPC_NATIVE_OVERRIDE_VOID(8F27E024, ET_ExciteRaceRun_8027e024, (CpuContext* ctx), (ctx));

// GameDoEve's contest runner (0x8023BEB8: li r6,0; li r7,0; b 0x8023B9EC), for -direct_to_track.
extern "C" void ET_RunContest_8023beb8(CpuContext* ctx)
{
    Pick pick;
    pick.eve = int32_t(ctx->gpr[3]);
    pick.contest = int32_t(ctx->gpr[4]);
    {
        const uint32_t save3 = ctx->gpr[3], save4 = ctx->gpr[4];
        pick.contestId = R32(Call(ctx, 0x80012870u));
        ctx->gpr[3] = save3;
        ctx->gpr[4] = save4;
    }
    const uint32_t r3 = ctx->gpr[3], r4 = ctx->gpr[4], r5 = ctx->gpr[5];
    RaceStarting(ctx, pick);
    ctx->gpr[3] = r3;
    ctx->gpr[4] = r4;
    ctx->gpr[5] = r5;
    ctx->gpr[6] = 0;
    ctx->gpr[7] = 0;
    Call(ctx, 0x8023B9ECu);
    RaceEnded();
}
PPC_NATIVE_OVERRIDE_VOID(8F23BEB8, ET_RunContest_8023beb8, (CpuContext* ctx), (ctx));

// Race render (0x8000B2AC), as the game has it, with the other players' trucks posed first.
extern "C" void ET_RaceRender_8000b2ac(CpuContext* ctx)
{
    ApplyGhosts();
    const uint32_t oldSp = ctx->gpr[1];
    const uint32_t sp = oldSp - 0x10;
    Memory::Write32(sp, oldSp);
    ctx->gpr[1] = sp;

    Call(ctx, 0x802C8530u);
    const uint32_t split = Call(ctx, 0x801DF060u);
    ctx->gpr[4] = split != 0 ? 0x8000B288u : 0x8000B210u;   // the two view callbacks
    ctx->gpr[3] = 1;
    Call(ctx, 0x801E81B8u);
    Call(ctx, 0x801DFFB4u);
    ctx->gpr[3] = 0;
    Call(ctx, 0x802C9A70u);
    if (Call(ctx, 0x801E8838u) != 0 && Call(ctx, 0x800217ACu) != 0) {
        ctx->gpr[3] = Call(ctx, 0x801E8838u) << 24;
        Call(ctx, 0x802C942Cu);
    }
    Call(ctx, 0x802C855Cu);
    Call(ctx, 0x801E85A4u);
    ctx->gpr[3] = 0;
    Call(ctx, 0x802D08C4u);
    Call(ctx, 0x802C2B48u);
    ctx->gpr[3] = 1;
    Call(ctx, 0x802D08C4u);

    ctx->gpr[1] = oldSp;
}
PPC_NATIVE_OVERRIDE_VOID(8F00B2AC, ET_RaceRender_8000b2ac, (CpuContext* ctx), (ctx));

} // namespace et_netplay_detail
