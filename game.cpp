// Hill Climber rules: see game.h. No SDK calls, no floating point at run time.
#include "game.h"

namespace hc {

constexpr uint16_t rgb(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

constexpr int G_STD = 64;          // standard gravity, Q12 px / tick^2 (about 1.4 g at 16 px/m)

// ---------------------------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------------------------
//                     name              blurb                                      price feature  decor   grav grip  amp(64,16,4,2 m)   ramp trend fuel
const MapDef MAPS[NMAPS] = {
    { "CLOVER HILLS",  "ROLLING GREEN HILLS. A GOOD PLACE TO START.",        0, F_HILLS,    D_TREE,   64, 256, { 70, 26, 3, 1 },  700, 30, 260,
      rgb(80, 150, 230), rgb(200, 230, 250), rgb(150, 200, 165), rgb(95, 165, 95), rgb(80, 185, 60), rgb(135, 95, 55), rgb(85, 58, 35), rgb(255, 255, 255) },
    { "DUNE SEA",      "SHARP SAND CRESTS AND SOFT, SLIPPERY SAND.",       3000, F_DUNES,    D_CACTUS, 64, 205, { 64, 20, 2, 0 },  650,  0, 270,
      rgb(90, 165, 230), rgb(250, 225, 175), rgb(235, 200, 145), rgb(220, 170, 100), rgb(245, 210, 125), rgb(220, 175, 100), rgb(175, 125, 70), rgb(255, 245, 190) },
    { "FROSTBITE PASS","STEEP SNOW AND ICE. GRIP IS EVERYTHING.",          8000, F_ICE,      D_PINE,   64, 125, { 70, 32, 2, 0 },  650, 40, 260,
      rgb(110, 140, 195), rgb(225, 235, 248), rgb(205, 218, 238), rgb(165, 185, 220), rgb(248, 252, 255), rgb(175, 212, 238), rgb(105, 145, 200), rgb(255, 255, 255) },
    { "RED CANYON",    "ROCK SHELVES, CLIFFS AND LEDGES AT SUNSET.",      15000, F_TERRACES, D_ROCK,   64, 275, { 95, 30, 4, 2 },  700, 20, 270,
      rgb(235, 125, 80), rgb(255, 215, 150), rgb(195, 110, 80), rgb(160, 75, 55), rgb(205, 115, 60), rgb(170, 82, 48), rgb(115, 48, 33), rgb(255, 235, 170) },
    { "MOON CRATERS",  "LOW GRAVITY. HUGE JUMPS. MIND THE CRATERS.",      30000, F_CRATERS,  D_NONE,   22, 256, { 80, 28, 4, 2 },  800,  0, 320,
      rgb(4, 4, 18), rgb(22, 22, 48), rgb(55, 55, 72), rgb(85, 85, 102), rgb(175, 175, 180), rgb(122, 122, 130), rgb(78, 78, 88), rgb(70, 130, 225) },
    { "MAGMA RIDGE",   "JAGGED VOLCANIC PEAKS. ONLY FOR THE BRAVE.",      50000, F_SPIKES,   D_BASALT, 68, 260, { 80, 30, 5, 2 },  550, 60, 260,
      rgb(45, 10, 15), rgb(140, 45, 25), rgb(75, 28, 28), rgb(48, 22, 22), rgb(255, 125, 35), rgb(68, 32, 30), rgb(32, 16, 16), rgb(255, 90, 0) },
    { "STUNT PARK",    "KICKERS AND TABLETOPS. FLIP FOR COINS!",          70000, F_RAMPS,    D_CONE,   64, 285, { 30, 10, 2, 0 },  900,  0, 290,
      rgb(20, 28, 70), rgb(95, 70, 130), rgb(45, 48, 85), rgb(32, 32, 55), rgb(95, 95, 108), rgb(62, 62, 72), rgb(40, 40, 48), rgb(250, 210, 40) },
};

//     name           blurb                                          price  style      bh hx  hy  wx0 wx1 wy0 wy1 r0 r1 mb mw eng  top   grip sK   sC  fuel air  wh  drv  colours                                    up
const VehDef VEHS[NVEH] = {
    { "BUGGY",       "LIGHT AND FORGIVING. REAR WHEEL DRIVE.",           0, S_BUGGY,   18, -3, -25, -21, 21, 12, 12, 11, 11, 4, 2,  24,  9000, 40,  40, 420, 24, 40, 700, 1, rgb(230, 60, 40),  rgb(250, 200, 40),  120 },
    { "SCRAMBLER",   "DIRT BIKE: FAST, FLIPS EASILY, SMALL TANK.",    6000, S_BIKE,    13, -4, -33, -20, 21, 10, 10, 12, 12, 3, 2,  27, 10500, 42,  32, 330, 20, 60, 1400, 1, rgb(40, 120, 230), rgb(240, 240, 240), 150 },
    { "TRACTOR",     "SLOW BUT CLIMBS ANYTHING. BIG FUEL TANK.",     12000, S_TRACTOR, 19, -8, -31, -20, 25,  7, 14, 16,  9, 6, 4,  34,  6200, 60,  56, 520, 36, 28, 400, 1, rgb(60, 160, 60),  rgb(250, 210, 40),  200 },
    { "RALLY CAR",   "FOUR WHEEL DRIVE AND A HIGH TOP SPEED.",       25000, S_RALLY,   22,  0, -20, -24, 24,  9,  9, 10, 10, 5, 2,  33, 14000, 44,  62, 520, 26, 36, 350, 3, rgb(240, 240, 245), rgb(30, 90, 200),  260 },
    { "MONSTER",     "HUGE SOFT WHEELS EAT BUMPS. 4WD.",             45000, S_MONSTER, 21, -2, -33, -28, 28, 22, 22, 17, 17, 6, 4,  33,  9800, 54,  30, 380, 28, 40, 800, 3, rgb(140, 60, 200), rgb(60, 220, 120),  330 },
    { "MOON ROVER",  "SPACE TECH: GRIPPY, AGILE, HUGE TANK. 4WD.",   80000, S_ROVER,   22,  4, -27, -25, 25, 14, 14, 13, 13, 5, 2,  37, 11500, 60,  38, 440, 42, 66, 500, 3, rgb(225, 225, 230), rgb(240, 140, 30),  420 },
};

static const uint8_t COST_MUL[MAX_LEVEL] = { 1, 2, 4, 7, 11, 16, 22, 30 };

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------
uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

int32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = (uint64_t)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (int32_t)r;
}

int iatan2(int32_t y, int32_t x) {
    if (x == 0 && y == 0) return 0;
    const int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int a;
    if (ax >= ay) {
        const int32_t r = (int32_t)(((int64_t)ay << 12) / ax);              // 0..4096
        a = (128 * r + ((((r * (4096 - r)) >> 12) * 89) >> 1)) >> 12;      // atan(r) in 1024ths
    } else {
        const int32_t r = (int32_t)(((int64_t)ax << 12) / ay);
        a = 256 - ((128 * r + ((((r * (4096 - r)) >> 12) * 89) >> 1)) >> 12);
    }
    if (x < 0) a = 512 - a;
    if (y < 0) a = -a;
    return a & 1023;
}

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t absi(int32_t v) { return v < 0 ? -v : v; }
// rounds to nearest: flooring (plain >>) biases every product towards -inf and parked cars creep
static inline int32_t mulq(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << (FXS - 1))) >> FXS); }

