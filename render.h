// Game state -> pixels (landscape 528 x 320, see gfx.h). Only render.cpp / gfx.cpp touch VRAM.
#pragma once

#include <stdint.h>
#include "game.h"

namespace render {

struct Camera {
    hc::fx x, y;              // world position of the top-left screen corner (Q12)
    int32_t zoom;             // world px per screen px, Q8 (256 = 1:1)
};

uint32_t mapSeed(int map);    // the fixed terrain seed of every map

// Builds the colour templates of a map (sky, ground, far hills). Cheap; call when the map changes.
void setMap(int map);

void cameraReset(Camera& c, const hc::Run& r);
void cameraFollow(Camera& c, const hc::Run& r);          // once per frame

// The whole game screen except the HUD: sky, parallax hills, terrain, decor, items, car.
void drawWorld(hc::Run& r, const Camera& c);
// Scenery for menus: map `map` around metre `atM` (shown at screen x carSx), optionally with a
// parked vehicle (veh >= 0). zoom: world px per screen px, Q8 (128 = everything twice as big).
void drawScenery(int map, int atM, int veh, int zoom = 256, int carSx = 264);
// HUD: fuel, distance, coins, pedals, pause button, popups. fps < 0 hides the counter.
void drawHud(const hc::Run& r, bool gas, bool brake, int fps);

// Vehicle picture for menus (no physics): centre (x, y) in screen px, scale in Q8 (256 = 1:1).
void drawVehiclePreview(int veh, int x, int y, int scale);
// Small terrain silhouette of a map inside a box (map cards).
void drawMapPreview(int map, int x, int y, int w, int h);

// --- widgets ----------------------------------------------------------------------------------
void drawPanel(int x, int y, int w, int h, uint16_t accent);
void drawButton(int x, int y, int w, int h, const char* label, int scale, uint16_t color);
void drawBar(int x, int y, int w, int h, int value, int max, uint16_t color);
void drawCoin(int cx, int cy, int r);
void drawPips(int x, int y, int level, int max, uint16_t on);
void dim();                                                 // darken the whole screen (overlays)

// HUD geometry (also used for touch hit tests in main.cpp)
constexpr int PAUSE_X = 494, PAUSE_Y = 4, PAUSE_W = 30, PAUSE_H = 26;
constexpr int PEDAL_W = 66, PEDAL_H = 48, PEDAL_Y = 320 - PEDAL_H - 6;

}  // namespace render
