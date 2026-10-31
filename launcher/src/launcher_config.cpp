#include "launcher_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

// The game's own config code, so both programs agree on where Config.toml lives (portable.txt
// next to the game, else %LOCALAPPDATA%\WiiCompiled or ~/.local/share/WiiCompiled).
#include "runtime_config.h"

namespace fs = std::filesystem;

namespace launcher_config {
namespace {

constexpr const char* kLauncherSection = "launcher";

std::string ReadLauncherString(const char* key) {
    try {
        std::ifstream input(RuntimeConfigFile::ResolveConfigPath(), std::ios::binary);
        if (!input) {
            return {};
        }
        const toml::value document = toml::parse(input, "Config.toml");
        if (auto value = RuntimeConfigFile::FindConfigValue<std::string>(document, kLauncherSection, key)) {
            return *value;
        }
    } catch (const std::exception&) {
    }
    return {};
}

void Write(std::string_view section, std::string_view key, const std::string& literal) {
    RuntimeConfigFile::WriteSetting(section, key, literal);
}

std::string Bool(bool value) { return value ? "true" : "false"; }

} // namespace

std::string PathToUtf8(const fs::path& path) { return RuntimeConfigFile::PathToUtf8(path); }
fs::path PathFromUtf8(const std::string& text) { return RuntimeConfigFile::PathFromUtf8(text); }

LauncherSettings Load() {
    RuntimeConfigFile::EnsureConfigFile();
    // Same one-time defaults the game applies on its first run (main.cpp): borderless, and the
    // picture rendered at the window's resolution. Applying them here keeps a choice made in the
    // launcher before the game's first run from being overwritten by them.
    if (RuntimeConfigFile::EtDefaultsVersion() < 1) {
        RuntimeConfigFile::SetDisplayMode("borderless");
        RuntimeConfigFile::SetResolutionMultiplier(0.0f);
        RuntimeConfigFile::SetEtDefaultsVersion(1);
        RuntimeConfigFile::Mutable() = RuntimeConfigFile::LoadConfigFile();
    }

    LauncherSettings s;
    s.displayMode = RuntimeConfigFile::DisplayMode("windowed");
    s.resolution = RuntimeConfigFile::ResolutionMultiplier(1.0f);
    s.widescreen = RuntimeConfigFile::WidescreenEnabled(true);
    s.vsync = RuntimeConfigFile::Vsync();
    s.frameRate = RuntimeConfigFile::FrameInterpolationFps(0);
    s.showFps = RuntimeConfigFile::ShowFps();
    s.graphicsApi = RuntimeConfigFile::GraphicsApi("auto");
    s.volumePercent = static_cast<int>(RuntimeConfigFile::AudioVolume(1.0f) * 100.0f + 0.5f);
    s.invertAirControl = RuntimeConfigFile::InvertAirControl();
    s.rumble = RuntimeConfigFile::RumbleEnabled();
    s.texturePacks = RuntimeConfigFile::TextureReplacements(false);
    s.musicFolder = ReadLauncherString("music_folder");
    return s;
}

void SetDisplayMode(const std::string& mode) { RuntimeConfigFile::SetDisplayMode(mode); }
void SetResolution(float multiplier) { RuntimeConfigFile::SetResolutionMultiplier(multiplier); }
void SetWidescreen(bool on) { Write("video", "widescreen", Bool(on)); }
void SetVsync(bool on) { RuntimeConfigFile::SetVsync(on); }
void SetFrameRate(unsigned fps) { RuntimeConfigFile::SetFrameInterpolationFps(fps); }
void SetShowFps(bool on) { RuntimeConfigFile::SetShowFps(on); }
void SetGraphicsApi(const std::string& api) { Write("video", "graphics_api", RuntimeConfigFile::FormatString(api)); }
void SetVolumePercent(int percent) { RuntimeConfigFile::SetAudioVolume(std::clamp(percent, 0, 100) / 100.0f); }
void SetInvertAirControl(bool on) { RuntimeConfigFile::SetInvertAirControl(on); }
void SetRumble(bool on) { RuntimeConfigFile::SetRumbleEnabled(on); }
void SetTexturePacks(bool on) { Write("video", "texture_replacements", Bool(on)); }
void SetMusicFolder(const std::string& folder) {
    Write(kLauncherSection, "music_folder", RuntimeConfigFile::FormatString(folder));
}

fs::path ConfigPath() { return RuntimeConfigFile::ResolveConfigPath(); }
fs::path UserDataDirectory() { return RuntimeConfigFile::ApplicationDataDirectory(); }
fs::path DefaultMusicFolder() { return UserDataDirectory() / "Music"; }
fs::path TextureFolder() { return UserDataDirectory() / "texture_replacements"; }

fs::path MusicFolder(const LauncherSettings& settings) {
    if (!settings.musicFolder.empty()) {
        return PathFromUtf8(settings.musicFolder);
    }
    return DefaultMusicFolder();
}

fs::path DiscFolder() { return RuntimeConfigFile::ResolvedDvdRoot(); }

bool DiscFound() {
    std::error_code ec;
    return fs::is_regular_file(DiscFolder() / "sys" / "main.dol", ec);
}

fs::path GameExecutable() {
    const auto directory = RuntimeConfigFile::ExecutableDirectory();
    if (!directory) {
        return {};
    }
#ifdef _WIN32
    const char* names[] = {"ExciteTruck.exe", "WiiCompiled.exe"};
#else
    const char* names[] = {"ExciteTruck", "WiiCompiled"};
#endif
    for (const char* name : names) {
        std::error_code ec;
        const fs::path candidate = *directory / name;
        if (fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

std::vector<fs::path> FindSongs(const fs::path& folder) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return files;
    }
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".mp3" && it->is_regular_file(ec)) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Same decoding as the game's window icon: files/mcbanner.res is a WIBN banner and the 48x48
// RGB5A3 icon sits 0x60A0 bytes in, stored in 4x4 tiles.
bool LoadGameIcon(std::vector<unsigned char>& rgba) {
    std::ifstream in(DiscFolder() / "files" / "mcbanner.res", std::ios::binary);
    if (!in) {
        return false;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t wibn = std::string::npos;
    for (size_t i = 0; i + 4 <= bytes.size(); i += 4) {
        if (bytes[i] == 'W' && bytes[i + 1] == 'I' && bytes[i + 2] == 'B' && bytes[i + 3] == 'N') {
            wibn = i;
            break;
        }
    }
    if (wibn == std::string::npos || wibn + 0x60A0 + 48 * 48 * 2 > bytes.size()) {
        return false;
    }
    const unsigned char* src = bytes.data() + wibn + 0x60A0;
    rgba.assign(48 * 48 * 4, 0);
    size_t k = 0;
    for (int ty = 0; ty < 48; ty += 4)
        for (int tx = 0; tx < 48; tx += 4)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x, k += 2) {
                    const uint32_t v = (uint32_t(src[k]) << 8) | src[k + 1];
                    uint32_t r, g, b, a;
                    if (v & 0x8000) {
                        a = 255; r = ((v >> 10) & 31) * 255 / 31; g = ((v >> 5) & 31) * 255 / 31; b = (v & 31) * 255 / 31;
                    } else {
                        a = ((v >> 12) & 7) * 255 / 7; r = ((v >> 8) & 15) * 17; g = ((v >> 4) & 15) * 17; b = (v & 15) * 17;
                    }
                    unsigned char* px = &rgba[((ty + y) * 48 + tx + x) * 4];
                    px[0] = (unsigned char)r; px[1] = (unsigned char)g; px[2] = (unsigned char)b; px[3] = (unsigned char)a;
                }
    return true;
}

} // namespace launcher_config