// ---------------------------------------------------------------------------------------------
// Terrain generator
// ---------------------------------------------------------------------------------------------
static int32_t lattice(int32_t k, uint32_t salt) {          // -4096..4095
    return (int32_t)(hash32((uint32_t)k * 0x9E3779B1u ^ salt) & 8191) - 4096;
}

// Smooth value noise, wavelength 2^shift samples. Q12 in -4096..4096.
static int32_t vnoise(int i, int shift, uint32_t salt) {
    const int32_t k = i >> shift;
    const int32_t t = ((i & ((1 << shift) - 1)) << 12) >> shift;        // Q12 0..4095
    const int32_t s = (((t * t) >> 12) * (3 * 4096 - 2 * t)) >> 12;     // smoothstep
    const int32_t a = lattice(k, salt), b = lattice(k + 1, salt);
    return a + (((b - a) * s) >> 12);
}

static int32_t smooth12(int32_t t) {                                   // smoothstep, Q12 in/out
    t = clampi(t, 0, 4096);
    return (((t * t) >> 12) * (3 * 4096 - 2 * t)) >> 12;
}

// Canyon shelf height of cell k (Q12 px): the noise at the cell start, quantised to a step
// that grows with distance (computed per shelf, so the terrain stays continuous).
static int32_t shelf(const MapDef& m, uint32_t seed, int k) {
    const int i = k * 32;
    const int32_t q = 256 + clampi((i * 256) / m.rampM, 0, 256 * 3);
    const int32_t S = (24 + (q >> 4)) << 12;
    const int32_t e = (int32_t)(((int64_t)(vnoise(i, 6, seed ^ 0xA5A5A5A5u) * m.amp[0] + vnoise(i, 4, seed ^ 0x3C3C1234u) * m.amp[1]) * q) >> 8);
    return e >= 0 ? (e / S) * S : -(((-e) + S - 1) / S) * S;
}

