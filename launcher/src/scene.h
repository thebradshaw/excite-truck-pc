// The launcher's backdrop: a desert canyon at golden hour with a dirt track and drifting dust,
// drawn with shapes so no game art is needed.
#pragma once

struct ImDrawList;
struct ImVec2;

void DrawScene(ImDrawList* draw, const ImVec2& size, float seconds);
