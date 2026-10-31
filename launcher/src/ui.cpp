#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_internal.h"

#include "achievements.h"
#include "launcher_config.h"
#include "scene.h"

extern const unsigned char et_font_barlow_regular[];
extern const unsigned int et_font_barlow_regular_size;
extern const unsigned char et_font_barlow_semibold[];
extern const unsigned int et_font_barlow_semibold_size;
extern const unsigned char et_font_barlowcondensed_extrabolditalic[];
extern const unsigned int et_font_barlowcondensed_extrabolditalic_size;

namespace fs = std::filesystem;
namespace cfg = launcher_config;

namespace {

// --- Look ---------------------------------------------------------------------------------------

const ImU32 kPanel = IM_COL32(18, 20, 36, 226);
const ImU32 kPanelBorder = IM_COL32(255, 176, 92, 80);
const ImU32 kAmber = IM_COL32(255, 182, 56, 255);
const ImU32 kText = IM_COL32(246, 242, 234, 255);
const ImU32 kTextDim = IM_COL32(178, 178, 196, 255);
const ImU32 kTextOff = IM_COL32(112, 114, 134, 255);
const ImU32 kError = IM_COL32(255, 128, 104, 255);

struct Fonts {
    ImFont* body = nullptr;     // help text, notes
    ImFont* label = nullptr;    // setting names
    ImFont* small = nullptr;    // choices, headings, captions
    ImFont* menu = nullptr;     // main menu
    ImFont* title = nullptr;    // help box title
    ImFont* logo = nullptr;     // EXCITE TRUCK
} F;

float g_scale = 1.0f;
float S(float v) { return v * g_scale; }

SDL_Window* g_window = nullptr;
ImTextureID g_logo = 0;
ImVec2 g_logoSize;
LauncherSettings g_settings;

enum class Page { Settings, Controls, Music, Mods, Achievements };
Page g_page = Page::Settings;
UiAction g_action = UiAction::None;

// Help box.
std::string g_helpTitle = "Ready";
std::string g_helpText = "Pick Start Game to race. Every change here is saved straight away.";
bool g_helpError = false;
ImGuiID g_helpItem = 0;

// Controller and keyboard focus. Every focusable thing registers itself while it is drawn, with
// its region (menu or page), row and column; moves are worked out from last frame's list.
enum Region { kMenuRegion = 0, kPageRegion = 1 };
enum class NavCmd { Up, Down, Left, Right, Accept, Back };
struct NavItem {
    ImGuiID id;
    int region, row, col;
    bool selected;   // the chosen value in a row of choices: where the cursor lands on that row
    bool slider;     // left/right change the value instead of moving
};
std::vector<NavItem> g_navItems, g_navLast;
int g_navRegion = kMenuRegion, g_navRow = -1, g_navCol = 0;
ImGuiID g_focus = 0;
ImGuiID g_activate = 0;          // pressed with A / Enter this frame
bool g_activatedByNav = false;   // the last press came from A / Enter, not the mouse
int g_sliderStep = 0;
bool g_scrollToFocus = false;
bool g_enterPage = false;        // after opening a page with A, put the cursor on it
bool g_resetPageScroll = false;
ImGuiID g_menuIds[8] = {};
bool IsPageMenuItem(size_t index);
Page MenuPage(size_t index);

// Music.
std::vector<fs::path> g_songs;
double g_lastScan = -100.0;
std::string g_musicNotice;
std::mutex g_pickMutex;
std::string g_pickedFolder;
bool g_folderPicked = false;
std::string g_musicFolderForGame;

constexpr size_t kGameSongLimit = 100;

// --- Small helpers ------------------------------------------------------------------------------

void SetHelp(const std::string& title, const std::string& text, bool error = false) {
    g_helpTitle = title;
    g_helpText = text;
    g_helpError = error;
}

void NavRow() {
    ++g_navRow;
    g_navCol = 0;
}

// Registers the item just drawn (call right after it). Returns true when it was pressed, by
// mouse click or by A / Enter while it had the cursor.
bool NavItemDone(bool clicked, const char* title, const char* help, bool selected = false, bool slider = false) {
    const ImGuiID id = ImGui::GetItemID();
    g_navItems.push_back({id, g_navRegion, g_navRow, g_navCol++, selected, slider});
    const ImGuiIO& io = ImGui::GetIO();
    if (clicked || (ImGui::IsItemHovered() && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))) {
        g_focus = id;
    }
    if (g_focus == id) {
        if (g_helpItem != id) {
            g_helpItem = id;
            SetHelp(title, help);
        }
        if (g_scrollToFocus) {
            ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
            g_scrollToFocus = false;
        }
    }
    if (g_activate == id) {
        g_activate = 0;
        g_activatedByNav = true;
        return true;
    }
    if (clicked) {
        g_activatedByNav = false;
    }
    return clicked;
}

bool ItemHasFocus() { return g_focus == ImGui::GetItemID(); }

// Where the cursor lands when it moves onto a row: the chosen value, else the nearest column.
ImGuiID RowEntry(int region, int row, int preferCol) {
    const NavItem* best = nullptr;
    for (const NavItem& item : g_navLast) {
        if (item.region != region || item.row != row) continue;
        if (item.selected) return item.id;
        if (best == nullptr || item.col <= preferCol) best = &item;
    }
    return best ? best->id : 0;
}

void FocusMenu() {
    for (size_t i = 0; i < std::size(g_menuIds); ++i) {
        if (g_menuIds[i] != 0 && IsPageMenuItem(i) && MenuPage(i) == g_page) {
            g_focus = g_menuIds[i];
            return;
        }
    }
}

void FocusPage() {
    int firstRow = INT32_MAX;
    for (const NavItem& item : g_navLast) {
        if (item.region == kPageRegion) firstRow = std::min(firstRow, item.row);
    }
    if (firstRow != INT32_MAX) {
        g_focus = RowEntry(kPageRegion, firstRow, 0);
        g_scrollToFocus = true;
    }
}

void Nav(NavCmd cmd) {
    const NavItem* cur = nullptr;
    for (const NavItem& item : g_navLast) {
        if (item.id == g_focus) cur = &item;
    }
    if (cur == nullptr) {
        g_focus = g_menuIds[0];
        return;
    }
    auto exists = [&](int region, int row) {
        return std::any_of(g_navLast.begin(), g_navLast.end(),
                           [&](const NavItem& i) { return i.region == region && i.row == row; });
    };
    auto at = [&](int region, int row, int col) -> ImGuiID {
        for (const NavItem& item : g_navLast) {
            if (item.region == region && item.row == row && item.col == col) return item.id;
        }
        return 0;
    };
    switch (cmd) {
        case NavCmd::Up:
        case NavCmd::Down: {
            const int row = cur->row + (cmd == NavCmd::Up ? -1 : 1);
            if (exists(cur->region, row)) {
                g_focus = RowEntry(cur->region, row, cur->col);
                g_scrollToFocus = true;
            }
            break;
        }
        case NavCmd::Left:
            if (cur->slider) {
                g_sliderStep = -1;
            } else if (ImGuiID id = at(cur->region, cur->row, cur->col - 1)) {
                g_focus = id;
            } else if (cur->region == kPageRegion) {
                FocusMenu();
            }
            break;
        case NavCmd::Right:
            if (cur->slider) {
                g_sliderStep = 1;
            } else if (ImGuiID id = at(cur->region, cur->row, cur->col + 1)) {
                g_focus = id;
            } else if (cur->region == kMenuRegion) {
                FocusPage();
            }
            break;
        case NavCmd::Accept:
            g_activate = g_focus;
            break;
        case NavCmd::Back:
            if (cur->region == kPageRegion) FocusMenu();
            break;
    }
}

