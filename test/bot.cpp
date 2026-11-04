// Bot runs (PC only): drives every vehicle on every map with human-like inputs (bot.h) and
// checks that the physics never explodes. Prints a table of distances for balancing.
//   bot                 all vehicles x maps, level 0 and fully upgraded
//   bot --veh V --map M one combination, verbose trace once per second
//   bot --career        a whole career: run, bank coins, buy the cheapest useful thing, repeat
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "bot.h"

using namespace hc;

static Run run;
static int failures = 0;

static uint32_t mapSeed(int map) { return 0x13579BDFu * (uint32_t)(map + 1) + 0x2468ACE0u; }   // = render::mapSeed

struct Result { int metres, coins, reason, secs, stuck; };

// Sanity checks every tick: finite, connected vehicle, nothing below the ground.
static bool sane(Run& r, const char** why) {
    const fx cx = carX(r);
    for (int i = 0; i < NP; ++i) {
        const fx dx = r.p[i].x - cx;
        if (dx > (120 << FXS) || dx < -(120 << FXS)) { *why = "vehicle torn apart"; return false; }
        if (r.p[i].y > groundAt(r.terr, r.p[i].x) + (6 << FXS)) { *why = "particle below the ground"; return false; }
        const fx vx = r.p[i].x - r.p[i].ox, vy = r.p[i].y - r.p[i].oy;
        if (vx > (40 << FXS) || vx < -(40 << FXS) || vy > (40 << FXS) || vy < -(40 << FXS)) { *why = "velocity exploded"; return false; }
    }
    return true;
}

static Result drive(const Profile& prof, int veh, int map, int skill, bool verbose, int maxSecs = 900) {
    runStart(run, prof, veh, map, mapSeed(map));
    int lastM = 0, stuckT = 0;
    Result res = { 0, 0, 0, 0, 0 };
    for (int t = 0; t < maxSecs * TICK_HZ; ++t) {
        const Input in = bot::drive(run, skill);
        step(run, in);
        const char* why;
        if (!sane(run, &why)) {
            printf("FAIL veh %d map %d at %d m, t=%d: %s\n", veh, map, run.bestX, t, why);
            ++failures;
            break;
        }
        if (verbose && t % TICK_HZ == 0)
            printf("t=%3ds x=%5dm ang=%4d gnd=%d kmh=%3d fuel=%3d%% coins=%d\n", t / TICK_HZ, carX(run) >> 16, bot::sang(bodyAngle(run)),
                   run.onGround, speedKmh(run), (int)((int64_t)run.fuel * 100 / run.c.fuelMax), coinsOf(run));
        if (run.over && run.overT > sec(1.3)) break;
        // stuck with fuel left (cannot climb): the player would give up
        if (t % (TICK_HZ * 4) == 0) {
            if (run.bestX <= lastM + 2 && !run.over) { if (++stuckT >= 4) { res.stuck = 1; run.over = END_QUIT; break; } }
            else stuckT = 0;
            lastM = run.bestX;
        }
    }
    res.metres = run.bestX;
    res.coins = coinsOf(run);
    res.reason = run.over;
    res.secs = run.ticks / TICK_HZ;
    return res;
}

static const char* reasonName(const Result& r) {
    if (r.stuck) return "stuck";
    switch (r.reason) { case END_CRASH: return "crash"; case END_FUEL: return "fuel"; case END_FINISH: return "finish"; default: return "time"; }
}

// Balance gates (CI fails if a tuning change breaks them):
//  * the starting buggy gets well past the first fuel can on the first map
//  * fully upgraded, every vehicle gets further on average than stock
static int sumAt[2];

static void table() {
    for (int lvl = 0; lvl <= MAX_LEVEL; lvl += MAX_LEVEL) {
        printf("\n=== all upgrades at level %d (careful bot) ===\n%-12s", lvl, "");
        for (int m = 0; m < NMAPS; ++m) printf(" %14.14s", MAPS[m].name);
        printf("\n");
        for (int v = 0; v < NVEH; ++v) {
            Profile p;
            profileNew(p);
            for (int u = 0; u < U_COUNT; ++u) p.level[v][u] = (uint8_t)lvl;
            printf("%-12s", VEHS[v].name);
            for (int m = 0; m < NMAPS; ++m) {
                const Result r = drive(p, v, m, 2, false);
                printf(" %6dm %-6s", r.metres, reasonName(r));
                sumAt[lvl ? 1 : 0] += r.metres;
                if (lvl == 0 && v == 0 && m == 0 && r.metres < 400) { printf("\nFAIL: the stock buggy only reaches %d m on the first map\n", r.metres); ++failures; }
                fflush(stdout);
            }
            printf("\n");
        }
    }
}

