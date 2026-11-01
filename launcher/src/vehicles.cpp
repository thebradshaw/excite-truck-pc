#include "vehicles.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

struct V2 {
    float x = 0, y = 0;
};
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, float s) { return {a.x * s, a.y * s}; }
float Dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
float Len(V2 a) { return std::sqrt(Dot(a, a)); }

struct V3 {
    float x, y, z;
};
V3 Norm(V3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0 ? V3{v.x / l, v.y / l, v.z / l} : V3{0, 0, 1};
}

// How a part's surface curves, which sets its normal for the lighting.
enum class Shade {
    Flat,      // no relief (thin lines, dark gaps)
    Slab,      // flat face with rounded edges `bevel` wide
    Dome,      // an ellipsoid over the ellipse (centre, radii)
    Tube,      // a round bar along the part's line
    Tyre,      // a round tyre between the radii, with tread blocks on its outer edge
    Rim,       // a wheel rim with spokes and a hub
    Glass,     // a dark window with a light streak
};

struct Part {
    enum Kind { Polygon, Ellipse, Line } kind = Polygon;
    std::vector<V2> points;   // Polygon: outline; Line: the path
    V2 centre, radii;         // Ellipse; also the dome for Shade::Dome, the wheel for Tyre and Rim
    float radius = 0;         // Line: half its thickness
    Shade shade = Shade::Slab;
    float tone = 0.6f;        // base brightness 0..1
    float shine = 0.2f;       // specular highlight strength
    float bevel = 0.015f;
    float inner = 0;          // Tyre: inner radius
    V2 boxMin, boxMax;        // bounds, filled in by Finish()
};

void Finish(Part& p) {
    V2 lo{1e9f, 1e9f}, hi{-1e9f, -1e9f};
    auto grow = [&](V2 q, float r) {
        lo = {std::min(lo.x, q.x - r), std::min(lo.y, q.y - r)};
        hi = {std::max(hi.x, q.x + r), std::max(hi.y, q.y + r)};
    };
    if (p.kind == Part::Ellipse) {
        grow(p.centre - p.radii, 0);
        grow(p.centre + p.radii, 0);
    } else {
        for (V2 q : p.points) grow(q, p.kind == Part::Line ? p.radius : 0);
    }
    p.boxMin = lo;
    p.boxMax = hi;
}

// --- Building parts ---------------------------------------------------------------------------

struct Builder {
    std::vector<Part> parts;   // back to front

    Part& Add(Part p) {
        Finish(p);
        parts.push_back(std::move(p));
        return parts.back();
    }
    Part& Poly(std::vector<V2> pts, float tone, float bevel = 0.015f, float shine = 0.2f) {
        Part p;
        p.points = std::move(pts);
        p.tone = tone;
        p.bevel = bevel;
        p.shine = shine;
        return Add(std::move(p));
    }
    Part& DomePoly(std::vector<V2> pts, V2 centre, V2 radii, float tone, float shine) {
        Part p;
        p.points = std::move(pts);
        p.shade = Shade::Dome;
        p.centre = centre;
        p.radii = radii;
        p.tone = tone;
        p.shine = shine;
        return Add(std::move(p));
    }
    Part& Oval(V2 centre, V2 radii, float tone, float shine = 0.3f, Shade shade = Shade::Dome) {
        Part p;
        p.kind = Part::Ellipse;
        p.centre = centre;
        p.radii = radii;
        p.shade = shade;
        p.tone = tone;
        p.shine = shine;
        return Add(std::move(p));
    }
    Part& Bar(std::vector<V2> path, float radius, float tone, float shine = 0.3f) {
        Part p;
        p.kind = Part::Line;
        p.points = std::move(path);
        p.radius = radius;
        p.shade = Shade::Tube;
        p.tone = tone;
        p.shine = shine;
        return Add(std::move(p));
    }
    Part& Groove(std::vector<V2> path, float radius, float tone) {
        Part& p = Bar(std::move(path), radius, tone, 0.0f);
        p.shade = Shade::Flat;
        return p;
    }
    void Wheel(V2 c, float r, float rim) {
        Part tyre;
        tyre.kind = Part::Ellipse;
        tyre.centre = c;
        tyre.radii = {r, r};
        tyre.inner = rim * 0.92f;
        tyre.shade = Shade::Tyre;
        tyre.tone = 0.34f;
        tyre.shine = 0.15f;
        Add(std::move(tyre));
        Oval(c, {rim, rim}, 0.72f, 0.5f, Shade::Rim);
    }
};

