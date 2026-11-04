// Renderer: see render.h. Landscape logical coordinates (gfx.h); the world is drawn column by
// column because a logical column is one contiguous VRAM row.
#include "render.h"
#include "gfx.h"

using namespace hc;
using gfx::rgb;

namespace render {

namespace {

constexpr int W = gfx::W, H = gfx::H;

constexpr uint16_t C_WHITE = rgb(255, 255, 255), C_BLACK = rgb(0, 0, 0);
constexpr uint16_t C_TIRE = rgb(34, 34, 40), C_RIM = rgb(185, 188, 198), C_HUB = rgb(70, 72, 80);
constexpr uint16_t C_DARK = rgb(45, 45, 55), C_GREY = rgb(125, 128, 140), C_LIGHT = rgb(205, 208, 215);
constexpr uint16_t C_GLASS = rgb(70, 110, 150), C_GOLD = rgb(255, 205, 50), C_RED = rgb(225, 55, 45);
constexpr uint16_t C_SKIN = rgb(240, 190, 150), C_PANEL = rgb(24, 28, 42), C_DIM = rgb(170, 178, 195);
constexpr uint16_t C_ORANGE = rgb(245, 140, 30);

int curMap = -1;
// 4-byte aligned so that copySpanP can use 32-bit moves against the (aligned) VRAM rows
uint16_t skyP[320] __attribute__((aligned(4)));          // physical order: skyP[p] is the sky at ly = 319 - p
uint16_t gA0[640] __attribute__((aligned(4)));           // ground template: row[p] = gA0[320 + g + p]
uint16_t gA1[642] __attribute__((aligned(4)));           // the same shifted by one (odd g)
int16_t farH[512], nearH[512];
uint16_t farC, nearC;
int16_t colTop[W];                       // first non-sky row of every column (sky decorations)
int16_t colGround[W];                    // ground surface row of every column
Camera cam;
int32_t invZ = 1 << 16;                  // 1 / zoom, Q16

Terrain menuTerr;                        // scenery for the menus

inline int sxq(fx wx) { return (int)(((int64_t)(wx - cam.x) * invZ) >> 24); }   // -> Q4 screen
inline int syq(fx wy) { return (int)(((int64_t)(wy - cam.y) * invZ) >> 24); }
inline int lenq(int pxLen) { return (pxLen * invZ) >> 12; }                     // px -> Q4 screen
inline int lenp(int pxLen) { return (pxLen * invZ) >> 16; }                     // px -> screen px

void setCamera(const Camera& c) {
    cam = c;
    invZ = (1 << 24) / (c.zoom < 64 ? 64 : c.zoom);
}

int32_t noiseWave(int k, uint32_t salt, int period) {   // smooth periodic noise, Q12 -4096..4096
    const int seg = 32;
    const int a = k / seg, f = k % seg;
    const int n = period / seg;
    const int32_t va = (int32_t)(hash32((uint32_t)(a % n) ^ salt) & 8191) - 4096;
    const int32_t vb = (int32_t)(hash32((uint32_t)((a + 1) % n) ^ salt) & 8191) - 4096;
    const int32_t t = (f << 12) / seg;
    const int32_t s = (((t * t) >> 12) * (3 * 4096 - 2 * t)) >> 12;
    return va + (((vb - va) * s) >> 12);
}

}  // namespace

uint32_t mapSeed(int map) { return 0x13579BDFu * (uint32_t)(map + 1) + 0x2468ACE0u; }

// ---------------------------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------------------------
void setMap(int map) {
    if (map == curMap) return;
    curMap = map;
    const MapDef& m = MAPS[map];
    for (int ly = 0; ly < 320; ++ly) skyP[319 - ly] = gfx::mix(m.skyTop, m.skyBot, (ly * 256) / 300);

    // ground: depth d below the surface -> colour G[d]
    static uint16_t G[640];
    for (int d = 0; d < 640; ++d) {
        uint16_t c;
        if (m.feature == F_ICE) {
            if (d < 2) c = C_WHITE;
            else if (d < 11) c = m.surface;
            else if (d < 13) c = gfx::shade(m.soil, 230);
            else c = gfx::mix(m.soil, m.deep, d > 120 ? 256 : ((d - 13) * 256) / 107);
        } else if (m.feature == F_RAMPS) {
            if (d < 2) c = gfx::tint(m.surface, 60);
            else if (d == 4 || d == 5) c = m.accent;            // painted line
            else if (d < 9) c = m.surface;
            else c = gfx::mix(m.soil, m.deep, d > 100 ? 256 : ((d - 9) * 256) / 91);
        } else if (m.feature == F_SPIKES) {
            if (d < 2) c = rgb(255, 220, 120);
            else if (d < 5) c = m.surface;
            else if (d < 9) c = gfx::mix(m.surface, m.soil, ((d - 5) * 256) / 4);
            else c = gfx::mix(m.soil, m.deep, d > 90 ? 256 : ((d - 9) * 256) / 81);
            if (d > 20 && (d % 31) < 2) c = rgb(150, 45, 20);   // glowing veins
        } else {
            if (d < 2) c = gfx::tint(m.surface, 70);
            else if (d < 7) c = m.surface;
            else if (d < 9) c = gfx::shade(m.surface, 170);
            else c = gfx::mix(m.soil, m.deep, d > 110 ? 256 : ((d - 9) * 256) / 101);
            if (d > 14 && ((d - 14) % 23) < 2) c = gfx::shade(c, 215);   // strata
        }
        G[d] = c;
    }
    for (int j = 0; j < 640; ++j) gA0[j] = G[639 - j];
    for (int j = 0; j < 641; ++j) gA1[j] = gA0[j + 1 < 640 ? j + 1 : 639];
    gA1[641] = gA0[639];

    // parallax silhouettes
    const uint32_t s = mapSeed(map);
    for (int k = 0; k < 512; ++k) {
        int32_t f, n;
        switch (m.feature) {
            case F_RAMPS: {                                        // city skyline
                const uint32_t h = hash32((uint32_t)(k / 18) ^ s);
                f = 30 + (int)(h % 70u);
                const uint32_t h2 = hash32((uint32_t)(k / 12) ^ s ^ 77u);
                n = 10 + (int)(h2 % 30u);
                break;
            }
            case F_SPIKES:
            case F_DUNES: {
                const int32_t a = noiseWave(k, s, 512), b = noiseWave((k * 3) & 511, s ^ 5u, 512);
                f = 25 + ((4096 - 2 * (a < 0 ? -a : a)) * 40 >> 12) + (b * 6 >> 12);
                n = 12 + (noiseWave((k * 2) & 511, s ^ 9u, 512) * 14 >> 12);
                break;
            }
            default: {
                const int32_t a = noiseWave(k, s, 512), b = noiseWave((k * 4) & 511, s ^ 5u, 512);
                f = 35 + (a * 34 >> 12) + (b * 8 >> 12);
                n = 14 + (noiseWave((k * 2) & 511, s ^ 9u, 512) * 14 >> 12);
                break;
            }
        }
        farH[k] = (int16_t)f;
        nearH[k] = (int16_t)n;
    }
    farC = m.far;
    nearC = m.nearHill;
}

// ---------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------
void cameraReset(Camera& c, const Run& r) {
    c.zoom = 256;
    c.x = carX(r) - (fx)((W * 36 / 100) << FXS);
    c.y = carY(r) - (fx)((H * 58 / 100) << FXS);
}

void cameraFollow(Camera& c, const Run& r) {
    const int32_t kmh = speedKmh(r);
    int32_t tz = 256 + (kmh > 90 ? 90 : kmh) * 9 / 10;           // zoom out with speed (to 1.3)
    if (r.airT > sec(0.5)) tz += 24;
    c.zoom += (tz - c.zoom) / 12;
    // look ahead in the direction of travel
    const fx vx = carX(r) - ((r.p[P_B0].ox + r.p[P_B1].ox) >> 1);
    int32_t lead = (int32_t)((W * 33 / 100) - ((vx * 8) >> FXS));
    if (lead < W * 27 / 100) lead = W * 27 / 100;
    if (lead > W * 45 / 100) lead = W * 45 / 100;
    const fx tx = carX(r) - ((lead * c.zoom) << 4);
    const fx ty = carY(r) - (((H * 56 / 100) * c.zoom) << 4);
    c.x += (tx - c.x) / 6;
    c.y += (ty - c.y) / 5;
    // never let the car leave the screen vertically
    const fx top = carY(r) - ((40 * c.zoom) << 4), bot = carY(r) - (((H - 50) * c.zoom) << 4);
    if (c.y > top) c.y = top;
    if (c.y < bot) c.y = bot;
}

// ---------------------------------------------------------------------------------------------
// Background + terrain
// ---------------------------------------------------------------------------------------------
namespace {

void skyDisc(int cx, int cy, int r, uint16_t c) {
    for (int dx = -r; dx <= r; ++dx) {
        const int x = cx + dx;
        if ((unsigned)x >= (unsigned)W) continue;
        const int h = gfx::isqrt(r * r - dx * dx);
        int y1 = cy + h;
        if (y1 >= colTop[x]) y1 = colTop[x] - 1;
        gfx::vspan(x, cy - h, y1, c);
    }
}

void skyDecor(int map) {
    const MapDef& m = MAPS[map];
    const int px = (int)(cam.x >> FXS);
    switch (m.feature) {
        case F_CRATERS: case F_RAMPS: case F_SPIKES: {
            // stars / embers, slow parallax
            const uint32_t s = mapSeed(map);
            const uint16_t sc = m.feature == F_SPIKES ? rgb(255, 150, 60) : rgb(230, 230, 255);
            for (int i = 0; i < 46; ++i) {
                const uint32_t h = hash32((uint32_t)i * 977u ^ s);
                const int x = (int)((h % 1056u) - (unsigned)(px / 24)) % 1056;
                const int xx = x < 0 ? x + 1056 : x;
                if (xx >= W) continue;
                const int y = (int)((h >> 12) % 170u);
                if (y < colTop[xx]) gfx::pixel(xx, y, sc);
                if (((h >> 20) & 7) == 0 && y + 1 < colTop[xx]) { gfx::pixel(xx + 1, y, sc); gfx::pixel(xx, y + 1, sc); }
            }
            if (m.feature == F_CRATERS) {                          // the home planet
                skyDisc(430, 58, 20, m.accent);
                skyDisc(424, 52, 7, rgb(70, 170, 90));
                skyDisc(438, 66, 5, rgb(70, 170, 90));
            } else if (m.feature == F_RAMPS) {
                skyDisc(80, 50, 15, rgb(240, 240, 220));
            } else {
                skyDisc(400, 70, 26, rgb(150, 40, 30));
            }
            break;
        }
        case F_TERRACES: skyDisc(380, 120, 34, rgb(255, 225, 140)); break;
        default: {
            skyDisc(440, 46, 18, m.feature == F_ICE ? rgb(255, 255, 235) : rgb(255, 240, 150));
            // clouds
            for (int i = 0; i < 5; ++i) {
                const uint32_t h = hash32((uint32_t)i * 31337u ^ mapSeed(map));
                int x = (int)((h % 900u) - (unsigned)(px / 14)) % 900;
                if (x < 0) x += 900;
                x -= 120;
                const int y = 30 + (int)((h >> 10) % 70u);
                const int w = 26 + (int)((h >> 18) % 26u);
                const uint16_t cc = gfx::tint(m.skyBot, 150);
                skyDisc(x, y, w / 3, cc);
                skyDisc(x + w / 3, y - 6, w / 3 + 2, cc);
                skyDisc(x + (2 * w) / 3, y, w / 3 - 1, cc);
                for (int k = x; k < x + (2 * w) / 3; ++k)
                    if ((unsigned)k < (unsigned)W && y + 4 < colTop[k]) gfx::vspan(k, y - 2, y + 4 < colTop[k] - 1 ? y + 4 : colTop[k] - 1, cc);
            }
            break;
        }
    }
}

// Every pixel of the play area is written exactly once here.
void drawLand(Terrain& t) {
    const int32_t stepW = cam.zoom << 4;                       // world Q12 per screen column
    const int i0 = cam.x >> (FXS + SEG_SHIFT);
    const int i1 = (int)((cam.x + (int64_t)stepW * W) >> (FXS + SEG_SHIFT));
    terrainEnsure(t, i0, i1 + 1);
    const int camPx = (int)(-(cam.y >> FXS));
    int farBase = 150 + (camPx - 210) / 30, nearBase = 196 + (camPx - 210) / 14;
    if (farBase < 70) farBase = 70;
    if (farBase > 250) farBase = 250;
    if (nearBase < 110) nearBase = 110;
    if (nearBase > 300) nearBase = 300;
    const int farOff = (int)(cam.x >> (FXS + 3)), nearOff = (int)(cam.x >> (FXS + 2));
    fx wx = cam.x + (stepW >> 1);
    uint16_t* row = gfx::fb;
    for (int c = 0; c < W; ++c, wx += stepW, row += gfx::PW) {
        const int i = wx >> (FXS + SEG_SHIFT);
        const int32_t f = (wx >> 4) & 4095;
        const fx a = t.h[i & (RING - 1)], b = t.h[(i + 1) & (RING - 1)];
        const fx h = a + (fx)(((int64_t)(b - a) * f) >> 12);
        int g = (int)(((int64_t)(h - cam.y) * invZ) >> 28);
        if (g < -320) g = -320;
        const int ga = g < 0 ? 0 : g > H ? H : g;
        int n = nearBase - nearH[(nearOff + c) & 511];
        if (n > ga) n = ga;
        if (n < 0) n = 0;
        int fa = farBase - farH[(farOff + c) & 511];
        if (fa > n) fa = n;
        if (fa < 0) fa = 0;
        colTop[c] = (int16_t)fa;
        colGround[c] = (int16_t)g;
        // physical row: p = 319 - ly
        gfx::copySpanP(row + (320 - fa), skyP + (320 - fa), fa);
        gfx::fillSpanP(row + (320 - n), n - fa, farC);
        gfx::fillSpanP(row + (320 - ga), ga - n, nearC);
        const int base = 320 + g;                                // row[p] = A[base + p]
        if (g < H) {
            if (base & 1) gfx::copySpanP(row, gA1 + (base - 1), 320 - ga);
            else gfx::copySpanP(row, gA0 + base, 320 - ga);
        }
    }
}

// --- decor --------------------------------------------------------------------------------------
void decor(int map, Terrain& t) {
    const int32_t stepW = cam.zoom << 4;
    const int slot0 = (int)(cam.x >> (FXS + SEG_SHIFT + 3)) - 1;
    const int slot1 = (int)((cam.x + (int64_t)stepW * W) >> (FXS + SEG_SHIFT + 3)) + 1;
    for (int s = slot0; s <= slot1; ++s) {
        int kind, size;
        if (!decorAt(map, t.seed, s, kind, size)) continue;
        const fx wx = ((fx)s << (FXS + SEG_SHIFT + 3)) + (4 << (FXS + SEG_SHIFT));
        const fx gy = groundAt(t, wx) + (3 << FXS);
        const int X = sxq(wx), Y = syq(gy);
        const int u = lenq(size);                                  // Q4 unit
        if (X < -40 * 16 || X > (W + 40) * 16) continue;
        switch (kind) {
            case D_TREE: {
                gfx::thickLineQ4(X, Y, X, Y - 3 * u, u / 2 + 8, rgb(110, 75, 40));
                gfx::fillCircleQ4(X, Y - 4 * u, (u * 3) / 2, rgb(50, 130, 55));
                gfx::fillCircleQ4(X - u / 2, Y - 4 * u - u / 2, u, rgb(70, 155, 65));
                break;
            }
            case D_CACTUS: {
                const uint16_t g = rgb(70, 140, 70);
                gfx::thickLineQ4(X, Y, X, Y - 4 * u, u, g);
                gfx::thickLineQ4(X - u, Y - 2 * u, X - u, Y - 3 * u, (u * 2) / 3, g);
                gfx::thickLineQ4(X - u, Y - 2 * u, X, Y - 2 * u, (u * 2) / 3, g);
                gfx::thickLineQ4(X + u, Y - (5 * u) / 2, X + u, Y - (7 * u) / 2, (u * 2) / 3, g);
                gfx::thickLineQ4(X + u, Y - (5 * u) / 2, X, Y - (5 * u) / 2, (u * 2) / 3, g);
                break;
            }
            case D_PINE: {
                gfx::thickLineQ4(X, Y, X, Y - u, u / 2 + 8, rgb(90, 60, 40));
                for (int k = 0; k < 3; ++k) {
                    const int by = Y - u - k * (u + u / 3), w = (3 - k) * u / 2 + u / 2;
                    const int xs[3] = { X - w, X + w, X }, ys[3] = { by, by, by - (u * 3) / 2 };
                    gfx::fillPolyQ4(xs, ys, 3, rgb(40, 95, 70));
                    const int xs2[3] = { X - w / 3, X + w / 3, X }, ys2[3] = { by - u, by - u, by - (u * 3) / 2 };
                    gfx::fillPolyQ4(xs2, ys2, 3, C_WHITE);
                }
                break;
            }
            case D_ROCK: {
                const int xs[6] = { X - 2 * u, X - u, X + u / 2, X + 2 * u, X + (5 * u) / 2, X - (5 * u) / 2 };
                const int ys[6] = { Y - u, Y - 2 * u, Y - (5 * u) / 2, Y - (3 * u) / 2, Y, Y };
                gfx::fillPolyQ4(xs, ys, 6, rgb(150, 70, 50));
                const int xs2[3] = { X - u, X + u / 2, X - u / 2 }, ys2[3] = { Y - 2 * u, Y - (5 * u) / 2, Y - u };
                gfx::fillPolyQ4(xs2, ys2, 3, rgb(185, 100, 70));
                break;
            }
            case D_BASALT: {
                const int xs[4] = { X - u, X - u / 2, X + u / 2, X + u }, ys[4] = { Y, Y - 4 * u, Y - (7 * u) / 2, Y };
                gfx::fillPolyQ4(xs, ys, 4, rgb(30, 22, 25));
                gfx::fillCircleQ4(X, Y - 4 * u, u / 2, rgb(255, 110, 30));
                break;
            }
            case D_CONE: {
                const int xs[3] = { X - u, X + u, X }, ys[3] = { Y, Y, Y - 3 * u };
                gfx::fillPolyQ4(xs, ys, 3, C_ORANGE);
                gfx::thickLineQ4(X - u / 2, Y - (3 * u) / 2, X + u / 2, Y - (3 * u) / 2, u / 2, C_WHITE);
                break;
            }
            default: break;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Vehicles
// ---------------------------------------------------------------------------------------------
namespace {

enum { K_C1, K_C2, K_DARK, K_GREY, K_GLASS, K_LIGHT, K_BLACK, K_ORANGE, K_DRIVER = 254, K_END = 255 };

// A part: polygon (n >= 3 points) or line (n = 2, w = width). Coordinates in vehicle px.
struct Part { uint8_t col; uint8_t n; uint8_t w; int8_t p[12]; };

const Part ART_BUGGY[] = {
    { K_GREY, 4, 0, { -27, -7, -19, -7, -19, 2, -27, 2 } },                                  // engine
    { K_C1, 5, 0, { -23, -9, 9, -9, 25, -3, 25, 4, -23, 4 } },                             // tub
    { K_C2, 4, 0, { -12, -6, 13, -6, 19, -2, -12, -2 } },                                  // stripe
    { K_DRIVER, 0, 0, {} },
    { K_DARK, 2, 3, { -15, -9, -9, -31 } },                                                // roll cage
    { K_DARK, 2, 3, { -9, -31, 6, -31 } },
    { K_DARK, 2, 3, { 6, -31, 12, -9 } },
    { K_DARK, 4, 0, { 18, -6, 26, -6, 26, -3, 18, -3 } },                                  // bumper
    { K_END, 0, 0, {} },
};

const Part ART_BIKE[] = {
    { K_GREY, 4, 0, { -7, -2, 6, -2, 6, 7, -7, 7 } },                                      // engine
    { K_DARK, 2, 3, { 4, -4, 21, 10 } },                                                   // down tube to the front axle
    { K_C1, 5, 0, { -12, -11, 6, -13, 14, -7, 5, -1, -10, -1 } },                          // tank
    { K_BLACK, 4, 0, { -20, -12, -5, -12, -5, -9, -20, -9 } },                             // seat
    { K_C2, 4, 0, { -24, -12, -18, -12, -18, -8, -26, -8 } },                              // tail
    { K_DRIVER, 0, 0, {} },
    { K_DARK, 2, 3, { 12, -8, 15, -19 } },                                                 // handlebar
    { K_C1, 4, 0, { 16, -12, 27, -9, 26, -6, 15, -9 } },                                   // front fender
    { K_END, 0, 0, {} },
};

const Part ART_TRACTOR[] = {
    { K_C1, 4, 0, { -23, -13, -2, -13, -2, 4, -23, 4 } },                                  // cab base
    { K_C1, 4, 0, { -3, -12, 27, -9, 27, 3, -3, 3 } },                                     // hood
    { K_DARK, 4, 0, { 24, -8, 28, -8, 28, 1, 24, 1 } },                                    // grille
    { K_DARK, 2, 3, { 15, -10, 15, -25 } },                                                // exhaust
    { K_DRIVER, 0, 0, {} },
    { K_DARK, 2, 3, { -22, -13, -22, -38 } },                                              // cab posts
    { K_DARK, 2, 3, { -2, -13, -2, -38 } },
    { K_C1, 4, 0, { -25, -41, 1, -41, 1, -37, -25, -37 } },                                // roof
    { K_C2, 4, 0, { -35, -8, -2, -8, -2, -4, -35, -4 } },                                  // rear fender
    { K_END, 0, 0, {} },
};

const Part ART_RALLY[] = {
    { K_C1, 6, 0, { -15, -8, -8, -20, 10, -20, 19, -8, 19, -6, -15, -6 } },                // cabin
    { K_DRIVER, 0, 0, {} },
    { K_GLASS, 4, 0, { 0, -18, 9, -18, 15, -9, 0, -9 } },                                  // windscreen
    { K_C1, 5, 0, { -31, -9, 22, -8, 33, -3, 33, 4, -31, 4 } },                            // body
    { K_C2, 4, 0, { -31, -3, 33, -1, 33, 1, -31, 1 } },                                    // stripe
    { K_C2, 4, 0, { -34, -15, -25, -15, -25, -12, -34, -12 } },                            // wing
    { K_DARK, 2, 2, { -29, -12, -29, -9 } },
    { K_ORANGE, 4, 0, { 30, -6, 33, -6, 33, -3, 30, -3 } },                                // light
    { K_END, 0, 0, {} },
};

const Part ART_MONSTER[] = {
    { K_DARK, 4, 0, { -24, -2, 24, -2, 24, 8, -24, 8 } },                                  // frame
    { K_C1, 4, 0, { 0, -12, 4, -27, 19, -27, 27, -12 } },                                  // cab
    { K_DRIVER, 0, 0, {} },
    { K_GLASS, 4, 0, { 7, -25, 17, -25, 22, -14, 5, -14 } },                               // window
    { K_C1, 4, 0, { -31, -13, 33, -13, 33, 0, -31, 0 } },                                  // body
    { K_C2, 4, 0, { -31, -9, 33, -9, 33, -6, -31, -6 } },                                  // flames stripe
    { K_LIGHT, 4, 0, { 30, -11, 34, -11, 34, -8, 30, -8 } },
    { K_END, 0, 0, {} },
};

const Part ART_ROVER[] = {
    { K_DARK, 2, 2, { -18, -6, -23, -32 } },                                               // antenna
    { K_LIGHT, 4, 0, { -27, -35, -19, -35, -19, -31, -27, -31 } },
    { K_GLASS, 4, 0, { -31, -17, -6, -13, -6, -10, -31, -14 } },                           // solar panel
    { K_DRIVER, 0, 0, {} },
    { K_C1, 4, 0, { -29, -7, 29, -7, 31, 2, -31, 2 } },                                    // deck
    { K_C2, 4, 0, { -29, -3, 31, -3, 31, -1, -30, -1 } },
    { K_GREY, 4, 0, { 10, -7, 22, -7, 22, -11, 10, -11 } },                                // console
    { K_DARK, 2, 2, { 18, -11, 14, -19 } },                                                // joystick
    { K_END, 0, 0, {} },
};

const Part* const ART[NVEH] = { ART_BUGGY, ART_BIKE, ART_TRACTOR, ART_RALLY, ART_MONSTER, ART_ROVER };

struct Pose {
    fx cx, cy;                 // chassis centre
    int32_t ux, uy;            // chassis x axis (Q12 unit)
    fx wx[2], wy[2];           // wheel centres
    fx hx, hy;                 // head
    int32_t wang[2];           // wheel angles (1024 per turn << 8)
};

uint16_t partColour(int k, const VehDef& v) {
    switch (k) {
        case K_C1: return v.c1;
        case K_C2: return v.c2;
        case K_DARK: return C_DARK;
        case K_GREY: return C_GREY;
        case K_GLASS: return C_GLASS;
        case K_LIGHT: return C_LIGHT;
        case K_ORANGE: return C_ORANGE;
        default: return C_BLACK;
    }
}

void localQ4(const Pose& ps, int lx, int ly, int& X, int& Y) {
    const fx wx = ps.cx + lx * ps.ux - ly * ps.uy;
    const fx wy = ps.cy + lx * ps.uy + ly * ps.ux;
    X = sxq(wx);
    Y = syq(wy);
}

void wheel(const Pose& ps, int w, int r, int style) {
    const int X = sxq(ps.wx[w]), Y = syq(ps.wy[w]);
    const int R = lenq(r);
    gfx::fillCircleQ4(X, Y, R, C_TIRE);
    if (style == S_MONSTER || style == S_TRACTOR) {
        // tread blocks
        const int a0 = (int)(ps.wang[w] >> 8);
        for (int k = 0; k < 8; ++k) {
            const int a = a0 + k * 128;
            const int ox = (gfx::icos(a) * R) >> 14, oy = (gfx::isin(a) * R) >> 14;
            gfx::fillCircleQ4(X + ox, Y + oy, R / 6 + 4, C_TIRE);
        }
    }
    const int rimR = (R * (style == S_BIKE ? 80 : 60)) / 100;
    const uint16_t rim = style == S_TRACTOR ? rgb(250, 210, 40) : style == S_ROVER ? rgb(240, 140, 30) : C_RIM;
    gfx::fillCircleQ4(X, Y, rimR, rim);
    if (style == S_BIKE) gfx::fillCircleQ4(X, Y, rimR - 24, C_TIRE);
    const int a0 = (int)(ps.wang[w] >> 8);
    for (int k = 0; k < 3; ++k) {
        const int a = a0 + k * 341;
        const int ox = (gfx::icos(a) * rimR) >> 14, oy = (gfx::isin(a) * rimR) >> 14;
        gfx::thickLineQ4(X, Y, X + ox, Y + oy, 24, C_HUB);
    }
    gfx::fillCircleQ4(X, Y, R / 5 + 6, C_HUB);
}

void driver(const Pose& ps, const VehDef& v) {
    // torso from the seat to the head, an arm to the front, then the helmet
    int sx, sy, ax, ay;
    localQ4(ps, v.headX - 2, -6, sx, sy);
    localQ4(ps, v.headX + 12, -14, ax, ay);
    const int hx = sxq(ps.hx), hy = syq(ps.hy);
    const int w = lenq(7);
    const uint16_t suit = v.style == S_ROVER ? C_LIGHT : rgb(60, 80, 160);
    gfx::thickLineQ4(sx, sy, hx, hy, w, suit);
    gfx::thickLineQ4((sx + hx * 2) / 3, (sy + hy * 2) / 3, ax, ay, lenq(4), suit);
    gfx::fillCircleQ4(ax, ay, lenq(2), C_SKIN);
    const int R = lenq(7);
    gfx::fillCircleQ4(hx, hy, R, v.style == S_ROVER ? C_WHITE : v.c2);
    // visor towards the front of the vehicle
    const int vx = hx + ((ps.ux * R) >> 13), vy = hy + ((ps.uy * R) >> 13) - R / 6;
    gfx::fillCircleQ4(vx, vy, (R * 11) / 20, v.style == S_ROVER ? C_GOLD : rgb(40, 50, 70));
}

void drawPose(const Pose& ps, int veh) {
    const VehDef& v = VEHS[veh];
    // suspension struts behind everything
    int ax, ay;
    const uint16_t strut = v.style == S_MONSTER ? rgb(240, 200, 40) : C_DARK;
    for (int w = 0; w < 2; ++w) {
        localQ4(ps, w ? v.bodyHalf - 2 : -v.bodyHalf + 2, 0, ax, ay);
        gfx::thickLineQ4(ax, ay, sxq(ps.wx[w]), syq(ps.wy[w]), lenq(4), strut);
    }
    wheel(ps, 0, v.r0, v.style);
    wheel(ps, 1, v.r1, v.style);
    int xs[6], ys[6];
    for (const Part* p = ART[veh]; p->col != K_END; ++p) {
        if (p->col == K_DRIVER) { driver(ps, v); continue; }
        const uint16_t c = partColour(p->col, v);
        for (int i = 0; i < p->n; ++i) localQ4(ps, p->p[2 * i], p->p[2 * i + 1], xs[i], ys[i]);
        if (p->n == 2) gfx::thickLineQ4(xs[0], ys[0], xs[1], ys[1], lenq(p->w), c);
        else gfx::fillPolyQ4(xs, ys, p->n, c);
    }
}

Pose restPose(int veh, fx cx, fx cy, int32_t ux, int32_t uy) {
    const VehDef& v = VEHS[veh];
    Pose ps;
    ps.cx = cx; ps.cy = cy; ps.ux = ux; ps.uy = uy;
    const int lx[2] = { v.wheelX0, v.wheelX1 }, ly[2] = { v.wheelY0, v.wheelY1 };
    for (int w = 0; w < 2; ++w) {
        ps.wx[w] = cx + lx[w] * ux - ly[w] * uy;
        ps.wy[w] = cy + lx[w] * uy + ly[w] * ux;
        ps.wang[w] = 0;
    }
    ps.hx = cx + v.headX * ux - v.headY * uy;
    ps.hy = cy + v.headX * uy + v.headY * ux;
    return ps;
}

// --- items -----------------------------------------------------------------------------------
void coin(int X, int Y, int R, int value) {
    uint16_t a, b;
    switch (value) {
        case 25: a = rgb(215, 225, 240); b = rgb(130, 145, 175); break;
        case 100: a = rgb(120, 230, 120); b = rgb(30, 130, 60); break;
        case 500: a = rgb(255, 120, 200); b = rgb(170, 40, 120); break;
        default: a = rgb(255, 215, 60); b = rgb(200, 130, 20); break;
    }
    gfx::fillCircleQ4(X, Y, R, b);
    gfx::fillCircleQ4(X - R / 8, Y - R / 8, (R * 3) / 4, a);
    gfx::fillCircleQ4(X - R / 3, Y - R / 3, R / 5 + 4, C_WHITE);
}

void fuelCan(int X, int Y, int u) {          // u = Q4 size unit (1 px at zoom 1)
    const int xs[5] = { X - 7 * u, X + 3 * u, X + 7 * u, X + 7 * u, X - 7 * u };
    const int ys[5] = { Y - 9 * u, Y - 9 * u, Y - 5 * u, Y + 9 * u, Y + 9 * u };
    gfx::fillPolyQ4(xs, ys, 5, rgb(150, 25, 20));
    const int xs2[5] = { X - 6 * u, X + 3 * u, X + 6 * u, X + 6 * u, X - 6 * u };
    const int ys2[5] = { Y - 8 * u, Y - 8 * u, Y - 5 * u, Y + 8 * u, Y + 8 * u };
    gfx::fillPolyQ4(xs2, ys2, 5, C_RED);
    gfx::thickLineQ4(X - 5 * u, Y - 11 * u, X + 1 * u, Y - 11 * u, 2 * u, rgb(150, 25, 20));
    gfx::thickLineQ4(X + 5 * u, Y - 8 * u, X + 7 * u, Y - 12 * u, 3 * u, C_DARK);
    gfx::thickLineQ4(X - 3 * u, Y - 2 * u, X + 3 * u, Y + 4 * u, 2 * u, C_GOLD);
    gfx::thickLineQ4(X + 3 * u, Y - 2 * u, X - 3 * u, Y + 4 * u, 2 * u, C_GOLD);
}

void signpost(int X, int Y, int m, uint16_t board, uint16_t ink, int u) {
    gfx::thickLineQ4(X, Y, X, Y - 26 * u, 2 * u + 8, C_DARK);
    char b[12];
    char* p = gfx::putInt(b, m);
    if (board != C_GOLD) { *p++ = 'M'; *p = 0; }
    const int tw = gfx::textWidth(b, 1);
    const int bx = (X >> 4) - tw / 2 - 3, by = (Y >> 4) - lenp(26) - 11;
    gfx::fillRect(bx, by, tw + 6, 11, board);
    gfx::text(bx + 3, by + 2, b, 1, ink);
}

void items(Run& r) {
    Terrain& t = r.terr;
    const int32_t stepW = cam.zoom << 4;
    const int m0 = (int)(cam.x >> (FXS + SEG_SHIFT)) - 2;
    const int m1 = (int)((cam.x + (int64_t)stepW * W) >> (FXS + SEG_SHIFT)) + 2;
    const int u = lenq(1);
    // distance signs and the record flag
    for (int m = (m0 < 100 ? 100 : (m0 / 100) * 100); m <= m1; m += 100) {
        if (m < m0) continue;
        const fx x = (fx)m << (FXS + SEG_SHIFT);
        signpost(sxq(x), syq(groundAt(t, x) + (2 << FXS)), m, C_WHITE, C_BLACK, u);
    }
    if (r.startBest > 20 && r.startBest >= m0 && r.startBest <= m1) {
        const fx x = (fx)r.startBest << (FXS + SEG_SHIFT);
        const int X = sxq(x), Y = syq(groundAt(t, x) + (2 << FXS));
        gfx::thickLineQ4(X, Y, X, Y - 44 * u, 2 * u + 8, C_DARK);
        const int xs[3] = { X, X + 22 * u, X }, ys[3] = { Y - 44 * u, Y - 38 * u, Y - 32 * u };
        gfx::fillPolyQ4(xs, ys, 3, C_RED);
        gfx::textShadow((X >> 4) + 4, (Y >> 4) - lenp(44) - 12, "BEST", 1, C_WHITE, C_BLACK);
    }
    // fuel
    for (int i = 0; i < r.nFuel; ++i) {
        const int fm = r.fuelAt[i];
        if (fm > m1) break;
        if (fm < m0 || fuelTaken(r, i)) continue;
        const fx x = (fx)fm << (FXS + SEG_SHIFT);
        const fx y = groundAt(t, x) - (20 << FXS) + (fx)((gfx::isin(r.ticks * 6) * 2) >> (14 - FXS));
        fuelCan(sxq(x), syq(y), u);
    }
    // coins
    const int R = lenq(7);
    for (int s = m0 / COIN_SLOT_M; s <= m1 / COIN_SLOT_M; ++s) {
        int value;
        if (!coinSlot(r, s, value)) continue;
        for (int k = 0; k < COINS_PER_SLOT; ++k) {
            if (coinTaken(r, s, k)) continue;
            const fx x = coinX(s, k);
            const int X = sxq(x);
            if (X < -200 || X > (W + 12) * 16) continue;
            coin(X, syq(groundAt(t, x) - (26 << FXS)), R, value);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public drawing
// ---------------------------------------------------------------------------------------------
void drawWorld(Run& r, const Camera& c) {
    setMap(r.map);
    setCamera(c);
    drawLand(r.terr);
    skyDecor(r.map);
    decor(r.map, r.terr);
    items(r);
    Pose ps;
    bodyFrame(r, ps.cx, ps.cy, ps.ux, ps.uy);
    ps.wx[0] = r.p[P_W0].x; ps.wy[0] = r.p[P_W0].y;
    ps.wx[1] = r.p[P_W1].x; ps.wy[1] = r.p[P_W1].y;
    ps.hx = r.p[P_HEAD].x; ps.hy = r.p[P_HEAD].y;
    ps.wang[0] = r.wheelAng[0]; ps.wang[1] = r.wheelAng[1];
    drawPose(ps, r.veh);
}

void drawScenery(int map, int atM, int veh, int zoom, int carSx) {
    setMap(map);
    if (menuTerr.map != map || menuTerr.seed != mapSeed(map)) terrainInit(menuTerr, map, mapSeed(map));
    const fx at = (fx)atM << (FXS + SEG_SHIFT);
    Camera c;
    c.zoom = zoom;
    c.x = at - ((carSx * zoom) << 4);
    c.y = groundAt(menuTerr, at) - (((H * 80 / 100) * zoom) << 4);
    setCamera(c);
    drawLand(menuTerr);
    skyDecor(map);
    decor(map, menuTerr);
    if (veh >= 0) {
        const VehDef& v = VEHS[veh];
        const fx x0 = at + (v.wheelX0 << FXS), x1 = at + (v.wheelX1 << FXS);
        const fx y0 = groundAt(menuTerr, x0) - (v.r0 << FXS), y1 = groundAt(menuTerr, x1) - (v.r1 << FXS);
        const fx dx = x1 - x0, dy = y1 - y0;
        const fx L = isqrt64((uint64_t)((int64_t)dx * dx + (int64_t)dy * dy));
        const int32_t ux = (int32_t)(((int64_t)dx << 12) / L), uy = (int32_t)(((int64_t)dy << 12) / L);
        // chassis centre: from the rear wheel go back along the body axis, then up by wheelY0
        const fx cx = x0 - v.wheelX0 * ux + v.wheelY0 * uy, cy = y0 - v.wheelX0 * uy - v.wheelY0 * ux;
        drawPose(restPose(veh, cx, cy, ux, uy), veh);
    }
}

void drawVehiclePreview(int veh, int x, int y, int scale) {
    Camera c;
    c.zoom = (256 * 256) / scale;
    c.x = -(fx)((x * c.zoom) << 4);
    c.y = -(fx)((y * c.zoom) << 4);
    setCamera(c);
    drawPose(restPose(veh, 0, 0, ONE, 0), veh);
}

void drawMapPreview(int map, int x, int y, int w, int h) {
    const MapDef& m = MAPS[map];
    const uint32_t seed = mapSeed(map);
    for (int k = 0; k < h; ++k) gfx::hspan(x, x + w - 1, y + k, gfx::mix(m.skyTop, m.skyBot, (k * 256) / h));
    // sample 40 .. 640 m
    static int32_t e[160];
    const int n = w < 160 ? w : 160;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for (int c = 0; c < n; ++c) {
        e[c] = rawHeight(map, seed, 40 + (c * 600) / n);
        if (e[c] < lo) lo = e[c];
        if (e[c] > hi) hi = e[c];
    }
    const int32_t span = hi - lo < 16 ? 16 : hi - lo;
    for (int c = 0; c < n; ++c) {
        const int top = y + h / 5 + (int)(((int64_t)(e[c] - lo) * (h * 3 / 5)) / span);
        gfx::vspan(x + c, top, top + 1, m.surface);
        gfx::vspan(x + c, top + 2, y + h - 1, m.soil);
    }
}

// ---------------------------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------------------------
void drawCoin(int cx, int cy, int r) { coin(cx * 16 + 8, cy * 16 + 8, r * 16, 5); }

void drawHud(const Run& r, bool gas, bool brake, int fps) {
    char b[32];
    // fuel gauge
    const int32_t pctF = (int32_t)(((int64_t)r.fuel * 100) / (r.c.fuelMax ? r.c.fuelMax : 1));
    gfx::fillRect(4, 4, 132, 22, C_PANEL);
    fuelCan(14 * 16, 15 * 16, 10);
    const bool low = pctF < 20;
    const uint16_t fc = pctF > 50 ? rgb(70, 210, 80) : pctF > 20 ? rgb(240, 200, 40) : rgb(240, 60, 40);
    gfx::frameRect(26, 9, 104, 12, C_DIM);
    if (!low || (r.ticks / 30) % 2 == 0) gfx::fillRect(28, 11, pctF, 8, fc);
    if (low && r.fuel > 0 && (r.ticks / 30) % 2 == 0) gfx::textShadow(26, 30, "LOW FUEL", 1, rgb(255, 80, 60), C_BLACK);
    // distance + best
    char* p = gfx::putInt(b, metres(r));
    gfx::putStr(p, " M");
    const int tw = gfx::textWidth(b, 3);
    gfx::textShadow(W / 2 - tw / 2, 6, b, 3, C_WHITE, C_BLACK);
    if (r.startBest > 0) {
        p = gfx::putStr(b, "BEST ");
        p = gfx::putInt(p, r.startBest > metres(r) ? r.startBest : metres(r));
        gfx::putStr(p, " M");
        gfx::textCentered(W / 2, 32, b, 1, C_DIM);
    }
    // coins
    gfx::putInt(b, coinsOf(r));
    const int cw = gfx::textWidth(b, 2);
    drawCoin(PAUSE_X - 18 - cw, 16, 8);
    gfx::textShadow(PAUSE_X - 6 - cw, 9, b, 2, C_GOLD, C_BLACK);
    // pause button
    gfx::fillRect(PAUSE_X, PAUSE_Y, PAUSE_W, PAUSE_H, C_PANEL);
    gfx::fillRect(PAUSE_X + 9, PAUSE_Y + 6, 4, 14, C_WHITE);
    gfx::fillRect(PAUSE_X + 17, PAUSE_Y + 6, 4, 14, C_WHITE);
    // speed
    p = gfx::putInt(b, speedKmh(r));
    gfx::putStr(p, " KM/H");
    gfx::textCentered(W / 2, H - 12, b, 1, C_WHITE);
    // pedals
    for (int k = 0; k < 2; ++k) {
        const bool on = k ? gas : brake;
        const int x = k ? W - PEDAL_W - 6 : 6;
        const uint16_t base = k ? rgb(40, 150, 60) : rgb(170, 50, 40);
        const uint16_t c = on ? gfx::tint(base, 90) : base;
        gfx::fillRect(x, PEDAL_Y + 4, PEDAL_W, PEDAL_H - 4, gfx::shade(c, 150));
        gfx::fillRect(x, PEDAL_Y + (on ? 4 : 0), PEDAL_W, PEDAL_H - 8, c);
        for (int g = 0; g < 3; ++g) gfx::fillRect(x + 8, PEDAL_Y + (on ? 12 : 8) + g * 10, PEDAL_W - 16, 3, gfx::shade(c, 180));
        gfx::textCentered(x + PEDAL_W / 2, PEDAL_Y - 10, k ? "GAS" : "BRAKE", 1, C_WHITE);
    }
    if (fps >= 0) {
        p = gfx::putInt(b, fps);
        gfx::putStr(p, " FPS");
        gfx::text(6, 44, b, 1, C_WHITE);
    }
    // popup
    if (r.popupT > 0 && r.popup != PU_NONE) {
        p = b;
        switch (r.popup) {
            case PU_AIR: p = gfx::putStr(p, "AIR TIME +"); gfx::putInt(p, r.popupVal); break;
            case PU_FLIP: p = gfx::putStr(p, "FLIP! +"); gfx::putInt(p, r.popupVal); break;
            case PU_DFLIP: p = gfx::putStr(p, "MULTI FLIP! +"); gfx::putInt(p, r.popupVal); break;
            case PU_FUEL: gfx::putStr(p, "FUEL!"); break;
            case PU_RECORD: gfx::putStr(p, "NEW RECORD!"); break;
            case PU_MILE: p = gfx::putInt(p, r.popupVal); p = gfx::putStr(p, " M  +"); gfx::putInt(p, r.popupVal / 5); break;
            default: *p = 0; break;
        }
        const uint16_t pc = r.popup == PU_FUEL ? rgb(255, 120, 90) : r.popup == PU_RECORD ? rgb(120, 230, 255) : C_GOLD;
        const int sc = 2;
        const int rise = (sec(1.6) - r.popupT) / 8;
        gfx::textShadow(W / 2 - gfx::textWidth(b, sc) / 2, 64 - rise, b, sc, pc, C_BLACK);
    }
}

// ---------------------------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------------------------
void drawPanel(int x, int y, int w, int h, uint16_t accent) {
    gfx::fillRect(x, y, w, h, C_PANEL);
    gfx::frameRect(x, y, w, h, accent, 2);
}

void drawButton(int x, int y, int w, int h, const char* label, int scale, uint16_t color) {
    gfx::fillRect(x, y + h - 4, w, 4, gfx::shade(color, 140));
    gfx::fillRect(x, y, w, h - 4, color);
    gfx::fillRect(x, y, w, 2, gfx::tint(color, 80));
    const int tw = gfx::textWidth(label, scale);
    gfx::textShadow(x + (w - tw) / 2, y + (h - 4 - 7 * scale) / 2, label, scale, C_WHITE, gfx::shade(color, 110));
}

void drawBar(int x, int y, int w, int h, int value, int max, uint16_t color) {
    gfx::fillRect(x, y, w, h, rgb(50, 54, 70));
    if (max > 0 && value > 0) gfx::fillRect(x, y, (w * (value > max ? max : value)) / max, h, color);
}

void drawPips(int x, int y, int level, int max, uint16_t on) {
    for (int i = 0; i < max; ++i) gfx::fillRect(x + i * 12, y, 10, 10, i < level ? on : rgb(60, 64, 82));
}

void dim() {
    gfx::u32a* d = (gfx::u32a*)gfx::fb;
    for (int n = gfx::PW * gfx::PH / 2; n > 0; --n, ++d) *d = (*d >> 1) & 0x7BEF7BEFu;
}

}  // namespace render