// Keyboard (arrows, Enter, Esc) and every connected controller (D-pad or left stick, A, B).
void ReadNavInput() {
    const auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    if (key(ImGuiKey_UpArrow)) Nav(NavCmd::Up);
    if (key(ImGuiKey_DownArrow)) Nav(NavCmd::Down);
    if (key(ImGuiKey_LeftArrow)) Nav(NavCmd::Left);
    if (key(ImGuiKey_RightArrow)) Nav(NavCmd::Right);
    if (key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter) || key(ImGuiKey_Space)) Nav(NavCmd::Accept);
    if (key(ImGuiKey_Escape) || key(ImGuiKey_Backspace)) Nav(NavCmd::Back);

    static int heldDir = -1;
    static double heldSince = 0.0, lastRepeat = 0.0;
    static bool prevA = false, prevB = false;
    int dir = -1;
    bool a = false, b = false;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        if (pad == nullptr) continue;
        const float x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
        const float y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -0.55f) dir = int(NavCmd::Up);
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 0.55f) dir = int(NavCmd::Down);
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -0.55f) dir = int(NavCmd::Left);
        else if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 0.55f) dir = int(NavCmd::Right);
        a = a || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
        b = b || SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
    }
    SDL_free(ids);
    const double now = SDL_GetTicks() / 1000.0;
    if (dir != heldDir) {
        heldDir = dir;
        heldSince = lastRepeat = now;
        if (dir >= 0) Nav(NavCmd(dir));
    } else if (dir >= 0 && now - heldSince > 0.40 && now - lastRepeat > 0.11) {
        lastRepeat = now;
        Nav(NavCmd(dir));
    }
    if (a && !prevA) Nav(NavCmd::Accept);
    if (b && !prevB) Nav(NavCmd::Back);
    prevA = a;
    prevB = b;
}

// A rounded outline around the item with the cursor.
void FocusFrame(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    draw->AddRectFilled(min, max, IM_COL32(255, 255, 255, 22), S(5));
    draw->AddRect(min, max, kAmber, S(5), 0, S(1.5f));
}

// One UTF-8 character starting at c.
const char* NextChar(const char* c) {
    ++c;
    while ((static_cast<unsigned char>(*c) & 0xC0) == 0x80) ++c;
    return c;
}

ImVec2 SpacedTextSize(ImFont* font, float size, const char* text, float spacing) {
    float width = 0.0f;
    int count = 0;
    for (const char* c = text; *c; c = NextChar(c), ++count) {
        width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, c, NextChar(c)).x;
    }
    return ImVec2(width + spacing * float(std::max(0, count - 1)), size);
}

void SpacedText(ImDrawList* draw, ImFont* font, float size, ImVec2 pos, ImU32 color, const char* text, float spacing) {
    for (const char* c = text; *c; c = NextChar(c)) {
        draw->AddText(font, size, pos, color, c, NextChar(c));
        pos.x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, c, NextChar(c)).x + spacing;
    }
}

void Heading(const char* text) {
    ImGui::Dummy(ImVec2(0, S(10)));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    SpacedText(ImGui::GetWindowDrawList(), F.small, F.small->FontSize, pos, kAmber, text, S(2.5f));
    ImGui::Dummy(ImVec2(0, F.small->FontSize + S(2)));
}

void Paragraph(const std::string& text, ImU32 color = kTextDim) {
    ImGui::PushFont(F.body);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// A setting: its name, then its choices in a row (left/right on the stick moves between them).
// Returns true when the player picked another choice.
bool ChoiceRow(const char* name, const char* help, const std::vector<const char*>& choices, int& index,
               bool enabled = true, const char* badge = nullptr) {
    bool changed = false;
    ImGui::PushID(name);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::Indent(S(8));
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        draw->AddText(F.label, F.label->FontSize, pos, enabled ? kText : kTextOff, name);
        if (badge) {
            const float w = F.label->CalcTextSizeA(F.label->FontSize, FLT_MAX, 0.0f, name).x;
            const ImVec2 size = SpacedTextSize(F.small, F.small->FontSize * 0.8f, badge, S(1.5f));
            const ImVec2 b0(pos.x + w + S(12), pos.y + (F.label->FontSize - size.y) * 0.5f - S(3));
            const ImVec2 b1(b0.x + size.x + S(14), b0.y + size.y + S(6));
            draw->AddRectFilled(b0, b1, IM_COL32(255, 182, 56, 40), S(4));
            SpacedText(draw, F.small, F.small->FontSize * 0.8f, ImVec2(b0.x + S(7), b0.y + S(3)), kAmber, badge, S(1.5f));
        }
        ImGui::Dummy(ImVec2(0, F.label->FontSize + S(2)));
    }
    if (enabled) {
        NavRow();
    }
    const float spacing = S(1.5f);
    for (int i = 0; i < int(choices.size()); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, S(10));
        }
        const ImVec2 textSize = SpacedTextSize(F.small, F.small->FontSize, choices[i], spacing);
        const ImVec2 size(textSize.x + S(12), textSize.y + S(14));
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("choice", size);
        ImGui::PopID();
        const bool selected = i == index;
        bool hot = false;
        if (enabled) {
            if (NavItemDone(clicked, name, help, selected) && i != index) {
                index = i;
                changed = true;
            }
            hot = ItemHasFocus();
        } else if (ImGui::IsItemHovered() && g_helpItem != ImGui::GetItemID()) {
            g_helpItem = ImGui::GetItemID();
            SetHelp(name, help);
        }
        const ImVec2 p0 = ImGui::GetItemRectMin();
        ImU32 color = !enabled ? kTextOff : selected ? kText : hot ? IM_COL32(230, 230, 240, 255) : kTextDim;
        if (hot) {
            FocusFrame(draw, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        SpacedText(draw, F.small, F.small->FontSize, ImVec2(p0.x + S(6), p0.y + S(5)), color, choices[i], spacing);
        if (selected && enabled) {
            const float y = p0.y + S(5) + textSize.y + S(4);
            draw->AddRectFilled(ImVec2(p0.x + S(6), y), ImVec2(p0.x + S(6) + textSize.x, y + S(2.5f)), kAmber, S(1));
        }
    }
    ImGui::Unindent(S(8));
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::PopID();
    return changed;
}