std::vector<V2> Arc(V2 c, V2 r, float a0, float a1, int steps = 24) {
    std::vector<V2> out;
    for (int i = 0; i <= steps; ++i) {
        const float a = a0 + (a1 - a0) * i / steps;
        out.push_back({c.x + std::cos(a) * r.x, c.y + std::sin(a) * r.y});
    }
    return out;
}

std::vector<V2> Curve(V2 a, V2 control, V2 b, int steps = 16) {
    std::vector<V2> out;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / steps, u = 1 - t;
        out.push_back(a * (u * u) + control * (2 * u * t) + b * (t * t));
    }
    return out;
}

void Append(std::vector<V2>& to, const std::vector<V2>& from) { to.insert(to.end(), from.begin(), from.end()); }

// The arch over a wheel, left to right, for a body whose underside is at y = bottom.
void WheelArch(std::vector<V2>& path, V2 c, float r, float bottom) {
    const float h = std::asin(std::clamp((c.y - bottom) / r, -1.0f, 1.0f));
    Append(path, Arc(c, {r, r}, kPi + h, 2 * kPi - h, 18));
}

// A flared arch over a wheel: a band between radii r0 and r1, above y = bottom.
std::vector<V2> Flare(V2 c, float r0, float r1, float bottom) {
    const float h0 = std::asin(std::clamp((c.y - bottom) / r0, -1.0f, 1.0f));
    const float h1 = std::asin(std::clamp((c.y - bottom) / r1, -1.0f, 1.0f));
    std::vector<V2> band = Arc(c, {r1, r1}, kPi + h1, 2 * kPi - h1, 20);
    std::vector<V2> inner = Arc(c, {r0, r0}, 2 * kPi - h0, kPi + h0, 20);
    Append(band, inner);
    return band;
}

// --- The vehicles -----------------------------------------------------------------------------

