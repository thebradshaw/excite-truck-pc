// The game's side of Network Play: one room connection, the members in it, and the races they
// start and join. Game independent: Excite Truck and Excitebots both drive it from their hooks
// (runtime/src/hle/*/..._netplay.cpp) through the calls below, once a frame from the game thread.
//
// Races, as Chris specified them (2026-11-13):
//  - Any member may start a race from the normal menus. The others get an invite and have 10
//    seconds to join it. They may also ignore it, or start a different race.
//  - Up to 6 people in a race; the game's CPUs fill the other slots. The starter's game runs
//    those CPUs and sends them to the others, so everyone sees the same field.
//  - Stars anyone earns go to everyone in that race. Rank requirements are multiplied by the
//    number of people in it.
//  - A member can only join a race from a cup their own save has unlocked; in Network Play every
//    race of an unlocked cup is open.
#pragma once

#include "netplay/connection.h"
#include "netplay/protocol.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace netplay {

constexpr int kRaceSlots = 6;
constexpr double kInviteSeconds = 10.0;
constexpr double kReadySeconds = 25.0;     // after the invite closes, how long joiners get to pick a vehicle
constexpr double kLoadSeconds = 45.0;      // how long the starter waits for everyone's race to load
constexpr double kStateHz = 20.0;          // vehicle updates per second

// Game payloads inside Msg::Send / Msg::Recv. First byte is the type.
enum class Game : uint8_t {
    Presence = 1,     // str activity text, u32 raceId (0 = none)
    Invite = 2,       // u32 raceId, str contestKey, str title, str cupKey, f32 secondsLeft
    InviteClosed = 3, // u32 raceId (no more joins; the race goes ahead or was cancelled)
    Join = 4,         // u32 raceId                                   (joiner -> starter)
    JoinReply = 5,    // u32 raceId, u8 accepted, u8 slot, str reason (starter -> joiner)
    Leave = 6,        // u32 raceId                                   (anyone in the race)
    Ready = 7,        // u32 raceId, u32 vehicle, u32 variant         (vehicle picked)
    Start = 8,        // u32 raceId, u8 n, n x {u8 member, u8 slot, u32 vehicle, u32 variant}
    Loaded = 9,       // u32 raceId                                   (race loaded, waiting at the line)
    Go = 10,          // u32 raceId                                   (starter: begin the countdown)
    State = 11,       // u32 raceId, u8 count, count x VehicleState   (own vehicle, or the CPUs)
    Star = 12,        // u32 raceId, u8 member, i32 amount, u8 kind, str label
    Finish = 13,      // u32 raceId, u8 slot, u8 place, f32 time
    Cancel = 14,      // u32 raceId                                   (starter left before the start)
};

struct VehicleState {
    uint8_t slot = 0;
    float time = 0;            // sender's race clock, seconds
    float pos[3] = {};
    float rot[4] = {0, 0, 0, 1};   // quaternion
    float vel[3] = {};
    uint32_t flags = 0;        // game specific (airborne, boosting, crashed...)
    uint32_t extra = 0;        // game specific
};

struct RaceMember {
    uint8_t member = 0;    // room member id
    uint8_t slot = 0;      // vehicle slot in the race (0..5)
    uint32_t vehicle = 0;  // game's vehicle id
    uint32_t variant = 0;  // paint / colour
    bool ready = false;
    bool loaded = false;
    bool finished = false;
};

// What the game should show or do now. Read Session::Race() every frame.
struct RaceView {
    enum Phase {
        None,          // not in a network race (playing solo is fine)
        Inviting,      // I started it: show "waiting for players" with the countdown, Start now
        Joining,       // I joined it: pick a vehicle, then Ready()
        WaitingStart,  // vehicle picked, waiting for the starter to start
        Loading,       // Start arrived: load this contest with these members
        AtLine,        // loaded; hold before the countdown until Go
        Racing,        // countdown and race
        Done,          // finished; results after everyone finishes (or leaves)
    } phase = None;
    uint32_t raceId = 0;
    bool starter = false;
    std::string contestKey, title, cupKey;
    double secondsLeft = 0;          // Inviting/Joining: until the invite closes
    std::vector<RaceMember> members; // people in the race, including me
    int Players() const { return int(members.size()); }
};

struct Invite {
    uint32_t raceId = 0;
    uint8_t from = 0;
    std::string fromName, contestKey, title, cupKey;
    double secondsLeft = 0;
};

struct StarEvent {
    uint8_t member = 0;
    std::string name;
    int32_t amount = 0;
    uint8_t kind = 0;
    std::string label;
};

struct Toast {
    std::string text;
    uint32_t color = 0xFFFFFFFF;
};

