// Network Play quick menu (included by ui.cpp inside its anonymous namespace, so it shares the
// launcher's look and controller navigation).
//
// Four short steps: host or join, which game, the room (name, password, server), and you (name,
// colour). Then Connect checks the room with the server and starts the game already in it.
// The game gets everything through WC_NET_* environment variables (see UiNetplayEnv).

// --- Saved choices (LauncherDataDir()/netplay.txt, key=value lines) --------------------------

struct NetPrefs {
    std::string name, room, password, cloudServer, friendAddress;
    int color = 0;
    int game = 0;
    bool host = true;
    bool useCloud = true;
    int port = netplay::kDefaultPort;
};

fs::path NetPrefsPath() { return LauncherDataDir() / "netplay.txt"; }

NetPrefs LoadNetPrefs() {
    NetPrefs p;
    p.cloudServer = ET_NETPLAY_SERVER;
    std::FILE* f = std::fopen(cfg::PathToUtf8(NetPrefsPath()).c_str(), "rb");
    if (!f) return p;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "name") p.name = v;
        else if (k == "room") p.room = v;
        else if (k == "password") p.password = v;
        else if (k == "cloud_server" && !v.empty()) p.cloudServer = v;
        else if (k == "friend_address") p.friendAddress = v;
        else if (k == "color") p.color = std::atoi(v.c_str());
        else if (k == "game") p.game = std::atoi(v.c_str());
        else if (k == "host") p.host = v == "1";
        else if (k == "cloud") p.useCloud = v == "1";
        else if (k == "port") p.port = std::clamp(std::atoi(v.c_str()), 1024, 65535);
    }
    std::fclose(f);
    return p;
}

void SaveNetPrefs(const NetPrefs& p) {
    std::error_code ec;
    fs::create_directories(LauncherDataDir(), ec);
    std::FILE* f = std::fopen(cfg::PathToUtf8(NetPrefsPath()).c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "name=%s\nroom=%s\npassword=%s\ncloud_server=%s\nfriend_address=%s\ncolor=%d\ngame=%d\nhost=%d\ncloud=%d\nport=%d\n",
                 p.name.c_str(), p.room.c_str(), p.password.c_str(), p.cloudServer == ET_NETPLAY_SERVER ? "" : p.cloudServer.c_str(),
                 p.friendAddress.c_str(), p.color, p.game, p.host ? 1 : 0, p.useCloud ? 1 : 0, p.port);
    std::fclose(f);
}

// --- Colours players can pick (shown over their vehicle in the race) --------------------------

struct NetColor {
    const char* name;
    uint32_t rgba;
};
const NetColor kNetColors[] = {
    {"Red", 0xE53935FF}, {"Orange", 0xFB8C00FF}, {"Yellow", 0xFDD835FF}, {"Green", 0x43A047FF},
    {"Cyan", 0x00ACC1FF}, {"Blue", 0x1E88E5FF},  {"Purple", 0x8E24AAFF}, {"Pink", 0xEC407AFF},
};
constexpr int kNetColorCount = int(std::size(kNetColors));
ImU32 NetImColor(uint32_t rgba) { return IM_COL32(rgba >> 24, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, 255); }

// --- State ---------------------------------------------------------------------------------------

enum NetStep { kNetHowStep, kNetGameStep, kNetRoomStep, kNetYouStep, kNetGoStep };

struct NetMenu {
    bool open = false;
    int step = kNetHowStep;
    NetPrefs prefs;
    char name[64] = {}, room[64] = {}, password[64] = {}, cloud[256] = {}, friendAddr[128] = {};
    std::unique_ptr<netplay::Session> check;
    double checkStarted = 0;
    std::string error;
    std::vector<std::pair<std::string, std::string>> env;   // for the game, once connected
} g_net;

std::string NetBuild() { return std::string("excitepc-") + ET_LAUNCHER_VERSION; }
const char* NetGameKey(int game) { return game == 1 ? "excitebots" : "excite_truck"; }

void CopyTo(char* dst, size_t n, const std::string& s) {
    std::snprintf(dst, n, "%s", s.c_str());
}

