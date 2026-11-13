#include "netplay/session.h"

#include <algorithm>

namespace netplay {

namespace {

double SteadySeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void WriteState(Writer& w, const VehicleState& s) {
    w.u8(s.slot).f32(s.time);
    for (float v : s.pos) w.f32(v);
    for (float v : s.rot) w.f32(v);
    for (float v : s.vel) w.f32(v);
    w.u32(s.flags).u32(s.extra);
}

VehicleState ReadState(Reader& r) {
    VehicleState s;
    s.slot = r.u8();
    s.time = r.f32();
    for (float& v : s.pos) v = r.f32();
    for (float& v : s.rot) v = r.f32();
    for (float& v : s.vel) v = r.f32();
    s.flags = r.u32();
    s.extra = r.u32();
    return s;
}

Writer Payload(Game type, uint32_t raceId) {
    Writer w;
    w.u8(uint8_t(type)).u32(raceId);
    return w;
}

} // namespace

Session::Session(SessionConfig cfg, TimeFn now) : cfg_(std::move(cfg)), now_(now ? std::move(now) : TimeFn(SteadySeconds)) {
    conn_ = Connection::Open(cfg_.url);
}

Session::~Session() {
    if (conn_) {
        if (race_.phase != RaceView::None) LeaveRace();
        conn_->Close();
    }
}

std::string Session::NameOf(uint8_t id) const {
    auto it = members_.find(id);
    return it == members_.end() ? std::string("Someone") : it->second.name;
}

uint32_t Session::ColorOf(uint8_t id) const {
    auto it = members_.find(id);
    return it == members_.end() ? 0xFFFFFFFFu : it->second.color;
}

std::string Session::ActivityOf(uint8_t id) const {
    if (id == myId_) return myActivity_;
    auto it = activity_.find(id);
    return it == activity_.end() ? std::string("In the menus") : it->second.first;
}

void Session::Toast_(std::string text, uint32_t color) {
    toasts_.push_back(Toast{std::move(text), color});
    if (toasts_.size() > 16) toasts_.pop_front();
}

void Session::Send(uint8_t to, const std::vector<uint8_t>& payload) {
    if (status_ != Status::InRoom) return;
    Writer w;
    w.u8(uint8_t(Msg::Send)).u8(to).bytes(payload.data(), payload.size());
    conn_->Send(std::move(w.b));
}

void Session::SendToRace(const std::vector<uint8_t>& payload) {
    for (auto& m : race_.members) {
        if (m.member != myId_) Send(m.member, payload);
    }
}

RaceMember* Session::Find(uint8_t member) {
    for (auto& m : race_.members) if (m.member == member) return &m;
    return nullptr;
}

void Session::ResetRace() {
    race_ = RaceView{};
    raceStarter_ = 0;
    remote_.fill(std::nullopt);
    stars_.clear();
    lastStateSent_ = -1;
}

void Session::SendPresence(uint8_t to) {
    Writer w;
    w.u8(uint8_t(Game::Presence)).str(myActivity_).u32(race_.raceId);
    Send(to, w.b);
}

void Session::SetActivity(const std::string& text) {
    if (text == myActivity_) return;
    myActivity_ = text;
    SendPresence(kToEveryone);
}

// ---------------------------------------------------------------------------------------------
// Connection and messages

void Session::Update() {
    if (!conn_) return;
    Event e;
    while (conn_->Poll(e)) {
        if (e.kind == Event::Connected) {
            Hello h;
            h.create = cfg_.create;
            h.room = cfg_.room;
            h.password = cfg_.password;
            h.name = cfg_.name;
            h.color = cfg_.color;
            h.game = cfg_.game;
            h.build = cfg_.build;
            conn_->Send(EncodeHello(h));
        } else if (e.kind == Event::Closed) {
            if (status_ != Status::Failed) {
                status_ = Status::Failed;
                if (error_.empty()) error_ = e.reason.empty() ? "Disconnected from the room." : e.reason;
            }
        } else {
            Reader r(e.data);
            const Msg type = Msg(r.u8());
            switch (type) {
                case Msg::Welcome: {
                    myId_ = r.u8();
                    const int n = r.u8();
                    members_.clear();
                    for (int i = 0; i < n && r.ok; ++i) {
                        Member m;
                        m.id = r.u8();
                        m.name = r.str();
                        m.color = r.u32();
                        members_[m.id] = m;
                    }
                    members_[myId_] = Member{myId_, CleanText(cfg_.name, kMaxNameBytes), cfg_.color};
                    status_ = Status::InRoom;
                    break;
                }
                case Msg::Error: {
                    const ErrorCode code = ErrorCode(r.u8());
                    const std::string text = r.str();
                    error_ = text.empty() ? ErrorText(code) : text;
                    status_ = Status::Failed;
                    break;
                }
                case Msg::Joined: {
                    Member m;
                    m.id = r.u8();
                    m.name = r.str();
                    m.color = r.u32();
                    if (!r.ok) break;
                    members_[m.id] = m;
                    Toast_(m.name + " joined the room", m.color);
                    SendPresence(m.id);
                    // A race that is still open: let them see it too.
                    if (race_.starter && race_.phase == RaceView::Inviting) {
                        Writer w = Payload(Game::Invite, race_.raceId);
                        w.str(race_.contestKey).str(race_.title).str(race_.cupKey).f32(float(inviteDeadlineMine_ - Now()));
                        Send(m.id, w.b);
                    }
                    break;
                }
                case Msg::Left: {
                    const uint8_t id = r.u8();
                    if (!r.ok) break;
                    MemberLeft(id);
                    activity_.erase(id);
                    if (auto m = members_.find(id); m != members_.end()) {
                        Toast_(m->second.name + " left the room", m->second.color);
                        members_.erase(m);
                    }
                    break;
                }
                case Msg::Recv: {
                    const uint8_t from = r.u8();
                    if (r.ok) Handle(from, r.rest());
                    break;
                }
                default:
                    break;
            }
        }
    }

    // Timers
    const double now = Now();
    invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return inviteDeadline_[i.raceId] <= now; }),
                   invites_.end());
    for (auto& i : invites_) i.secondsLeft = inviteDeadline_[i.raceId] - now;

    switch (race_.phase) {
        case RaceView::Inviting:
            race_.secondsLeft = std::max(0.0, inviteDeadlineMine_ - now);
            if (now >= inviteDeadlineMine_) {
                Writer w = Payload(Game::InviteClosed, race_.raceId);
                Send(kToEveryone, w.b);
                race_.phase = RaceView::WaitingStart;
                phaseDeadline_ = now + kReadySeconds;
            }
            break;
        case RaceView::Joining:
            race_.secondsLeft = std::max(0.0, phaseDeadline_ - now);
            break;
        default:
            break;
    }
    if (race_.starter && race_.phase == RaceView::WaitingStart) {
        bool allReady = true;
        for (auto& m : race_.members) allReady &= m.ready;
        if (allReady || now >= phaseDeadline_) BroadcastStart();
    }
    if (race_.starter && race_.phase == RaceView::AtLine) {
        bool allLoaded = true;
        for (auto& m : race_.members) allLoaded &= m.loaded;
        if (allLoaded || now >= phaseDeadline_) {
            SendToRace(Payload(Game::Go, race_.raceId).b);
            race_.phase = RaceView::Racing;
        }
    }
}

