// Hill Climber rules: terrain, vehicles, physics, pickups, stunts and progression.
// Pure C++ (no SDK, no floating point) so it runs unchanged in the PC tests.
//
// Units: world coordinates are pixels at zoom 1 (16 px = 1 metre), y points DOWN (like the
// screen). Positions are Q12 fixed point (`fx`). One tick = 1/120 s; the physics runs one
// Verlet step per tick.
#pragma once

#include <stdint.h>

namespace hc {

typedef int32_t fx;
constexpr int FXS = 12;
constexpr fx ONE = 1 << FXS;
constexpr int TICK_HZ = 120;
constexpr int sec(double s) { return (int)(s * TICK_HZ + 0.5); }
constexpr fx px(double p) { return (fx)(p * ONE + (p < 0 ? -0.5 : 0.5)); }

constexpr int PX_PER_M = 16;                 // world pixels per metre
constexpr int SEG_SHIFT = 4;                 // terrain sample every 16 px (1 m)
constexpr int SEG = 1 << SEG_SHIFT;
constexpr int RING = 512;                    // cached terrain samples (512 m around the car)
constexpr int MAX_M = 30000;                 // end of every road (Q12 world x must fit in int32)

// ---------------------------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------------------------
enum Feature : uint8_t { F_HILLS, F_DUNES, F_ICE, F_TERRACES, F_CRATERS, F_SPIKES, F_RAMPS };
enum Decor : uint8_t { D_TREE, D_CACTUS, D_PINE, D_ROCK, D_NONE, D_BASALT, D_CONE };

struct MapDef {
    const char* name;
    const char* blurb;
    int32_t price;
    Feature feature;
    Decor decor;
    int16_t gravity;          // Q12 px / tick^2
    int16_t grip;             // Q8 multiplier for tyre grip (256 = normal)
    int16_t amp[4];           // noise amplitude in px for 64 m, 16 m, 4 m and 2 m wavelengths
    int16_t rampM;            // metres over which the terrain amplitude doubles
    int16_t trend;            // uphill trend in px per 100 m
    int16_t fuelGap;          // metres between fuel cans at the start (grows with distance)
    // colours (RGB565)
    uint16_t skyTop, skyBot, far, nearHill, surface, soil, deep, accent;
};

constexpr int NMAPS = 7;
extern const MapDef MAPS[NMAPS];

// ---------------------------------------------------------------------------------------------
// Vehicles
// ---------------------------------------------------------------------------------------------
enum Upgrade { U_ENGINE, U_TIRES, U_SUSP, U_FUEL, U_AIR, U_COUNT };
constexpr int MAX_LEVEL = 8;

enum Style : uint8_t { S_BUGGY, S_BIKE, S_MONSTER, S_TRACTOR, S_RALLY, S_ROVER };

struct VehDef {
    const char* name;
    const char* blurb;
    int32_t price;
    Style style;
    // geometry, px at zoom 1 (x forward, y down, origin = middle of the chassis line)
    int8_t bodyHalf;          // chassis points at (-bodyHalf, 0) and (+bodyHalf, 0)
    int8_t headX, headY;      // driver's head (crash point)
    int8_t wheelX0, wheelX1;  // rear / front wheel x
    int8_t wheelY0, wheelY1;  // rear / front wheel centre y at rest
    int8_t r0, r1;            // wheel radii
    // physics
    uint8_t massBody, massWheel;     // relative masses
    int16_t engine;           // Q12 px/tick^2 of wheel surface acceleration at level 0
    int16_t topSpeed;         // Q12 px/tick at level 0
    int16_t grip;             // Q12 max friction impulse per tick at level 0
    int16_t susK, susC;       // suspension spring / damper (Q12) at level 0
    int16_t fuel;             // tank in seconds of driving at level 0
    int16_t air;              // air control torque (Q12) at level 0
    int16_t wheelie;          // engine reaction torque (Q12 fraction of drive)
    uint8_t drive;            // bit 0 rear wheel, bit 1 front wheel
    uint16_t c1, c2;          // body colours
    int16_t upBase;           // upgrade cost base
};

constexpr int NVEH = 6;
extern const VehDef VEHS[NVEH];

// ---------------------------------------------------------------------------------------------
// Player progress (saved)
// ---------------------------------------------------------------------------------------------
struct Profile {
    int32_t coins;
    int32_t best[NMAPS];          // metres
    int32_t totalM;               // metres driven, all runs
    int32_t totalCoins;           // coins earned, all runs
    int32_t runs;
    int32_t flips;
    uint8_t level[NVEH][U_COUNT];
    uint8_t vehOwned;             // bitmask
    uint8_t mapOwned;             // bitmask
    uint8_t veh, map;             // current choice
};

void profileNew(Profile& p);
int32_t upgradeCost(const Profile& p, int veh, int u);    // 0 = maxed
bool buyUpgrade(Profile& p, int veh, int u);
bool buyVehicle(Profile& p, int veh);
bool buyMap(Profile& p, int map);
inline bool ownsVeh(const Profile& p, int v) { return (p.vehOwned >> v) & 1; }
inline bool ownsMap(const Profile& p, int m) { return (p.mapOwned >> m) & 1; }

// ---------------------------------------------------------------------------------------------
// Terrain: a deterministic height function per map, cached in a ring of 1 m samples.
// ---------------------------------------------------------------------------------------------
struct Terrain {
    uint8_t map;
    uint32_t seed;
    int lo, hi;                   // cached sample indices [lo, hi)
    fx h[RING];                   // surface y at x = i * SEG
    int16_t tx[RING], ty[RING];   // unit tangent of segment i -> i+1 (Q12, tx > 0)
    fx len[RING];                 // segment length (Q12)
};

void terrainInit(Terrain& t, int map, uint32_t seed);
void terrainEnsure(Terrain& t, int i0, int i1);         // make samples i0..i1 available
fx groundAt(Terrain& t, fx x);                          // surface y (linear interpolation)
int32_t rawHeight(int map, uint32_t seed, int i);       // Q4 px, the generator itself (tests, previews)

// ---------------------------------------------------------------------------------------------
// A run
// ---------------------------------------------------------------------------------------------
enum PIdx { P_W0, P_W1, P_B0, P_B1, P_HEAD, NP };

struct Particle { fx x, y, ox, oy; };

struct Tuned {                    // vehicle constants after upgrades and map modifiers
    fx rad[NP];
    int32_t inv[NP];              // inverse mass, Q12
    fx restBody, restHead0, restHead1, restArm0, restArm1, restSus0, restSus1;
    fx travelLo0, travelHi0, travelLo1, travelHi1;
    int32_t engine, topSpeed, reverseTop, brake, grip, susK, susC, air, airMax, wheelie, gravity;
    int32_t massRatio;            // total mass / wheel mass, Q12
    int32_t fuelMax, fuelIdle, fuelGas;
    uint8_t drive;
};

enum EndReason : uint8_t { END_NONE, END_CRASH, END_FUEL, END_QUIT, END_FINISH };
enum Popup : uint8_t { PU_NONE, PU_AIR, PU_FLIP, PU_DFLIP, PU_FUEL, PU_RECORD, PU_MILE };

constexpr int COIN_SLOT_M = 30;               // a coin group every 30 m (unless a fuel can is there)
constexpr int NSLOTS = MAX_M / COIN_SLOT_M + 2;
constexpr int COINS_PER_SLOT = 5;
constexpr int MAX_FUEL_CANS = 256;

struct Input {
    bool gas, brake;
};

struct Run {
    uint8_t veh, map;
    uint8_t over;                 // EndReason
    uint8_t onGround;             // bit 0 rear wheel, bit 1 front wheel touching
    uint8_t bodyTouch;            // chassis touching the ground this tick
    uint8_t popup;
    uint8_t recordShown;
    uint8_t lastGas;
    Tuned c;
    Particle p[NP];
    fx spin[2];                   // wheel surface speed (px/tick Q12), + = rolling forward
    int32_t wheelAng[2];          // wheel rotation, 1024 per turn << 8
    int32_t fuel;                 // Q12 ticks
    int32_t ticks, overT;
    int32_t airT;                 // ticks with no wheel on the ground
    int32_t rotAcc;               // body rotation while airborne (1024 = one turn)
    int32_t lastAng;
    int32_t bestX;                // furthest metre reached
    int32_t startBest;            // the record before this run (metres)
    int32_t coinsPickup, coinsStunt, coinsDist;
    int32_t flips, airBest, stillT;
    int32_t popupT, popupVal;
    int32_t nextMile;             // next distance bonus (metres)
    int16_t fuelAt[MAX_FUEL_CANS];   // metre position of every fuel can
    int nFuel;
    uint8_t fuelTaken[MAX_FUEL_CANS / 8];
    uint8_t coinTaken[NSLOTS];    // bit k = coin k of that slot collected
    Terrain terr;
};

void runStart(Run& r, const Profile& prof, int veh, int map, uint32_t seed);
void step(Run& r, const Input& in);              // one tick
inline int32_t coinsOf(const Run& r) { return r.coinsPickup + r.coinsStunt + r.coinsDist; }
inline int32_t metres(const Run& r) { return r.bestX; }
void finishRun(Run& r, Profile& p);              // bank coins, update records

// --- queries used by the renderer and tests ----------------------------------------------------
int bodyAngle(const Run& r);                     // 0..1023, 0 = level, increasing = nose down
void bodyFrame(const Run& r, fx& cx, fx& cy, int32_t& ux, int32_t& uy);   // centre + unit x axis (Q12)
// Midpoints as a + (b - a) / 2: two Q12 x positions past 16 km would overflow int32 if added.
inline fx mid(fx a, fx b) { return a + ((b - a) >> 1); }
inline fx carX(const Run& r) { return mid(r.p[P_B0].x, r.p[P_B1].x); }
inline fx carY(const Run& r) { return mid(r.p[P_B0].y, r.p[P_B1].y); }
inline fx carOldX(const Run& r) { return mid(r.p[P_B0].ox, r.p[P_B1].ox); }
inline fx carOldY(const Run& r) { return mid(r.p[P_B0].oy, r.p[P_B1].oy); }
int32_t speedKmh(const Run& r);

// Coins: slot s (every COIN_SLOT_M metres), k = 0..4. Returns false if the slot has no coins.
bool coinSlot(const Run& r, int s, int& value);
fx coinX(int s, int k);
inline bool coinTaken(const Run& r, int s, int k) { return (r.coinTaken[s] >> k) & 1; }
inline bool fuelTaken(const Run& r, int i) { return (r.fuelTaken[i >> 3] >> (i & 7)) & 1; }

// Decor (trees, rocks...) every 8 m slot: returns false for an empty slot.
bool decorAt(int map, uint32_t seed, int slot, int& kind, int& size);

// helpers
int iatan2(int32_t y, int32_t x);                // 0..1023 (y down, 0 = +x)
int32_t isqrt64(uint64_t v);
uint32_t hash32(uint32_t x);

}  // namespace hc