bool ToggleRow(const char* name, const char* help, bool& value, const char* off = "OFF", const char* on = "ON") {
    int index = value ? 1 : 0;
    if (ChoiceRow(name, help, {off, on}, index)) {
        value = index == 1;
        return true;
    }
    return false;
}

// A button on a page. Buttons drawn side by side share a row: call NavRow() before the first.
bool PageButton(const char* label, const char* help) {
    ImFont* font = F.label;
    const ImVec2 text = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, label);
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(text.x + S(32), text.y + S(16)));
    const bool pressed = NavItemDone(clicked, label, help);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    const bool hot = ItemHasFocus();
    draw->AddRectFilled(min, max, ImGui::IsItemActive() ? IM_COL32(255, 182, 56, 120) : hot ? IM_COL32(255, 182, 56, 70)
                                                                                         : IM_COL32(255, 182, 56, 30), S(6));
    draw->AddRect(min, max, hot ? kAmber : IM_COL32(255, 182, 56, 130), S(6), 0, hot ? S(2) : S(1));
    draw->AddText(font, font->FontSize, ImVec2(min.x + S(16), min.y + S(8)), kText, label);
    return pressed;
}

// The volume bar: drag it, or left/right on the stick in steps of 5.
bool VolumeSlider(const char* title, const char* help, int& value) {
    NavRow();
    const ImVec2 size(S(320), S(30));
    const bool clicked = ImGui::InvisibleButton("##volume", size);
    NavItemDone(clicked, title, help, false, true);
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const int v = int(std::round(std::clamp((ImGui::GetIO().MousePos.x - min.x) / (max.x - min.x), 0.0f, 1.0f) * 100.0f));
        changed = v != value;
        value = v;
    }
    if (ItemHasFocus() && g_sliderStep != 0) {
        const int v = std::clamp((value / 5 + g_sliderStep) * 5, 0, 100);
        changed = changed || v != value;
        value = v;
    }
    g_sliderStep = 0;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float cy = (min.y + max.y) * 0.5f;
    const float x = min.x + (max.x - min.x) * value / 100.0f;
    if (ItemHasFocus()) {
        FocusFrame(draw, ImVec2(min.x - S(8), min.y - S(2)), ImVec2(max.x + S(70), max.y + S(2)));
    }
    draw->AddRectFilled(ImVec2(min.x, cy - S(3)), ImVec2(max.x, cy + S(3)), IM_COL32(60, 62, 86, 255), S(3));
    draw->AddRectFilled(ImVec2(min.x, cy - S(3)), ImVec2(x, cy + S(3)), kAmber, S(3));
    draw->AddCircleFilled(ImVec2(x, cy), S(8), IM_COL32(255, 236, 200, 255), 24);
    char text[16];
    std::snprintf(text, sizeof text, "%d%%", value);
    draw->AddText(F.small, F.small->FontSize, ImVec2(max.x + S(16), cy - F.small->FontSize * 0.5f), kText, text);
    return changed;
}

// --- Music --------------------------------------------------------------------------------------

void ScanMusic(bool force = false) {
    const double now = SDL_GetTicks() / 1000.0;
    if (!force && now - g_lastScan < 3.0) {
        return;
    }
    g_lastScan = now;
    const fs::path folder = cfg::MusicFolder(g_settings);
    std::error_code ec;
    fs::create_directories(folder, ec);
    g_songs = cfg::FindSongs(folder);
    g_musicFolderForGame = g_settings.musicFolder.empty() ? std::string() : cfg::PathToUtf8(folder);
}

void OpenFolder(const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    // A plain path opens the folder in Explorer (ShellExecute) or the file manager (xdg-open).
    SDL_OpenURL(cfg::PathToUtf8(folder).c_str());
}

void SDLCALL OnFolderPicked(void*, const char* const* files, int) {
    if (files == nullptr || files[0] == nullptr) {
        return;   // cancelled or failed
    }
    std::lock_guard<std::mutex> lock(g_pickMutex);
    g_pickedFolder = files[0];
    g_folderPicked = true;
}

void ApplyPickedFolder() {
    std::string picked;
    {
        std::lock_guard<std::mutex> lock(g_pickMutex);
        if (!g_folderPicked) {
            return;
        }
        g_folderPicked = false;
        picked = g_pickedFolder;
    }
    g_settings.musicFolder = picked;
    cfg::SetMusicFolder(picked);
    g_musicNotice.clear();
    ScanMusic(true);
}

// --- Pages --------------------------------------------------------------------------------------

int IndexOf(const std::vector<std::string>& values, const std::string& value, int fallback = 0) {
    const auto it = std::find(values.begin(), values.end(), value);
    return it == values.end() ? fallback : int(it - values.begin());
}