struct SessionConfig {
    std::string url;          // ws://host:port or wss://...
    std::string room, password, name;
    uint32_t color = 0xFFFFFFFF;
    std::string game, build;
    bool create = false;
};

class Session {
public:
    using Seconds = std::chrono::duration<double>;
    using TimeFn = std::function<double()>;   // seconds, monotonic (tests drive their own clock)

    explicit Session(SessionConfig cfg, TimeFn now = nullptr);
    ~Session();

    enum class Status { Connecting, InRoom, Failed };
    Status GetStatus() const { return status_; }
    const std::string& Error() const { return error_; }   // Failed: why, for the player
    uint8_t MyId() const { return myId_; }
    const SessionConfig& Config() const { return cfg_; }
    const std::map<uint8_t, Member>& Members() const { return members_; }   // includes me
    std::string NameOf(uint8_t id) const;
    uint32_t ColorOf(uint8_t id) const;
    // What each member is doing, for the room overlay ("In the menus", "Racing Canada 1").
    std::string ActivityOf(uint8_t id) const;

    // Pump the connection and timers. Call once a frame.
    void Update();

    // ---- Menus
    void SetActivity(const std::string& text);
    // I picked a race in the menus. Opens it to the room for kInviteSeconds.
    void StartRace(const std::string& contestKey, const std::string& title, const std::string& cupKey);
    // Starter: stop waiting and start with whoever is in.
    void StartNow();
    // Invites I can still take (newest first). The game greys out ones whose cup is locked.
    const std::vector<Invite>& Invites() const { return invites_; }
    void JoinRace(uint32_t raceId);
    // Vehicle picked (starter and joiners both call this).
    void Ready(uint32_t vehicle, uint32_t variant);
    // Back out of the race (menus, or quitting it).
    void LeaveRace();

    // ---- In the race
    const RaceView& Race() const { return race_; }
    void Loaded();                         // my race is loaded and at the line
    // My own vehicle (and, as the starter, the CPUs). Sent at kStateHz.
    void SendStates(const std::vector<VehicleState>& states);
    // Latest received state for a slot, if any arrived. Interpolation is the game's job.
    const std::optional<VehicleState>& Remote(uint8_t slot) const { return remote_[slot]; }
    bool SlotIsRemote(uint8_t slot) const;    // a person on another PC (or a CPU run by the starter)
    bool SlotIsMine(uint8_t slot) const;
    bool RunsCpus() const { return race_.starter; }
    void EarnedStars(int32_t amount, uint8_t kind, const std::string& label);
    // Stars other members earned in my race, oldest first. The game adds them and shows them.
    bool PollStars(StarEvent& out);
    void Finished(uint8_t place, float time);
    // Toasts for the overlay ("Mia joined the room").
    bool PollToast(Toast& out);

    // ---- Rank rule: requirements x players in the race; saved results / players.
    int RankFactor() const { return race_.phase == RaceView::None ? 1 : std::max(1, race_.Players()); }

private:
    double Now() const { return now_(); }
    void Handle(uint8_t from, const std::vector<uint8_t>& payload);
    void Send(uint8_t to, const std::vector<uint8_t>& payload);
    void SendToRace(const std::vector<uint8_t>& payload);
    void Toast_(std::string text, uint32_t color = 0xFFFFFFFF);
    void MemberLeft(uint8_t id);
    void BroadcastStart();
    void ResetRace();
    void SendPresence(uint8_t to);
    RaceMember* Find(uint8_t member);

    SessionConfig cfg_;
    TimeFn now_;
    std::unique_ptr<Connection> conn_;
    Status status_ = Status::Connecting;
    std::string error_;
    uint8_t myId_ = 0;
    std::map<uint8_t, Member> members_;
    std::map<uint8_t, std::pair<std::string, uint32_t>> activity_;
    std::string myActivity_ = "In the menus";

    std::vector<Invite> invites_;
    std::map<uint32_t, double> inviteDeadline_;
    RaceView race_;
    uint8_t raceStarter_ = 0;
    double phaseDeadline_ = 0;
    double inviteDeadlineMine_ = 0;
    uint32_t raceCounter_ = 0;
    double lastStateSent_ = -1;
    std::array<std::optional<VehicleState>, kRaceSlots> remote_{};
    std::deque<StarEvent> stars_;
    std::deque<Toast> toasts_;
};

// Rank requirements in Network Play: every threshold times the number of players.
inline int ScaleRequirement(int stars, int players) { return stars * std::max(1, players); }
// Stars saved to a player's own save after a Network Play race (Chris: back to a factor of 1).
inline int SoloStars(int sharedStars, int players) { return sharedStars / std::max(1, players); }

} // namespace netplay