std::string RandomRoomName() {
    static const char* words[] = {"Turbo", "Canyon", "Boost", "Dune", "Summit", "Rally", "Thunder", "Ridge", "Nitro", "Comet"};
    const uint64_t r = SDL_GetPerformanceCounter() ^ (uint64_t(SDL_GetTicksNS()) * 2654435761u);
    return std::string(words[r % std::size(words)]) + "-" + std::to_string(1000 + (r / 16) % 9000);
}

void NetplayOpen() {
    g_net.open = true;
    g_net.step = kNetHowStep;
    g_net.error.clear();
    g_net.check.reset();
    g_net.prefs = LoadNetPrefs();
    CopyTo(g_net.name, sizeof g_net.name, g_net.prefs.name);
    CopyTo(g_net.room, sizeof g_net.room, g_net.prefs.room);
    CopyTo(g_net.password, sizeof g_net.password, g_net.prefs.password);
    CopyTo(g_net.cloud, sizeof g_net.cloud, g_net.prefs.cloudServer);
    CopyTo(g_net.friendAddr, sizeof g_net.friendAddr, g_net.prefs.friendAddress);
    g_focus = 0;
    g_enterPage = true;
    SetHelp("Network Play", "Race together online: up to 6 players in a race, and CPUs fill the empty spots. "
                            "Stars everyone earns are shared, and the rank targets grow with the number of players.");
    g_helpItem = 0;
}

void NetplayClose() {
    g_net.open = false;
    g_net.check.reset();
    g_focus = 0;
    g_helpItem = 0;
}

void NetplayBack() {
    if (g_net.check) {
        g_net.check.reset();
        return;
    }
    g_net.error.clear();
    if (g_net.step == kNetHowStep) NetplayClose();
    else --g_net.step;
    g_focus = 0;
}

void SaveFromBuffers() {
    NetPrefs& p = g_net.prefs;
    p.name = netplay::CleanText(g_net.name, netplay::kMaxNameBytes);
    p.room = netplay::CleanText(g_net.room, netplay::kMaxRoomBytes);
    p.password = g_net.password;
    p.cloudServer = netplay::CleanText(g_net.cloud, 255);
    p.friendAddress = netplay::CleanText(g_net.friendAddr, 127);
    SaveNetPrefs(p);
}

// What's missing before the next step, or "".
std::string NetStepProblem(int step) {
    const NetPrefs& p = g_net.prefs;
    if (step == kNetRoomStep) {
        if (netplay::CleanText(g_net.room, netplay::kMaxRoomBytes).empty()) return p.host ? "Give the room a name." : "Type the room's name.";
        if (p.useCloud && netplay::CleanText(g_net.cloud, 255).empty()) return "Type the cloud server's address (it starts with wss://).";
        if (!p.host && !p.useCloud && netplay::CleanText(g_net.friendAddr, 127).empty()) return "Type the address of the PC hosting the room.";
        netplay::Url u;
        if (p.useCloud && !netplay::ParseUrl(g_net.cloud, u)) return "That cloud server address isn't valid.";
        if (!p.host && !p.useCloud && !netplay::ParseUrl(g_net.friendAddr, u)) return "That address isn't valid.";
    }
    if (step == kNetYouStep && netplay::CleanText(g_net.name, netplay::kMaxNameBytes).empty()) return "Type the name the others will see over your vehicle.";
    return {};
}

std::string NetServerUrl() {
    const NetPrefs& p = g_net.prefs;
    if (p.useCloud) return netplay::CleanText(g_net.cloud, 255);
    if (p.host) return "ws://127.0.0.1:" + std::to_string(p.port);
    return netplay::CleanText(g_net.friendAddr, 127);
}

