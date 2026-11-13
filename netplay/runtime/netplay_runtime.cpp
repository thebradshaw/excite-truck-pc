// Network Play inside the games. See netplay_runtime.h.

#include "netplay_runtime.h"

#include "netplay/relay_server.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace NetplayRuntime {

namespace {

using Clock = std::chrono::steady_clock;

struct ShownToast {
    netplay::Toast toast;
    Clock::time_point shownAt;
};

struct Tag {
    float x = 0, y = 0;
    bool visible = false;
};

struct State {
    bool started = false;
    bool active = false;
    std::unique_ptr<netplay::RelayServer> relay;
    std::unique_ptr<netplay::Session> session;
    Bridge* bridge = nullptr;
    std::deque<ShownToast> toasts;
    std::array<Tag, netplay::kRaceSlots> tags{};
    bool viewDown = false, viewPressed = false;
    double viewHeldFor = 0;
    Clock::time_point lastFrame = Clock::now();
    std::string relayError;
    bool reportedFailure = false;
    std::mutex mutex;
} g;

const char* Env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

void Start() {
    if (g.started) return;
    g.started = true;
    if (!*Env("WC_NET_URL")) return;
    g.active = true;
    if (const int port = std::atoi(Env("WC_NET_HOST_PORT")); port > 0) {
        g.relay = std::make_unique<netplay::RelayServer>();
        if (!g.relay->Start(uint16_t(port), &g.relayError)) g.relay.reset();
    }
    netplay::SessionConfig c;
    c.url = Env("WC_NET_URL");
    c.room = Env("WC_NET_ROOM");
    c.password = Env("WC_NET_PASSWORD");
    c.name = Env("WC_NET_NAME");
    c.color = uint32_t(std::strtoul(Env("WC_NET_COLOR"), nullptr, 16));
    if (c.color == 0) c.color = 0xFFFFFFFF;
    c.create = std::string(Env("WC_NET_CREATE")) == "1";
    c.game = Env("WC_NET_GAME");
    c.build = Env("WC_NET_BUILD");
    g.session = std::make_unique<netplay::Session>(c);
    std::fprintf(stderr, "[netplay] %s room \"%s\" on %s as %s\n", c.create ? "opening" : "joining", c.room.c_str(),
                 c.url.c_str(), c.name.c_str());
}

bool ViewButtonDown() {
    bool down = false;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) {
            if (SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i])) down |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
        }
        SDL_free(ids);
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys && keys[SDL_SCANCODE_TAB]) down = true;
    return down;
}

ImU32 Rgba(uint32_t c, float alpha = 1.0f) {
    return IM_COL32(c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, int(255 * alpha));
}

std::string Seconds(double s) {
    char text[16];
    std::snprintf(text, sizeof text, "%d", int(std::ceil(std::max(0.0, s))));
    return text;
}

} // namespace

void Register(Bridge* bridge) { g.bridge = bridge; }

bool Active() {
    Start();
    return g.active;
}

netplay::Session* Session() {
    Start();
    return g.session.get();
}

bool OwnsViewButton() { return Active() && g.session && g.session->GetStatus() == netplay::Session::Status::InRoom; }

void Frame() {
    if (!Active()) return;
    std::lock_guard lock(g.mutex);
    netplay::Session& s = *g.session;
    s.Update();

    const auto now = Clock::now();
    const double dt = std::chrono::duration<double>(now - g.lastFrame).count();
    g.lastFrame = now;
    const bool down = ViewButtonDown();
    g.viewPressed = down && !g.viewDown;
    g.viewDown = down;
    g.viewHeldFor = down ? g.viewHeldFor + dt : 0;

    netplay::Toast t;
    while (s.PollToast(t)) g.toasts.push_back({t, now});
    if (s.GetStatus() == netplay::Session::Status::Failed && !g.reportedFailure) {
        g.reportedFailure = true;
        std::fprintf(stderr, "[netplay] %s\n", s.Error().c_str());
    }

    if (s.GetStatus() != netplay::Session::Status::InRoom || !g.bridge) return;
    const netplay::RaceView& race = s.Race();
    if (race.phase >= netplay::RaceView::Loading) g.bridge->RaceFrame(s);

    // View: join the newest invite (menus), or start now (the starter, waiting at the line).
    if (g.viewPressed) {
        if (race.starter && race.phase == netplay::RaceView::Inviting) {
            s.StartNow();
        } else if (race.phase == netplay::RaceView::None && !s.Invites().empty() && g.bridge->CanJoinNow()) {
            const netplay::Invite inv = s.Invites().front();
            if (g.bridge->CupUnlocked(inv.cupKey)) {
                s.JoinRace(inv.raceId);
                g.bridge->BeginRace(inv.contestKey);
            } else {
                g.toasts.push_back({{"You haven't unlocked " + inv.title + " yet.", 0xFF8070FF}, now});
            }
        }
    }
}