// A whole career with a simple shopping strategy. Prints the progress after every run.
static bool career() {
    Profile p;
    profileNew(p);
    int runsPerMap[NMAPS] = {};
    for (int n = 1; n <= 400; ++n) {
        const Result r = drive(p, p.veh, p.map, 1, false);
        runsPerMap[p.map]++;
        Run& rr = run;
        finishRun(rr, p);
        // shopping: next map if affordable and the current one is "done", else upgrades, else vehicles
        bool bought = true;
        while (bought) {
            bought = false;
            const int nextMap = p.map + 1;
            if (nextMap < NMAPS && !ownsMap(p, nextMap) && p.coins >= MAPS[nextMap].price && p.best[p.map] > 600) {
                buyMap(p, nextMap); p.map = (uint8_t)nextMap; bought = true;
                printf("  -> bought map %s\n", MAPS[nextMap].name);
                continue;
            }
            // cheapest upgrade
            int bu = -1; int32_t bc = 0x7FFFFFFF;
            for (int u = 0; u < U_COUNT; ++u) { const int32_t c = upgradeCost(p, p.veh, u); if (c && c < bc) { bc = c; bu = u; } }
            const int nextVeh = p.veh + 1;
            if (nextVeh < NVEH && !ownsVeh(p, nextVeh) && p.coins >= VEHS[nextVeh].price && (bu < 0 || bc * 3 > VEHS[nextVeh].price / 4)) {
                buyVehicle(p, nextVeh); p.veh = (uint8_t)nextVeh; bought = true;
                printf("  -> bought vehicle %s\n", VEHS[nextVeh].name);
                continue;
            }
            if (bu >= 0 && p.coins >= bc) { buyUpgrade(p, p.veh, bu); bought = true; }
        }
        if (n <= 10 || n % 10 == 0)
            printf("run %3d  %-11s %-14s %5d m %-6s +%5d coins  bank %7d  levels %d%d%d%d%d\n", n, VEHS[p.veh].name, MAPS[p.map].name, r.metres,
                   reasonName(r), r.coins, p.coins, p.level[p.veh][0], p.level[p.veh][1], p.level[p.veh][2], p.level[p.veh][3], p.level[p.veh][4]);
        if (p.mapOwned == (1 << NMAPS) - 1 && p.vehOwned == (1 << NVEH) - 1) { printf("everything unlocked after %d runs\n", n); return true; }
    }
    printf("FAIL: not everything unlocked after 400 runs\n");
    return false;
}

int main(int argc, char** argv) {
    int veh = -1, map = -1, lvl = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--career")) { const bool ok = career(); return failures || !ok ? 1 : 0; }
        if (!strcmp(argv[i], "--veh") && i + 1 < argc) veh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--map") && i + 1 < argc) map = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) lvl = atoi(argv[++i]);
    }
    if (veh >= 0 || map >= 0) {
        Profile p;
        profileNew(p);
        for (int v = 0; v < NVEH; ++v) for (int u = 0; u < U_COUNT; ++u) p.level[v][u] = (uint8_t)lvl;
        const Result r = drive(p, veh < 0 ? 0 : veh, map < 0 ? 0 : map, 2, true);
        printf("%d m, %d coins, %s, %d s\n", r.metres, r.coins, reasonName(r), r.secs);
        return failures ? 1 : 0;
    }
    table();
    printf("\naverage distance: stock %d m, fully upgraded %d m\n", sumAt[0] / (NVEH * NMAPS), sumAt[1] / (NVEH * NMAPS));
    if (sumAt[1] < sumAt[0] * 3 / 2) { printf("FAIL: upgrades do not help enough\n"); ++failures; }
    if (failures) { printf("\n%d physics failures\n", failures); return 1; }
    return 0;
}