void NetplayConnect() {
    SaveFromBuffers();
    const NetPrefs& p = g_net.prefs;
    g_net.error.clear();
    g_net.env.clear();
    char color[16];
    std::snprintf(color, sizeof color, "%08X", kNetColors[std::clamp(p.color, 0, kNetColorCount - 1)].rgba);
    g_net.env = {
        {"WC_NET_URL", NetServerUrl()}, {"WC_NET_ROOM", p.room},     {"WC_NET_PASSWORD", p.password},
        {"WC_NET_NAME", p.name},        {"WC_NET_COLOR", color},     {"WC_NET_CREATE", p.host ? "1" : "0"},
        {"WC_NET_BUILD", NetBuild()},   {"WC_NET_GAME", NetGameKey(p.game)},
    };
    if (p.host && !p.useCloud) {
        // The game runs the room itself. Make sure the port is free first.
        netplay::RelayServer probe;
        std::string error;
        if (!probe.Start(uint16_t(p.port), &error)) {
            g_net.error = error;
            return;
        }
        probe.Stop();
        g_net.env.push_back({"WC_NET_HOST_PORT", std::to_string(p.port)});
        g_game = GameAt(p.game);
        g_action = UiAction::LaunchNetplay;
        NetplayClose();
        return;
    }
    // Check the room with the server now, so mistakes show here rather than in the game.
    netplay::SessionConfig c;
    c.url = NetServerUrl();
    c.room = p.room;
    c.password = p.password;
    c.name = p.name;
    c.color = kNetColors[std::clamp(p.color, 0, kNetColorCount - 1)].rgba;
    c.game = NetGameKey(p.game);
    c.build = NetBuild();
    c.create = p.host;
    g_net.check = std::make_unique<netplay::Session>(c);
    g_net.checkStarted = SDL_GetTicks() / 1000.0;
}

void PumpNetCheck() {
    if (!g_net.check) return;
    g_net.check->Update();
    const auto status = g_net.check->GetStatus();
    if (status == netplay::Session::Status::InRoom) {
        g_net.check.reset();   // leave; the game joins (or opens) the room again in a moment
        g_game = GameAt(g_net.prefs.game);
        g_action = UiAction::LaunchNetplay;
        NetplayClose();
    } else if (status == netplay::Session::Status::Failed) {
        g_net.error = g_net.check->Error();
        g_net.check.reset();
    } else if (SDL_GetTicks() / 1000.0 - g_net.checkStarted > 15.0) {
        g_net.error = "The server didn't answer. Check the address and your internet connection.";
        g_net.check.reset();
    }
}

// --- Drawing -------------------------------------------------------------------------------------

// A big two-line choice button (title and a line under it). Returns true when pressed.
bool NetBigChoice(const char* title, const char* line, const char* help, float width, bool selected) {
    const float h = F.label->FontSize + F.body->FontSize + S(30);
    const bool clicked = ImGui::InvisibleButton(title, ImVec2(width, h));
    const bool pressed = NavItemDone(clicked, title, help, selected);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const bool hot = ItemHasFocus();
    draw->AddRectFilled(min, max, hot ? IM_COL32(255, 182, 56, 60) : IM_COL32(255, 255, 255, 14), S(8));
    draw->AddRect(min, max, hot ? kAmber : selected ? IM_COL32(255, 182, 56, 150) : IM_COL32(255, 255, 255, 50), S(8), 0, hot ? S(2) : S(1));
    draw->AddText(F.label, F.label->FontSize, ImVec2(min.x + S(16), min.y + S(10)), hot ? kAmber : kText, title);
    draw->AddText(F.body, F.body->FontSize, ImVec2(min.x + S(16), min.y + S(14) + F.label->FontSize), kTextDim, line, nullptr,
                  width - S(32));
    return pressed;
}