// Excite Truck: a lifted off-road pickup with big tyres, a roll bar and a light bar.
std::vector<Part> Truck() {
    Builder b;
    const float r = 0.115f, rim = 0.064f, bottom = 0.30f;
    const V2 rear{0.22f, 0.37f}, front{0.79f, 0.37f};

    b.Poly({{0.10f, 0.27f}, {0.92f, 0.27f}, {0.90f, 0.335f}, {0.12f, 0.335f}}, 0.22f, 0.01f, 0.0f);   // chassis
    b.Bar({{0.22f, 0.37f}, {0.30f, 0.29f}}, 0.012f, 0.3f);   // rear shock
    b.Bar({{0.79f, 0.37f}, {0.71f, 0.29f}}, 0.012f, 0.3f);   // front shock
    b.Wheel(rear, r, rim);
    b.Wheel(front, r, rim);

    // Roll bar in the bed, behind the cab.
    b.Bar({{0.26f, 0.185f}, {0.285f, 0.105f}, {0.42f, 0.105f}, {0.44f, 0.185f}}, 0.009f, 0.62f, 0.5f);
    b.Bar({{0.285f, 0.105f}, {0.40f, 0.185f}}, 0.007f, 0.5f, 0.4f);

    // Body: bed, doors and bonnet, with a gap around each wheel.
    std::vector<V2> body{{0.035f, bottom}};
    WheelArch(body, rear, r + 0.022f, bottom);
    WheelArch(body, front, r + 0.022f, bottom);
    Append(body, {{0.955f, bottom}, {0.968f, 0.24f}, {0.948f, 0.19f}, {0.77f, 0.168f}, {0.47f, 0.175f},
                  {0.445f, 0.182f}, {0.06f, 0.182f}, {0.03f, 0.195f}});
    b.Poly(body, 0.64f, 0.02f, 0.35f);

    // Cab, windows and the light bar on its roof.
    b.Poly({{0.47f, 0.18f}, {0.495f, 0.066f}, {0.655f, 0.066f}, {0.768f, 0.172f}}, 0.6f, 0.014f, 0.35f);
    Part& w1 = b.Poly({{0.493f, 0.088f}, {0.578f, 0.088f}, {0.578f, 0.158f}, {0.471f, 0.158f}}, 0.2f);
    w1.shade = Shade::Glass;
    Part& w2 = b.Poly({{0.596f, 0.088f}, {0.65f, 0.088f}, {0.722f, 0.158f}, {0.596f, 0.158f}}, 0.2f);
    w2.shade = Shade::Glass;
    b.Poly({{0.52f, 0.052f}, {0.53f, 0.066f}, {0.515f, 0.066f}}, 0.4f, 0.004f);
    b.Poly({{0.62f, 0.052f}, {0.63f, 0.066f}, {0.615f, 0.066f}}, 0.4f, 0.004f);
    b.Poly({{0.495f, 0.03f}, {0.65f, 0.03f}, {0.65f, 0.056f}, {0.495f, 0.056f}}, 0.38f, 0.008f, 0.2f);
    for (float x : {0.515f, 0.553f, 0.592f, 0.63f}) b.Oval({x, 0.043f}, {0.012f, 0.0105f}, 0.92f, 1.0f);

    // Flared wheel arches, bumpers, lights and the mirror.
    b.Poly(Flare(rear, r + 0.022f, r + 0.05f, bottom + 0.004f), 0.42f, 0.012f, 0.25f);
    b.Poly(Flare(front, r + 0.022f, r + 0.05f, bottom + 0.004f), 0.42f, 0.012f, 0.25f);
    b.Poly({{0.928f, 0.252f}, {0.988f, 0.252f}, {0.995f, 0.302f}, {0.93f, 0.312f}}, 0.36f, 0.01f, 0.3f);
    b.Poly({{0.012f, 0.258f}, {0.062f, 0.258f}, {0.062f, 0.312f}, {0.018f, 0.312f}}, 0.36f, 0.01f, 0.3f);
    b.Oval({0.95f, 0.207f}, {0.013f, 0.011f}, 0.95f, 1.0f);
    b.Oval({0.04f, 0.215f}, {0.007f, 0.018f}, 0.55f, 0.6f);
    b.Poly({{0.735f, 0.135f}, {0.768f, 0.135f}, {0.768f, 0.16f}, {0.742f, 0.16f}}, 0.5f, 0.006f, 0.3f);

    // Panel lines, a crease along the side, the door handle and the fuel cap.
    b.Groove({{0.445f, 0.19f}, {0.445f, 0.288f}}, 0.0025f, 0.28f);
    b.Groove({{0.6f, 0.185f}, {0.6f, 0.288f}}, 0.0025f, 0.28f);
    b.Groove({{0.745f, 0.18f}, {0.74f, 0.25f}}, 0.0025f, 0.28f);
    b.Groove({{0.07f, 0.212f}, {0.94f, 0.212f}}, 0.002f, 0.82f);
    b.Poly({{0.615f, 0.2f}, {0.645f, 0.2f}, {0.645f, 0.207f}, {0.615f, 0.207f}}, 0.3f, 0.003f);
    b.Oval({0.4f, 0.235f}, {0.012f, 0.012f}, 0.5f, 0.5f);
    return b.parts;
}