void LocalRaceChosen(const std::string& contestKey, const std::string& title, const std::string& cupKey) {
    if (!Active() || !g.bridge) return;
    std::lock_guard lock(g.mutex);
    netplay::Session& s = *g.session;
    if (s.GetStatus() != netplay::Session::Status::InRoom) return;
    uint32_t vehicle = 0, variant = 0;
    g.bridge->CurrentVehicle(vehicle, variant);
    const netplay::RaceView& race = s.Race();
    if ((race.phase == netplay::RaceView::Joining || race.phase == netplay::RaceView::WaitingStart) && race.contestKey == contestKey) {
        s.Ready(vehicle, variant);   // the race I joined
        return;
    }
    s.StartRace(contestKey, title, cupKey);
    s.Ready(vehicle, variant);
}

bool HoldAtLine() {
    if (!Active()) return false;
    std::lock_guard lock(g.mutex);
    netplay::Session& s = *g.session;
    const auto phase = s.Race().phase;
    if (phase == netplay::RaceView::Loading) s.Loaded();
    switch (s.Race().phase) {
        case netplay::RaceView::Inviting:
        case netplay::RaceView::Joining:
        case netplay::RaceView::WaitingStart:
        case netplay::RaceView::Loading:
        case netplay::RaceView::AtLine:
            return true;
        default:
            return false;
    }
}

void LocalRaceEnded() {
    if (!Active()) return;
    std::lock_guard lock(g.mutex);
    g.session->LeaveRace();
    g.tags.fill(Tag{});
}

void SetNameTag(uint8_t slot, float x, float y, bool visible) {
    if (slot >= g.tags.size()) return;
    std::lock_guard lock(g.mutex);
    g.tags[slot] = Tag{x, y, visible};
}

// --- Overlay ---------------------------------------------------------------------------------------

