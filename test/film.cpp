// Film tool: a bot drives every map with every vehicle (or the given ones) and the real renderer
// draws frames into a fake VRAM, written as landscape PPMs (out/film_*.ppm, see ppm2png.py).
//   film [veh] [map] [seconds] [every]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "render.h"
#include "bot.h"

static uint16_t g_vram[320 * 528];
static uint16_t* host_vram() { return g_vram; }
uint16_t* (*_FP_LCD_GetVRAMAddress)() __asm__("LCD_GetVRAMAddress") = host_vram;

static void shot(const char* name) {
    char path[256];
    snprintf(path, sizeof path, "out/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n528 320\n255\n");
    for (int ly = 0; ly < 320; ++ly)
        for (int lx = 0; lx < 528; ++lx) {
            const uint16_t c = g_vram[lx * 320 + (319 - ly)];
            const uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static hc::Run run;

int main(int argc, char** argv) {
    gfx::init();
    const int v0 = argc > 1 ? atoi(argv[1]) : -1, m0 = argc > 2 ? atoi(argv[2]) : -1;
    const int secs = argc > 3 ? atoi(argv[3]) : 24, every = argc > 4 ? atoi(argv[4]) : 8;
    static hc::Profile prof;
    hc::profileNew(prof);
    for (int map = 0; map < hc::NMAPS; ++map) {
        if (m0 >= 0 && map != m0) continue;
        const int veh = v0 >= 0 ? v0 : map % hc::NVEH;
        hc::runStart(run, prof, veh, map, render::mapSeed(map));
        render::Camera cam;
        render::cameraReset(cam, run);
        int frame = 0;
        for (int t = 0; t < secs * hc::TICK_HZ; t += 4) {
            hc::Input in = bot::drive(run);
            for (int k = 0; k < 4; ++k) hc::step(run, in);
            render::cameraFollow(cam, run);
            if ((t / 4) % (every * 30) == 0 || (run.over && run.overT < 5)) {
                render::drawWorld(run, cam);
                render::drawHud(run, in.gas, in.brake, -1);
                char name[64];
                snprintf(name, sizeof name, "film_m%d_v%d_%02d", map, veh, frame++);
                shot(name);
            }
            if (run.over && run.overT > hc::sec(1)) break;
        }
        printf("map %d veh %d: %d m, %d coins, end %d\n", map, veh, run.bestX, hc::coinsOf(run), run.over);
    }
    return 0;
}
