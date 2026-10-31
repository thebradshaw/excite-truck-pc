#include "scene.h"

#include <cmath>
#include <vector>

#include "imgui.h"

namespace {

ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// A smooth ridge line: a few sine waves, the same every frame.
float Ridge(float x, float seed) {
    return 0.55f * std::sin(x * 2.1f + seed) + 0.30f * std::sin(x * 5.3f + seed * 2.7f) +
           0.15f * std::sin(x * 11.7f + seed * 1.3f);
}

// Fills the area between a ridge line and the bottom edge.
void FillRidge(ImDrawList* draw, const ImVec2& size, float baseY, float amplitude, float seed, ImU32 color,
               float frequency = 1.0f) {
    std::vector<ImVec2> points;
    const int steps = 96;
    for (int i = 0; i <= steps; ++i) {
        const float u = float(i) / steps;
        points.push_back(ImVec2(u * size.x, baseY - amplitude * (0.5f + 0.5f * Ridge(u * frequency * 3.0f, seed))));
    }
    points.push_back(ImVec2(size.x, size.y));
    points.push_back(ImVec2(0.0f, size.y));
    draw->AddConcavePolyFilled(points.data(), int(points.size()), color);
}

// A flat-topped mesa: steep sides, a lit top edge.
void Mesa(ImDrawList* draw, float left, float right, float top, float bottom, ImU32 body, ImU32 lit) {
    const float h = bottom - top;
    const ImVec2 shape[] = {
        {left, bottom}, {left + h * 0.35f, top + h * 0.12f}, {left + h * 0.42f, top},
        {right - h * 0.30f, top}, {right - h * 0.22f, top + h * 0.18f}, {right, bottom},
    };
    draw->AddConcavePolyFilled(shape, 6, body);
    const ImVec2 band[] = {
        {left + h * 0.42f, top}, {right - h * 0.30f, top}, {right - h * 0.27f, top + h * 0.07f},
        {left + h * 0.39f, top + h * 0.07f},
    };
    draw->AddConvexPolyFilled(band, 4, lit);
}

void Cactus(ImDrawList* draw, float x, float groundY, float height, ImU32 color) {
    const float w = height * 0.16f;
    draw->AddRectFilled(ImVec2(x - w * 0.5f, groundY - height), ImVec2(x + w * 0.5f, groundY), color, w * 0.5f);
    // Arms.
    const float armY = groundY - height * 0.55f;
    draw->AddRectFilled(ImVec2(x - w * 1.9f, armY), ImVec2(x - w * 0.4f, armY + w * 0.8f), color, w * 0.4f);
    draw->AddRectFilled(ImVec2(x - w * 1.9f, armY - height * 0.25f), ImVec2(x - w * 1.1f, armY + w * 0.8f), color, w * 0.4f);
    const float armY2 = groundY - height * 0.70f;
    draw->AddRectFilled(ImVec2(x + w * 0.4f, armY2), ImVec2(x + w * 1.7f, armY2 + w * 0.8f), color, w * 0.4f);
    draw->AddRectFilled(ImVec2(x + w * 0.9f, armY2 - height * 0.20f), ImVec2(x + w * 1.7f, armY2 + w * 0.8f), color, w * 0.4f);
}

} // namespace