// Excitebots: a robot beetle on wheels, with a glossy shell, a horn, antennae and jointed legs.
std::vector<Part> Beetle() {
    Builder b;
    const float r = 0.10f, rim = 0.058f;
    const V2 rear{0.24f, 0.36f}, front{0.64f, 0.36f};

    // Legs on the far side, in shadow.
    b.Bar({{0.3f, 0.26f}, {0.35f, 0.31f}, {0.33f, 0.4f}}, 0.009f, 0.28f, 0.1f);
    b.Bar({{0.58f, 0.26f}, {0.61f, 0.31f}, {0.62f, 0.4f}}, 0.009f, 0.28f, 0.1f);

    b.Wheel(rear, r, rim);
    b.Wheel(front, r, rim);

    b.Oval({0.45f, 0.272f}, {0.37f, 0.045f}, 0.32f, 0.1f);   // underside

    // Shell: a glossy dome with a rim of bolts, split down the middle like wing cases.
    std::vector<V2> shell = Arc({0.42f, 0.27f}, {0.38f, 0.25f}, 2 * kPi - 0.05f, kPi + 0.05f, 40);
    b.DomePoly(shell, {0.42f, 0.27f}, {0.38f, 0.25f}, 0.62f, 0.9f);
    b.Poly({{0.05f, 0.245f}, {0.79f, 0.245f}, {0.795f, 0.29f}, {0.045f, 0.29f}}, 0.42f, 0.012f, 0.3f);
    for (float x : {0.12f, 0.25f, 0.38f, 0.51f, 0.64f, 0.75f}) b.Oval({x, 0.267f}, {0.008f, 0.008f}, 0.8f, 0.8f);
    b.Groove(Arc({0.42f, 0.275f}, {0.33f, 0.2f}, kPi + 0.35f, 2 * kPi - 0.5f, 30), 0.003f, 0.22f);
    b.Groove(Arc({0.42f, 0.275f}, {0.25f, 0.15f}, kPi + 0.45f, 2 * kPi - 0.6f, 24), 0.0018f, 0.78f);
    b.Groove(Arc({0.42f, 0.275f}, {0.16f, 0.1f}, kPi + 0.6f, 2 * kPi - 0.75f, 18), 0.0018f, 0.78f);

    // Thorax, head, eye, horn and jaws.
    b.Oval({0.8f, 0.205f}, {0.075f, 0.07f}, 0.52f, 0.6f);
    b.Oval({0.885f, 0.22f}, {0.062f, 0.052f}, 0.5f, 0.6f);
    b.Bar(Curve({0.915f, 0.185f}, {1.0f, 0.165f}, {0.985f, 0.06f}), 0.012f, 0.62f, 0.6f);
    b.Oval({0.985f, 0.06f}, {0.009f, 0.009f}, 0.65f, 0.6f);
    b.Bar(Curve({0.925f, 0.255f}, {0.975f, 0.27f}, {0.965f, 0.31f}), 0.008f, 0.45f, 0.4f);
    b.Oval({0.905f, 0.205f}, {0.019f, 0.019f}, 0.9f, 1.0f);
    b.Oval({0.899f, 0.199f}, {0.005f, 0.005f}, 1.0f, 0.0f, Shade::Flat);   // glint

    // Antennae with ball tips.
    b.Bar(Curve({0.86f, 0.17f}, {0.9f, 0.06f}, {0.965f, 0.0f}), 0.005f, 0.5f, 0.4f);
    b.Bar(Curve({0.85f, 0.17f}, {0.845f, 0.05f}, {0.895f, -0.03f}), 0.005f, 0.5f, 0.4f);
    b.Oval({0.965f, 0.0f}, {0.011f, 0.011f}, 0.7f, 0.8f);
    b.Oval({0.895f, -0.03f}, {0.011f, 0.011f}, 0.7f, 0.8f);

    // Near legs, jointed.
    const std::vector<std::vector<V2>> legs = {
        {{0.14f, 0.265f}, {0.08f, 0.315f}, {0.065f, 0.4f}},
        {{0.44f, 0.275f}, {0.475f, 0.33f}, {0.455f, 0.405f}},
        {{0.79f, 0.265f}, {0.855f, 0.315f}, {0.88f, 0.39f}},
    };
    for (const auto& leg : legs) {
        b.Bar(leg, 0.011f, 0.52f, 0.5f);
        b.Oval(leg[1], {0.015f, 0.015f}, 0.6f, 0.6f);
        b.Oval(leg[0], {0.014f, 0.014f}, 0.5f, 0.5f);
    }
    return b.parts;
}

// --- Rendering --------------------------------------------------------------------------------