// Elevation (up = positive) in Q12 px before the start blend.
static int32_t elevation(const MapDef& m, uint32_t seed, int i) {
    const int32_t q = 256 + clampi((i * 256) / m.rampM, 0, 256 * 3);   // Q8 amplitude growth (x1 .. x4)
    const int32_t n0 = vnoise(i, 6, seed ^ 0xA5A5A5A5u);
    const int32_t n1 = vnoise(i, 4, seed ^ 0x3C3C1234u);
    const int32_t n2 = vnoise(i, 2, seed ^ 0x5EED0042u);
    const int32_t n3 = vnoise(i, 1, seed ^ 0x77777777u);
    int32_t big, mid;
    switch (m.feature) {
        case F_DUNES:   big = (4096 - 2 * absi(n0)) * m.amp[0]; mid = n1 * m.amp[1]; break;
        case F_SPIKES:  big = n0 * m.amp[0]; mid = (4096 - 2 * absi(n1)) * m.amp[1]; break;
        default:        big = n0 * m.amp[0]; mid = n1 * m.amp[1]; break;
    }
    int32_t e = (int32_t)(((int64_t)(big + mid) * q) >> 8);
    const int32_t small = n2 * m.amp[2] + n3 * m.amp[3];
    e += (int32_t)(((int64_t)small * (q > 512 ? 512 : q)) >> 8);

    if (m.feature == F_TERRACES) {
        // shelves along x: every 32 m cell is flat, then a ramp to the next shelf. The ramp gets
        // longer for bigger steps so that the slope stays around 40 degrees.
        const int C = 32;
        const int k = i / C, xin = i % C;
        const int32_t ek = shelf(m, seed, k), en = shelf(m, seed, k + 1);
        const int32_t d = en - ek, ad = absi(d) >> 12;                 // px
        int RL = (ad * 3) / 22;                                        // metres: slope ~ 1.5 * d / RL <= 0.85
        if (RL < 6) RL = 6;
        if (RL > 26) RL = 26;
        const int32_t t = ((xin - (C - RL)) << 12) / RL;
        e = ek + (int32_t)(((int64_t)d * smooth12(t)) >> 12) + (int32_t)(((int64_t)small * (q > 512 ? 512 : q)) >> 8);
    } else if (m.feature == F_CRATERS) {
        const int cell = i >> 6;
        for (int c = cell - 1; c <= cell + 1; ++c) {
            if (c < 1) continue;
            const uint32_t h = hash32((uint32_t)c * 0x51ED27u ^ seed);
            const int R = 7 + (int)(h % 10u) + (q > 512 ? 3 : 0);       // radius in m
            const int cx = c * 64 + 12 + (int)((h >> 8) % 40u);
            const int32_t D = (R * (3 * 256 + q)) >> 8;                  // depth px (4R .. 7R)
            const int32_t u = ((i - cx) << 12) / R;                      // Q12, -inf..inf
            const int32_t au = absi(u);
            if (au < 4096) e += (int32_t)(((int64_t)((au * au) >> 12) - 4096) * D);
            const int32_t rd = absi(au - 4096);
            if (rd < 1434) e += (int32_t)((((int64_t)4096 - (((int64_t)rd * rd * 8) >> 12)) * D) / 6);
        }
    } else if (m.feature == F_RAMPS) {
        const int cell = i / 90;
        const uint32_t h = hash32((uint32_t)cell * 0x2F1B3u ^ seed);
        if (cell >= 1) {
            const int s = cell * 90 + 20 + (int)(h % 20u);
            const int L = 12 + (int)((h >> 8) % 10u);
            const int32_t H = (36 + (int)((h >> 16) % 30u)) * q >> 8;       // ramp height px
            const int x = i - s;
            if ((h >> 24) & 1) {                                         // kicker, then a drop
                if (x >= 0 && x < L) e += (int32_t)((int64_t)H * ((x * x) << 12) / (L * L));
            } else {                                                      // tabletop
                const int top = 10;
                if (x >= 0 && x < L) e += (int32_t)(((int64_t)H * smooth12((x << 12) / L)));
                else if (x >= L && x < L + top) e += H << 12;
                else if (x >= L + top && x < 2 * L + top) e += (int32_t)(((int64_t)H * smooth12(((2 * L + top - x) << 12) / L)));
            }
        }
    }
    e += (int32_t)(((int64_t)i * m.trend << 12) / 100);
    return e;
}

int32_t rawHeight(int map, uint32_t seed, int i) {
    const MapDef& m = MAPS[map];
    if (i <= 0) {
        // the start pad, and a wall behind it
        const int32_t e0 = 0;
        return -(e0 + ((int32_t)(-i) * (int32_t)(-i > 3 ? 48 : 0) << 4));
    }
    if (i >= MAX_M) return rawHeight(map, seed, MAX_M - 1) - (((int32_t)(i - MAX_M + 1) * 48) << 4);
    int32_t e = elevation(m, seed, i);
    // flat start: 12 m pad, then blend in over 40 m
    if (i < 52) {
        const int32_t t = smooth12(((i - 12) << 12) / 40);
        e = (int32_t)(((int64_t)e * t) >> 12);
    }
    return -(e >> 8);                        // y down, Q4 px
}

void terrainInit(Terrain& t, int map, uint32_t seed) {
    t.map = (uint8_t)map;
    t.seed = seed;
    t.lo = t.hi = 0;
}

static void genSample(Terrain& t, int i) {
    const int k = i & (RING - 1);
    const fx a = rawHeight(t.map, t.seed, i) << 8, b = rawHeight(t.map, t.seed, i + 1) << 8;
    t.h[k] = a;
    const int64_t dx = (int64_t)SEG << FXS, dy = b - a;
    const fx L = isqrt64((uint64_t)(dx * dx + dy * dy));
    t.len[k] = L;
    t.tx[k] = (int16_t)((dx << 12) / L);
    t.ty[k] = (int16_t)((dy << 12) / L);
}

void terrainEnsure(Terrain& t, int i0, int i1) {
    if (t.hi <= t.lo) {                       // empty: start a window around i0
        t.lo = t.hi = i0;
    }
    if (i1 - i0 >= RING - 1) i1 = i0 + RING - 2;
    while (t.hi <= i1 + 1) {
        genSample(t, t.hi);
        ++t.hi;
        if (t.hi - t.lo > RING) ++t.lo;
    }
    while (t.lo > i0) {
        --t.lo;
        genSample(t, t.lo);
        if (t.hi - t.lo > RING) --t.hi;
    }
}

fx groundAt(Terrain& t, fx x) {
    const int i = x >> (FXS + SEG_SHIFT);
    terrainEnsure(t, i, i + 1);
    const fx a = t.h[i & (RING - 1)], b = t.h[(i + 1) & (RING - 1)];
    const int32_t f = x & ((1 << (FXS + SEG_SHIFT)) - 1);           // Q16 fraction of the segment
    return a + (fx)(((int64_t)(b - a) * f) >> (FXS + SEG_SHIFT));
}