// A labelled text box. A / Enter (or a click) starts typing; Enter or a click elsewhere ends it.
void NetTextRow(const char* label, const char* help, char* buf, size_t size, ImGuiInputTextFlags flags = 0, const char* hint = "") {
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::Indent(S(8));
    ImGui::GetWindowDrawList()->AddText(F.label, F.label->FontSize, ImGui::GetCursorScreenPos(), kText, label);
    ImGui::Dummy(ImVec2(0, F.label->FontSize + S(4)));
    NavRow();
    ImGui::PushFont(F.body);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 20));
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10), S(7)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - S(16));
    ImGui::InputTextWithHint((std::string("##") + label).c_str(), hint, buf, size, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
    const ImGuiID id = ImGui::GetItemID();
    const bool active = ImGui::IsItemActive();
    if (NavItemDone(ImGui::IsItemActivated(), label, help) && g_activatedByNav) {
        ImGui::ActivateItemByID(id);
    }
    if (ItemHasFocus() || active) {
        FocusFrame(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }
    ImGui::Unindent(S(8));
    ImGui::Dummy(ImVec2(0, S(6)));
}

void NetColorRow() {
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::Indent(S(8));
    ImGui::GetWindowDrawList()->AddText(F.label, F.label->FontSize, ImGui::GetCursorScreenPos(), kText, "Colour");
    ImGui::Dummy(ImVec2(0, F.label->FontSize + S(4)));
    NavRow();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (int i = 0; i < kNetColorCount; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, S(10));
        ImGui::PushID(i);
        const float d = S(34);
        const bool clicked = ImGui::InvisibleButton("swatch", ImVec2(d, d));
        ImGui::PopID();
        const bool selected = g_net.prefs.color == i;
        const std::string help = std::string(kNetColors[i].name) + ": your name and marker show in this colour in the race.";
        if (NavItemDone(clicked, "Colour", help.c_str(), selected)) g_net.prefs.color = i;
        const ImVec2 c((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                       (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        draw->AddCircleFilled(c, d * 0.40f, NetImColor(kNetColors[i].rgba), 28);
        if (selected) draw->AddCircle(c, d * 0.48f, kText, 28, S(2.5f));
        if (ItemHasFocus()) draw->AddCircle(c, d * 0.54f, kAmber, 28, S(2));
    }
    ImGui::Unindent(S(8));
    ImGui::Dummy(ImVec2(0, S(8)));
}

void DrawNetplay(float W, float H, float margin, float footer) {
    PumpNetCheck();
    if (!g_net.open) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    NetPrefs& p = g_net.prefs;

    const float panelW = std::min(W - margin * 2.0f, S(700));
    const float panelH = std::min(H - margin * 2.0f - footer, S(620));
    const ImVec2 min((W - panelW) * 0.5f, std::max(margin, (H - footer - panelH) * 0.5f));
    const ImVec2 max(min.x + panelW, min.y + panelH);
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 6, 10, 140));
    Panel(draw, min, max);

    // Title and step dots.
    const char* stepNames[] = {"HOW DO YOU WANT TO PLAY?", "WHICH GAME?", p.host ? "YOUR ROOM" : "THE ROOM", "YOU", "READY"};
    SpacedText(draw, F.small, F.small->FontSize, ImVec2(min.x + S(26), min.y + S(22)), kAmber, "NETWORK PLAY", S(3));
    for (int i = 0; i <= kNetGoStep; ++i) {
        const ImVec2 c(max.x - S(26) - S(18) * float(kNetGoStep - i), min.y + S(22) + F.small->FontSize * 0.5f);
        draw->AddCircleFilled(c, S(4.5f), i == g_net.step ? kAmber : i < g_net.step ? IM_COL32(255, 182, 56, 120) : IM_COL32(255, 255, 255, 50));
    }
    ShadowText(draw, F.title, F.title->FontSize, ImVec2(min.x + S(26), min.y + S(28) + F.small->FontSize), kText, stepNames[g_net.step]);

    // Help line at the bottom of the panel.
    const float helpH = F.body->FontSize * 3.2f + F.label->FontSize;
    const float buttonsY = max.y - S(22) - F.label->FontSize - S(16);
    const ImVec2 helpMin(min.x + S(26), buttonsY - helpH - S(6));
    draw->AddText(F.label, F.label->FontSize, helpMin, g_net.error.empty() && !g_helpError ? kAmber : kError,
                  g_net.error.empty() ? g_helpTitle.c_str() : "Can't connect");
    draw->AddText(F.body, F.body->FontSize, ImVec2(helpMin.x, helpMin.y + F.label->FontSize + S(4)), g_net.error.empty() ? kTextDim : kText,
                  g_net.error.empty() ? g_helpText.c_str() : g_net.error.c_str(), nullptr, panelW - S(52));

    // The step's content.
    const float contentTop = min.y + S(40) + F.small->FontSize + F.title->FontSize;
    ImGui::SetCursorScreenPos(ImVec2(min.x + S(18), contentTop));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(8), S(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), S(2)));
    ImGui::BeginChild("netplay", ImVec2(panelW - S(36), helpMin.y - contentTop - S(8)), ImGuiChildFlags_AlwaysUseWindowPadding);
    g_navRegion = kPageRegion;
    const float avail = ImGui::GetContentRegionAvail().x - S(16);
    bool next = false;

    switch (g_net.step) {
        case kNetHowStep: {
            ImGui::Dummy(ImVec2(0, S(6)));
            ImGui::Indent(S(8));
            NavRow();
            if (NetBigChoice("Host a room", "Open a room and give its name to your friends.",
                             "Open a new room. Your friends join it by typing its name.", avail, p.host)) {
                p.host = true;
                next = true;
            }
            ImGui::Dummy(ImVec2(0, S(8)));
            NavRow();
            if (NetBigChoice("Join a room", "A friend already opened one: type its name.",
                             "Join a friend's room by its name (and password, if it has one).", avail, !p.host)) {
                p.host = false;
                next = true;
            }
            ImGui::Unindent(S(8));
            break;
        }
        case kNetGameStep: {
            ImGui::Dummy(ImVec2(0, S(6)));
            ImGui::Indent(S(8));
            const float w = (avail - S(12)) * 0.5f;
            NavRow();
            for (int g = 0; g < kGameCount; ++g) {
                if (g > 0) ImGui::SameLine(0.0f, S(12));
                const char* title = Game(GameAt(g)).title;
                const char* line = g == 0 ? "Main races: Excite, Tree Runs and more." : "Main races (not the poker races yet).";
                if (NetBigChoice(title, line, p.host ? "Everyone in the room plays this game." : "Pick the game the room is playing.", w, p.game == g)) {
                    p.game = g;
                    next = true;
                }
            }
            ImGui::Unindent(S(8));
            break;
        }
        case kNetRoomStep: {
            NetTextRow("Room name", p.host ? "Friends type this name to join. Capitals don't matter."
                                            : "The room's name, as your friend typed it (capitals don't matter).",
                       g_net.room, sizeof g_net.room, 0, p.host ? "e.g. Chris's Garage" : "");
            if (p.host) {
                NavRow();
                ImGui::Indent(S(8));
                if (PageButton("Make up a name", "Fill in a random room name.")) CopyTo(g_net.room, sizeof g_net.room, RandomRoomName());
                ImGui::Unindent(S(8));
                ImGui::Dummy(ImVec2(0, S(4)));
            }
            NetTextRow("Password", p.host ? "Optional. Only people who know it can join." : "Only if the room has one.",
                       g_net.password, sizeof g_net.password, ImGuiInputTextFlags_Password, "none");
            int where = p.useCloud ? 0 : 1;
            if (ChoiceRow("Server", p.host ? "Cloud: works for friends anywhere, nothing to set up. This PC: the room runs on your PC "
                                             "(your network, or friends who can reach your PC)."
                                           : "Cloud for rooms on the cloud server; A friend's PC when the host chose This PC.",
                          {"CLOUD", p.host ? "THIS PC" : "A FRIEND'S PC"}, where)) {
                p.useCloud = where == 0;
            }
            if (p.useCloud && (std::string(ET_NETPLAY_SERVER).empty() || std::string(g_net.cloud) != ET_NETPLAY_SERVER)) {
                NetTextRow("Cloud server address", "The address of the Network Play cloud server, like wss://excite-netplay.name.workers.dev",
                           g_net.cloud, sizeof g_net.cloud, 0, "wss://...");
            }
            if (!p.useCloud && !p.host) {
                NetTextRow("Host's address", "The host's IP address, as their launcher shows it (port 47630 unless they changed it).",
                           g_net.friendAddr, sizeof g_net.friendAddr, 0, "192.168.1.20");
            }
            if (!p.useCloud && p.host) {
                std::string text = "Friends choose A FRIEND'S PC and type one of: ";
                const auto addrs = netplay::LocalAddresses();
                for (size_t i = 0; i < addrs.size() && i < 3; ++i) text += (i ? ", " : "") + addrs[i];
                if (addrs.empty()) text += "(no network found)";
                text += ". Friends outside your home need port " + std::to_string(p.port) +
                        " (TCP) forwarded on your router, or a network tool like Tailscale.";
                ImGui::Indent(S(8));
                Paragraph(text);
                ImGui::Unindent(S(8));
            }
            break;
        }
        case kNetYouStep: {
            NetTextRow("Your name", "Shown over your vehicle and in the room list.", g_net.name, sizeof g_net.name, 0, "e.g. Chris");
            NetColorRow();
            break;
        }
        case kNetGoStep: {
            ImGui::Dummy(ImVec2(0, S(4)));
            ImGui::Indent(S(8));
            const std::string summary = std::string(p.host ? "Open" : "Join") + " the room \"" + netplay::CleanText(g_net.room, 32) +
                                        "\" in " + Game(GameAt(p.game)).title + " as " + netplay::CleanText(g_net.name, 24) + ", " +
                                        (p.useCloud ? "on the cloud server." : p.host ? "hosted on this PC." : "on your friend's PC.");
            Paragraph(summary, kText);
            ImGui::Dummy(ImVec2(0, S(8)));
            Paragraph("In the game: pick any race as usual and the others get 10 seconds to join it. Press View (Back) "
                      "to see who's in the room and join their races. Stars are shared, and rank targets are multiplied by "
                      "the number of players; your own save keeps the result divided back by that number.");
            ImGui::Dummy(ImVec2(0, S(12)));
            NavRow();
            if (g_net.check) {
                const char* dots[] = {"Connecting", "Connecting.", "Connecting..", "Connecting..."};
                PageButton(dots[(SDL_GetTicks() / 300) % 4], "Checking the room with the server. B to stop.");
            } else if (PageButton(p.host ? "Open the room and play" : "Join and play", "Start the game in this room.")) {
                NetplayConnect();
            }
            ImGui::Unindent(S(8));
            break;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // Back / Next along the bottom.
    g_navRegion = kHomeBottomRegion;
    NavRow();
    if (TextLink(g_net.step == kNetHowStep ? "Cancel" : "Back", "Go back a step (B).", ImVec2(min.x + S(18), buttonsY))) NetplayBack();
    if (g_net.step >= kNetRoomStep && g_net.step < kNetGoStep) {
        const float w = TextLinkWidth("Next");
        if (TextLink("Next", "Go on to the next step.", ImVec2(max.x - S(18) - w, buttonsY))) next = true;
    }

    if (next) {
        const std::string problem = NetStepProblem(g_net.step);
        if (!problem.empty()) {
            SetHelp("Not yet", problem, true);
            g_helpItem = g_focus;
        } else {
            SaveFromBuffers();
            g_net.step = std::min(g_net.step + 1, int(kNetGoStep));
            g_net.error.clear();
            g_focus = 0;
            g_enterPage = true;
        }
    }
}

// The Network Play button on the home screen, centred on the line between the two games.
void DrawNetplayButton(ImVec2 centre) {
    const char* label = "NETWORK PLAY";
    const float fontSize = F.small->FontSize * 0.95f;
    const ImVec2 text = SpacedTextSize(F.small, fontSize, label, S(1.2f));
    const ImVec2 size(text.x + S(26), text.y + S(24));
    const ImVec2 min(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f), max(min.x + size.x, min.y + size.y);
    g_navRegion = kNetButtonRegion;
    NavRow();
    ImGui::SetCursorScreenPos(min);
    const bool clicked = ImGui::InvisibleButton("netplay", size);
    const bool pressed = NavItemDone(clicked, "Network Play",
                                     "Race with friends online: open a room or join one. Up to 6 players in a race; "
                                     "CPUs fill the other spots and stars are shared.");
    const bool hot = ItemHasFocus() || ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    draw->AddRectFilled(ImVec2(min.x + S(2), min.y + S(3)), ImVec2(max.x + S(2), max.y + S(3)), IM_COL32(10, 6, 4, 120), r);
    draw->AddRectFilled(min, max, hot ? IM_COL32(255, 182, 56, 235) : IM_COL32(18, 20, 36, 235), r);
    draw->AddRect(min, max, hot ? IM_COL32(255, 230, 190, 255) : kAmber, r, 0, S(1.8f));
    SpacedText(draw, F.small, fontSize, ImVec2(centre.x - text.x * 0.5f, centre.y - text.y * 0.5f),
               hot ? IM_COL32(24, 16, 8, 255) : kText, label, S(1.2f));
    if (pressed) NetplayOpen();
}