void SettingsPage() {
    Heading("DISPLAY");
    {
        static const std::vector<std::string> modes = {"windowed", "borderless", "exclusive"};
        int index = IndexOf(modes, g_settings.displayMode, 1);
        if (ChoiceRow("Window Mode",
                "Borderless fills the screen and lets you switch to other windows quickly. Fullscreen takes over "
                "the display. Windowed gives you a normal window you can resize.",
                {"WINDOWED", "BORDERLESS", "FULLSCREEN"}, index)) {
            g_settings.displayMode = modes[size_t(index)];
            cfg::SetDisplayMode(g_settings.displayMode);
        }
    }
    {
        static const float values[] = {0.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
        int index = 0;
        for (int i = 0; i < 6; ++i) {
            if (std::fabs(values[i] - g_settings.resolution) < 0.01f) index = i;
        }
        if (ChoiceRow("Resolution",
                "How sharp the game is drawn. Auto matches your window (1080p on a 1080p screen, 4K on a 4K "
                "screen). 480p is the Wii's own picture. Higher settings need a faster graphics card.",
                {"AUTO", "480P", "720P", "960P", "1440P", "1920P"}, index)) {
            g_settings.resolution = values[index];
            cfg::SetResolution(g_settings.resolution);
        }
    }
    {
        int index = g_settings.widescreen ? 1 : 0;
        if (ChoiceRow("Aspect Ratio",
                "Widescreen fills a 16:9 screen, like the Wii's widescreen mode. Original keeps the 4:3 picture.",
                {"ORIGINAL 4:3", "WIDESCREEN"}, index)) {
            g_settings.widescreen = index == 1;
            cfg::SetWidescreen(g_settings.widescreen);
        }
    }
#ifdef _WIN32
    {
        static const std::vector<std::string> apis = {"auto", "d3d12", "vulkan"};
        int index = IndexOf(apis, g_settings.graphicsApi);
        if (ChoiceRow("Graphics API",
                "Auto picks the best one for your PC. If you see flickering or lines, try the other one.",
                {"AUTO", "DIRECT3D 12", "VULKAN"}, index)) {
            g_settings.graphicsApi = apis[size_t(index)];
            cfg::SetGraphicsApi(g_settings.graphicsApi);
        }
    }
#endif
    {
        static const unsigned values[] = {1, 2, 4, 8};
        int index = 0;
        for (int i = 0; i < 4; ++i) {
            if (values[i] == g_settings.msaa) index = i;
        }
        if (ChoiceRow("Anti-Aliasing",
                "Smooths jagged edges (MSAA). Higher settings cost more graphics card time. Applies the next time "
                "the game starts.",
                {"OFF", "2X", "4X", "8X"}, index)) {
            g_settings.msaa = values[index];
            cfg::SetMsaa(g_settings.msaa);
        }
    }
    if (ToggleRow("Sharper Textures",
            "Keeps the track and ground sharp when seen at an angle (16x anisotropic filtering). Applies the "
            "next time the game starts.",
            g_settings.anisotropic)) {
        cfg::SetAnisotropic(g_settings.anisotropic);
    }

    Heading("MOTION");
    {
        int index = g_settings.frameRate >= 180 ? 2 : g_settings.frameRate >= 120 ? 1 : 0;
        if (ChoiceRow("Frame Rate",
                "60 is the game's own speed. 120 and 180 draw smooth in-between frames; the game itself still "
                "runs at its normal speed. Use them with a 120 Hz or faster screen and VSync on.",
                {"60", "120", "180"}, index)) {
            static const unsigned rates[] = {0, 120, 180};
            g_settings.frameRate = rates[index];
            cfg::SetFrameRate(g_settings.frameRate);
        }
    }
    if (ToggleRow("VSync",
            "On keeps the picture from tearing. Off shows each frame as soon as it is ready, which can tear.",
            g_settings.vsync)) {
        cfg::SetVsync(g_settings.vsync);
    }
    if (ToggleRow("Show FPS", "Shows the frame rate in the corner of the screen.", g_settings.showFps)) {
        cfg::SetShowFps(g_settings.showFps);
    }

    Heading("VIEW");
    {
        static const float values[] = {1.0f, 1.5f, 2.0f, 3.0f};
        int index = 0;
        for (int i = 0; i < 4; ++i) {
            if (std::fabs(values[i] - g_settings.drawDistance) < 0.01f) index = i;
        }
        if (ChoiceRow("Draw Distance",
                "How far down the track you can see. Pushes the fog and the far edge of the scenery out: "
                "Original is the Wii's view, 3X shows the most.",
                {"ORIGINAL", "1.5X", "2X", "3X"}, index)) {
            g_settings.drawDistance = values[index];
            cfg::SetDrawDistance(g_settings.drawDistance);
        }
    }

    Heading("AUDIO");
    {
        ImGui::Indent(S(8));
        ImGui::Dummy(ImVec2(0, S(4)));
        ImGui::GetWindowDrawList()->AddText(F.label, F.label->FontSize, ImGui::GetCursorScreenPos(), kText, "Volume");
        ImGui::Dummy(ImVec2(0, F.label->FontSize + S(4)));
        if (VolumeSlider("Volume", "How loud the whole game is. With a controller, move left or right on the bar.",
                         g_settings.volumePercent) && !ImGui::IsItemActive()) {
            cfg::SetVolumePercent(g_settings.volumePercent);
        }
        if (ImGui::IsItemDeactivated()) {
            cfg::SetVolumePercent(g_settings.volumePercent);
        }
        ImGui::Unindent(S(8));
        ImGui::Dummy(ImVec2(0, S(12)));
    }
}

// An Xbox-style button glyph: a coloured circle with its letter, or a rounded pill for triggers.
void Glyph(ImDrawList* draw, ImVec2 centre, const char* label, ImU32 color, bool pill) {
    const float r = S(15);
    ImFont* font = pill ? F.small : F.label;
    const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, label);
    if (pill) {
        const float w = std::max(size.x + S(16), r * 2.0f);
        draw->AddRectFilled(ImVec2(centre.x - w * 0.5f, centre.y - r * 0.8f), ImVec2(centre.x + w * 0.5f, centre.y + r * 0.8f),
                            IM_COL32(52, 56, 74, 255), r * 0.8f);
        draw->AddRect(ImVec2(centre.x - w * 0.5f, centre.y - r * 0.8f), ImVec2(centre.x + w * 0.5f, centre.y + r * 0.8f),
                      IM_COL32(150, 154, 176, 255), r * 0.8f, 0, S(1.5f));
        draw->AddText(font, font->FontSize, ImVec2(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f), kText, label);
    } else {
        draw->AddCircleFilled(centre, r, IM_COL32(36, 38, 52, 255), 32);
        draw->AddCircle(centre, r, color, 32, S(2));
        draw->AddText(font, font->FontSize, ImVec2(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f), color, label);
    }
}

void ControlLine(const char* label, ImU32 color, bool pill, const char* action, const char* alt = nullptr) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowH = S(40);
    Glyph(draw, ImVec2(pos.x + S(30), pos.y + rowH * 0.5f), label, color, pill);
    float x = pos.x + S(64);
    if (alt) {
        Glyph(draw, ImVec2(pos.x + S(84), pos.y + rowH * 0.5f), alt, IM_COL32(150, 154, 176, 255), true);
        x = pos.x + S(112);
    }
    draw->AddText(F.label, F.label->FontSize, ImVec2(x, pos.y + (rowH - F.label->FontSize) * 0.5f), kText, action);
    ImGui::Dummy(ImVec2(0, rowH));
}

void ControlsPage() {
    Heading("XBOX CONTROLLER");
    ImGui::Indent(S(8));
    ControlLine("RT", 0, true, "Drive", "RB");
    ControlLine("X", IM_COL32(80, 150, 255, 255), false, "Brake", "LT");
    ControlLine("A", IM_COL32(110, 200, 80, 255), false, "Turbo, and OK in menus");
    ControlLine("LS", 0, true, "Steer, and tilt the truck in the air");
    ControlLine("Y", IM_COL32(240, 200, 60, 255), false, "The Wii Remote's A button", "VIEW");
    ControlLine("B", IM_COL32(240, 80, 70, 255), false, "Back in menus");
    ControlLine("MENU", 0, true, "Pause");
    ImGui::Unindent(S(8));

    Heading("OPTIONS");
    if (ToggleRow("Air Control",
            "Flight style: push the stick up to dip the truck's nose, like a plane. Direct: push up to lift the nose.",
            g_settings.invertAirControl, "DIRECT", "FLIGHT STYLE")) {
        cfg::SetInvertAirControl(g_settings.invertAirControl);
    }
    if (ToggleRow("Vibration", "Rumble on your controller when you land, crash and boost.", g_settings.rumble)) {
        cfg::SetRumble(g_settings.rumble);
    }
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::Indent(S(8));
    Paragraph("Keyboard and other controllers work too. To change which button does what, press F10 in the game "
              "and open Controllers.");
    ImGui::Unindent(S(8));
}

