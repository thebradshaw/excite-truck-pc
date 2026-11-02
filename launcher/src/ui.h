// The launcher's screens: a game chooser that also sets games up from their discs, then each
// game's own menu and settings pages (the same layout as the 1080 Snowboarding launcher).
#pragma once

#include "games.h"

struct SDL_Window;
struct SDL_Renderer;

enum class UiAction { None, Launch, Quit };

void UiInit(SDL_Window* window, SDL_Renderer* renderer);
void UiShutdown();
// Loads the fonts for a window of the given size. Returns true when the font atlas changed
// (the renderer's font texture must then be rebuilt).
bool UiUpdateFonts(float windowWidth, float windowHeight);
void UiFrame(float seconds);
UiAction UiTakeAction();
// The game whose menu is open (the one Start Game starts).
GameId UiSelectedGame();
// The music folder to hand to the game (empty: its default).
const char* UiMusicFolderForGame();
// A file dropped on the window (mouseX < 0: not dropped with the mouse): disc images go to their
// game's card, MP3s into Excite Truck's music folder.
void UiDropFile(const char* path, float mouseX);
// The window came back to the front: rescan installs and folders.
void UiRefresh();
// Shows an error in the help box (for example when the game could not be started).
void UiShowError(const char* title, const char* text);
