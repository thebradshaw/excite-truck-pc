// Grey, shaded side views of each game's vehicle, drawn behind the logos on the home screen.
// They are drawn here from simple shapes (not taken from the games) and rendered once on the CPU.
#pragma once

#include <cstdint>
#include <vector>

#include "banner.h"
#include "games.h"

// The picture covers x 0..1 and y kVehicleTop..kVehicleTop+kVehicleHeight in the vehicle's own
// units (1 = its length); the body sits around y 0.24.
constexpr float kVehicleTop = -0.10f;
constexpr float kVehicleHeight = 0.6f;

// Renders the vehicle `width` pixels wide into `rgba` (straight alpha, R=G=B), facing right, or
// left when `faceLeft`. The light comes from the top left either way; `solid` leaves it white, as
// a plain silhouette. Returns the height.
int RenderVehicle(GameId game, bool faceLeft, int width, std::vector<uint32_t>& rgba, bool solid = false);

// The texture in the disc's channel banner that the game's silhouette is traced from: Excite
// Truck's strip of truck silhouettes, Excitebots' line drawing of the grasshopper bot.
const char* SilhouetteSource(GameId game);

// Traces a smooth, solid white silhouette (alpha only) from that banner texture, scaled up.
bool TraceSilhouette(GameId game, const Image& source, Image& out);
