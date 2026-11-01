#include "launcher_config.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <system_error>

#include <SDL3/SDL.h>

#include "toml.hpp"

namespace fs = std::filesystem;

namespace launcher_config {
namespace {

GameId g_game = GameId::Truck;

using Ordered = toml::ordered_value;

fs::path ConfigPathFor(GameId id) { return InstallDir(id) / "UserData" / "Config.toml"; }

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
}

// The parsed Config.toml of the selected game, re-read when the file changes.
struct Document {
    fs::path path;
    fs::file_time_type stamp{};
    std::optional<Ordered> root;
};
Document g_doc;

const Ordered* Find(std::string_view section, std::string_view key) {
    const fs::path path = ConfigPath();
    std::error_code ec;
    const auto stamp = fs::last_write_time(path, ec);
    if (path != g_doc.path || ec || stamp != g_doc.stamp) {
        g_doc.path = path;
        g_doc.stamp = ec ? fs::file_time_type{} : stamp;
        g_doc.root.reset();
        try {
            g_doc.root = toml::parse_str<toml::ordered_type_config>(ReadText(path));
        } catch (const std::exception&) {
        }
    }
    if (!g_doc.root || !g_doc.root->is_table()) return nullptr;
    const Ordered* table = &*g_doc.root;
    std::istringstream parts{std::string(section)};
    for (std::string part; std::getline(parts, part, '.');) {
        if (!table->is_table() || !table->as_table().count(part)) return nullptr;
        table = &table->as_table().at(part);
    }
    if (!table->is_table() || !table->as_table().count(std::string(key))) return nullptr;
    return &table->as_table().at(std::string(key));
}

bool GetBool(std::string_view section, std::string_view key, bool fallback) {
    const Ordered* v = Find(section, key);
    return v && v->is_boolean() ? v->as_boolean() : fallback;
}
double GetNumber(std::string_view section, std::string_view key, double fallback) {
    const Ordered* v = Find(section, key);
    if (v && v->is_floating()) return v->as_floating();
    if (v && v->is_integer()) return double(v->as_integer());
    return fallback;
}
std::string GetString(std::string_view section, std::string_view key, const std::string& fallback) {
    const Ordered* v = Find(section, key);
    return v && v->is_string() ? v->as_string() : fallback;
}

// Sets one key, keeping the rest of the file (comments included) as it was. `literal` is TOML.
void Write(std::string_view section, std::string_view key, const std::string& literal) {
    const fs::path path = ConfigPath();
    try {
        auto root = toml::parse_str<toml::ordered_type_config>(ReadText(path));
        auto value = toml::parse_str<toml::ordered_type_config>("v = " + literal);
        Ordered* table = &root;
        std::istringstream parts{std::string(section)};
        for (std::string part; std::getline(parts, part, '.');) {
            auto& parent = table->as_table();
            if (!parent.count(part)) parent[part] = Ordered(toml::ordered_table{});
            table = &parent.at(part);
            if (!table->is_table()) return;
        }
        if (table->as_table_fmt().fmt == toml::table_format::implicit) {
            table->as_table_fmt().fmt = toml::table_format::multiline;
        }
        auto& slot = table->as_table()[std::string(key)];
        auto& updated = value.as_table().at("v");
        updated.comments() = slot.comments();
        slot = std::move(updated);
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const fs::path temp = path.string() + ".tmp";
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << toml::format(root);
        }
        fs::rename(temp, path, ec);
        if (ec) {
            fs::remove(path, ec);
            fs::rename(temp, path, ec);
        }
    } catch (const std::exception&) {
        // A Config.toml the game can't read either: leave it for the player to fix.
    }
}

std::string Bool(bool value) { return value ? "true" : "false"; }
std::string Str(const std::string& value) { return toml::format(toml::value(value)); }
std::string Num(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

} // namespace

void SelectGame(GameId id) { g_game = id; }
GameId SelectedGame() { return g_game; }

