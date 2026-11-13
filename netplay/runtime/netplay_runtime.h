// Network Play inside the games (both Excite Truck and Excitebots). Copied into the runtime as
// runtime/src/netplay/ by the build scripts; the room protocol itself is the netplay library.
//
// The launcher starts a game with WC_NET_* variables; with none set, everything here stays off
// and costs nothing. The game hooks (et_netplay.cpp, eb_netplay.cpp) call in through Bridge and
// the functions below; vi.cpp calls Frame() once a presented frame and settings_overlay.cpp calls
// DrawOverlay() with the other ImGui overlays.
#pragma once

#include "netplay/session.h"

#include <cstdint>
#include <string>

namespace NetplayRuntime {

// What the game can do for Network Play. Each game registers one at startup.
struct Bridge {
    virtual ~Bridge() = default;
    // In the menus, free to start a race an invite asks for.
    virtual bool CanJoinNow() = 0;
    // The player's own save has this cup unlocked (Network Play opens every race of it).
    virtual bool CupUnlocked(const std::string& cupKey) = 0;
    // Start the race an invite is for (the game's normal pre-race flow follows).
    virtual void BeginRace(const std::string& contestKey) = 0;
    // The vehicle and paint the player would race with now.
    virtual void CurrentVehicle(uint32_t& vehicle, uint32_t& variant) = 0;
    // A game frame (guest thread) for per-race work: sending and applying vehicle states.
    virtual void RaceFrame(netplay::Session& session) = 0;
};

void Register(Bridge* bridge);
bool Active();                       // started with WC_NET_URL
netplay::Session* Session();        // null when not active
// Once a presented frame (game thread).
void Frame();
// ImGui overlay: room badge, toasts, invites, the starter's countdown, name tags.
void DrawOverlay();
// The View button is Network Play's in the menus while a room is open (et_input skips it).
bool OwnsViewButton();

// Race hooks for the games.
// The player picked a race in the menus (any game mode it can be shared from).
void LocalRaceChosen(const std::string& contestKey, const std::string& title, const std::string& cupKey);
// True while the race must stay at the start line (waiting for the others).
bool HoldAtLine();
// The race left (finished, quit, or restarted).
void LocalRaceEnded();

// Name tags, set by the game each frame for the slots of other people (screen pixels, 0..1).
void SetNameTag(uint8_t slot, float x, float y, bool visible);

} // namespace NetplayRuntime