void DrawScene(ImDrawList* draw, const ImVec2& size, float seconds) {
    const float W = size.x, H = size.y;
    const float horizon = H * 0.56f;

    // Sky: deep blue overhead to a warm glow at the horizon.
    const ImU32 skyTop = Rgb(22, 78, 172), skyMid = Rgb(78, 150, 226), skyLow = Rgb(255, 196, 128);
    draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H * 0.30f), skyTop, skyTop, skyMid, skyMid);
    draw->AddRectFilledMultiColor(ImVec2(0, H * 0.30f), ImVec2(W, horizon + 2.0f), skyMid, skyMid, skyLow, skyLow);

    // Sun, low in the sky.
    const ImVec2 sun(W * 0.30f, horizon - H * 0.16f);
    for (int i = 6; i >= 1; --i) {
        draw->AddCircleFilled(sun, H * (0.045f + 0.035f * i), Rgb(255, 226, 160, 16), 64);
    }
    draw->AddCircleFilled(sun, H * 0.05f, Rgb(255, 244, 214), 64);

    // Clouds drifting slowly to the right.
    struct Cloud { float x, y, scale, speed; };
    const Cloud clouds[] = {
        {0.08f, 0.10f, 1.0f, 0.004f}, {0.52f, 0.16f, 0.7f, 0.006f}, {0.80f, 0.07f, 1.2f, 0.003f},
        {0.30f, 0.24f, 0.6f, 0.005f},
    };
    for (const Cloud& c : clouds) {
        float x = std::fmod(c.x + seconds * c.speed, 1.3f) - 0.15f;
        const float cx = x * W, cy = c.y * H, r = H * 0.035f * c.scale;
        const ImU32 col = Rgb(255, 255, 255, 70);
        draw->AddCircleFilled(ImVec2(cx, cy), r, col, 32);
        draw->AddCircleFilled(ImVec2(cx + r * 1.2f, cy + r * 0.25f), r * 0.85f, col, 32);
        draw->AddCircleFilled(ImVec2(cx - r * 1.1f, cy + r * 0.3f), r * 0.75f, col, 32);
        draw->AddRectFilled(ImVec2(cx - r * 1.6f, cy + r * 0.3f), ImVec2(cx + r * 1.9f, cy + r * 0.9f), col, r * 0.4f);
    }

    // Distant hazy ridges.
    FillRidge(draw, size, horizon - H * 0.02f, H * 0.10f, 1.7f, Rgb(176, 128, 160));
    FillRidge(draw, size, horizon + H * 0.01f, H * 0.07f, 4.2f, Rgb(170, 106, 112), 1.6f);

    // Mesas in the middle distance.
    const ImU32 mesaBody = Rgb(178, 86, 52), mesaLit = Rgb(232, 142, 82);
    Mesa(draw, W * 0.02f, W * 0.20f, horizon - H * 0.15f, horizon + H * 0.04f, mesaBody, mesaLit);
    Mesa(draw, W * 0.36f, W * 0.47f, horizon - H * 0.09f, horizon + H * 0.04f, mesaBody, mesaLit);
    Mesa(draw, W * 0.60f, W * 0.86f, horizon - H * 0.18f, horizon + H * 0.04f, mesaBody, mesaLit);

    // Ground: sandy near the horizon, darker red dirt up close.
    const ImU32 sandFar = Rgb(226, 156, 92), sandNear = Rgb(150, 76, 40);
    draw->AddRectFilledMultiColor(ImVec2(0, horizon + H * 0.03f), ImVec2(W, H), sandFar, sandFar, sandNear, sandNear);
    FillRidge(draw, size, horizon + H * 0.10f, H * 0.05f, 2.9f, Rgb(206, 128, 70), 0.8f);
    FillRidge(draw, size, horizon + H * 0.22f, H * 0.06f, 5.5f, Rgb(178, 98, 52), 0.7f);

    // A dirt track sweeping in from the bottom towards the horizon.
    {
        std::vector<ImVec2> left, right;
        const int steps = 40;
        for (int i = 0; i <= steps; ++i) {
            const float t = float(i) / steps;                         // 0 = near, 1 = far
            const float y = H - t * (H - horizon - H * 0.035f);
            const float centre = W * (0.18f + 0.22f * t + 0.10f * std::sin(t * 3.4f));
            const float half = W * (0.16f * (1.0f - t) * (1.0f - t) + 0.006f);
            left.push_back(ImVec2(centre - half, y));
            right.push_back(ImVec2(centre + half, y));
        }
        std::vector<ImVec2> poly(left);
        poly.insert(poly.end(), right.rbegin(), right.rend());
        draw->AddConcavePolyFilled(poly.data(), int(poly.size()), Rgb(120, 62, 34));
        // Tyre ruts.
        for (float lane : {0.35f, 0.65f}) {
            std::vector<ImVec2> rut;
            for (size_t i = 0; i < left.size(); ++i) {
                rut.push_back(ImVec2(left[i].x + (right[i].x - left[i].x) * lane, left[i].y));
            }
            draw->AddPolyline(rut.data(), int(rut.size()), Rgb(92, 44, 24, 200), 0, H * 0.006f);
        }
    }

    // Cacti on the near dunes.
    const ImU32 cactus = Rgb(70, 92, 52);
    Cactus(draw, W * 0.06f, horizon + H * 0.14f, H * 0.08f, cactus);
    Cactus(draw, W * 0.43f, horizon + H * 0.09f, H * 0.05f, cactus);
    Cactus(draw, W * 0.92f, horizon + H * 0.20f, H * 0.11f, cactus);

    // Dust drifting across the screen.
    for (int i = 0; i < 48; ++i) {
        const float seed = float(i) * 12.9898f;
        const float fx = std::fmod(std::fabs(std::sin(seed) * 43758.5453f), 1.0f);
        const float fy = std::fmod(std::fabs(std::sin(seed * 1.7f) * 24634.6345f), 1.0f);
        const float speed = 0.015f + 0.03f * fy;
        const float x = std::fmod(fx + seconds * speed, 1.0f) * W;
        const float y = (0.35f + 0.65f * fy) * H + std::sin(seconds * 0.8f + seed) * H * 0.01f;
        const float r = H * (0.002f + 0.003f * fx);
        draw->AddCircleFilled(ImVec2(x, y), r, Rgb(255, 236, 200, 60 + int(80 * fx)), 12);
    }

    // Soft vignette towards the edges so the panels read well.
    const ImU32 clear = Rgb(0, 0, 0, 0), shade = Rgb(10, 14, 30, 110);
    draw->AddRectFilledMultiColor(ImVec2(0, H * 0.70f), ImVec2(W, H), clear, clear, shade, shade);
}
