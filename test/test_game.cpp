// Unit tests for the rules (game.cpp) and the save format (save.cpp). PC only.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "save.h"

using namespace hc;

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKM(c, ...) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t seedOf(int map) { return 0x13579BDFu * (uint32_t)(map + 1) + 0x2468ACE0u; }
static Run run;
static Profile prof;

static void testMath() {
    for (int deg = -179; deg <= 180; deg += 7) {
        const double a = deg * M_PI / 180;
        const int32_t x = (int32_t)(cos(a) * 100000), y = (int32_t)(sin(a) * 100000);
        int want = (int)lround(deg * 1024.0 / 360.0) & 1023;
        int got = iatan2(y, x);
        int d = got - want;
        if (d > 512) d -= 1024;
        if (d < -512) d += 1024;
        CHECKM(d >= -2 && d <= 2, "atan2 %d deg: got %d want %d", deg, got, want);
    }
    CHECK(iatan2(0, 0) == 0);
    const uint64_t vals[] = { 0, 1, 2, 3, 4, 15, 16, 17, 1000000, 4294967295ull, 4294967296ull, 123456789012345ull, (1ull << 62) - 1 };
    for (uint64_t v : vals) {
        const uint64_t r = (uint64_t)isqrt64(v);
        CHECKM(r * r <= v && (r + 1) * (r + 1) > v, "isqrt64(%llu) = %llu", (unsigned long long)v, (unsigned long long)r);
    }
}

static void testTerrain() {
    for (int m = 0; m < NMAPS; ++m) {
        const uint32_t s = seedOf(m);
        CHECK(rawHeight(m, s, 500) == rawHeight(m, s, 500));
        // continuity: no 1 m step taller than 6 px x amplitude growth (catches broken features)
        int32_t worst = 0;
        int at = 0;
        for (int i = 1; i < MAX_M - 1; ++i) {
            const int32_t d = rawHeight(m, s, i + 1) - rawHeight(m, s, i);       // Q4, + = downhill
            if (-d > worst) { worst = -d; at = i; }
        }
        // up-steps: never a wall of more than 4.5 px per px (77 degrees)
        CHECKM(worst <= 16 * 16 * 45 / 10, "map %d: up-step of %d px at %d m", m, worst / 16, at);
        // the start pad is flat
        for (int i = 0; i <= 12; ++i) CHECK(rawHeight(m, s, i) == 0);
        // the wall behind the start rises
        CHECK(rawHeight(m, s, -10) < -16 * 100);
    }
    // the ring cache gives the same heights as the generator, also after moving back and forth
    static Terrain t;
    terrainInit(t, 2, seedOf(2));
    const int probes[] = { 10, 300, 900, 899, 2000, 120, 5000, 4700, 29990, 3 };
    for (int p : probes) {
        const fx x = ((fx)p << (FXS + SEG_SHIFT)) + (7 << FXS);
        const fx g = groundAt(t, x);
        const int32_t a = rawHeight(2, seedOf(2), p) << 8, b = rawHeight(2, seedOf(2), p + 1) << 8;
        const fx want = a + (fx)(((int64_t)(b - a) * (7 << FXS)) >> (FXS + SEG_SHIFT));
        CHECKM(g == want, "groundAt(%d m) %d want %d", p, g, want);
    }
}

static void freshProfile() { profileNew(prof); }

static int sang(int a) { a &= 1023; return a >= 512 ? a - 1024 : a; }