void Session::Handle(uint8_t from, const std::vector<uint8_t>& payload) {
    Reader r(payload);
    const Game type = Game(r.u8());
    if (type == Game::Presence) {
        std::string text = r.str();
        const uint32_t raceId = r.u32();
        if (r.ok) activity_[from] = {CleanText(text, 64), raceId};
        return;
    }
    const uint32_t raceId = r.u32();
    if (!r.ok) return;
    const bool mine = raceId != 0 && raceId == race_.raceId;

    switch (type) {
        case Game::Invite: {
            Invite inv;
            inv.raceId = raceId;
            inv.from = from;
            inv.fromName = NameOf(from);
            inv.contestKey = r.str();
            inv.title = r.str();
            inv.cupKey = r.str();
            const float left = r.f32();
            if (!r.ok || left <= 0) break;
            inv.secondsLeft = std::min<double>(left, kInviteSeconds);
            inviteDeadline_[raceId] = Now() + inv.secondsLeft;
            invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.raceId == raceId; }), invites_.end());
            invites_.insert(invites_.begin(), inv);
            break;
        }
        case Game::InviteClosed:
            invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.raceId == raceId; }), invites_.end());
            break;
        case Game::Join: {
            if (!mine || !race_.starter) {
                Writer w = Payload(Game::JoinReply, raceId);
                w.u8(0).u8(0).str("That race has already started.");
                Send(from, w.b);
                break;
            }
            if (Find(from)) break;
            std::string reason;
            if (race_.phase != RaceView::Inviting) reason = "That race has already started.";
            else if (race_.Players() >= kRaceSlots) reason = "That race is full.";
            Writer w = Payload(Game::JoinReply, raceId);
            if (!reason.empty()) {
                w.u8(0).u8(0).str(reason);
                Send(from, w.b);
                break;
            }
            uint8_t slot = 0;
            for (bool taken = true; taken; ++slot) {
                taken = false;
                for (auto& m : race_.members) taken |= m.slot == slot;
                if (!taken) break;
            }
            RaceMember m;
            m.member = from;
            m.slot = slot;
            race_.members.push_back(m);
            w.u8(1).u8(slot).str("");
            Send(from, w.b);
            Toast_(NameOf(from) + " joined your race", ColorOf(from));
            break;
        }
        case Game::JoinReply: {
            if (!mine || race_.phase != RaceView::Joining) break;
            const bool ok = r.u8() != 0;
            const uint8_t slot = r.u8();
            const std::string reason = r.str();
            if (!ok) {
                Toast_(reason.empty() ? "Couldn't join that race." : reason);
                ResetRace();
                break;
            }
            if (RaceMember* me = Find(myId_)) me->slot = slot;
            break;
        }
        case Game::Ready: {
            if (!mine) break;
            const uint32_t vehicle = r.u32();
            const uint32_t variant = r.u32();
            if (RaceMember* m = Find(from)) {
                m->ready = true;
                m->vehicle = vehicle;
                m->variant = variant;
            }
            break;
        }
        case Game::Leave:
        case Game::Cancel: {
            if (!mine) {
                invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.raceId == raceId; }), invites_.end());
                break;
            }
            MemberLeft(from);
            break;
        }
        case Game::Start: {
            if (!mine || race_.starter) break;
            const int n = r.u8();
            std::vector<RaceMember> list;
            for (int i = 0; i < n && r.ok; ++i) {
                RaceMember m;
                m.member = r.u8();
                m.slot = r.u8();
                m.vehicle = r.u32();
                m.variant = r.u32();
                m.ready = true;
                list.push_back(m);
            }
            if (!r.ok) break;
            bool inIt = false;
            for (auto& m : list) inIt |= m.member == myId_;
            if (!inIt) {
                Toast_("The race started without you (you didn't pick a vehicle in time).");
                ResetRace();
                break;
            }
            race_.members = list;
            race_.phase = RaceView::Loading;
            break;
        }
        case Game::Loaded:
            if (mine) {
                if (RaceMember* m = Find(from)) m->loaded = true;
            }
            break;
        case Game::Go:
            if (mine && !race_.starter && (race_.phase == RaceView::AtLine || race_.phase == RaceView::Loading)) {
                race_.phase = RaceView::Racing;
            }
            break;
        case Game::State: {
            if (!mine) break;
            const int n = r.u8();
            for (int i = 0; i < n && r.ok; ++i) {
                VehicleState s = ReadState(r);
                if (!r.ok || s.slot >= kRaceSlots) break;
                // Only take a slot from the one who owns it.
                const RaceMember* owner = nullptr;
                for (auto& m : race_.members) if (m.slot == s.slot) owner = &m;
                const bool ok = owner ? owner->member == from : from == raceStarter_;
                if (ok && SlotIsRemote(s.slot)) remote_[s.slot] = s;
            }
            break;
        }
        case Game::Star: {
            if (!mine) break;
            StarEvent s;
            s.member = r.u8();
            s.amount = r.i32();
            s.kind = r.u8();
            s.label = r.str();
            if (!r.ok || s.member != from) break;
            s.name = NameOf(from);
            stars_.push_back(std::move(s));
            break;
        }
        case Game::Finish: {
            if (!mine) break;
            r.u8();   // slot
            r.u8();   // place
            r.f32();  // time
            if (RaceMember* m = Find(from)) m->finished = true;
            break;
        }
        default:
            break;
    }
}

