// The settings the launcher shows, read from and written to the game's own Config.toml.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct LauncherSettings {
    std::string displayMode = "borderless";   // windowed, borderless, exclusive
    float resolution = 0.0f;                  // 0 = follow the window, else a multiple of 480p
    bool widescreen = true;
    bool vsync = false;
    unsigned frameRate = 0;                   // [video] frame_interpolation_fps: 0 (60 fps) or 120
    bool showFps = true;
    std::string graphicsApi = "auto";         // auto, d3d12, vulkan (Windows only)
    int volumePercent = 100;
    bool invertAirControl = true;
    bool rumble = true;
    bool texturePacks = false;
    std::string musicFolder;                  // empty = the default Music folder
};

namespace launcher_config {

// Reads Config.toml (creating it the way the game would) and applies the game's one-time
// Excite Truck defaults, so the first launch shows what the game will really use.
LauncherSettings Load();

// Each setter writes one value straight away, like the game's F10 bar.
void SetDisplayMode(const std::string& mode);
void SetResolution(float multiplier);
void SetWidescreen(bool on);
void SetVsync(bool on);
void SetFrameRate(unsigned fps);
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
bool DiscFound();
// ExciteTruck(.exe) next to the launcher; empty when it isn't there.
std::filesystem::path GameExecutable();

std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& text);

// Every MP3 under the folder, in the order the game's SD card lists them.
std::vector<std::filesystem::path> FindSongs(const std::filesystem::path& folder);

// The game's 48x48 save icon from the disc (RGBA8), for the window icon.
bool LoadGameIcon(std::vector<unsigned char>& rgba);

} // namespace launcher_config
