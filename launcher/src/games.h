// The games the launcher knows about, where each one is installed and how it is set up.
#pragma once

#include <filesystem>
#include <string>

enum class GameId { Truck = 0, Bots = 1 };
constexpr int kGameCount = 2;

struct GameDef {
    GameId id;
    const char* key;        // "truck", "bots": setup folder names, test overrides
    const char* title;      // shown to players
    const char* discId;     // the Wii disc's game id (USA)
    const char* folder;     // install folder name
    const char* exe;        // the game's program, without .exe
    const char* logoTexture;   // its logo in the disc's channel banner (files/opening.bnr)
    const char* nameHint;   // lower-case text a disc image's file name usually holds
};

const GameDef& Game(GameId id);
inline GameId GameAt(int index) { return GameId(index); }

// Where the game is (or will be) installed. Windows: %LOCALAPPDATA%\Programs\<folder>.
// Linux: beside the launcher's folder (<repo>/<folder>). ET_LAUNCHER_<KEY>_DIR overrides it.
std::filesystem::path InstallDir(GameId id);
std::filesystem::path GameExecutable(GameId id);   // empty when not installed
bool IsInstalled(GameId id);

// The build script the launcher runs to set the game up, from the folder the launcher was
// installed with (setup/<key>/). Empty when this launcher has no setup for the game yet.
// ET_LAUNCHER_<KEY>_SCRIPT overrides it.
std::filesystem::path SetupScript(GameId id);

// Folder the launcher keeps its own files in (setup logs).
std::filesystem::path LauncherDataDir();
std::filesystem::path LauncherDir();

// Which game a disc image (or extracted disc folder) is: by the id in its header, else by its
// file name for zips. Returns -1 if it can't tell, else the GameId as int. `known` is set when
// the image was readable and is a Wii disc we don't support.
int IdentifyDisc(const std::filesystem::path& path, std::string* discId = nullptr);
bool LooksLikeDiscImage(const std::filesystem::path& path);
