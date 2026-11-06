// The settings the launcher shows, read from and written to the selected game's Config.toml
// (<install dir>/UserData/Config.toml: the games are installed portable).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "games.h"

struct LauncherSettings {
    std::string displayMode = "borderless";   // windowed, borderless, exclusive
    float resolution = 0.0f;                  // 0 = follow the window, else a multiple of 480p
    bool widescreen = true;
    bool vsync = false;
    unsigned frameRate = 0;                   // [video] frame_interpolation_fps: 0 (60 fps), 120, 180
    unsigned msaa = 1;                        // [video] msaa: 1, 2, 4, 8
    bool anisotropic = true;                  // [video] anisotropic_filtering
    float drawDistance = 1.0f;                // [excite_truck] draw_distance: 1.0 to 4.0
    bool showFps = true;
    std::string graphicsApi = "auto";         // auto, d3d12, vulkan (Windows only)
    int volumePercent = 100;
    bool invertAirControl = true;
    bool rumble = true;
    bool texturePacks = false;
    std::string musicFolder;                  // empty = the default Music folder
};

namespace launcher_config {

// Every call below works on this game's files.
void SelectGame(GameId id);
GameId SelectedGame();

// Reads Config.toml and applies the game's one-time defaults (Excite Truck: borderless, Auto
// resolution), so the first launch shows what the game will really use.
LauncherSettings Load();

// Each setter writes one value straight away, like the game's F10 bar.
void SetDisplayMode(const std::string& mode);
void SetResolution(float multiplier);
void SetWidescreen(bool on);
void SetVsync(bool on);
void SetFrameRate(unsigned fps);
void SetMsaa(unsigned samples);
void SetAnisotropic(bool on);
void SetDrawDistance(float multiplier);
void SetShowFps(bool on);
void SetGraphicsApi(const std::string& api);
void SetVolumePercent(int percent);
void SetInvertAirControl(bool on);
void SetRumble(bool on);
void SetTexturePacks(bool on);
void SetMusicFolder(const std::string& folder);   // empty = back to the default

std::filesystem::path ConfigPath();
std::filesystem::path UserDataDirectory();
std::filesystem::path DefaultMusicFolder();
std::filesystem::path MusicFolder(const LauncherSettings& settings);
std::filesystem::path TextureFolder();
// The extracted disc (holds sys/main.dol), from [paths] dvd_root.
std::filesystem::path DiscFolder();
std::filesystem::path DiscFolder(GameId id);
bool DiscFound();

std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& text);

// Every MP3 under the folder, in the order the game's SD card lists them.
std::vector<std::filesystem::path> FindSongs(const std::filesystem::path& folder);

// A game's 48x48 save icon from its disc (RGBA8), for the window icon.
bool LoadGameIcon(GameId id, std::vector<unsigned char>& rgba);

} // namespace launcher_config