bool InPolygon(const std::vector<V2>& poly, V2 p) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const V2 a = poly[i], c = poly[j];
        if ((a.y > p.y) != (c.y > p.y) && p.x < (c.x - a.x) * (p.y - a.y) / (c.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

// Distance from p to a path, and the nearest point on it.
float PathDistance(const std::vector<V2>& path, V2 p, bool closed, V2* nearest) {
    float best = 1e9f;
    const size_t n = path.size();
    for (size_t i = 0; i + (closed ? 0 : 1) < n; ++i) {
        const V2 a = path[i], c = path[(i + 1) % n];
        const V2 d = c - a;
        const float len = Dot(d, d);
        const float t = len > 0 ? std::clamp(Dot(p - a, d) / len, 0.0f, 1.0f) : 0.0f;
        const V2 q = a + d * t;
        const float dist = Len(p - q);
        if (dist < best) {
            best = dist;
            if (nearest) *nearest = q;
        }
    }
    return best;
}

bool Covers(const Part& part, V2 p) {
    if (p.x < part.boxMin.x || p.y < part.boxMin.y || p.x > part.boxMax.x || p.y > part.boxMax.y) return false;
    switch (part.kind) {
        case Part::Polygon: return InPolygon(part.points, p);
        case Part::Ellipse: {
            const V2 q{(p.x - part.centre.x) / part.radii.x, (p.y - part.centre.y) / part.radii.y};
            return Dot(q, q) <= 1.0f;
        }
        case Part::Line: return PathDistance(part.points, p, false, nullptr) <= part.radius;
    }
    return false;
}

// The light, from the top left towards the viewer. A vehicle facing left is drawn mirrored, so its
// light is mirrored too, to end up top left on screen.
V3 g_light = Norm({-0.5f, -0.75f, 0.65f});

float Lit(V3 n, float tone, float shine) {
    const V3 kLight = g_light;
    const V3 kHalf = Norm({kLight.x, kLight.y, kLight.z + 1.0f});
    const float diffuse = std::max(0.0f, n.x * kLight.x + n.y * kLight.y + n.z * kLight.z);
    const float spec = std::pow(std::max(0.0f, n.x * kHalf.x + n.y * kHalf.y + n.z * kHalf.z), 30.0f);
    return tone * (0.42f + 0.7f * diffuse) + spec * shine * 0.55f;
}

// A normal leaning outwards from a centre: `out` is the unit direction, `t` how far (0..1).
V3 Lean(V2 out, float t) {
    t = std::clamp(t, -0.98f, 0.98f);
    return Norm({out.x * t, out.y * t, std::sqrt(1.0f - t * t)});
}

float ShadePart(const Part& part, V2 p) {
    switch (part.shade) {
        case Shade::Flat: return part.tone;
        case Shade::Glass: {
            // Dark glass with a soft diagonal reflection.
            const float s = p.x * 1.0f + p.y * 1.4f;
            const float streak = std::exp(-std::pow((std::fmod(s * 9.0f, 1.0f) - 0.5f) * 5.0f, 2.0f));
            return part.tone + 0.18f * streak + 0.08f * (0.16f - std::min(0.16f, p.y)) * 6.0f;
        }
        case Shade::Slab: {
            V2 nearest;
            const float d = PathDistance(part.points, p, true, &nearest);
            if (d >= part.bevel || d <= 0.0f) return Lit({0, 0, 1}, part.tone, part.shine);
            const V2 out = (nearest - p) * (1.0f / d);
            return Lit(Lean(out, 1.0f - d / part.bevel), part.tone, part.shine);
        }
        case Shade::Dome: {
            const V2 q{(p.x - part.centre.x) / part.radii.x, (p.y - part.centre.y) / part.radii.y};
            const float r2 = std::min(Dot(q, q), 0.98f);
            return Lit(Norm({q.x, q.y, std::sqrt(1.0f - r2) * 0.9f}), part.tone, part.shine);
        }
        case Shade::Tube: {
            V2 nearest;
            const float d = PathDistance(part.points, p, false, &nearest);
            if (d <= 0.0f) return Lit({0, 0, 1}, part.tone, part.shine);
            return Lit(Lean((p - nearest) * (1.0f / d), d / part.radius), part.tone, part.shine);
        }
        case Shade::Tyre: {
            const V2 v = p - part.centre;
            const float dist = Len(v);
            const float r1 = part.radii.x, r0 = part.inner;
            const float mid = (r0 + r1) * 0.5f, half = (r1 - r0) * 0.5f;
            const V2 out = dist > 0 ? v * (1.0f / dist) : V2{0, -1};
            float tone = part.tone;
            // Tread blocks around the outside, and the sidewall's lettering band.
            const float angle = std::atan2(v.y, v.x);
            if (dist > r1 - (r1 - r0) * 0.28f && std::fmod(angle / (2 * kPi) * 22.0f + 22.0f, 1.0f) < 0.45f) tone *= 0.55f;
            if (std::abs(dist - mid) < half * 0.12f) tone *= 1.25f;
            return Lit(Lean(out, (dist - mid) / half), tone, part.shine);
        }
        case Shade::Rim: {
            const V2 v = p - part.centre;
            const float dist = Len(v), r = part.radii.x;
            const V2 out = dist > 0 ? v * (1.0f / dist) : V2{0, -1};
            if (dist < r * 0.28f) {   // the hub
                return Lit(Lean(out, dist / (r * 0.28f) * 0.8f), part.tone * 1.05f, part.shine);
            }
            const float angle = std::atan2(v.y, v.x);
            const float spoke = std::fmod(angle / (2 * kPi) * 6.0f + 6.0f, 1.0f);
            if (dist < r * 0.8f && spoke > 0.22f && spoke < 0.78f) {
                return part.tone * 0.28f;   // the openings between the spokes
            }
            if (dist > r * 0.8f) {   // the lip, rounded
                return Lit(Lean(out, (dist - r * 0.9f) / (r * 0.1f)), part.tone, part.shine);
            }
            // A spoke: rounded across its width.
            const float across = (spoke < 0.5f ? spoke : spoke - 1.0f) / 0.22f;
            const V2 side{-out.y, out.x};
            return Lit(Lean(side, across * 0.8f), part.tone, part.shine);
        }
    }
    return part.tone;
}

} // namespace

int RenderVehicle(GameId game, bool faceLeft, int width, std::vector<uint32_t>& rgba, bool solid) {
    const std::vector<Part> parts = game == GameId::Truck ? Truck() : Beetle();
    g_light = Norm({faceLeft ? 0.5f : -0.5f, -0.75f, 0.65f});
    const int height = int(width * kVehicleHeight);
    // Drawn at twice the size and averaged down, for smooth edges.
    const int ss = 2, w2 = width * ss, h2 = height * ss;
    std::vector<float> value(size_t(width) * height, 0.0f), cover(size_t(width) * height, 0.0f);
    for (int y = 0; y < h2; ++y) {
        for (int x = 0; x < w2; ++x) {
            float px = (x + 0.5f) / w2;
            if (faceLeft) px = 1.0f - px;   // mirror the shape, keep the light where it is
            const V2 p{px, kVehicleTop + (y + 0.5f) / w2};
            for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
                if (!Covers(*it, p)) continue;
                const float v = ShadePart(*it, p);
                const size_t i = size_t(y / ss) * width + x / ss;
                value[i] += std::clamp(v, 0.0f, 1.0f);
                cover[i] += 1.0f;
                break;
            }
        }
    }
    rgba.assign(size_t(width) * height, 0);
    for (size_t i = 0; i < rgba.size(); ++i) {
        if (cover[i] <= 0.0f) continue;
        const uint32_t g = solid ? 255u : uint32_t(std::clamp(value[i] / cover[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        const uint32_t a = uint32_t(cover[i] / (ss * ss) * 255.0f + 0.5f);
        rgba[i] = (a << 24) | (g << 16) | (g << 8) | g;
    }
    return height;
}

// --- Silhouettes traced from the disc's banner art --------------------------------------------

const char* SilhouetteSource(GameId game) {
    return game == GameId::Truck ? "veh_strip.tpl" : "Bot_Grasshopper.tpl";
}

namespace {

using Mask = std::vector<float>;

// Box blur, `r` pixels each way, along both axes.
void Blur(Mask& m, int w, int h, int r) {
    Mask tmp(m.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            int n = 0;
            for (int k = std::max(0, x - r); k <= std::min(w - 1, x + r); ++k, ++n) sum += m[size_t(y) * w + k];
            tmp[size_t(y) * w + x] = sum / n;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            int n = 0;
            for (int k = std::max(0, y - r); k <= std::min(h - 1, y + r); ++k, ++n) sum += tmp[size_t(k) * w + x];
            m[size_t(y) * w + x] = sum / n;
        }
    }
}

// Grows (or with `shrink`, thins) the mask by `r` pixels.
void Spread(Mask& m, int w, int h, int r, bool shrink) {
    Mask out(m.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float v = shrink ? 1.0f : 0.0f;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    const float s = (xx < 0 || yy < 0 || xx >= w || yy >= h) ? 0.0f : m[size_t(yy) * w + xx];
                    v = shrink ? std::min(v, s) : std::max(v, s);
                }
            }
            out[size_t(y) * w + x] = v;
        }
    }
    m.swap(out);
}