void MusicPage() {
    ScanMusic();
    const fs::path folder = cfg::MusicFolder(g_settings);
    Heading("YOUR MUSIC");
    ImGui::Indent(S(8));
    Paragraph("Race to your own songs. Put MP3 files in your music folder (or drop them on this window), then in "
              "the game pick Options > Race Music > All Random.", kText);
    ImGui::Dummy(ImVec2(0, S(6)));
    ImGui::PushFont(F.small);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(cfg::PathToUtf8(folder).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, S(6)));
    NavRow();
    if (PageButton("Open Music Folder", "Opens the folder so you can copy your MP3 files into it.")) {
        OpenFolder(folder);
    }
    ImGui::SameLine(0.0f, S(10));
    if (PageButton("Choose Folder...", "Use a folder you already have, for example your own Music folder. "
                                       "Songs in folders inside it count too.")) {
        const std::string current = cfg::PathToUtf8(folder);
        SDL_ShowOpenFolderDialog(OnFolderPicked, nullptr, g_window, current.c_str(), false);
    }
    if (!g_settings.musicFolder.empty()) {
        ImGui::SameLine(0.0f, S(10));
        if (PageButton("Use Default", "Go back to the Music folder that came with the game.")) {
            g_settings.musicFolder.clear();
            cfg::SetMusicFolder("");
            g_musicNotice.clear();
            ScanMusic(true);
        }
    }
    ImGui::Dummy(ImVec2(0, S(6)));
    if (!g_musicNotice.empty()) {
        Paragraph(g_musicNotice, kAmber);
    }
    ImGui::Unindent(S(8));

    char heading[64];
    if (g_songs.empty()) {
        std::snprintf(heading, sizeof heading, "NO SONGS YET");
    } else {
        std::snprintf(heading, sizeof heading, "%zu SONG%s", g_songs.size(), g_songs.size() == 1 ? "" : "S");
    }
    Heading(heading);
    ImGui::Indent(S(8));
    if (g_songs.empty()) {
        Paragraph("The game's own soundtrack plays until you add some.");
    }
    ImGui::PushFont(F.body);
    for (size_t i = 0; i < g_songs.size(); ++i) {
        const std::string name = cfg::PathToUtf8(g_songs[i].stem());
        ImGui::PushStyleColor(ImGuiCol_Text, i < kGameSongLimit ? kText : kTextOff);
        ImGui::Text("%2zu   %s", i + 1, name.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    if (g_songs.size() > kGameSongLimit) {
        Paragraph("The game plays the first 100 songs.");
    }
    ImGui::Unindent(S(8));
}

void ModsPage() {
    Heading("TEXTURE PACKS");
    if (ToggleRow("Texture Packs",
            "Replaces the game's textures with ones from the texture folder, in Dolphin's format "
            "(tex1_ files, PNG or DDS). Read when the game starts.",
            g_settings.texturePacks)) {
        cfg::SetTexturePacks(g_settings.texturePacks);
    }
    ImGui::Indent(S(8));
    NavRow();
    if (PageButton("Open Texture Folder", "Opens the folder texture packs go in.")) {
        OpenFolder(cfg::TextureFolder());
    }
    ImGui::Dummy(ImVec2(0, S(8)));
    Paragraph("Packs made for Excite Truck in Dolphin work here: copy the pack's files into the texture folder "
              "and turn Texture Packs on.");
    ImGui::Unindent(S(8));
}

// --- Achievements -------------------------------------------------------------------------------

std::vector<Achievement> g_achievements;
double g_lastAchievementScan = -100.0;

void ScanAchievements(bool force = false) {
    const double now = SDL_GetTicks() / 1000.0;
    if (!force && now - g_lastAchievementScan < 3.0) {
        return;
    }
    g_lastAchievementScan = now;
    g_achievements = LoadAchievements();
}

// "2026-10-31T09:41:07Z" -> "31 Oct 2026"
std::string FormatUnlockDate(const std::string& iso) {
    int year = 0, month = 0, day = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%d", &year, &month, &day) != 3 || month < 1 || month > 12) {
        return {};
    }
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char text[32];
    std::snprintf(text, sizeof text, "%d %s %d", day, kMonths[month - 1], year);
    return text;
}

// One achievement: a gold medal when unlocked, an empty ring while locked. Each row takes the
// cursor so a controller can scroll the list, and the help box shows the whole description.
void AchievementRow(const Achievement& a, float width) {
    ImGui::PushID(a.id.c_str());
    NavRow();
    const float rowH = S(58);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(width, rowH));
    std::string help = a.description;
    if (a.unlocked) {
        const std::string date = FormatUnlockDate(a.unlockedAt);
        help += date.empty() ? "\n\nUnlocked." : "\n\nUnlocked on " + date + ".";
    } else {
        help += "\n\nNot unlocked yet.";
    }
    NavItemDone(clicked, a.title.c_str(), help.c_str());
    const bool hot = ItemHasFocus();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    if (hot) {
        draw->AddRectFilled(min, max, IM_COL32(255, 182, 56, 34), S(6));
        draw->AddRect(min, max, kAmber, S(6), 0, S(1.5f));
    }

    const ImVec2 centre(min.x + S(26), min.y + rowH * 0.5f);
    const float r = S(13);
    if (a.unlocked) {
        draw->AddCircleFilled(centre, r, kAmber, 32);
        draw->AddCircle(centre, r, IM_COL32(255, 230, 160, 255), 32, S(1.5f));
        // A tick.
        const ImVec2 p[3] = {ImVec2(centre.x - r * 0.45f, centre.y + r * 0.02f),
                             ImVec2(centre.x - r * 0.10f, centre.y + r * 0.38f),
                             ImVec2(centre.x + r * 0.48f, centre.y - r * 0.34f)};
        draw->AddPolyline(p, 3, IM_COL32(40, 28, 10, 255), 0, S(2.5f));
    } else {
        draw->AddCircle(centre, r, IM_COL32(150, 154, 176, 200), 32, S(2));
    }

    // Points (and the date) on the right, title and description on the left.
    char points[16];
    std::snprintf(points, sizeof points, "%d", a.points);
    const float pointsW = F.label->CalcTextSizeA(F.label->FontSize, FLT_MAX, 0.0f, points).x;
    const float ptsLabelW = F.small->CalcTextSizeA(F.small->FontSize, FLT_MAX, 0.0f, " PTS").x;
    const float right = max.x - S(12);
    const float titleY = min.y + S(9);
    draw->AddText(F.label, F.label->FontSize, ImVec2(right - ptsLabelW - pointsW, titleY),
                  a.unlocked ? kAmber : kTextDim, points);
    draw->AddText(F.small, F.small->FontSize,
                  ImVec2(right - ptsLabelW, titleY + F.label->FontSize - F.small->FontSize - S(1)), kTextDim, " PTS");
    float rightColumn = pointsW + ptsLabelW;
    const std::string date = a.unlocked ? FormatUnlockDate(a.unlockedAt) : std::string();
    if (!date.empty()) {
        const float dateW = F.small->CalcTextSizeA(F.small->FontSize, FLT_MAX, 0.0f, date.c_str()).x;
        draw->AddText(F.small, F.small->FontSize, ImVec2(right - dateW, titleY + F.label->FontSize + S(4)), kTextDim,
                      date.c_str());
        rightColumn = std::max(rightColumn, dateW);
    }

    const float textX = min.x + S(52);
    const ImVec4 clip(textX, min.y, right - rightColumn - S(16), max.y);
    draw->AddText(F.label, F.label->FontSize, ImVec2(textX, titleY), a.unlocked ? kText : IM_COL32(205, 205, 218, 255),
                  a.title.c_str(), nullptr, 0.0f, &clip);
    draw->AddText(F.small, F.small->FontSize, ImVec2(textX, titleY + F.label->FontSize + S(4)), kTextDim,
                  a.description.c_str(), nullptr, 0.0f, &clip);
    ImGui::PopID();
}