bool decorAt(int map, uint32_t seed, int slot, int& kind, int& size) {
    if (MAPS[map].decor == D_NONE || slot < 2) return false;
    const uint32_t h = hash32((uint32_t)slot * 0x1F123u ^ seed ^ 0xDEC0u);
    if ((h & 255) > 100) return false;
    kind = MAPS[map].decor;
    size = 6 + (int)((h >> 8) % 7u);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------------------------
void profileNew(Profile& p) {
    uint8_t* b = (uint8_t*)&p;
    for (unsigned i = 0; i < sizeof p; ++i) b[i] = 0;
    p.vehOwned = 1;
    p.mapOwned = 1;
}

int32_t upgradeCost(const Profile& p, int veh, int u) {
    const int l = p.level[veh][u];
    if (l >= MAX_LEVEL) return 0;
    return (int32_t)VEHS[veh].upBase * COST_MUL[l];
}

bool buyUpgrade(Profile& p, int veh, int u) {
    const int32_t c = upgradeCost(p, veh, u);
    if (c == 0 || p.coins < c || !ownsVeh(p, veh)) return false;
    p.coins -= c;
    ++p.level[veh][u];
    return true;
}

bool buyVehicle(Profile& p, int veh) {
    if (ownsVeh(p, veh) || p.coins < VEHS[veh].price) return false;
    p.coins -= VEHS[veh].price;
    p.vehOwned |= (uint8_t)(1 << veh);
    return true;
}

bool buyMap(Profile& p, int map) {
    if (ownsMap(p, map) || p.coins < MAPS[map].price) return false;
    p.coins -= MAPS[map].price;
    p.mapOwned |= (uint8_t)(1 << map);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Run setup
// ---------------------------------------------------------------------------------------------
static fx dist(fx ax, fx ay, fx bx, fx by) {
    const int64_t dx = bx - ax, dy = by - ay;
    return isqrt64((uint64_t)(dx * dx + dy * dy));
}

static int32_t pct(int32_t v, int perLevel, int level) { return v * (100 + perLevel * level) / 100; }

static void tune(Run& r, const Profile& prof) {
    const VehDef& v = VEHS[r.veh];
    const MapDef& m = MAPS[r.map];
    const uint8_t* lv = prof.level[r.veh];
    Tuned& c = r.c;
    c.rad[P_W0] = v.r0 << FXS;
    c.rad[P_W1] = v.r1 << FXS;
    c.rad[P_B0] = c.rad[P_B1] = 5 << FXS;
    c.rad[P_HEAD] = 6 << FXS;
    const int mh = 1;                                                    // the driver is light
    c.inv[P_W0] = c.inv[P_W1] = (4 << 12) / v.massWheel;
    c.inv[P_B0] = c.inv[P_B1] = (4 << 12) / v.massBody;
    c.inv[P_HEAD] = (4 << 12) / mh;
    const int M = 2 * v.massBody + mh + 2 * v.massWheel;
    c.massRatio = (M << 12) / v.massWheel;

    // geometry
    const fx b0x = -v.bodyHalf << FXS, b1x = v.bodyHalf << FXS;
    const fx hx = v.headX << FXS, hy = v.headY << FXS;
    const fx w0x = v.wheelX0 << FXS, w1x = v.wheelX1 << FXS, wy0 = v.wheelY0 << FXS, wy1 = v.wheelY1 << FXS;
    c.restBody = b1x - b0x;
    c.restHead0 = dist(b0x, 0, hx, hy);
    c.restHead1 = dist(b1x, 0, hx, hy);
    c.restArm0 = dist(w0x, wy0, b1x, 0);
    c.restArm1 = dist(w1x, wy1, b0x, 0);
    c.restSus0 = dist(w0x, wy0, b0x, 0);
    c.restSus1 = dist(w1x, wy1, b1x, 0);
    c.travelLo0 = (wy0 * 4) / 10;
    c.travelHi0 = (wy0 * 15) / 10 + (3 << FXS);
    c.travelLo1 = (wy1 * 4) / 10;
    c.travelHi1 = (wy1 * 15) / 10 + (3 << FXS);

    const int32_t gScale = (m.gravity << 12) / G_STD;                    // Q12
    c.gravity = m.gravity;
    // low gravity: less engine force too, or every vehicle would flip over backwards on the moon
    c.engine = mulq(pct(v.engine, 9, lv[U_ENGINE]), 2048 + gScale / 2);
    c.topSpeed = pct(v.topSpeed, 6, lv[U_ENGINE]);
    c.reverseTop = c.topSpeed / 3;
    c.brake = 40;
    c.grip = mulq(pct(v.grip, 11, lv[U_TIRES]) * m.grip >> 8, gScale);
    if (c.grip < 6) c.grip = 6;
    c.susK = pct(v.susK, 9, lv[U_SUSP]);
    c.susC = pct(v.susC, 10, lv[U_SUSP]);
    c.air = pct(v.air, 13, lv[U_AIR]);
    c.airMax = pct(5000, 8, lv[U_AIR]) * v.bodyHalf / 20;                   // max end-point spin speed
    c.wheelie = mulq(v.wheelie, gScale);
    c.fuelMax = pct(v.fuel, 15, lv[U_FUEL]) * TICK_HZ * 256;
    c.fuelIdle = 90;
    c.fuelGas = 166;
    c.drive = v.drive;
}

static void place(Particle& p, fx x, fx y) { p.x = p.ox = x; p.y = p.oy = y; }

void runStart(Run& r, const Profile& prof, int veh, int map, uint32_t seed) {
    uint8_t* b = (uint8_t*)&r;
    for (unsigned i = 0; i < sizeof r; ++i) b[i] = 0;
    r.veh = (uint8_t)veh;
    r.map = (uint8_t)map;
    tune(r, prof);
    terrainInit(r.terr, map, seed);
    const VehDef& v = VEHS[veh];
    const fx x0 = 6 * PX_PER_M << FXS;                    // 6 m from the start wall
    const fx lowest = ((v.wheelY0 + v.r0 > v.wheelY1 + v.r1) ? v.wheelY0 + v.r0 : v.wheelY1 + v.r1) << FXS;
    const fx y0 = groundAt(r.terr, x0) - lowest - (2 << FXS);
    place(r.p[P_B0], x0 - (v.bodyHalf << FXS), y0);
    place(r.p[P_B1], x0 + (v.bodyHalf << FXS), y0);
    place(r.p[P_HEAD], x0 + (v.headX << FXS), y0 + (v.headY << FXS));
    place(r.p[P_W0], x0 + (v.wheelX0 << FXS), y0 + (v.wheelY0 << FXS));
    place(r.p[P_W1], x0 + (v.wheelX1 << FXS), y0 + (v.wheelY1 << FXS));
    r.fuel = r.c.fuelMax;
    r.startBest = prof.best[map];
    r.nextMile = 500;
    // fuel cans: the gaps grow with distance
    const MapDef& m = MAPS[map];
    int pos = m.fuelGap;
    for (int i = 0; i < MAX_FUEL_CANS && pos < MAX_M - 50; ++i) {
        r.fuelAt[i] = (int16_t)pos;
        r.nFuel = i + 1;
        pos += m.fuelGap + (i * m.fuelGap) / 4;
    }
    r.lastAng = bodyAngle(r);
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------
int bodyAngle(const Run& r) {
    return iatan2(r.p[P_B1].y - r.p[P_B0].y, r.p[P_B1].x - r.p[P_B0].x);
}

void bodyFrame(const Run& r, fx& cx, fx& cy, int32_t& ux, int32_t& uy) {
    cx = carX(r);
    cy = carY(r);
    const fx dx = r.p[P_B1].x - r.p[P_B0].x, dy = r.p[P_B1].y - r.p[P_B0].y;
    const fx L = dist(0, 0, dx, dy);
    if (L <= 0) { ux = ONE; uy = 0; return; }
    ux = (int32_t)(((int64_t)dx << 12) / L);
    uy = (int32_t)(((int64_t)dy << 12) / L);
}

int32_t speedKmh(const Run& r) {
    const fx vx = carX(r) - ((r.p[P_B0].ox + r.p[P_B1].ox) >> 1);
    const fx vy = carY(r) - ((r.p[P_B0].oy + r.p[P_B1].oy) >> 1);
    return (dist(0, 0, vx, vy) * 27) >> FXS;          // px/tick -> km/h at 16 px/m, 120 Hz
}

fx coinX(int s, int k) { return ((s * COIN_SLOT_M + 8) * PX_PER_M + k * 28) << FXS; }

bool coinSlot(const Run& r, int s, int& value) {
    if (s < 1 || s >= NSLOTS) return false;
    const int m0 = s * COIN_SLOT_M;
    if (m0 + 12 >= MAX_M) return false;
    const uint32_t h = hash32((uint32_t)s * 0xC01Du ^ r.terr.seed);
    if ((h & 255) < 70) return false;                              // ~27 % empty
    // keep clear of fuel cans
    for (int i = 0; i < r.nFuel; ++i) {
        const int d = r.fuelAt[i] - (m0 + 10);
        if (d > -14 && d < 14) return false;
        if (d > 30) break;
    }
    const int roll = (int)((h >> 8) % 100u);
    value = 5;
    if (m0 >= 500 && roll < 25) value = 25;
    if (m0 >= 1500 && roll < 14) value = 100;
    if (m0 >= 3000 && roll < 5) value = 500;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------------------------
struct Contact { fx nx, ny; bool hit; };

static void solveDist(Particle& a, Particle& b, int32_t ia, int32_t ib, fx rest) {
    const fx dx = b.x - a.x, dy = b.y - a.y;
    const fx L = isqrt64((uint64_t)((int64_t)dx * dx + (int64_t)dy * dy));
    if (L <= 0) return;
    const int32_t diff = (int32_t)(((int64_t)(L - rest) << 12) / L);   // Q12 fraction
    const int32_t sum = ia + ib;
    const int32_t wa = (int32_t)(((int64_t)ia << 12) / sum), wb = 4096 - wa;
    const fx cx = (fx)(((int64_t)dx * diff) >> 12), cy = (fx)(((int64_t)dy * diff) >> 12);
    a.x += mulq(cx, wa); a.y += mulq(cy, wa);
    b.x -= mulq(cx, wb); b.y -= mulq(cy, wb);
}

// Keep a wheel within its suspension travel, measured along the chassis "down" axis: lo..hi
// px below the chassis point on its side. Without this a swing arm can rotate the wheel up
// through the chassis.
static void solveTravel(Particle& w, Particle& b, Particle& b0, Particle& b1, int32_t iw, int32_t ib, fx restBody, fx lo, fx hi) {
    const int32_t ux = (int32_t)(((int64_t)(b1.x - b0.x) << 12) / restBody), uy = (int32_t)(((int64_t)(b1.y - b0.y) << 12) / restBody);
    const int32_t px_ = -uy, py_ = ux;                                  // body down axis (unit-ish)
    const fx ly = (fx)(((int64_t)(w.x - b.x) * px_ + (int64_t)(w.y - b.y) * py_) >> 12);
    fx d;
    if (ly < lo) d = lo - ly;
    else if (ly > hi) d = hi - ly;
    else return;
    const int32_t wa = (int32_t)(((int64_t)iw << 12) / (iw + ib)), wb = 4096 - wa;
    const fx dx = mulq(d, px_), dy = mulq(d, py_);
    w.x += mulq(dx, wa); w.y += mulq(dy, wa);
    b.x -= mulq(dx, wb); b.y -= mulq(dy, wb);
}

static void spring(Run& r, int w, int bpt, fx rest, fx* ax, fx* ay) {
    Particle& a = r.p[w];
    Particle& b = r.p[bpt];
    const fx dx = a.x - b.x, dy = a.y - b.y;
    const fx L = isqrt64((uint64_t)((int64_t)dx * dx + (int64_t)dy * dy));
    if (L <= 0) return;
    const int32_t nx = (int32_t)(((int64_t)dx << 12) / L), ny = (int32_t)(((int64_t)dy << 12) / L);
    const fx rvx = (a.x - a.ox) - (b.x - b.ox), rvy = (a.y - a.oy) - (b.y - b.oy);
    const fx rv = (fx)(((int64_t)rvx * nx + (int64_t)rvy * ny) >> 12);
    const int32_t f = mulq(L - rest, r.c.susK) + mulq(rv, r.c.susC);     // + = stretched: pull together
    const fx fx_ = mulq(f, nx), fy_ = mulq(f, ny);
    ax[w] -= mulq(fx_, r.c.inv[w]); ay[w] -= mulq(fy_, r.c.inv[w]);
    ax[bpt] += mulq(fx_, r.c.inv[bpt]); ay[bpt] += mulq(fy_, r.c.inv[bpt]);
}

// Push particle `pi` out of the terrain; remembers the contact normal. Moves the old position
// by the same amount so that the push does not create velocity (the response comes later).
static void collide(Run& r, int pi, Contact& ct) {
    Particle& p = r.p[pi];
    const fx rad = r.c.rad[pi];
    Terrain& t = r.terr;
    const int i0 = (p.x - rad) >> (FXS + SEG_SHIFT), i1 = (p.x + rad) >> (FXS + SEG_SHIFT);
    terrainEnsure(t, i0, i1 + 1);
    bool any = false;
    for (int i = i0; i <= i1; ++i) {
        const int k = i & (RING - 1);
        const fx ax = (fx)i << (FXS + SEG_SHIFT), ay = t.h[k];
        const int32_t tx = t.tx[k], ty = t.ty[k];
        const fx dx = p.x - ax, dy = p.y - ay;
        const fx s = (fx)(((int64_t)dx * tx + (int64_t)dy * ty) >> 12);
        if (s < 0 || s > t.len[k]) continue;
        const int32_t nx = ty, ny = -tx;                                   // out of the ground
        const fx d = (fx)(((int64_t)dx * nx + (int64_t)dy * ny) >> 12);
        if (d >= rad || d < -(24 << FXS)) continue;
        const fx pen = rad - d;
        const fx mx = mulq(pen, nx), my = mulq(pen, ny);
        p.x += mx; p.y += my; p.ox += mx; p.oy += my;
        ct.nx = nx; ct.ny = ny; ct.hit = true;
        any = true;
    }
    // convex corners
    for (int i = i0; i <= i1 + 1; ++i) {
        const fx vx = (fx)i << (FXS + SEG_SHIFT), vy = t.h[i & (RING - 1)];
        const fx dx = p.x - vx, dy = p.y - vy;
        if (dx > rad || dx < -rad || dy > rad || dy < -rad) continue;
        const int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy;
        if (d2 >= (int64_t)rad * rad) continue;
        const fx d = isqrt64((uint64_t)d2);
        if (d <= 0) continue;
        // only if the particle is on the air side of the corner
        if (p.y > groundAt(t, p.x)) continue;
        const int32_t nx = (int32_t)(((int64_t)dx << 12) / d), ny = (int32_t)(((int64_t)dy << 12) / d);
        const fx pen = rad - d;
        const fx mx = mulq(pen, nx), my = mulq(pen, ny);
        p.x += mx; p.y += my; p.ox += mx; p.oy += my;
        if (!any) { ct.nx = nx; ct.ny = ny; }
        ct.hit = true;
        any = true;
    }
    // safety net: a centre below the surface goes straight back up
    const fx g = groundAt(t, p.x);
    if (p.y > g) {
        const fx my = g - rad / 2 - p.y;
        p.y += my; p.oy += my;
        if (!ct.hit) { ct.nx = 0; ct.ny = -ONE; ct.hit = true; }
    }
}

// velocity response of one contact: inelastic normal, friction / traction along the ground
static void respond(Run& r, int pi, const Contact& ct, int32_t& driveF) {
    Particle& p = r.p[pi];
    const int32_t nx = ct.nx, ny = ct.ny, tx = -ny, ty = nx;
    fx vx = p.x - p.ox, vy = p.y - p.oy;
    const fx vn = (fx)(((int64_t)vx * nx + (int64_t)vy * ny) >> 12);
    const fx vt = (fx)(((int64_t)vx * tx + (int64_t)vy * ty) >> 12);
    fx dvn = 0, dvt = 0;
    if (vn < 0) dvn = -vn + ((-vn * 20) >> 8);                     // small bounce
    if (pi <= P_W1) {
        const int w = pi;
        const fx slip = r.spin[w] - vt;
        // exact matching would need slip / (R + S); clamp to the tyre's grip
        const int32_t denom = (7 << 12) + r.c.massRatio;
        int32_t f = (int32_t)(((int64_t)slip << 12) / denom);
        int32_t lim = r.c.grip + (vn < 0 ? (-vn >> 3) : 0);
        f = clampi(f, -lim, lim);
        r.spin[w] -= f * 7;
        dvt = mulq(f, r.c.massRatio);
        if ((r.c.drive >> w) & 1) driveF += f;
    } else {
        // chassis / head scraping along the ground
        const int32_t lim = r.c.gravity;
        dvt = -clampi(vt, -lim, lim);
    }
    vx += mulq(dvn, nx) + mulq(dvt, tx);
    vy += mulq(dvn, ny) + mulq(dvt, ty);
    p.ox = p.x - vx;
    p.oy = p.y - vy;
}

static void award(Run& r, int kind, int32_t val) {
    r.popup = (uint8_t)kind;
    r.popupVal = val;
    r.popupT = sec(1.6);
}

void step(Run& r, const Input& in0) {
    Input in = in0;
    if (r.over) { in.gas = false; in.brake = false; ++r.overT; }
    ++r.ticks;
    Tuned& c = r.c;

    // fuel
    if (!r.over) {
        r.fuel -= c.fuelIdle + ((in.gas || in.brake) ? c.fuelGas : 0);
        if (r.fuel < 0) r.fuel = 0;
    }
    if (r.fuel <= 0) { in.gas = false; in.brake = false; }
    r.lastGas = in.gas;

    // engine / brakes act on the wheel spin
    int32_t engineTorque = 0;
    for (int w = 0; w < 2; ++w) {
        fx& s = r.spin[w];
        const bool driven = (c.drive >> w) & 1;
        if (in.gas && driven) {
            // full power up to 75 % of top speed, then fading out
            int32_t a = c.engine * 8;
            if (s > (c.topSpeed * 3) / 4) a = (int32_t)(((int64_t)a * (c.topSpeed - s) * 4) / c.topSpeed);
            if (a > 0) { s += a; engineTorque += a; }
        } else if (in.brake) {
            if (s > 0) { s -= c.brake * 8; if (s < 0) s = 0; }
            else if (driven && s > -c.reverseTop) { s -= c.engine * 4; engineTorque -= c.engine * 4; }
        } else {
            // rolling resistance: proportional + a small constant part, which stops the
            // integer noise from slowly rolling a parked car (slopes > ~0.2 degrees still roll it)
            s -= s / 512;
            s -= s > 10 ? 10 : s < -10 ? -10 : s;
        }
    }

    // gravity, suspension springs, torques
    fx ax[NP], ay[NP];
    for (int i = 0; i < NP; ++i) { ax[i] = 0; ay[i] = c.gravity; }
    spring(r, P_W0, P_B0, c.restSus0, ax, ay);
    spring(r, P_W1, P_B1, c.restSus1, ax, ay);

    fx cx, cy;
    int32_t ux, uy;
    bodyFrame(r, cx, cy, ux, uy);
    const int32_t perpX = -uy, perpY = ux;                        // body "down" axis
    const bool air = r.onGround == 0 && r.airT > 4;
    // angular velocity: relative perpendicular speed of the chassis end points
    const fx rvx = (r.p[P_B1].x - r.p[P_B1].ox) - (r.p[P_B0].x - r.p[P_B0].ox);
    const fx rvy = (r.p[P_B1].y - r.p[P_B1].oy) - (r.p[P_B0].y - r.p[P_B0].oy);
    const fx angV = (fx)(((int64_t)rvx * perpX + (int64_t)rvy * perpY) >> 12);   // + = nose going down
    int32_t tq = 0;                                                // + = nose down
    if (air) {
        if (in.gas && angV > -c.airMax) tq -= c.air;
        if (in.brake && angV < c.airMax) tq += c.air;
        tq -= angV / 256;                                           // a little rotational drag
    } else if (r.onGround) {
        tq -= mulq(engineTorque >> 3, c.wheelie);
    }
    if (tq) {
        const fx tx_ = mulq(tq, perpX), ty_ = mulq(tq, perpY);
        ax[P_B1] += tx_; ay[P_B1] += ty_;
        ax[P_B0] -= tx_; ay[P_B0] -= ty_;
    }

    // Verlet integration with a tiny air drag
    for (int i = 0; i < NP; ++i) {
        Particle& p = r.p[i];
        const fx vx = p.x - p.ox, vy = p.y - p.oy;
        p.ox = p.x; p.oy = p.y;
        p.x += vx - vx / 1024 + ax[i];          // tiny air drag; '/' rounds towards zero, '>>' would
        p.y += vy - vy / 1024 + ay[i];          // push every slow particle to +x (cars would creep)
    }

    // constraints + collisions
    Contact ct[NP];
    for (int i = 0; i < NP; ++i) ct[i].hit = false;
    const int32_t* inv = c.inv;
    for (int it = 0; it < 3; ++it) {
        solveDist(r.p[P_B0], r.p[P_B1], inv[P_B0], inv[P_B1], c.restBody);
        solveDist(r.p[P_B0], r.p[P_HEAD], inv[P_B0], inv[P_HEAD], c.restHead0);
        solveDist(r.p[P_B1], r.p[P_HEAD], inv[P_B1], inv[P_HEAD], c.restHead1);
        solveDist(r.p[P_W0], r.p[P_B1], inv[P_W0], inv[P_B1], c.restArm0);
        solveDist(r.p[P_W1], r.p[P_B0], inv[P_W1], inv[P_B0], c.restArm1);
        solveTravel(r.p[P_W0], r.p[P_B0], r.p[P_B0], r.p[P_B1], inv[P_W0], inv[P_B0], c.restBody, c.travelLo0, c.travelHi0);
        solveTravel(r.p[P_W1], r.p[P_B1], r.p[P_B0], r.p[P_B1], inv[P_W1], inv[P_B1], c.restBody, c.travelLo1, c.travelHi1);
        for (int i = 0; i < NP; ++i) collide(r, i, ct[i]);
    }
    int32_t driveF = 0;
    for (int i = 0; i < NP; ++i) if (ct[i].hit) respond(r, i, ct[i], driveF);
    (void)driveF;

    // wheel rotation for drawing
    for (int w = 0; w < 2; ++w) {
        const int rad = (w ? VEHS[r.veh].r1 : VEHS[r.veh].r0);
        r.wheelAng[w] += (r.spin[w] * 163 / rad) >> 4;
    }

    // ground state, airtime and flips
    const uint8_t was = r.onGround;
    r.onGround = (uint8_t)((ct[P_W0].hit ? 1 : 0) | (ct[P_W1].hit ? 2 : 0));
    r.bodyTouch = (uint8_t)(ct[P_B0].hit || ct[P_B1].hit);
    const int ang = bodyAngle(r);
    int d = ang - r.lastAng;
    if (d > 512) d -= 1024;
    if (d < -512) d += 1024;
    r.lastAng = ang;
    if (r.onGround == 0 && !r.bodyTouch) {
        ++r.airT;
        r.rotAcc += d;
    } else {
        if (was == 0 && r.onGround && !r.over) {
            // landed
            const int flips = (absi(r.rotAcc) + 200) / 1024;
            if (flips > 0) {
                r.flips += flips;
                const int32_t v = 50 * flips * flips;
                r.coinsStunt += v;
                award(r, flips > 1 ? PU_DFLIP : PU_FLIP, v);
            } else if (r.airT >= sec(1.2)) {
                const int32_t v = (r.airT * 10) / TICK_HZ * 5;     // 50 coins per second of air
                r.coinsStunt += v;
                award(r, PU_AIR, v);
            }
            if (r.airT > r.airBest) r.airBest = r.airT;
        }
        r.airT = 0;
        r.rotAcc = 0;
    }

    if (r.popupT > 0) --r.popupT;
    if (r.over) return;

    // crash: the driver's head hits the ground
    if (ct[P_HEAD].hit) { r.over = END_CRASH; r.overT = 0; return; }

    // distance, records, milestones
    const int32_t m = carX(r) >> (FXS + SEG_SHIFT);
    if (m > r.bestX) {
        r.bestX = m;
        if (!r.recordShown && r.startBest >= 50 && m > r.startBest) { r.recordShown = 1; award(r, PU_RECORD, m); }
        if (m >= r.nextMile) {
            const int32_t v = r.nextMile / 5;                      // +100 at 500 m, +200 at 1000 m ...
            r.coinsDist += v;
            award(r, PU_MILE, r.nextMile);
            r.popupVal = r.nextMile;
            r.nextMile += 500;
        }
    }
    if (m >= MAX_M - 30) { r.over = END_FINISH; r.overT = 0; return; }

    // pickups (around the chassis centre and the head)
    const fx hx = r.p[P_HEAD].x, hy = r.p[P_HEAD].y;
    const int s0 = (m - 6) / COIN_SLOT_M, s1 = (m + 3) / COIN_SLOT_M;
    for (int s = s0 < 1 ? 1 : s0; s <= s1; ++s) {
        int value;
        if (!coinSlot(r, s, value)) continue;
        for (int k = 0; k < COINS_PER_SLOT; ++k) {
            if (coinTaken(r, s, k)) continue;
            const fx x = coinX(s, k), y = groundAt(r.terr, x) - (26 << FXS);
            const fx dxc = x - cx, dyc = y - cy, dxh = x - hx, dyh = y - hy;
            const fx R = 26 << FXS;
            if ((absi(dxc) < R && absi(dyc) < R) || (absi(dxh) < R && absi(dyh) < R)) {
                r.coinTaken[s] |= (uint8_t)(1 << k);
                r.coinsPickup += value;
            }
        }
    }
    for (int i = 0; i < r.nFuel; ++i) {
        const int fm = r.fuelAt[i];
        if (fm > m + 4) break;
        if (fm < m - 6 || fuelTaken(r, i)) continue;
        const fx x = (fx)fm << (FXS + SEG_SHIFT), y = groundAt(r.terr, x) - (20 << FXS);
        if (absi(x - cx) < (26 << FXS) && absi(y - cy) < (34 << FXS)) {
            r.fuelTaken[i >> 3] |= (uint8_t)(1 << (i & 7));
            r.fuel = c.fuelMax;
            award(r, PU_FUEL, 0);
        }
    }

    // out of fuel: wait until the car has (almost) stopped
    if (r.fuel <= 0) {
        const fx vx = cx - ((r.p[P_B0].ox + r.p[P_B1].ox) >> 1);
        if (absi(vx) < (ONE >> 4)) ++r.stillT; else r.stillT = 0;
        if (r.stillT > sec(1.5)) { r.over = END_FUEL; r.overT = 0; }
    }
}

void finishRun(Run& r, Profile& p) {
    const int32_t c = coinsOf(r);
    p.coins += c;
    if (p.coins > 99999999) p.coins = 99999999;
    p.totalCoins += c;
    p.totalM += r.bestX;
    p.flips += r.flips;
    ++p.runs;
    if (r.bestX > p.best[r.map]) p.best[r.map] = r.bestX;
}

}  // namespace hc