std::string PathToUtf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
fs::path PathFromUtf8(const std::string& text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

LauncherSettings Load() {
    // Excite Truck's one-time defaults (its main.cpp applies the same on first run): borderless,
    // and the picture rendered at the window's resolution. Applying them here keeps a choice
    // made in the launcher before the game's first run from being overwritten by them.
    if (g_game == GameId::Truck && GetNumber("excite_truck", "defaults_version", 0) < 1) {
        Write("video", "display_mode", Str("borderless"));
        Write("video", "resolution_multiplier", "0");
        Write("excite_truck", "defaults_version", "1");
    }
    LauncherSettings s;
    s.displayMode = GetString("video", "display_mode", "windowed");
    s.resolution = float(std::max(0.0, GetNumber("video", "resolution_multiplier", 1.0)));
    s.widescreen = GetBool("video", "widescreen", true);
    s.vsync = GetBool("video", "vsync", false);
    s.frameRate = unsigned(GetNumber("video", "frame_interpolation_fps", 0));
    s.msaa = unsigned(GetNumber("video", "msaa", 1));
    s.anisotropic = GetBool("video", "anisotropic_filtering", true);
    s.drawDistance = float(GetNumber("excite_truck", "draw_distance", 1.0));
    s.showFps = GetBool("video", "show_fps", true);
    s.graphicsApi = GetString("video", "graphics_api", "auto");
    s.volumePercent = int(std::clamp(GetNumber("audio", "volume", 1.0), 0.0, 1.0) * 100.0 + 0.5);
    s.invertAirControl = GetBool("controller", "invert_air_control", true);
    s.rumble = GetBool("controller", "rumble", true);
    s.texturePacks = GetBool("video", "texture_replacements", false);
    s.musicFolder = GetString("launcher", "music_folder", "");
    return s;
}

void SetDisplayMode(const std::string& mode) { Write("video", "display_mode", Str(mode)); }
void SetResolution(float multiplier) { Write("video", "resolution_multiplier", Num(multiplier)); }
void SetWidescreen(bool on) { Write("video", "widescreen", Bool(on)); }
void SetVsync(bool on) { Write("video", "vsync", Bool(on)); }
void SetFrameRate(unsigned fps) { Write("video", "frame_interpolation_fps", std::to_string(fps)); }
void SetMsaa(unsigned samples) { Write("video", "msaa", std::to_string(samples)); }
void SetAnisotropic(bool on) { Write("video", "anisotropic_filtering", Bool(on)); }
void SetDrawDistance(float multiplier) { Write("excite_truck", "draw_distance", Num(multiplier)); }
void SetShowFps(bool on) { Write("video", "show_fps", Bool(on)); }
void SetGraphicsApi(const std::string& api) { Write("video", "graphics_api", Str(api)); }
void SetVolumePercent(int percent) { Write("audio", "volume", Num(std::clamp(percent, 0, 100) / 100.0)); }
void SetInvertAirControl(bool on) { Write("controller", "invert_air_control", Bool(on)); }
void SetRumble(bool on) { Write("controller", "rumble", Bool(on)); }
void SetTexturePacks(bool on) { Write("video", "texture_replacements", Bool(on)); }
void SetMusicFolder(const std::string& folder) { Write("launcher", "music_folder", Str(folder)); }

fs::path ConfigPath() { return ConfigPathFor(g_game); }
fs::path UserDataDirectory() { return InstallDir(g_game) / "UserData"; }
fs::path DefaultMusicFolder() { return UserDataDirectory() / "Music"; }
fs::path TextureFolder() { return UserDataDirectory() / "texture_replacements"; }

fs::path MusicFolder(const LauncherSettings& settings) {
    if (!settings.musicFolder.empty()) {
        return PathFromUtf8(settings.musicFolder);
    }
    return DefaultMusicFolder();
}

fs::path DiscFolder(GameId id) {
    // [paths] dvd_root, relative to the config file's folder; the build scripts put the disc
    // in <install dir>/Disc.
    const GameId previous = g_game;
    g_game = id;
    const std::string root = GetString("paths", "dvd_root", "");
    g_game = previous;
    if (root.empty()) {
        return InstallDir(id) / "Disc";
    }
    fs::path path = PathFromUtf8(root);
    if (path.is_relative()) {
        path = ConfigPathFor(id).parent_path() / path;
    }
    return path.lexically_normal();
}
fs::path DiscFolder() { return DiscFolder(g_game); }

bool DiscFound() {
    std::error_code ec;
    return fs::is_regular_file(DiscFolder() / "sys" / "main.dol", ec);
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
bool LoadGameIcon(GameId id, std::vector<unsigned char>& rgba) {
    std::ifstream in(DiscFolder(id) / "files" / "mcbanner.res", std::ios::binary);
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
