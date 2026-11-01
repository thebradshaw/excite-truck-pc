#include "games.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <system_error>

#include "launcher_config.h"

namespace fs = std::filesystem;

namespace {

const GameDef kGames[kGameCount] = {
    {GameId::Truck, "truck", "Excite Truck", "REXE01", "ExciteTruck", "ExciteTruck", "ex_logo.tpl", "truck"},
    {GameId::Bots, "bots", "Excitebots", "RX3E01", "Excitebots", "Excitebots", "BannerLogo_b.tpl", "bots"},
};

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

fs::path EnvPath(const std::string& name) {
    const char* value = SDL_getenv(name.c_str());
    return value && *value ? launcher_config::PathFromUtf8(value) : fs::path();
}

std::string Upper(const char* text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return out;
}

std::string ReadId(const fs::path& path, uint64_t offset) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(std::streamoff(offset));
    char id[6] = {};
    if (!in.read(id, 6)) return {};
    for (char c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return {};
    }
    return std::string(id, 6);
}

} // namespace

const GameDef& Game(GameId id) { return kGames[int(id)]; }

fs::path LauncherDir() {
    const char* base = SDL_GetBasePath();
    return base ? launcher_config::PathFromUtf8(base) : fs::current_path();
}

fs::path LauncherDataDir() {
#ifdef _WIN32
    if (const char* local = SDL_getenv("LOCALAPPDATA"); local && *local) {
        return launcher_config::PathFromUtf8(local) / "ExcitePC";
    }
#else
    if (const char* xdg = SDL_getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return launcher_config::PathFromUtf8(xdg) / "excite-pc";
    }
    if (const char* home = SDL_getenv("HOME"); home && *home) {
        return launcher_config::PathFromUtf8(home) / ".local" / "share" / "excite-pc";
    }
#endif
    return LauncherDir() / "LauncherData";
}

fs::path InstallDir(GameId id) {
    const GameDef& game = Game(id);
    if (fs::path over = EnvPath("ET_LAUNCHER_" + Upper(game.key) + "_DIR"); !over.empty()) {
        return over;
    }
#ifdef _WIN32
    if (const char* local = SDL_getenv("LOCALAPPDATA"); local && *local) {
        return launcher_config::PathFromUtf8(local) / "Programs" / game.folder;
    }
    return LauncherDataDir() / game.folder;
#else
    // Linux: scripts/build-launcher.sh puts the launcher in <repo>/ExcitePC, and the build scripts
    // put the games beside it (<repo>/ExciteTruck, <repo>/Excitebots).
    return (LauncherDir() / ".." / game.folder).lexically_normal();
#endif
}

fs::path GameExecutable(GameId id) {
    const fs::path dir = InstallDir(id);
#ifdef _WIN32
    const std::string suffix = ".exe";
#else
    const std::string suffix;
#endif
    std::error_code ec;
    for (const std::string name : {std::string(Game(id).exe), std::string("WiiCompiled")}) {
        const fs::path candidate = dir / (name + suffix);
        if (fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

bool IsInstalled(GameId id) { return !GameExecutable(id).empty(); }

fs::path SetupScript(GameId id) {
    const GameDef& game = Game(id);
    if (fs::path over = EnvPath("ET_LAUNCHER_" + Upper(game.key) + "_SCRIPT"); !over.empty()) {
        return over;
    }
#ifdef _WIN32
    const char* name = "build-windows.ps1";
#else
    const char* name = "build-linux.sh";
#endif
    std::error_code ec;
    const fs::path candidate = LauncherDir() / "setup" / game.key / name;
    return fs::is_regular_file(candidate, ec) ? candidate : fs::path();
}

bool LooksLikeDiscImage(const fs::path& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        return fs::is_regular_file(path / "sys" / "main.dol", ec);
    }
    const std::string name = Lower(launcher_config::PathToUtf8(path.filename()));
    static const std::regex pattern(R"(\.(wbfs|iso|rvz|ciso|wia|gcz|zip)$|\.zip\.\d{3}$)");
    return std::regex_search(name, pattern);
}

int IdentifyDisc(const fs::path& path, std::string* discId) {
    std::error_code ec;
    std::string id;
    if (fs::is_directory(path, ec)) {
        id = ReadId(path / "sys" / "boot.bin", 0);
    } else {
        std::ifstream in(path, std::ios::binary);
        char magic[4] = {};
        in.read(magic, 4);
        if (std::memcmp(magic, "WBFS", 4) == 0) {
            // The disc header copy starts one "hd sector" in; the sector size is 1 << byte 8.
            in.seekg(8);
            const int shift = in.get();
            if (shift > 0 && shift < 20) id = ReadId(path, uint64_t(1) << shift);
        } else if (std::memcmp(magic, "RVZ\x01", 4) == 0 || std::memcmp(magic, "WIA\x01", 4) == 0) {
            id = ReadId(path, 0x58);   // header 2's copy of the disc header
        } else if (std::memcmp(magic, "PK", 2) != 0) {
            id = ReadId(path, 0);      // plain ISO
        }
    }
    if (discId) *discId = id;
    for (int i = 0; i < kGameCount; ++i) {
        if (!id.empty() && id == kGames[i].discId) return i;
    }
    if (id.empty()) {
        // Zips (and unreadable files): go by the name.
        const std::string name = Lower(launcher_config::PathToUtf8(path.filename()));
        for (int i = 0; i < kGameCount; ++i) {
            if (name.find(kGames[i].nameHint) != std::string::npos) return i;
        }
    }
    return -1;
}