void AchievementsPage() {
    ScanAchievements();
    if (g_achievements.empty()) {
        Heading("ACHIEVEMENTS");
        ImGui::Indent(S(8));
        Paragraph("Play the game once and its achievements show up here.", kText);
        ImGui::Dummy(ImVec2(0, S(6)));
        Paragraph("Achievements are saved on this PC, next to your settings. Unlock them in the game and they "
                  "show up here, with the date you earned each one.");
        ImGui::Unindent(S(8));
        return;
    }

    int unlocked = 0, points = 0, totalPoints = 0;
    for (const Achievement& a : g_achievements) {
        totalPoints += a.points;
        if (a.unlocked) {
            ++unlocked;
            points += a.points;
        }
    }
    char heading[96];
    std::snprintf(heading, sizeof heading, "%d OF %zu UNLOCKED  \xC2\xB7  %d OF %d POINTS", unlocked,
                  g_achievements.size(), points, totalPoints);
    Heading(heading);

    // Progress bar.
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, h = S(6);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), IM_COL32(255, 255, 255, 30), h * 0.5f);
        const float t = g_achievements.empty() ? 0.0f : float(unlocked) / float(g_achievements.size());
        if (t > 0.0f) {
            draw->AddRectFilled(pos, ImVec2(pos.x + std::max(h, w * t), pos.y + h), kAmber, h * 0.5f);
        }
        ImGui::Dummy(ImVec2(0, h + S(8)));
    }

    const float width = ImGui::GetContentRegionAvail().x;
    for (const Achievement& a : g_achievements) {
        AchievementRow(a, width);
    }
    ImGui::Dummy(ImVec2(0, S(8)));
    ImGui::Indent(S(8));
    Paragraph("Saved on this PC. In the game, F10 > Achievements shows the same list.");
    ImGui::Unindent(S(8));
}

// --- Left column --------------------------------------------------------------------------------

// A small red "PC" tag; (right, top) is its top-right corner.
void PcTag(ImDrawList* draw, float right, float top, float height) {
    const ImVec2 text = F.label->CalcTextSizeA(height, FLT_MAX, 0.0f, "PC");
    const ImVec2 t1(right, top + height * 1.1f);
    const ImVec2 t0(t1.x - text.x - height * 0.7f, top);
    draw->AddRectFilled(ImVec2(t0.x + height * 0.08f, t0.y + height * 0.1f), ImVec2(t1.x + height * 0.08f, t1.y + height * 0.1f),
                        IM_COL32(20, 8, 4, 120), height * 0.25f);
    draw->AddRectFilled(t0, t1, IM_COL32(220, 40, 30, 255), height * 0.25f);
    draw->AddText(F.label, height, ImVec2(t0.x + height * 0.35f, t0.y + height * 0.05f), IM_COL32_WHITE, "PC");
}

void DrawLogo(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    const float boxW = max.x - min.x, boxH = max.y - min.y;
    if (g_logo) {
        // The logo from the disc's channel banner, as big as fits, centred.
        const float scale = std::min(boxW * 0.92f / g_logoSize.x, boxH / g_logoSize.y);
        const ImVec2 size(g_logoSize.x * scale, g_logoSize.y * scale);
        const ImVec2 at(min.x + (boxW - size.x) * 0.5f, min.y + (boxH - size.y) * 0.5f);
        draw->AddImage(g_logo, at, ImVec2(at.x + size.x, at.y + size.y));
        // "PC" under the end of TRUCK.
        PcTag(draw, at.x + size.x * 0.735f, at.y + size.y * 0.80f, size.y * 0.10f);
        return;
    }
    ImFont* font = F.logo;
    // EXCITE across most of the column, TRUCK smaller under it to the right.
    const float base = font->FontSize;
    const float wideW = font->CalcTextSizeA(base, FLT_MAX, 0.0f, "EXCITE").x;
    float size = std::min(base * (boxW * 0.80f) / wideW, boxH * 0.62f);
    const ImVec2 exciteSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, "EXCITE");
    const float truckSizePx = size * 0.56f;
    const ImVec2 truckSize = font->CalcTextSizeA(truckSizePx, FLT_MAX, 0.0f, "TRUCK");
    const float totalH = exciteSize.y * 0.82f + truckSize.y;
    const ImVec2 excitePos(min.x + (boxW - exciteSize.x) * 0.5f, min.y + (boxH - totalH) * 0.5f);
    const ImVec2 truckPos(excitePos.x + exciteSize.x - truckSize.x - size * 0.04f, excitePos.y + exciteSize.y * 0.80f);

    auto outlined = [&](ImVec2 pos, float px, const char* text, ImU32 top, ImU32 bottom, float outline) {
        // Shadow, outline, then the face with a top-to-bottom gradient.
        draw->AddText(font, px, ImVec2(pos.x + outline * 1.6f, pos.y + outline * 1.8f), IM_COL32(20, 8, 4, 120), text);
        const int steps = 16;
        for (int i = 0; i < steps; ++i) {
            const float a = float(i) / steps * 6.2831853f;
            draw->AddText(font, px, ImVec2(pos.x + std::cos(a) * outline, pos.y + std::sin(a) * outline),
                          IM_COL32(40, 12, 6, 255), text);
        }
        const int start = draw->VtxBuffer.Size;
        draw->AddText(font, px, pos, IM_COL32_WHITE, text);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size,
            ImVec2(pos.x, pos.y + px * 0.18f), ImVec2(pos.x, pos.y + px * 0.92f), top, bottom);
    };
    outlined(excitePos, size, "EXCITE", IM_COL32(255, 236, 90, 255), IM_COL32(236, 54, 28, 255), size * 0.045f);
    outlined(truckPos, truckSizePx, "TRUCK", IM_COL32(255, 255, 255, 255), IM_COL32(190, 200, 222, 255), size * 0.035f);

    // A small "PC" tag before TRUCK.
    PcTag(draw, truckPos.x - size * 0.10f, truckPos.y + truckSize.y * 0.30f, truckSize.y * 0.42f);
}