void Session::MemberLeft(uint8_t id) {
    const std::string name = NameOf(id);
    const uint32_t color = ColorOf(id);
    invites_.erase(std::remove_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.from == id; }), invites_.end());
    if (race_.phase != RaceView::None && Find(id)) {
        const bool starterGone = id == raceStarter_ && !race_.starter;
        race_.members.erase(std::remove_if(race_.members.begin(), race_.members.end(), [&](const RaceMember& m) { return m.member == id; }),
                            race_.members.end());
        if (starterGone && race_.phase != RaceView::Racing && race_.phase != RaceView::Done) {
            Toast_(name + " left, so their race was cancelled.", color);
            ResetRace();
        } else {
            Toast_(name + " left the race", color);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Menus

void Session::StartRace(const std::string& contestKey, const std::string& title, const std::string& cupKey) {
    if (race_.phase != RaceView::None) LeaveRace();
    ResetRace();
    race_.phase = RaceView::Inviting;
    race_.starter = true;
    race_.raceId = (uint32_t(myId_ + 1) << 24) | (++raceCounter_ & 0xFFFFFF);
    race_.contestKey = contestKey;
    race_.title = title;
    race_.cupKey = cupKey;
    raceStarter_ = myId_;
    RaceMember me;
    me.member = myId_;
    me.slot = 0;
    race_.members.push_back(me);
    inviteDeadlineMine_ = Now() + kInviteSeconds;
    race_.secondsLeft = kInviteSeconds;
    Writer w = Payload(Game::Invite, race_.raceId);
    w.str(contestKey).str(title).str(cupKey).f32(float(kInviteSeconds));
    Send(kToEveryone, w.b);
    SetActivity("Starting " + title);
}

void Session::StartNow() {
    if (race_.starter && race_.phase == RaceView::Inviting) inviteDeadlineMine_ = Now();
}

void Session::JoinRace(uint32_t raceId) {
    auto it = std::find_if(invites_.begin(), invites_.end(), [&](const Invite& i) { return i.raceId == raceId; });
    if (it == invites_.end()) return;
    const Invite inv = *it;
    if (race_.phase != RaceView::None) LeaveRace();
    ResetRace();
    race_.phase = RaceView::Joining;
    race_.raceId = raceId;
    race_.contestKey = inv.contestKey;
    race_.title = inv.title;
    race_.cupKey = inv.cupKey;
    raceStarter_ = inv.from;
    RaceMember starter;
    starter.member = inv.from;
    starter.slot = 0;
    RaceMember me;
    me.member = myId_;
    me.slot = 0xFF;   // until the starter replies
    race_.members = {starter, me};
    phaseDeadline_ = Now() + inv.secondsLeft + kReadySeconds;
    Send(inv.from, Payload(Game::Join, raceId).b);
    invites_.erase(it);
    SetActivity("Joining " + inv.title);
}

void Session::Ready(uint32_t vehicle, uint32_t variant) {
    RaceMember* me = Find(myId_);
    if (!me) return;
    me->ready = true;
    me->vehicle = vehicle;
    me->variant = variant;
    if (race_.starter) return;   // the starter's choice travels in Start
    Writer w = Payload(Game::Ready, race_.raceId);
    w.u32(vehicle).u32(variant);
    Send(raceStarter_, w.b);
    if (race_.phase == RaceView::Joining) race_.phase = RaceView::WaitingStart;
}

void Session::BroadcastStart() {
    // Joiners who never picked a vehicle are left out.
    std::vector<RaceMember> in;
    for (auto& m : race_.members) {
        if (m.ready) {
            in.push_back(m);
        } else {
            Writer w = Payload(Game::JoinReply, race_.raceId);
            w.u8(0).u8(0).str("The race started without you (you didn't pick a vehicle in time).");
            Send(m.member, w.b);
        }
    }
    race_.members = in;
    Writer w = Payload(Game::Start, race_.raceId);
    w.u8(uint8_t(in.size()));
    for (auto& m : in) w.u8(m.member).u8(m.slot).u32(m.vehicle).u32(m.variant);
    SendToRace(w.b);
    race_.phase = RaceView::Loading;
    SetActivity("Racing " + race_.title);
}

void Session::LeaveRace() {
    if (race_.phase == RaceView::None) return;
    if (race_.starter && race_.phase == RaceView::Inviting) {
        Send(kToEveryone, Payload(Game::Cancel, race_.raceId).b);
    } else {
        const Game type = race_.starter && race_.phase != RaceView::Racing && race_.phase != RaceView::Done ? Game::Cancel : Game::Leave;
        if (race_.phase == RaceView::Joining || race_.phase == RaceView::WaitingStart) {
            Send(raceStarter_, Payload(type, race_.raceId).b);
        } else {
            SendToRace(Payload(type, race_.raceId).b);
        }
    }
    ResetRace();
    SetActivity("In the menus");
}

// ---------------------------------------------------------------------------------------------
// Racing

void Session::Loaded() {
    if (race_.phase != RaceView::Loading) return;
    if (RaceMember* me = Find(myId_)) me->loaded = true;
    race_.phase = RaceView::AtLine;
    if (race_.starter) {
        phaseDeadline_ = Now() + kLoadSeconds;
    } else {
        Send(raceStarter_, Payload(Game::Loaded, race_.raceId).b);
    }
}

bool Session::SlotIsMine(uint8_t slot) const {
    for (auto& m : race_.members) if (m.member == myId_) return m.slot == slot;
    return false;
}

bool Session::SlotIsRemote(uint8_t slot) const {
    if (race_.phase < RaceView::Loading || slot >= kRaceSlots) return false;
    bool starterHere = false;
    for (auto& m : race_.members) {
        if (m.slot == slot) return m.member != myId_;
        starterHere |= m.member == raceStarter_;
    }
    return !race_.starter && starterHere;   // a CPU, run by the starter
}

void Session::SendStates(const std::vector<VehicleState>& states) {
    if (race_.phase != RaceView::Racing && race_.phase != RaceView::Done && race_.phase != RaceView::AtLine) return;
    if (race_.Players() < 2) return;
    const double now = Now();
    if (lastStateSent_ >= 0 && now - lastStateSent_ < 1.0 / kStateHz) return;
    lastStateSent_ = now;
    Writer w = Payload(Game::State, race_.raceId);
    w.u8(uint8_t(std::min<size_t>(states.size(), kRaceSlots)));
    for (size_t i = 0; i < states.size() && i < kRaceSlots; ++i) WriteState(w, states[i]);
    SendToRace(w.b);
}

void Session::EarnedStars(int32_t amount, uint8_t kind, const std::string& label) {
    if (race_.phase != RaceView::Racing && race_.phase != RaceView::Done) return;
    Writer w = Payload(Game::Star, race_.raceId);
    w.u8(myId_).i32(amount).u8(kind).str(label);
    SendToRace(w.b);
}

bool Session::PollStars(StarEvent& out) {
    if (stars_.empty()) return false;
    out = std::move(stars_.front());
    stars_.pop_front();
    return true;
}

void Session::Finished(uint8_t place, float time) {
    if (race_.phase != RaceView::Racing) return;
    RaceMember* me = Find(myId_);
    if (!me) return;
    me->finished = true;
    Writer w = Payload(Game::Finish, race_.raceId);
    w.u8(me->slot).u8(place).f32(time);
    SendToRace(w.b);
    race_.phase = RaceView::Done;
}

bool Session::PollToast(Toast& out) {
    if (toasts_.empty()) return false;
    out = std::move(toasts_.front());
    toasts_.pop_front();
    return true;
}

} // namespace netplay