// Fills everything a line drawing encloses: whatever can't be reached from the border.
void FillEnclosed(Mask& m, int w, int h) {
    std::vector<char> outside(m.size(), 0);
    std::vector<int> stack;
    auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const size_t i = size_t(y) * w + x;
        if (outside[i] || m[i] > 0.5f) return;
        outside[i] = 1;
        stack.push_back(int(i));
    };
    for (int x = 0; x < w; ++x) push(x, 0), push(x, h - 1);
    for (int y = 0; y < h; ++y) push(0, y), push(w - 1, y);
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        push(i % w + 1, i / w), push(i % w - 1, i / w), push(i % w, i / w + 1), push(i % w, i / w - 1);
    }
    for (size_t i = 0; i < m.size(); ++i) m[i] = outside[i] ? 0.0f : 1.0f;
}

} // namespace

bool TraceSilhouette(GameId game, const Image& source, Image& out) {
    if (source.width <= 0 || source.height <= 0) return false;
    const int sw = source.width;
    auto alpha = [&](int x, int y) { return source.rgba[(size_t(y) * sw + x) * 4 + 3]; };

    // The part of the texture to trace, and how.
    int x0 = 0, y0 = 0, x1 = sw, y1 = source.height;
    if (game == GameId::Truck) {
        // A column of truck silhouettes, each with a fainter shadow: take the last (a lifted
        // truck with a light bar), without its shadow.
        std::vector<std::pair<int, int>> rows;
        for (int y = 0, start = -1; y <= source.height; ++y) {
            bool any = false;
            for (int x = 0; y < source.height && x < sw && !any; ++x) any = alpha(x, y) > 0;
            if (any && start < 0) start = y;
            if (!any && start >= 0) {
                if (y - start > 20) rows.push_back({start, y});
                start = -1;
            }
        }
        if (rows.empty()) return false;
        y0 = rows.back().first;
        y1 = rows.back().second;
    }
    const int w = x1 - x0, h = y1 - y0;
    Mask m(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int a = alpha(x0 + x, y0 + y);
            m[size_t(y) * w + x] = game == GameId::Truck ? (a >= 140 ? 1.0f : 0.0f) : (a > 40 ? 1.0f : 0.0f);
        }
    }
    if (game == GameId::Bots) {
        // A line drawing: close small gaps, fill it in, then take back the extra edge.
        Spread(m, w, h, 2, false);
        FillEnclosed(m, w, h);
        Spread(m, w, h, 1, true);
    }

    // Crop to the shape, with a margin for the smoothing.
    int bx0 = w, by0 = h, bx1 = -1, by1 = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (m[size_t(y) * w + x] > 0.5f) {
                bx0 = std::min(bx0, x), by0 = std::min(by0, y), bx1 = std::max(bx1, x), by1 = std::max(by1, y);
            }
        }
    }
    if (bx1 < 0) return false;
    const int pad = 2;
    bx0 -= pad, by0 -= pad, bx1 += pad, by1 += pad;

    // Scale up smoothly: sample the mask with bilinear filtering, blur, then sharpen the edge.
    const int scale = game == GameId::Truck ? 8 : 4;
    const int ow = (bx1 - bx0 + 1) * scale, oh = (by1 - by0 + 1) * scale;
    auto at = [&](int x, int y) {
        return (x < 0 || y < 0 || x >= w || y >= h) ? 0.0f : m[size_t(y) * w + x];
    };
    Mask big(size_t(ow) * oh);
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            const float fx = bx0 + (x + 0.5f) / scale - 0.5f, fy = by0 + (y + 0.5f) / scale - 0.5f;
            const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
            const float tx = fx - ix, ty = fy - iy;
            big[size_t(y) * ow + x] = (at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty) +
                                      (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty;
        }
    }
    Blur(big, ow, oh, scale / 2);
    Blur(big, ow, oh, scale / 2);
    out.width = ow;
    out.height = oh;
    out.rgba.assign(size_t(ow) * oh * 4, 255);
    for (size_t i = 0; i < big.size(); ++i) {
        out.rgba[i * 4 + 3] = (unsigned char)(std::clamp((big[i] - 0.5f) * 5.0f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return true;
}