struct MenuItem {
    const char* label;
    const char* help;
    Page page;            // the page it opens (Start and Exit open none)
    UiAction action;
};

const MenuItem kMenu[] = {
    {"Start Game", "Start racing. Your settings are already saved.", Page::Settings, UiAction::Launch},
    {"Achievements", "The achievements you have unlocked in the game, and the ones still to get.", Page::Achievements, UiAction::None},
    {"Controls", "The Xbox controller layout, air control and vibration.", Page::Controls, UiAction::None},
    {"Settings", "Window, resolution, frame rate and sound.", Page::Settings, UiAction::None},
    {"Music", "Race to your own MP3s.", Page::Music, UiAction::None},
    {"Mods", "Texture packs.", Page::Mods, UiAction::None},
    {"Exit", "Close the launcher.", Page::Settings, UiAction::Quit},
};

bool IsPageItem(const MenuItem& item) { return item.action == UiAction::None; }
bool IsPageMenuItem(size_t index) { return index < std::size(kMenu) && IsPageItem(kMenu[index]); }
Page MenuPage(size_t index) { return kMenu[index].page; }

void DrawMenu() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float itemH = F.menu->FontSize * 1.18f;
    for (size_t i = 0; i < std::size(kMenu); ++i) {
        const MenuItem& item = kMenu[i];
        const bool current = IsPageItem(item) && item.page == g_page;
        NavRow();
        ImGui::PushID(int(i));
        const bool clicked = ImGui::InvisibleButton("item", ImVec2(ImGui::GetContentRegionAvail().x, itemH));
        ImGui::PopID();
        g_menuIds[i] = ImGui::GetItemID();
        const bool pressed = NavItemDone(clicked, item.label, item.help);
        const ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        const bool hot = ItemHasFocus();
        if (hot) {
            draw->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 20), S(6));
            draw->AddRectFilled(ImVec2(p0.x, p0.y + S(6)), ImVec2(p0.x + S(4), p1.y - S(6)), kAmber, S(2));
        }
        const ImU32 color = current ? kAmber : hot ? kText : IM_COL32(220, 220, 232, 255);
        SpacedText(draw, F.menu, F.menu->FontSize, ImVec2(p0.x + S(14), p0.y + (itemH - F.menu->FontSize) * 0.5f),
                   color, item.label, S(1.5f));
        if (pressed) {
            if (item.action != UiAction::None) {
                g_action = item.action;
            } else {
                g_page = item.page;
                g_resetPageScroll = true;
                // With a controller or keyboard, jump into the page that just opened.
                if (g_activatedByNav) {
                    g_enterPage = true;
                }
                if (item.page == Page::Music) {
                    ScanMusic(true);
                }
            }
        }
    }
}

void DrawHelp(ImVec2 min, ImVec2 max) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float pad = S(20);
    draw->AddText(F.title, F.title->FontSize, ImVec2(min.x + pad, min.y + pad - S(2)), g_helpError ? kError : kAmber,
                  g_helpTitle.c_str());
    draw->AddText(F.body, F.body->FontSize, ImVec2(min.x + pad, min.y + pad + F.title->FontSize + S(6)), kText,
                  g_helpText.c_str(), nullptr, max.x - min.x - pad * 2.0f);
}

void Panel(ImDrawList* draw, ImVec2 min, ImVec2 max) {
    draw->AddRectFilled(min, max, kPanel, S(12));
    draw->AddRect(min, max, kPanelBorder, S(12), 0, S(1));
}

void ControllerHints(ImDrawList* draw, ImVec2 rightBottom) {
    struct Hint { const char* glyph; ImU32 color; const char* text; };
    const Hint hints[] = {
        {"A", IM_COL32(110, 200, 80, 255), "Select"},
        {"B", IM_COL32(240, 80, 70, 255), "Back"},
    };
    float x = rightBottom.x;
    const float y = rightBottom.y - S(14);
    for (int i = int(std::size(hints)) - 1; i >= 0; --i) {
        const ImVec2 size = F.small->CalcTextSizeA(F.small->FontSize, FLT_MAX, 0.0f, hints[i].text);
        x -= size.x;
        draw->AddText(F.small, F.small->FontSize, ImVec2(x, y - size.y * 0.5f), kText, hints[i].text);
        x -= S(22);
        const float r = S(10);
        draw->AddCircleFilled(ImVec2(x + r - S(8), y), r, hints[i].color, 24);
        const ImVec2 g = F.small->CalcTextSizeA(F.small->FontSize * 0.85f, FLT_MAX, 0.0f, hints[i].glyph);
        draw->AddText(F.small, F.small->FontSize * 0.85f, ImVec2(x + r - S(8) - g.x * 0.5f, y - g.y * 0.5f),
                      IM_COL32(20, 20, 20, 255), hints[i].glyph);
        x -= S(24);
    }
}

} // namespace

// --- Public -------------------------------------------------------------------------------------

void UiSetLogo(void* texture, int width, int height) {
    g_logo = reinterpret_cast<ImTextureID>(texture);
    g_logoSize = ImVec2(float(width), float(height));
}

void UiInit(SDL_Window* window) {
    g_window = window;
    g_settings = cfg::Load();
    ScanMusic(true);
    // For screenshots: ET_LAUNCHER_PAGE=settings|controls|music|mods|achievements.
    if (const char* page = SDL_getenv("ET_LAUNCHER_PAGE")) {
        const std::string name = page;
        g_page = name == "controls" ? Page::Controls : name == "music" ? Page::Music : name == "mods" ? Page::Mods
               : name == "achievements" ? Page::Achievements : Page::Settings;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // Navigation is the launcher's own (rows and columns), so ImGui's is left off.
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);


    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.ScrollbarRounding = 6.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImColor(kText);
    c[ImGuiCol_TextDisabled] = ImColor(kTextOff);
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImColor(IM_COL32(40, 42, 62, 255));
    c[ImGuiCol_FrameBgHovered] = ImColor(IM_COL32(56, 58, 82, 255));
    c[ImGuiCol_FrameBgActive] = ImColor(IM_COL32(70, 70, 98, 255));
    c[ImGuiCol_SliderGrab] = ImColor(kAmber);
    c[ImGuiCol_SliderGrabActive] = ImColor(IM_COL32(255, 210, 120, 255));
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImColor(IM_COL32(255, 255, 255, 50));
    c[ImGuiCol_ScrollbarGrabHovered] = ImColor(IM_COL32(255, 255, 255, 80));
    c[ImGuiCol_ScrollbarGrabActive] = ImColor(IM_COL32(255, 255, 255, 110));
    c[ImGuiCol_NavCursor] = ImColor(kAmber);
    c[ImGuiCol_TextSelectedBg] = ImColor(IM_COL32(255, 182, 56, 90));
}