static void testRest() {
    // every vehicle parked on every map: settles on both wheels, does not creep or vibrate
    for (int m = 0; m < NMAPS; ++m)
        for (int v = 0; v < NVEH; ++v) {
            freshProfile();
            runStart(run, prof, v, m, seedOf(m));
            Input none = { false, false };
            for (int t = 0; t < sec(3); ++t) step(run, none);
            const fx x0 = carX(run);
            for (int t = 0; t < sec(2); ++t) step(run, none);
            const fx moved = carX(run) - x0;
            CHECKM(moved < (1 << FXS) && moved > -(1 << FXS), "veh %d map %d creeps %d", v, m, moved);
            CHECKM(run.onGround == 3, "veh %d map %d: wheels on ground %d", v, m, run.onGround);
            CHECKM(sang(bodyAngle(run)) > -20 && sang(bodyAngle(run)) < 20, "veh %d map %d tilted %d", v, m, sang(bodyAngle(run)));
            CHECK(!run.over);
            CHECK(!run.bodyTouch);
        }
}

static void testDrive() {
    freshProfile();
    runStart(run, prof, 0, 0, seedOf(0));
    Input gas = { true, false }, brake = { false, true }, none = { false, false };
    for (int t = 0; t < sec(1); ++t) step(run, none);
    const fx x0 = carX(run);
    const int32_t f0 = run.fuel;
    for (int t = 0; t < sec(2); ++t) step(run, gas);
    CHECK(carX(run) > x0 + (8 << (FXS + SEG_SHIFT)));            // moved more than 8 m
    CHECK(speedKmh(run) > 15);
    CHECK(run.fuel < f0);
    // brake to a stop, then reverse
    for (int t = 0; t < sec(4); ++t) step(run, brake);
    const fx x1 = carX(run);
    for (int t = 0; t < sec(2); ++t) step(run, brake);
    CHECK(carX(run) < x1);
    CHECK(!run.over);
    // distance is metres from the start line
    CHECK(metres(run) >= 8);
}

static void testFuel() {
    freshProfile();
    runStart(run, prof, 0, 0, seedOf(0));
    run.fuel = 50;                                               // almost empty
    Input gas = { true, false };
    for (int t = 0; t < sec(1); ++t) step(run, gas);
    CHECK(run.fuel == 0);
    for (int t = 0; t < sec(8) && !run.over; ++t) step(run, gas);
    CHECK(run.over == END_FUEL);
    // a can refills the tank
    freshProfile();
    runStart(run, prof, 0, 0, seedOf(0));
    run.fuel = run.c.fuelMax / 2;
    const fx fx0 = (fx)run.fuelAt[0] << (FXS + SEG_SHIFT);
    // teleport the car just before the can
    const fx dx = fx0 - carX(run) - (2 << FXS);
    fx dy = 0;
    {
        static Terrain t;
        terrainInit(t, 0, seedOf(0));
        dy = groundAt(t, fx0) - groundAt(t, carX(run)) - (6 << FXS);
    }
    for (int i = 0; i < NP; ++i) { run.p[i].x += dx; run.p[i].ox += dx; run.p[i].y += dy; run.p[i].oy += dy; }
    Input none = { false, false };
    for (int t = 0; t < 4; ++t) step(run, none);
    CHECK(fuelTaken(run, 0));
    CHECK(run.fuel > run.c.fuelMax - run.c.fuelMax / 20);
    CHECK(run.popup == PU_FUEL);
    // fuel cans get rarer
    for (int i = 2; i < run.nFuel; ++i) CHECK(run.fuelAt[i] - run.fuelAt[i - 1] > run.fuelAt[i - 1] - run.fuelAt[i - 2]);
}

static void testCrash() {
    freshProfile();
    runStart(run, prof, 0, 0, seedOf(0));
    // turn the car upside down 30 px above the ground
    const fx ox = carX(run), oy = carY(run);
    const fx cx = ox, cy = oy - (30 << FXS);
    for (int i = 0; i < NP; ++i) {
        const fx lx = run.p[i].x - ox, ly = run.p[i].y - oy;
        run.p[i].x = run.p[i].ox = cx - lx;
        run.p[i].y = run.p[i].oy = cy - ly;
    }
    Input none = { false, false };
    for (int t = 0; t < sec(3) && !run.over; ++t) step(run, none);
    CHECK(run.over == END_CRASH);
}

