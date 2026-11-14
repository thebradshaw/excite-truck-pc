// Network Play end-to-end test: a local relay and three game sessions going through a race.
//   netplay_test [ws-url]   (without a URL it starts its own relay on a free port)
#include "netplay/connection.h"
#include "netplay/relay_server.h"
#include "netplay/session.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>

using namespace netplay;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static double g_clock = 1000.0;   // the sessions' clock; the test moves it forward

static bool Pump(std::vector<Session*> sessions, const std::function<bool()>& until, double seconds = 5) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        for (Session* s : sessions) s->Update();
        if (until()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static void TestParseUrl() {
    Url u;
    CHECK(ParseUrl("wss://relay.example.workers.dev", u) && u.secure && u.port == 443 && u.path == "/");
    CHECK(ParseUrl("ws://192.168.1.5:47630/room", u) && !u.secure && u.port == 47630 && u.path == "/room");
    CHECK(ParseUrl("192.168.1.5", u) && !u.secure && u.port == kDefaultPort && u.host == "192.168.1.5");
    CHECK(ParseUrl("https://x.dev/ws", u) && u.secure && u.path == "/ws");
    CHECK(!ParseUrl("ws://:12", u));
    CHECK(RoomKey("  Chris's Room ") == "chris's room");
    CHECK(ScaleRequirement(100, 2) == 200 && SoloStars(220, 2) == 110);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestParseUrl();

    RelayServer relay;
    std::string url;
    if (argc > 1) {
        url = argv[1];
    } else {
        std::string err;
        uint16_t port = 47700;
        while (!relay.Start(port, &err) && port < 47800) ++port;
        CHECK(relay.Running());
        url = "ws://127.0.0.1:" + std::to_string(port);
    }
    std::printf("relay: %s\n", url.c_str());

    auto clock = [] { return g_clock; };
    auto cfg = [&](const char* name, bool create, const char* room = "Test Room") {
        SessionConfig c;
        c.url = url;
        c.room = room;
        c.password = "pw";
        c.name = name;
        c.color = 0xFF4040FF;
        c.game = "excite_truck";
        c.build = "test";
        c.create = create;
        return c;
    };

    // Joining a room that doesn't exist fails with a clear reason.
    {
        Session ghost(cfg("Ghost", false, "No Such Room"), clock);
        CHECK(Pump({&ghost}, [&] { return ghost.GetStatus() == Session::Status::Failed; }));
        CHECK(ghost.Error().find("No room") != std::string::npos);
    }

    Session a(cfg("Chris", true), clock);
    CHECK(Pump({&a}, [&] { return a.GetStatus() == Session::Status::InRoom; }));
    Session b(cfg("Mia", false), clock), c(cfg("Sam", false), clock);
    CHECK(Pump({&a, &b, &c}, [&] { return b.GetStatus() == Session::Status::InRoom && c.GetStatus() == Session::Status::InRoom; }));
    CHECK(Pump({&a, &b, &c}, [&] { return a.Members().size() == 3 && b.Members().size() == 3 && c.Members().size() == 3; }));

    {   // Same name, wrong password, wrong game
        Session dup(cfg("mia", false), clock);
        CHECK(Pump({&dup}, [&] { return dup.GetStatus() == Session::Status::Failed; }));
        CHECK(dup.Error().find("name") != std::string::npos);
        auto bad = cfg("Zed", false);
        bad.password = "nope";
        Session wrong(bad, clock);
        CHECK(Pump({&wrong}, [&] { return wrong.GetStatus() == Session::Status::Failed; }));
        CHECK(wrong.Error().find("password") != std::string::npos);
        auto other = cfg("Zed", false);
        other.game = "excitebots";
        Session og(other, clock);
        CHECK(Pump({&og}, [&] { return og.GetStatus() == Session::Status::Failed; }));
        CHECK(og.Error().find("other game") != std::string::npos);
    }
    CHECK(Pump({&a, &b, &c}, [&] { return a.Members().size() == 3; }));

    // Chris starts Canada 1. Mia joins, Sam does something else.
    a.StartRace("canada_1", "Canada Excite Race 1", "bronze");
    a.Ready(3, 1);
    CHECK(a.Race().phase == RaceView::Inviting && a.Race().starter);
    CHECK(Pump({&a, &b, &c}, [&] { return !b.Invites().empty() && !c.Invites().empty(); }));
    CHECK(b.Invites()[0].title == "Canada Excite Race 1" && b.Invites()[0].fromName == "Chris");
    b.JoinRace(b.Invites()[0].raceId);
    CHECK(b.Race().phase == RaceView::Joining);
    CHECK(Pump({&a, &b, &c}, [&] { return a.Race().Players() == 2; }));
    b.Ready(5, 2);
    CHECK(b.Race().phase == RaceView::WaitingStart);

    // The invite closes after 10 seconds; the race starts once everyone is ready.
    g_clock += kInviteSeconds + 0.1;
    CHECK(Pump({&a, &b, &c}, [&] { return a.Race().phase == RaceView::Loading && b.Race().phase == RaceView::Loading; }));
    CHECK(c.Invites().empty());
    CHECK(a.RankFactor() == 2 && b.RankFactor() == 2 && c.RankFactor() == 1);
    CHECK(b.Race().members.size() == 2);
    uint8_t bSlot = 0xFF;
    for (auto& m : b.Race().members) if (m.member == b.MyId()) bSlot = m.slot;
    CHECK(bSlot == 1);
    CHECK(a.SlotIsRemote(1) && !a.SlotIsRemote(2) && a.SlotIsMine(0));   // A runs the CPUs
    CHECK(b.SlotIsRemote(0) && b.SlotIsRemote(2) && b.SlotIsMine(1));    // B sees A's CPUs

    // Both load; the starter says go.
    a.Loaded();
    CHECK(a.Race().phase == RaceView::AtLine);
    b.Loaded();
    CHECK(Pump({&a, &b, &c}, [&] { return a.Race().phase == RaceView::Racing && b.Race().phase == RaceView::Racing; }));

    // Vehicle states: A sends its truck and the CPUs, B its own.
    std::vector<VehicleState> aStates(6);
    for (int i = 0; i < 6; ++i) { aStates[i].slot = uint8_t(i); aStates[i].pos[0] = float(i * 10); }
    aStates.erase(aStates.begin() + 1);   // not B's slot
    a.SendStates(aStates);
    VehicleState bs;
    bs.slot = 1;
    bs.pos[2] = 42;
    b.SendStates({bs});
    CHECK(Pump({&a, &b, &c}, [&] { return a.Remote(1).has_value() && b.Remote(4).has_value() && b.Remote(0).has_value(); }));
    CHECK(a.Remote(1) && a.Remote(1)->pos[2] == 42);
    CHECK(b.Remote(4) && b.Remote(4)->pos[0] == 40);
    CHECK(!a.Remote(4).has_value());

    // Stars are shared within the race only.
    b.EarnedStars(5, 1, "Tree Run");
    StarEvent star;
    CHECK(Pump({&a, &b, &c}, [&] { return a.PollStars(star); }));
    CHECK(star.amount == 5 && star.name == "Mia" && star.label == "Tree Run");
    CHECK(!c.PollStars(star));

    a.Finished(1, 120.5f);
    CHECK(Pump({&a, &b, &c}, [&] {
        for (auto& m : b.Race().members) if (m.member == a.MyId()) return m.finished;
        return false;
    }));

    // Sam starts another race alone while that one runs.
    c.StartRace("china_2", "China Excite Race 2", "silver");
    c.Ready(1, 0);
    c.StartNow();
    CHECK(Pump({&a, &b, &c}, [&] { return c.Race().phase == RaceView::Loading; }));
    CHECK(c.RankFactor() == 1);

    // Mia drops out of the room: Chris's race keeps going without her.
    b.LeaveRace();
    CHECK(Pump({&a, &b, &c}, [&] { return a.Race().Players() == 1; }));
    Toast t;
    bool sawToast = false;
    while (a.PollToast(t)) sawToast |= t.text.find("Mia") != std::string::npos;
    CHECK(sawToast);

    std::printf(g_failures ? "%d check(s) failed\n" : "all Network Play checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