bool UiUpdateFonts(float windowWidth, float windowHeight) {
    const float scale = std::max(0.6f, std::min(windowHeight / 720.0f, windowWidth / 1280.0f));
    if (F.body != nullptr && std::fabs(scale - g_scale) < 0.04f) {
        return false;
    }
    g_scale = scale;
    ImGui::GetStyle().ScrollbarSize = S(10);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0};
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;   // the fonts live in the program
    config.OversampleH = 2;
    auto add = [&](const unsigned char* data, unsigned int size, float px) {
        return io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), int(size), std::round(px), &config, ranges);
    };
    F.body = add(et_font_barlow_regular, et_font_barlow_regular_size, S(19));
    F.label = add(et_font_barlow_semibold, et_font_barlow_semibold_size, S(21));
    F.small = add(et_font_barlow_semibold, et_font_barlow_semibold_size, S(15));
    F.menu = add(et_font_barlow_semibold, et_font_barlow_semibold_size, S(29));
    F.title = add(et_font_barlowcondensed_extrabolditalic, et_font_barlowcondensed_extrabolditalic_size, S(32));
    F.logo = add(et_font_barlowcondensed_extrabolditalic, et_font_barlowcondensed_extrabolditalic_size, S(132));
    io.FontDefault = F.body;
    io.Fonts->Build();
    return true;
}

void UiFrame(float seconds) {
    ApplyPickedFolder();
    const ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;

    DrawScene(ImGui::GetBackgroundDrawList(), io.DisplaySize, seconds);

    // Moves use last frame's items; this frame's are collected while drawing.
    g_navLast.swap(g_navItems);
    g_navItems.clear();
    g_navRow = -1;
    if (g_enterPage) {
        g_enterPage = false;
        FocusPage();
    }
    if (std::none_of(g_navLast.begin(), g_navLast.end(), [](const NavItem& i) { return i.id == g_focus; })) {
        g_focus = g_menuIds[0];   // start on Start Game, or after the focused item went away
    }
    ReadNavInput();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("launcher", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const float margin = S(22);
    const float footer = S(26);
    const float leftW = std::min(W * 0.44f, S(600));
    const float rightX = std::max(margin + leftW + S(28), W * 0.47f);

    // Logo, help and menu on the left.
    const ImVec2 logoMin(margin, margin), logoMax(margin + leftW, H * 0.31f);
    DrawLogo(draw, logoMin, logoMax);

    const float menuH = F.menu->FontSize * 1.18f * float(std::size(kMenu)) + S(28);
    const ImVec2 menuMin(margin, H - footer - margin * 0.5f - menuH), menuMax(margin + leftW, H - footer - margin * 0.5f);
    const ImVec2 helpMin(margin, std::max(logoMax.y + S(8), H * 0.33f)), helpMax(margin + leftW, menuMin.y - S(14));
    Panel(draw, helpMin, helpMax);
    DrawHelp(helpMin, helpMax);

    Panel(draw, menuMin, menuMax);
    ImGui::SetCursorScreenPos(ImVec2(menuMin.x + S(10), menuMin.y + S(14)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("menu", ImVec2(leftW - S(20), menuH - S(28)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    g_navRegion = kMenuRegion;
    DrawMenu();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // The current page on the right.
    const ImVec2 pageMin(rightX, margin), pageMax(W - margin, H - footer - margin * 0.5f);
    Panel(draw, pageMin, pageMax);
    ImGui::SetCursorScreenPos(ImVec2(pageMin.x + S(4), pageMin.y + S(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8), S(2)));
    ImGui::BeginChild("page", ImVec2(pageMax.x - pageMin.x - S(8), pageMax.y - pageMin.y - S(16)),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    g_navRegion = kPageRegion;
    if (g_resetPageScroll) {
        ImGui::SetScrollY(0.0f);
        g_resetPageScroll = false;
    }
    switch (g_page) {
        case Page::Settings: SettingsPage(); break;
        case Page::Controls: ControlsPage(); break;
        case Page::Music: MusicPage(); break;
        case Page::Mods: ModsPage(); break;
        case Page::Achievements: AchievementsPage(); break;
    }
    ImGui::Dummy(ImVec2(0, S(12)));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    // Footer: version on the left, controller hints on the right.
    draw->AddText(F.small, F.small->FontSize, ImVec2(margin + S(4), H - footer + S(2)), IM_COL32(255, 255, 255, 170),
                  "Excite Truck PC  \xC2\xB7  launcher " ET_LAUNCHER_VERSION);
    ControllerHints(draw, ImVec2(W - margin, H - S(2)));

    ImGui::End();
    if (SDL_getenv("ET_LAUNCHER_DEBUG")) {
        static ImGuiID last = 0;
        if (g_focus != last) {
            last = g_focus;
            SDL_Log("frame %d focus=%08x help=%s", ImGui::GetFrameCount(), g_focus, g_helpTitle.c_str());
        }
    }
}

UiAction UiTakeAction() {
    const UiAction action = g_action;
    g_action = UiAction::None;
    return action;
}

const char* UiMusicFolderForGame() { return g_musicFolderForGame.c_str(); }

void UiDropFile(const char* path) {
    const fs::path source = cfg::PathFromUtf8(path);
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        files = cfg::FindSongs(source);
    } else {
        std::string ext = source.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        if (ext == ".mp3") {
            files.push_back(source);
        }
    }
    g_page = Page::Music;
    if (files.empty()) {
        g_musicNotice = "Only MP3 files can be added.";
        return;
    }
    const fs::path folder = cfg::MusicFolder(g_settings);
    fs::create_directories(folder, ec);
    int copied = 0;
    for (const fs::path& file : files) {
        const fs::path target = folder / file.filename();
        if (fs::equivalent(file, target, ec)) {
            continue;
        }
        if (fs::copy_file(file, target, fs::copy_options::skip_existing, ec) && !ec) {
            ++copied;
        }
    }
    g_musicNotice = copied == 1 ? "Added 1 song." : "Added " + std::to_string(copied) + " songs.";
    ScanMusic(true);
}

void UiRefresh() {
    ScanMusic(true);
    ScanAchievements(true);
}

void UiShowError(const char* title, const char* text) {
    SetHelp(title, text, true);
    // Keep it up until the player moves to another item.
    g_helpItem = g_focus;
}
