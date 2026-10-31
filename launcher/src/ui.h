// The launcher screen: logo, help box and main menu on the left, a page of settings on the right
// (the same layout as the 1080 Snowboarding launcher).
#pragma once

struct SDL_Window;

enum class UiAction { None, Launch, Quit };

void UiInit(SDL_Window* window);
// The game's own logo (an SDL texture), drawn in place of the text logo. nullptr = text logo.
void UiSetLogo(void* texture, int width, int height);
// Loads the fonts for a window of the given size. Returns true when the font atlas changed
// (the renderer's font texture must then be rebuilt).
bool UiUpdateFonts(float windowWidth, float windowHeight);
void UiFrame(float seconds);
UiAction UiTakeAction();
// Settings saved by the player and the music folder to hand to the game.
const char* UiMusicFolderForGame();
// A file dropped on the window: MP3s are copied into the music folder.
void UiDropFile(const char* path);
// The window came back to the front: rescan the music folder.
void UiRefresh();
// Shows an error in the help box (for example when the game could not be started).
void UiShowError(const char* title, const char* text);
