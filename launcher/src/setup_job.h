// Builds a game from the player's disc by running its build script in the background, one game
// at a time, and turns the script's output into a progress bar.
//
// What the launcher reads from a build script's output (one item per line):
//   ==> Step name          a new step; shown under the progress bar
//   ##progress P           overall progress is now P percent (0-100)
//   ##progress P Q         the steps that follow run from P to Q percent, and ninja's [n/m]
//                          lines move the bar between the two
//   [n/m] ...              ninja / CMake build progress
//   Error: text            why the build stopped (the last one is shown)
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "games.h"

enum class SetupState { Idle, Queued, Running, Done, Failed, Cancelled };

struct SetupStatus {
    SetupState state = SetupState::Idle;
    float progress = 0.0f;          // 0..1
    std::string step;               // current step, or the error when Failed
    double startedAt = 0.0;         // SDL_GetTicks seconds
    double finishedAt = 0.0;
    std::filesystem::path disc;
    std::filesystem::path log;      // the launcher's copy of the script output
};

void SetupStart(GameId id, const std::filesystem::path& disc);   // queues it behind any other
void SetupCancel(GameId id);
SetupStatus SetupGet(GameId id);
bool SetupBusy();                     // something queued or running
void SetupClear(GameId id);           // back to Idle after Done/Failed/Cancelled
void SetupStopAll();                  // on exit: stop the running build and its compilers
// Call once per frame: starts the next queued build when the previous one ends.
// Returns true when a build finished since the last call.
bool SetupPump();