static void testCoins() {
    freshProfile();
    runStart(run, prof, 0, 0, seedOf(0));
    int s = 1, value = 0;
    while (!coinSlot(run, s, value)) ++s;
    CHECK(value == 5);
    // drive the bot-free way: teleport above the coin row and let it fall through it
    const fx x = coinX(s, 2);
    static Terrain t;
    terrainInit(t, 0, seedOf(0));
    const fx dx = x - carX(run), dy = groundAt(t, x) - (40 << FXS) - carY(run);
    for (int i = 0; i < NP; ++i) { run.p[i].x += dx; run.p[i].ox += dx; run.p[i].y += dy; run.p[i].oy += dy; }
    Input none = { false, false };
    for (int t2 = 0; t2 < sec(1); ++t2) step(run, none);
    CHECK(coinTaken(run, s, 2));
    CHECK(run.coinsPickup >= value);
    const int32_t c = run.coinsPickup;
    for (int t2 = 0; t2 < sec(1); ++t2) step(run, none);
    CHECK(run.coinsPickup == c);                                 // no double pickups
    // coin values grow with distance, slots near fuel cans stay empty
    bool big = false;
    for (int k = 1; k < NSLOTS; ++k) {
        int v;
        if (!coinSlot(run, k, v)) continue;
        if (k * COIN_SLOT_M < 500) CHECK(v == 5);
        if (v >= 100) big = true;
        for (int i = 0; i < run.nFuel; ++i) CHECK(run.fuelAt[i] < k * COIN_SLOT_M - 4 || run.fuelAt[i] > k * COIN_SLOT_M + 24);
    }
    CHECK(big);
}

static void testFlip() {
    // low gravity, full air control: jump off a 200 px drop, hold gas (nose up) until one full
    // backwards turn is done, then level out with the brake like a player would.
    freshProfile();
    for (int v = 0; v < NVEH; ++v) prof.level[v][U_AIR] = MAX_LEVEL;
    runStart(run, prof, 0, 4, seedOf(4));
    const fx up = 260 << FXS;
    for (int i = 0; i < NP; ++i) { run.p[i].y -= up; run.p[i].oy -= up; }
    int32_t maxRot = 0;
    for (int t = 0; t < sec(10) && !run.over; ++t) {
        Input in = { false, false };
        if (run.airT > 0) {
            if (run.rotAcc > -1000) in.gas = true;
            else if (sang(bodyAngle(run)) < -20) in.brake = true;
        }
        step(run, in);
        if (run.rotAcc < maxRot) maxRot = run.rotAcc;
        if (run.flips) break;
    }
    CHECKM(maxRot <= -1000, "rotation in the air %d", maxRot);
    CHECKM(run.flips == 1 && run.coinsStunt > 0 && run.popup == PU_FLIP, "flips %d over %d", run.flips, run.over);
}

static void testProfile() {
    Profile p;
    profileNew(p);
    CHECK(ownsVeh(p, 0) && !ownsVeh(p, 1));
    CHECK(ownsMap(p, 0) && !ownsMap(p, 1));
    CHECK(!buyUpgrade(p, 0, U_ENGINE));                          // no coins
    p.coins = 1000000;
    const int32_t c0 = upgradeCost(p, 0, U_ENGINE);
    CHECK(c0 == VEHS[0].upBase);
    CHECK(buyUpgrade(p, 0, U_ENGINE));
    CHECK(p.coins == 1000000 - c0 && p.level[0][U_ENGINE] == 1);
    CHECK(upgradeCost(p, 0, U_ENGINE) > c0);
    for (int i = 1; i < MAX_LEVEL; ++i) CHECK(buyUpgrade(p, 0, U_ENGINE));
    CHECK(upgradeCost(p, 0, U_ENGINE) == 0);
    CHECK(!buyUpgrade(p, 0, U_ENGINE));
    CHECK(!buyUpgrade(p, 1, U_ENGINE));                          // not owned
    CHECK(buyVehicle(p, 1) && ownsVeh(p, 1));
    CHECK(!buyVehicle(p, 1));
    CHECK(buyMap(p, 3) && ownsMap(p, 3));
    p.coins = 5;
    CHECK(!buyMap(p, 6) && !buyVehicle(p, 5));
    // prices and upgrade bases increase with the index
    for (int i = 1; i < NVEH; ++i) CHECK(VEHS[i].price > VEHS[i - 1].price && VEHS[i].upBase >= VEHS[i - 1].upBase);
    for (int i = 1; i < NMAPS; ++i) CHECK(MAPS[i].price > MAPS[i - 1].price);
    // banking a run
    Profile q;
    profileNew(q);
    runStart(run, q, 0, 0, seedOf(0));
    run.bestX = 321;
    run.coinsPickup = 100; run.coinsStunt = 50; run.coinsDist = 0;
    finishRun(run, q);
    CHECK(q.coins == 150 && q.best[0] == 321 && q.runs == 1 && q.totalM == 321);
    run.bestX = 100;
    finishRun(run, q);
    CHECK(q.best[0] == 321 && q.coins == 300);
}