void DrawOverlay() {
    if (!Active()) return;
    std::lock_guard lock(g.mutex);
    netplay::Session& s = *g.session;
    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    const float scale = std::max(1.0f, H / 720.0f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize() * scale;
    const ImU32 panel = IM_COL32(14, 16, 28, 215), amber = IM_COL32(255, 182, 56, 255), text = IM_COL32(246, 242, 234, 255),
                dim = IM_COL32(178, 178, 196, 255);
    const auto now = Clock::now();

    auto box = [&](ImVec2 min, ImVec2 max) {
        draw->AddRectFilled(min, max, panel, 8 * scale);
        draw->AddRect(min, max, IM_COL32(255, 176, 92, 90), 8 * scale);
    };
    auto textAt = [&](ImVec2 p, ImU32 c, const std::string& t, float size = 0) {
        draw->AddText(font, size > 0 ? size : fs, p, c, t.c_str());
    };
    auto width = [&](const std::string& t, float size = 0) { return font->CalcTextSizeA(size > 0 ? size : fs, FLT_MAX, 0, t.c_str()).x; };

    // Room badge, top left.
    {
        std::string line;
        ImU32 color = text;
        switch (s.GetStatus()) {
            case netplay::Session::Status::Connecting: line = "Network Play: connecting..."; break;
            case netplay::Session::Status::InRoom:
                line = "Network Play  \xC2\xB7  " + s.Config().room + "  \xC2\xB7  " + std::to_string(s.Members().size()) +
                       (s.Members().size() == 1 ? " player" : " players");
                break;
            case netplay::Session::Status::Failed:
                line = "Network Play: " + s.Error();
                color = IM_COL32(255, 128, 104, 255);
                break;
        }
        if (!g.relayError.empty()) line += "  (" + g.relayError + ")";
        const ImVec2 p(12 * scale, 10 * scale);
        box(p, ImVec2(p.x + width(line, fs * 0.85f) + 20 * scale, p.y + fs * 0.85f + 12 * scale));
        textAt(ImVec2(p.x + 10 * scale, p.y + 6 * scale), color, line, fs * 0.85f);
    }

    // Name tags over the others' vehicles.
    if (s.Race().phase >= netplay::RaceView::Loading) {
        for (const auto& m : s.Race().members) {
            if (m.member == s.MyId() || m.slot >= g.tags.size() || !g.tags[m.slot].visible) continue;
            const std::string name = s.NameOf(m.member);
            const float w = width(name);
            const ImVec2 c(g.tags[m.slot].x * W, g.tags[m.slot].y * H);
            const ImVec2 min(c.x - w * 0.5f - 8 * scale, c.y - fs - 10 * scale), max(c.x + w * 0.5f + 8 * scale, c.y - 2 * scale);
            draw->AddRectFilled(min, max, IM_COL32(10, 10, 16, 170), 5 * scale);
            draw->AddRectFilled(ImVec2(min.x, max.y - 3 * scale), max, Rgba(s.ColorOf(m.member)), 2 * scale);
            textAt(ImVec2(min.x + 8 * scale, min.y + 3 * scale), Rgba(s.ColorOf(m.member)), name);
            draw->AddTriangleFilled(ImVec2(c.x - 5 * scale, max.y), ImVec2(c.x + 5 * scale, max.y), ImVec2(c.x, max.y + 6 * scale),
                                    Rgba(s.ColorOf(m.member)));
        }
    }

    // Toasts, top right, 4 seconds each.
    while (!g.toasts.empty() && now - g.toasts.front().shownAt > std::chrono::seconds(4)) g.toasts.pop_front();
    {
        float y = 10 * scale;
        for (const auto& t : g.toasts) {
            const float w = width(t.toast.text) + 24 * scale;
            const ImVec2 min(W - w - 12 * scale, y), max(W - 12 * scale, y + fs + 14 * scale);
            box(min, max);
            draw->AddRectFilled(min, ImVec2(min.x + 4 * scale, max.y), Rgba(t.toast.color), 2 * scale);
            textAt(ImVec2(min.x + 14 * scale, min.y + 7 * scale), text, t.toast.text);
            y = max.y + 6 * scale;
        }
    }

    if (s.GetStatus() != netplay::Session::Status::InRoom) return;
    const netplay::RaceView& race = s.Race();
    auto centerPanel = [&](float top, const std::vector<std::pair<std::string, ImU32>>& lines) {
        float w = 0;
        for (auto& l : lines) w = std::max(w, width(l.first));
        w += 40 * scale;
        const float h = lines.size() * (fs + 6 * scale) + 22 * scale;
        const ImVec2 min((W - w) * 0.5f, top), max(min.x + w, top + h);
        box(min, max);
        float y = min.y + 12 * scale;
        for (auto& l : lines) {
            textAt(ImVec2(min.x + 20 * scale, y), l.second, l.first);
            y += fs + 6 * scale;
        }
    };

    // The race I'm in, before it starts.
    if (race.phase == netplay::RaceView::Inviting || race.phase == netplay::RaceView::WaitingStart ||
        race.phase == netplay::RaceView::Joining || race.phase == netplay::RaceView::Loading || race.phase == netplay::RaceView::AtLine) {
        std::vector<std::pair<std::string, ImU32>> lines;
        lines.push_back({race.title, amber});
        std::string who = "Racing: ";
        for (size_t i = 0; i < race.members.size(); ++i) who += (i ? ", " : "") + s.NameOf(race.members[i].member);
        lines.push_back({who + "  (" + std::to_string(race.Players()) + " of 6, CPUs fill the rest)", text});
        if (race.phase == netplay::RaceView::Inviting) {
            lines.push_back({"Waiting " + Seconds(race.secondsLeft) + "s for others to join.  View: start now", dim});
        } else if (race.phase == netplay::RaceView::Joining || race.phase == netplay::RaceView::WaitingStart) {
            lines.push_back({"Joining... pick your truck and the race starts with the others", dim});
        } else {
            lines.push_back({"Waiting for everyone to reach the start line...", dim});
        }
        if (race.Players() > 1) {
            lines.push_back({"Stars are shared; rank targets x" + std::to_string(race.Players()), dim});
        }
        centerPanel(H * 0.10f, lines);
    } else if (race.phase == netplay::RaceView::None && !s.Invites().empty()) {
        const netplay::Invite& inv = s.Invites().front();
        const bool canJoin = g.bridge && g.bridge->CanJoinNow();
        const bool unlocked = g.bridge && g.bridge->CupUnlocked(inv.cupKey);
        std::vector<std::pair<std::string, ImU32>> lines;
        lines.push_back({inv.fromName + " is starting " + inv.title + "  (" + Seconds(inv.secondsLeft) + "s)", Rgba(s.ColorOf(inv.from))});
        lines.push_back({!unlocked ? "Not unlocked in your save" : canJoin ? "View: join this race" : "Finish your race to join the next one",
                         unlocked && canJoin ? text : dim});
        centerPanel(H * 0.10f, lines);
    }

    // Room list while View is held (menus).
    if (g.viewDown && g.viewHeldFor > 0.35 && race.phase == netplay::RaceView::None) {
        std::vector<std::pair<std::string, ImU32>> lines;
        lines.push_back({"Room " + s.Config().room, amber});
        for (const auto& [id, m] : s.Members()) {
            lines.push_back({m.name + (id == s.MyId() ? " (you)" : "") + "  \xC2\xB7  " + s.ActivityOf(id), Rgba(m.color)});
        }
        centerPanel(H * 0.30f, lines);
    }
}

} // namespace NetplayRuntime