static void testUpgradesHelp() {
    // a fully upgraded engine accelerates harder and tops out faster than the stock one
    int32_t top[2];
    for (int k = 0; k < 2; ++k) {
        Profile p;
        profileNew(p);
        p.level[0][U_ENGINE] = (uint8_t)(k ? MAX_LEVEL : 0);
        runStart(run, p, 0, 0, seedOf(0));
        Input gas = { true, false };
        for (int t = 0; t < sec(2.5); ++t) step(run, gas);
        top[k] = carX(run);
    }
    CHECK(top[1] > top[0] + (5 << (FXS + SEG_SHIFT)));
    Profile p;
    profileNew(p);
    runStart(run, p, 0, 0, seedOf(0));
    const int32_t f0 = run.c.fuelMax;
    p.level[0][U_FUEL] = 4;
    runStart(run, p, 0, 0, seedOf(0));
    CHECK(run.c.fuelMax > f0);
}

static void testSave() {
    save::Data d, e;
    save::defaults(d);
    d.set.pace = 6;
    d.set.timer = 1;
    d.set.autoZoom = 0;
    d.prof.coins = 123456;
    d.prof.vehOwned = 0x0B;
    d.prof.mapOwned = 0x05;
    d.prof.veh = 3;
    d.prof.map = 2;
    d.prof.best[2] = 1234;
    d.prof.level[3][U_TIRES] = 5;
    static uint8_t rec[save::RECORD_SIZE];
    save::encode(d, rec);
    CHECK(save::decode(rec, e));
    CHECK(memcmp(&d, &e, sizeof d) == 0);
    // corruption, bad version, bad values
    rec[20] ^= 0x40;
    CHECK(!save::decode(rec, e));
    CHECK(e.set.pace == 4 && e.prof.coins == 0);                 // defaults
    save::encode(d, rec);
    rec[3] = save::VERSION + 1;
    CHECK(!save::decode(rec, e));
    save::Data bad = d;
    bad.prof.veh = 2;                                            // not owned
    save::encode(bad, rec);
    CHECK(!save::decode(rec, e));
    bad = d;
    bad.prof.level[0][0] = MAX_LEVEL + 1;
    save::encode(bad, rec);
    CHECK(!save::decode(rec, e));
    bad = d;
    bad.set.pace = 0;
    save::encode(bad, rec);
    CHECK(!save::decode(rec, e));
    // file round trip (host build writes out/test.sav)
    CHECK(save::store(d));
    CHECK(save::load(e));
    CHECK(e.prof.coins == 123456 && e.prof.best[2] == 1234);
}

int main() {
    testMath();
    testTerrain();
    testRest();
    testDrive();
    testFuel();
    testCrash();
    testCoins();
    testFlip();
    testProfile();
    testUpgradesHelp();
    testSave();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
