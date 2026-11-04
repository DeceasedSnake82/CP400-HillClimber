// ============================================================================
//  Hill Climber - a physics hill-climbing driving game for the CP400 (Hollyhock 3)
//
//  Hold the calculator SIDEWAYS (keypad on the right). Drive as far as you can
//  over procedurally generated hills before you run out of fuel or land on your
//  head. Collect coins, do flips, buy vehicles, upgrades and new maps.
//
//  game.cpp    rules: terrain, Verlet physics, pickups, progression (no SDK)
//  render.cpp  game state -> pixels      gfx.cpp  rotated framebuffer + font
//  save.cpp    settings / progress file  main.cpp menus, input, timing, loop
// ============================================================================

#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <stdint.h>

#include "game.h"
#include "gfx.h"
#include "render.h"
#include "save.h"

APP_NAME("Hill Climber")
APP_DESCRIPTION("Physics hill-climbing: 6 vehicles, 7 maps, upgrades. Hold the calculator sideways.")
APP_AUTHOR("DeceasedSnake82")
APP_VERSION("1.0.0")

using gfx::rgb;
using namespace hc;

#ifdef HC_HOST
extern uint8_t host_rtc();
#endif

namespace {

// ---------------------------------------------------------------------------
// Timing: the SDK has no timer. FRAME mode advances `pace` ticks (1/120 s) per frame;
// RTC mode (opt-in) reads the SH7305 128 Hz counter R64CNT (see cp400.md).
// ---------------------------------------------------------------------------
uint8_t rtcRead() {
#ifdef HC_HOST
    return host_rtc() & 0x7F;
#else
    volatile uint8_t* const r64 = (volatile uint8_t*)0xA413FEC0;
    uint8_t a, b;
    do { a = *r64; b = *r64; } while (a != b);
    return a & 0x7F;
#endif
}

constexpr int MAX_TICKS = 12;              // per frame (below 10 fps the game slows down)

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------
constexpr uint16_t C_WHITE = rgb(255, 255, 255), C_BLACK = rgb(0, 0, 0);
constexpr uint16_t C_GOLD = rgb(255, 210, 70), C_DIM = rgb(175, 185, 205), C_RED = rgb(230, 70, 60);
constexpr uint16_t C_GREEN = rgb(45, 165, 70), C_BLUE = rgb(45, 115, 200), C_PURPLE = rgb(115, 80, 175);
constexpr uint16_t C_BROWN = rgb(155, 105, 45), C_GREY = rgb(85, 90, 108), C_DARKRED = rgb(155, 50, 50);
constexpr uint16_t C_BG = rgb(18, 22, 34), C_BG2 = rgb(30, 36, 56);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum Screen { SC_TITLE, SC_GARAGE, SC_MAPS, SC_RUN, SC_RESULT, SC_SETTINGS, SC_HELP };

save::Data data;
bool saveOk = true;
Screen screen = SC_TITLE;
bool quit;
char msg[48];
int msgTimer;

Run run;
render::Camera cam;
bool paused;
int garageSel;                             // vehicle shown in the garage
int mapSel;                                // map highlighted on the map screen
bool resetArmed;                           // settings: "reset progress" pressed once
int runRecordBefore;

// input (run)
bool touchDown;
int touchX, touchY;
bool tapGas, tapBrake;                     // key presses latched until the next tick batch

// timing
uint8_t rtcLast;
uint32_t rtcAcc;
int rtcIdle;
int fps = -1, fpsFrames, fpsTicks;

Profile& P() { return data.prof; }

void setMsg(const char* s) { gfx::putStr(msg, s); msgTimer = 1; }
void persist() { saveOk = save::store(data); }

inline bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
bool keyDown(uint16_t sc) { return Input_GetKeyStateU(&sc); }

void centered(int y, const char* s, int scale, uint16_t c) {
    gfx::textShadow((gfx::W - gfx::textWidth(s, scale)) / 2, y, s, scale, c, C_BLACK);
}

char* putCoins(char* d, int32_t v) {
    // 1234567 -> "1,234,567"
    char tmp[16];
    char* e = gfx::putInt(tmp, v);
    const int n = (int)(e - tmp);
    for (int i = 0; i < n; ++i) {
        *d++ = tmp[i];
        const int left = n - 1 - i;
        if (left > 0 && left % 3 == 0 && tmp[i] != '-') *d++ = ',';
    }
    *d = 0;
    return d;
}

// Word-wrapped text, returns the y below the last line.
int wrapText(int x, int y, int w, const char* s, uint16_t c) {
    const int maxChars = (w + 1) / 6;
    char line[96];
    while (*s) {
        int n = 0, cut = -1;
        while (s[n] && n < maxChars) { if (s[n] == ' ') cut = n; ++n; }
        if (s[n] && cut > 0) n = cut;
        for (int i = 0; i < n && i < 95; ++i) line[i] = s[i];
        line[n < 95 ? n : 95] = 0;
        gfx::text(x, y, line, 1, c);
        y += 11;
        s += n;
        while (*s == ' ') ++s;
    }
    return y;
}

void drawBg() {
    for (int x = 0; x < gfx::W; ++x) gfx::vspan(x, 0, gfx::H - 1, C_BG);
    gfx::fillRect(0, 0, gfx::W, 34, C_BG2);
}

void drawHeader(const char* title) {
    drawBg();
    gfx::textShadow(12, 9, title, 2, C_WHITE, C_BLACK);
    char b[24];
    putCoins(b, P().coins);
    const int w = gfx::textWidth(b, 2);
    render::drawCoin(gfx::W - 24 - w, 16, 8);
    gfx::textShadow(gfx::W - 10 - w, 10, b, 2, C_GOLD, C_BLACK);
}

void drawFooterMsg() {
    if (msgTimer > 0 || !saveOk) {
        const char* m = msgTimer > 0 ? msg : "PROGRESS NOT SAVED (FILE ERROR)";
        gfx::fillRect(0, gfx::H - 14, gfx::W, 14, C_BLACK);
        gfx::textCentered(gfx::W / 2, gfx::H - 11, m, 1, C_GOLD);
    }
}

// ---------------------------------------------------------------------------
// Runs
// ---------------------------------------------------------------------------
void startRun() {
    Profile& p = P();
    runStart(run, p, p.veh, p.map, render::mapSeed(p.map));
    render::cameraReset(cam, run);
    runRecordBefore = p.best[p.map];
    paused = false;
    touchDown = false;
    tapGas = tapBrake = false;
    screen = SC_RUN;
    if (data.set.timer) rtcLast = rtcRead();
}

void endRun(uint8_t reason) {
    if (!run.over) { run.over = reason; run.overT = 0; }
    finishRun(run, P());
    persist();
    paused = false;
    screen = SC_RESULT;
}

int ticksThisFrame() {
    if (!data.set.timer) return data.set.pace;
    const uint8_t now = rtcRead();
    const uint8_t d = (uint8_t)((now - rtcLast) & 0x7F);
    rtcLast = now;
    fpsTicks += d;
    ++fpsFrames;
    if (fpsTicks >= 128) { fps = fpsFrames * 128 / fpsTicks; fpsFrames = 0; fpsTicks = 0; }
    rtcAcc += (uint32_t)d * TICK_HZ;            // 128 Hz -> 120 Hz ticks
    int n = (int)(rtcAcc >> 7);
    rtcAcc &= 127;
    if (n == 0) {
        if (++rtcIdle > 200) { data.set.timer = 0; setMsg("RTC STOPPED - FRAME TIMER"); }
    } else rtcIdle = 0;
    return n > MAX_TICKS ? MAX_TICKS : n;
}

// Gas: arrow DOWN (= screen right), EXE, 1 2 3, + ; brake: arrow UP (= screen left), 7 8 9, -.
// Touch: hold the right half for gas, the left half to brake.
bool gasHeld() {
    return keyDown(ScancodeDown) || keyDown(ScancodeEXE) || keyDown(Scancode1) || keyDown(Scancode2) || keyDown(Scancode3) ||
           keyDown(ScancodePlus) || (touchDown && touchX >= gfx::W / 2);
}
bool brakeHeld() {
    return keyDown(ScancodeUp) || keyDown(Scancode7) || keyDown(Scancode8) || keyDown(Scancode9) || keyDown(ScancodeMinus) ||
           (touchDown && touchX < gfx::W / 2);
}

bool lastGas, lastBrake;

void runFrame() {
    Input in;
    in.gas = gasHeld() || tapGas;
    in.brake = brakeHeld() || tapBrake;
    if (in.gas && in.brake) in.brake = false;
    tapGas = tapBrake = false;
    lastGas = in.gas;
    lastBrake = in.brake;
    const int n = ticksThisFrame();
    for (int i = 0; i < n; ++i) {
        step(run, in);
        if (run.over && run.overT >= sec(1.3)) { endRun(run.over); return; }
    }
    if (data.set.autoZoom) render::cameraFollow(cam, run);
    else { const int32_t z = cam.zoom; render::cameraFollow(cam, run); cam.zoom = z; }
}

// ---------------------------------------------------------------------------
// Drawing: title
// ---------------------------------------------------------------------------
constexpr int BTN_H = 42;
constexpr int T_DRIVE_X = 300, T_DRIVE_Y = 176, T_DRIVE_W = 210, T_DRIVE_H = 56;
constexpr int T_ROW_Y = 252, T_ROW_W = 94, T_ROW_GAP = 8, T_ROW_X0 = 13;

void drawTitle() {
    render::drawScenery(P().map, 64, P().veh, 140, 150);
    centered(12, "HILL", 4, C_GOLD);
    centered(44, "CLIMBER", 6, C_WHITE);
    char b[64];
    char* p = gfx::putStr(b, VEHS[P().veh].name);
    p = gfx::putStr(p, "  -  ");
    p = gfx::putStr(p, MAPS[P().map].name);
    if (P().best[P().map] > 0) { p = gfx::putStr(p, "  -  BEST "); p = gfx::putInt(p, P().best[P().map]); gfx::putStr(p, " M"); }
    gfx::fillRect(0, 96, gfx::W, 14, rgb(0, 0, 0));
    gfx::textCentered(gfx::W / 2, 99, b, 1, C_WHITE);
    putCoins(b, P().coins);
    const int w = gfx::textWidth(b, 2);
    gfx::fillRect(gfx::W - w - 36, 4, w + 32, 22, rgb(0, 0, 0));
    render::drawCoin(gfx::W - 22 - w, 15, 7);
    gfx::text(gfx::W - 10 - w, 9, b, 2, C_GOLD);
    render::drawButton(T_DRIVE_X, T_DRIVE_Y, T_DRIVE_W, T_DRIVE_H, "DRIVE!", 3, C_GREEN);
    static const char* const L[5] = { "GARAGE", "MAPS", "SETUP", "HELP", "QUIT" };
    const uint16_t C[5] = { C_BLUE, C_BROWN, C_PURPLE, C_GREY, C_DARKRED };
    for (int i = 0; i < 5; ++i) render::drawButton(T_ROW_X0 + i * (T_ROW_W + T_ROW_GAP), T_ROW_Y, T_ROW_W, BTN_H, L[i], 2, C[i]);
    gfx::fillRect(0, gfx::H - 14, gfx::W, 14, rgb(0, 0, 0));
    gfx::textCentered(gfx::W / 2, gfx::H - 11, "EXE DRIVE   1 GARAGE   2 MAPS   = SETUP   . HELP   CLEAR QUIT", 1, C_DIM);
    drawFooterMsg();
}

// ---------------------------------------------------------------------------
// Garage
// ---------------------------------------------------------------------------
constexpr int G_UP_X = 272, G_UP_Y = 42, G_UP_W = 246, G_UP_ROW = 44;
constexpr int G_TAB_Y = 270, G_TAB_W = 84, G_TAB_H = 36;
constexpr int G_BUY_X = 430, G_BUY_W = 82;

const char* const UP_NAMES[U_COUNT] = { "ENGINE", "TIRES", "SUSPENSION", "FUEL TANK", "AIR CONTROL" };

// 0..10 rating of a vehicle stat with its upgrades (for the bars)
int statOf(int v, int which) {
    const VehDef& d = VEHS[v];
    const uint8_t* lv = P().level[v];
    switch (which) {
        case 0: return (d.engine * (100 + 14 * lv[U_ENGINE])) / 100 / 7;          // power
        case 1: return (d.topSpeed / 100 * (100 + 6 * lv[U_ENGINE])) / 100 / 18;  // speed
        case 2: return (d.grip * (2 - ((d.drive & 2) ? 0 : 1)) * (100 + 11 * lv[U_TIRES])) / 100 / 12;  // grip
        case 3: return (d.fuel * (100 + 15 * lv[U_FUEL])) / 100 / 10;             // fuel
        default: return (d.air * (100 + 13 * lv[U_AIR])) / 100 / 3;               // air control
    }
}

void drawGarage() {
    drawHeader("GARAGE");
    const int v = garageSel;
    const VehDef& d = VEHS[v];
    const bool owned = ownsVeh(P(), v);
    // vehicle card
    render::drawPanel(8, 40, 256, 226, owned ? d.c1 : C_GREY);
    gfx::fillRect(10, 66, 252, 82, gfx::mix(MAPS[P().map].skyTop, MAPS[P().map].skyBot, 140));
    gfx::fillRect(10, 140, 252, 8, MAPS[P().map].surface);
    {
        // as big as fits between the name (y 70) and the ground line (y 141)
        const int below = VEHS[v].wheelY0 + VEHS[v].r0, tall = below - VEHS[v].headY + 8;
        int sc = (71 * 256) / tall;
        if (sc > 384) sc = 384;
        render::drawVehiclePreview(v, 136, 141 - (below * sc) / 256, sc);
    }
    gfx::textShadow(18, 47, d.name, 2, C_WHITE, C_BLACK);
    if (P().veh == v) gfx::text(204, 51, "IN USE", 1, C_GOLD);
    wrapText(18, 153, 240, d.blurb, C_DIM);
    static const char* const ST[5] = { "POWER", "SPEED", "GRIP", "FUEL", "AIR" };
    for (int i = 0; i < 5; ++i) {
        gfx::text(18, 180 + i * 16, ST[i], 1, C_DIM);
        render::drawBar(72, 179 + i * 16, 182, 9, statOf(v, i), 20, i == 3 ? rgb(240, 200, 40) : rgb(90, 200, 110));
    }
    // upgrades or the price
    render::drawPanel(G_UP_X - 4, 40, G_UP_W + 8, 226, C_GREY);
    char b[32];
    if (owned) {
        for (int u = 0; u < U_COUNT; ++u) {
            const int y = G_UP_Y + 2 + u * G_UP_ROW;
            b[0] = (char)('1' + u); b[1] = 0;
            gfx::text(G_UP_X + 4, y + 4, b, 1, C_GOLD);
            gfx::text(G_UP_X + 16, y + 4, UP_NAMES[u], 1, C_WHITE);
            render::drawPips(G_UP_X + 16, y + 18, P().level[v][u], MAX_LEVEL, rgb(90, 200, 110));
            const int32_t c = upgradeCost(P(), v, u);
            if (c == 0) render::drawButton(G_BUY_X, y + 4, G_BUY_W, 34, "MAX", 2, C_GREY);
            else {
                putCoins(b, c);
                render::drawButton(G_BUY_X, y + 4, G_BUY_W, 34, b, b[5] ? 1 : 2, P().coins >= c ? C_GREEN : C_GREY);
            }
        }
    } else {
        gfx::textCentered(G_UP_X + G_UP_W / 2, 80, "LOCKED", 3, C_RED);
        putCoins(b, d.price);
        render::drawCoin(G_UP_X + G_UP_W / 2 - gfx::textWidth(b, 3) / 2 - 14, 135, 9);
        gfx::textCentered(G_UP_X + G_UP_W / 2 + 8, 125, b, 3, C_GOLD);
        render::drawButton(G_UP_X + 33, 178, 180, 50, "BUY (EXE)", 2, P().coins >= d.price ? C_GREEN : C_GREY);
    }
    // vehicle tabs
    for (int i = 0; i < NVEH; ++i) {
        const int x = 10 + i * (G_TAB_W + 2);
        const bool sel = i == v;
        gfx::fillRect(x, G_TAB_Y, G_TAB_W, G_TAB_H, sel ? C_BG2 : rgb(26, 30, 46));
        gfx::frameRect(x, G_TAB_Y, G_TAB_W, G_TAB_H, sel ? C_GOLD : C_GREY, sel ? 2 : 1);
        gfx::textCentered(x + G_TAB_W / 2, G_TAB_Y + 7, VEHS[i].name, 1, ownsVeh(P(), i) ? C_WHITE : C_DIM);
        if (ownsVeh(P(), i)) gfx::textCentered(x + G_TAB_W / 2, G_TAB_Y + 21, P().veh == i ? "IN USE" : "OWNED", 1, P().veh == i ? C_GOLD : rgb(90, 200, 110));
        else { putCoins(b, VEHS[i].price); gfx::textCentered(x + G_TAB_W / 2, G_TAB_Y + 21, b, 1, C_GOLD); }
    }
    gfx::text(12, gfx::H - 10, "ARROWS PICK   1-5 UPGRADE   EXE DRIVE / BUY   CLEAR BACK", 1, C_DIM);
    drawFooterMsg();
}

// ---------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------
constexpr int M_CW = 122, M_CH = 112, M_GAP = 6, M_Y0 = 40;

void mapCardPos(int i, int& x, int& y) {
    if (i < 4) { x = 11 + i * (M_CW + M_GAP); y = M_Y0; }
    else { x = 11 + (M_CW + M_GAP) / 2 + (i - 4) * (M_CW + M_GAP); y = M_Y0 + M_CH + M_GAP; }
}

void drawMaps() {
    drawHeader("MAPS");
    char b[40];
    for (int i = 0; i < NMAPS; ++i) {
        int x, y;
        mapCardPos(i, x, y);
        const bool sel = i == mapSel, owned = ownsMap(P(), i);
        gfx::fillRect(x, y, M_CW, M_CH, rgb(26, 30, 46));
        render::drawMapPreview(i, x + 4, y + 4, M_CW - 8, 62);
        if (!owned) gfx::blendRect(x + 4, y + 4, M_CW - 8, 62, C_BLACK, 140);
        gfx::frameRect(x, y, M_CW, M_CH, sel ? C_GOLD : C_GREY, sel ? 3 : 1);
        gfx::textCentered(x + M_CW / 2, y + 72, MAPS[i].name, 1, C_WHITE);
        if (owned) {
            char* p = gfx::putStr(b, "BEST ");
            p = gfx::putInt(p, P().best[i]);
            gfx::putStr(p, " M");
            gfx::textCentered(x + M_CW / 2, y + 88, b, 1, P().best[i] > 0 ? C_GOLD : C_DIM);
            if (P().map == i) gfx::textCentered(x + M_CW / 2, y + 100, "SELECTED", 1, rgb(90, 200, 110));
        } else {
            putCoins(b, MAPS[i].price);
            gfx::textCentered(x + M_CW / 2, y + 88, b, 2, P().coins >= MAPS[i].price ? C_GOLD : C_DIM);
            gfx::textCentered(x + M_CW / 2, y + 30, "LOCKED", 2, C_WHITE);
        }
    }
    const MapDef& m = MAPS[mapSel];
    gfx::text(14, 274, m.name, 2, C_WHITE);
    wrapText(14, 292, 370, m.blurb, C_DIM);
    const bool owned = ownsMap(P(), mapSel);
    if (owned) render::drawButton(400, 270, 118, 40, "DRIVE", 2, C_GREEN);
    else render::drawButton(400, 270, 118, 40, "BUY", 2, P().coins >= m.price ? C_GREEN : C_GREY);
    gfx::text(14, gfx::H - 10, "ARROWS PICK   EXE DRIVE / BUY   CLEAR BACK", 1, C_DIM);
    drawFooterMsg();
}

// ---------------------------------------------------------------------------
// Run, pause, results
// ---------------------------------------------------------------------------
constexpr int PZ_Y = 150, PZ_W = 140, PZ_H = 50;
constexpr int PZ_X0 = 44, PZ_X1 = 194, PZ_X2 = 344;

void drawRun() {
    render::drawWorld(run, cam);
    render::drawHud(run, lastGas, lastBrake, data.set.showFps ? fps : -1);
    if (run.over == END_CRASH && run.overT > 0) centered(96, "CRASH!", 4, C_RED);
    else if (run.over == END_FUEL) centered(96, "OUT OF FUEL", 4, C_GOLD);
    if (paused) {
        render::dim();
        centered(80, "PAUSED", 4, C_WHITE);
        render::drawButton(PZ_X0, PZ_Y, PZ_W, PZ_H, "RESUME", 2, C_GREEN);
        render::drawButton(PZ_X1, PZ_Y, PZ_W, PZ_H, "RESTART", 2, C_BLUE);
        render::drawButton(PZ_X2, PZ_Y, PZ_W, PZ_H, "END RUN", 2, C_DARKRED);
        gfx::textCentered(gfx::W / 2, 220, "EXE RESUME   1 RESTART   CLEAR END RUN (COINS ARE KEPT)", 1, C_DIM);
    }
}

constexpr int R_BTN_Y = 266, R_BTN_W = 118, R_BTN_GAP = 8, R_BTN_X0 = 23;

void drawResult() {
    render::drawWorld(run, cam);
    render::dim();
    render::dim();
    const Run& r = run;
    const char* title = "RUN OVER";
    uint16_t tc = C_WHITE;
    switch (r.over) {
        case END_CRASH: title = "DRIVER DOWN!"; tc = C_RED; break;
        case END_FUEL: title = "OUT OF FUEL"; tc = C_GOLD; break;
        case END_FINISH: title = "END OF THE ROAD!"; tc = rgb(120, 230, 255); break;
        default: break;
    }
    centered(12, title, 3, tc);
    char b[48];
    char* p = gfx::putInt(b, metres(r));
    gfx::putStr(p, " M");
    centered(46, b, 5, C_WHITE);
    if (metres(r) > runRecordBefore && runRecordBefore > 0) centered(88, "NEW RECORD!", 2, rgb(120, 230, 255));
    else if (runRecordBefore > 0) {
        p = gfx::putStr(b, "BEST ");
        p = gfx::putInt(p, P().best[r.map]);
        gfx::putStr(p, " M");
        centered(90, b, 1, C_DIM);
    }
    render::drawPanel(124, 108, 280, 148, C_GOLD);
    static const char* const ROW[3] = { "PICKUPS", "STUNTS", "DISTANCE" };
    const int32_t val[3] = { r.coinsPickup, r.coinsStunt, r.coinsDist };
    for (int i = 0; i < 3; ++i) {
        gfx::text(140, 120 + i * 22, ROW[i], 2, C_DIM);
        putCoins(b, val[i]);
        gfx::text(388 - gfx::textWidth(b, 2), 120 + i * 22, b, 2, C_WHITE);
    }
    gfx::fillRect(140, 186, 248, 2, C_GREY);
    gfx::text(140, 194, "TOTAL", 2, C_GOLD);
    putCoins(b, coinsOf(r));
    gfx::text(388 - gfx::textWidth(b, 2), 194, b, 2, C_GOLD);
    p = gfx::putStr(b, "BANK ");
    putCoins(p, P().coins);
    gfx::text(140, 220, b, 1, C_DIM);
    p = gfx::putStr(b, "FLIPS ");
    p = gfx::putInt(p, r.flips);
    p = gfx::putStr(p, "   BEST AIR ");
    p = gfx::putInt(p, r.airBest / TICK_HZ);
    *p++ = '.';
    p = gfx::putInt(p, (r.airBest % TICK_HZ) * 10 / TICK_HZ);
    gfx::putStr(p, "S");
    gfx::text(140, 236, b, 1, C_DIM);
    static const char* const L[4] = { "RETRY", "GARAGE", "MAPS", "MENU" };
    const uint16_t C[4] = { C_GREEN, C_BLUE, C_BROWN, C_GREY };
    for (int i = 0; i < 4; ++i) render::drawButton(R_BTN_X0 + i * (R_BTN_W + R_BTN_GAP), R_BTN_Y, R_BTN_W, BTN_H, L[i], 2, C[i]);
    gfx::textCentered(gfx::W / 2, gfx::H - 10, "EXE RETRY   1 GARAGE   2 MAPS   CLEAR MENU", 1, C_DIM);
    drawFooterMsg();
}

// ---------------------------------------------------------------------------
// Settings / help
// ---------------------------------------------------------------------------
constexpr int S_X = 330, S_ROW = 50, S_Y0 = 44;

void drawSettings() {
    drawHeader("SETUP");
    char b[48];
    gfx::text(24, S_Y0 + 12, "TICKS PER FRAME", 2, C_WHITE);
    gfx::text(24, S_Y0 + 32, "FRAME TIMER: RAISE IF THE GAME FEELS SLOW", 1, C_DIM);
    render::drawButton(S_X, S_Y0, 50, 40, "-", 3, C_GREY);
    gfx::putInt(b, data.set.pace);
    gfx::textCentered(S_X + 80, S_Y0 + 10, b, 3, C_GOLD);
    render::drawButton(S_X + 110, S_Y0, 50, 40, "+", 3, C_GREY);
    static const char* const LB[3] = { "TIMER", "SHOW FPS", "AUTO ZOOM" };
    static const char* const HL[3] = { "RTC = REAL SPEED (EXPERIMENTAL)", "ONLY WITH THE RTC TIMER", "ZOOM OUT AT SPEED" };
    const char* VAL[3] = { data.set.timer ? "RTC" : "FRAME", data.set.showFps ? "ON" : "OFF", data.set.autoZoom ? "ON" : "OFF" };
    for (int i = 0; i < 3; ++i) {
        const int y = S_Y0 + (i + 1) * S_ROW;
        b[0] = (char)('1' + i); b[1] = 0;
        gfx::text(8, y + 12, b, 2, C_GOLD);
        gfx::text(24, y + 12, LB[i], 2, C_WHITE);
        gfx::text(24, y + 32, HL[i], 1, C_DIM);
        render::drawButton(S_X, y, 160, 40, VAL[i], 2, C_PURPLE);
    }
    const int y = S_Y0 + 4 * S_ROW;
    gfx::text(8, y + 12, "4", 2, C_GOLD);
    gfx::text(24, y + 12, "RESET PROGRESS", 2, C_WHITE);
    gfx::text(24, y + 32, resetArmed ? "PRESS AGAIN TO ERASE EVERYTHING!" : "COINS, VEHICLES, UPGRADES, RECORDS", 1, resetArmed ? C_RED : C_DIM);
    render::drawButton(S_X, y, 160, 40, resetArmed ? "REALLY?" : "RESET", 2, C_DARKRED);
    gfx::text(12, gfx::H - 10, "+/- TICKS   1-4 TOGGLE   EXE/CLEAR BACK", 1, C_DIM);
    drawFooterMsg();
}

void drawHelp() {
    drawHeader("HOW TO PLAY");
    static const char* const LINES[] = {
        "HOLD THE CALCULATOR SIDEWAYS, KEYPAD ON THE RIGHT.",
        "",
        "GAS:    ARROW DOWN (POINTS RIGHT), EXE, 1 2 3, +",
        "        OR HOLD THE RIGHT HALF OF THE SCREEN",
        "BRAKE:  ARROW UP (POINTS LEFT), 7 8 9, -",
        "        OR HOLD THE LEFT HALF.  REVERSES WHEN STOPPED.",
        "",
        "IN THE AIR: GAS TILTS THE NOSE UP, BRAKE TILTS IT DOWN.",
        "LAND ON YOUR WHEELS - NOT ON THE DRIVER'S HEAD!",
        "",
        "FUEL RUNS OUT: GRAB THE RED CANS. THEY GET RARER.",
        "COINS: PICKUPS, FLIPS, LONG AIR TIME, EVERY 500 M.",
        "SPEND THEM ON UPGRADES, VEHICLES AND MAPS.",
        "",
        "CLEAR / THE II BUTTON PAUSES.  SAVED AUTOMATICALLY.",
    };
    for (unsigned i = 0; i < sizeof LINES / sizeof LINES[0]; ++i) gfx::text(24, 46 + (int)i * 16, LINES[i], 1, i == 0 ? C_GOLD : C_WHITE);
    gfx::textCentered(gfx::W / 2, gfx::H - 12, "PRESS ANY KEY", 1, C_DIM);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void openGarage() { garageSel = P().veh; screen = SC_GARAGE; }
void openMaps() { mapSel = P().map; screen = SC_MAPS; }

void garageAction() {
    Profile& p = P();
    if (ownsVeh(p, garageSel)) {
        p.veh = (uint8_t)garageSel;
        persist();
        startRun();
        return;
    }
    if (buyVehicle(p, garageSel)) { p.veh = (uint8_t)garageSel; persist(); setMsg("NEW VEHICLE!"); }
    else setMsg("NOT ENOUGH COINS");
}

void garageUpgrade(int u) {
    Profile& p = P();
    if (!ownsVeh(p, garageSel)) return;
    if (buyUpgrade(p, garageSel, u)) { persist(); setMsg("UPGRADED!"); }
    else setMsg(upgradeCost(p, garageSel, u) == 0 ? "ALREADY AT THE MAXIMUM" : "NOT ENOUGH COINS");
}

void mapAction() {
    Profile& p = P();
    if (ownsMap(p, mapSel)) {
        p.map = (uint8_t)mapSel;
        persist();
        startRun();
        return;
    }
    if (buyMap(p, mapSel)) { p.map = (uint8_t)mapSel; persist(); setMsg("NEW MAP UNLOCKED!"); }
    else setMsg("NOT ENOUGH COINS");
}

void toggleTimer() {
    if (data.set.timer) { data.set.timer = 0; fps = -1; persist(); setMsg("TIMER: FRAME"); return; }
    const uint8_t a = rtcRead();
    bool moved = false;
    for (volatile uint32_t n = 0; n < 4000000u && !moved; n = n + 1) if (rtcRead() != a) moved = true;
    if (!moved) { setMsg("RTC NOT RUNNING - KEPT FRAME TIMER"); return; }
    data.set.timer = 1;          // saved only after the check passed
    persist();
    setMsg("TIMER: RTC");
}

void pace(int d) {
    int p = data.set.pace + d;
    data.set.pace = (uint8_t)(p < 1 ? 1 : p > 8 ? 8 : p);
    persist();
}

void settingsToggle(int i) {
    if (i != 3) resetArmed = false;
    switch (i) {
        case 0: toggleTimer(); break;
        case 1: data.set.showFps ^= 1; persist(); break;
        case 2: data.set.autoZoom ^= 1; persist(); break;
        case 3:
            if (!resetArmed) { resetArmed = true; break; }
            resetArmed = false;
            profileNew(data.prof);
            persist();
            setMsg("PROGRESS ERASED");
            break;
    }
}

void resultAction(int i) {
    switch (i) {
        case 0: startRun(); break;
        case 1: openGarage(); break;
        case 2: openMaps(); break;
        default: screen = SC_TITLE; break;
    }
}

// --- keys -------------------------------------------------------------------
bool isGasKey(int c) { return c == KEYCODE_DOWN || c == KEYCODE_EXE || c == KEYCODE_1 || c == KEYCODE_2 || c == KEYCODE_3 || c == KEYCODE_PLUS; }
bool isBrakeKey(int c) { return c == KEYCODE_UP || c == KEYCODE_7 || c == KEYCODE_8 || c == KEYCODE_9 || c == KEYCODE_MINUS; }

void runKey(int code) {
    if (paused) {
        if (code == KEYCODE_EXE) paused = false;
        else if (code == KEYCODE_1) startRun();
        else if (code == KEYCODE_POWER_CLEAR) endRun(END_QUIT);
        return;
    }
    if (code == KEYCODE_POWER_CLEAR) { if (!run.over) paused = true; return; }
    if (isGasKey(code)) tapGas = true;
    else if (isBrakeKey(code)) tapBrake = true;
}

void handleKey(int code) {
    if (code != KEYCODE_4 && screen == SC_SETTINGS) resetArmed = false;
    switch (screen) {
        case SC_TITLE:
            if (code == KEYCODE_POWER_CLEAR) quit = true;
            else if (code == KEYCODE_EXE) startRun();
            else if (code == KEYCODE_1) openGarage();
            else if (code == KEYCODE_2) openMaps();
            else if (code == KEYCODE_EQUALS) screen = SC_SETTINGS;
            else if (code == KEYCODE_DOT) screen = SC_HELP;
            break;
        case SC_GARAGE:
            if (code == KEYCODE_UP) garageSel = (garageSel + NVEH - 1) % NVEH;           // rotated: UP = left
            else if (code == KEYCODE_DOWN) garageSel = (garageSel + 1) % NVEH;
            else if (code >= KEYCODE_1 && code <= KEYCODE_5) garageUpgrade(code - KEYCODE_1);
            else if (code == KEYCODE_EXE) garageAction();
            else if (code == KEYCODE_POWER_CLEAR) {
                if (ownsVeh(P(), garageSel) && P().veh != garageSel) { P().veh = (uint8_t)garageSel; persist(); }
                screen = SC_TITLE;
            }
            break;
        case SC_MAPS:
            if (code == KEYCODE_UP) mapSel = (mapSel + NMAPS - 1) % NMAPS;
            else if (code == KEYCODE_DOWN) mapSel = (mapSel + 1) % NMAPS;
            else if (code == KEYCODE_RIGHT || code == KEYCODE_LEFT) mapSel = mapSel < 4 ? (mapSel + 4 > NMAPS - 1 ? NMAPS - 1 : mapSel + 4) : mapSel - 4;
            else if (code == KEYCODE_EXE) mapAction();
            else if (code == KEYCODE_POWER_CLEAR) {
                if (ownsMap(P(), mapSel) && P().map != mapSel) { P().map = (uint8_t)mapSel; persist(); }
                screen = SC_TITLE;
            }
            break;
        case SC_RUN: runKey(code); break;
        case SC_RESULT:
            if (code == KEYCODE_EXE) resultAction(0);
            else if (code == KEYCODE_1) resultAction(1);
            else if (code == KEYCODE_2) resultAction(2);
            else if (code == KEYCODE_POWER_CLEAR) resultAction(3);
            break;
        case SC_SETTINGS:
            if (code == KEYCODE_PLUS) pace(1);
            else if (code == KEYCODE_MINUS) pace(-1);
            else if (code >= KEYCODE_1 && code <= KEYCODE_4) settingsToggle(code - KEYCODE_1);
            else if (code == KEYCODE_EXE || code == KEYCODE_POWER_CLEAR) screen = SC_TITLE;
            break;
        case SC_HELP: screen = SC_TITLE; break;
    }
}

// --- touch ------------------------------------------------------------------
void runTouch(int dir, int x, int y) {
    if (dir == TOUCH_UP) { touchDown = false; return; }
    if (dir == TOUCH_HOLD_DRAG) { if (touchDown) { touchX = x; touchY = y; } return; }
    if (dir != TOUCH_DOWN) return;
    if (paused) {
        if (inRect(x, y, PZ_X0, PZ_Y, PZ_W, PZ_H)) paused = false;
        else if (inRect(x, y, PZ_X1, PZ_Y, PZ_W, PZ_H)) startRun();
        else if (inRect(x, y, PZ_X2, PZ_Y, PZ_W, PZ_H)) endRun(END_QUIT);
        return;
    }
    if (inRect(x, y, render::PAUSE_X - 6, 0, render::PAUSE_W + 12, render::PAUSE_H + 10)) {
        if (!run.over) paused = true;
        touchDown = false;
        return;
    }
    touchDown = true; touchX = x; touchY = y;
    if (x >= gfx::W / 2) tapGas = true; else tapBrake = true;
}

void handleTouch(int dir, int px, int py) {
    if ((unsigned)px >= (unsigned)gfx::PW || (unsigned)py >= (unsigned)gfx::PH) {
        if (dir == TOUCH_UP) touchDown = false;
        return;
    }
    const int x = py, y = gfx::PW - 1 - px;           // panel -> landscape
    if (screen == SC_RUN) { runTouch(dir, x, y); return; }
    if (dir != TOUCH_DOWN) return;
    if (screen == SC_SETTINGS && !inRect(x, y, S_X, S_Y0 + 4 * S_ROW, 160, 40)) resetArmed = false;
    switch (screen) {
        case SC_TITLE:
            if (inRect(x, y, T_DRIVE_X, T_DRIVE_Y, T_DRIVE_W, T_DRIVE_H)) { startRun(); break; }
            for (int i = 0; i < 5; ++i) {
                if (!inRect(x, y, T_ROW_X0 + i * (T_ROW_W + T_ROW_GAP), T_ROW_Y, T_ROW_W, BTN_H)) continue;
                if (i == 0) openGarage();
                else if (i == 1) openMaps();
                else if (i == 2) screen = SC_SETTINGS;
                else if (i == 3) screen = SC_HELP;
                else quit = true;
            }
            break;
        case SC_GARAGE:
            for (int i = 0; i < NVEH; ++i)
                if (inRect(x, y, 10 + i * (G_TAB_W + 2), G_TAB_Y, G_TAB_W, G_TAB_H)) { if (garageSel == i && ownsVeh(P(), i)) garageAction(); else garageSel = i; return; }
            if (ownsVeh(P(), garageSel)) {
                for (int u = 0; u < U_COUNT; ++u)
                    if (inRect(x, y, G_BUY_X, G_UP_Y + 6 + u * G_UP_ROW, G_BUY_W, 34)) garageUpgrade(u);
            } else if (inRect(x, y, G_UP_X + 33, 178, 180, 50)) garageAction();
            if (inRect(x, y, 0, 0, 160, 34)) screen = SC_TITLE;          // tap the title to go back
            break;
        case SC_MAPS:
            for (int i = 0; i < NMAPS; ++i) {
                int cx, cy;
                mapCardPos(i, cx, cy);
                if (inRect(x, y, cx, cy, M_CW, M_CH)) { if (mapSel == i) mapAction(); else mapSel = i; return; }
            }
            if (inRect(x, y, 400, 270, 118, 40)) mapAction();
            if (inRect(x, y, 0, 0, 160, 34)) screen = SC_TITLE;
            break;
        case SC_RESULT:
            for (int i = 0; i < 4; ++i)
                if (inRect(x, y, R_BTN_X0 + i * (R_BTN_W + R_BTN_GAP), R_BTN_Y, R_BTN_W, BTN_H)) resultAction(i);
            break;
        case SC_SETTINGS:
            if (inRect(x, y, S_X, S_Y0, 50, 40)) pace(-1);
            else if (inRect(x, y, S_X + 110, S_Y0, 50, 40)) pace(1);
            else for (int i = 0; i < 4; ++i) if (inRect(x, y, S_X, S_Y0 + (i + 1) * S_ROW, 160, 40)) settingsToggle(i);
            if (inRect(x, y, 0, 0, 160, 34)) screen = SC_TITLE;
            break;
        case SC_HELP: screen = SC_TITLE; break;
        default: break;
    }
}

// Returns true if at least one event that can change a static screen (a key press or a touch
// down) was handled; key releases, drags and timer events only update input state.
bool pollEvents(bool wait) {
    bool any = false;
    for (int n = 0; n < 32; ++n) {
        struct Input_Event ev;
        uint8_t* raw = (uint8_t*)&ev;
        for (unsigned i = 0; i < sizeof ev; ++i) raw[i] = 0;
        GetInput(&ev, (n == 0 && wait) ? 0xFFFFFFFF : 0, 0x10);
        if (ev.type == EVENT_KEY) {
            if (ev.data.key.direction == KEY_PRESSED) { handleKey(ev.data.key.keyCode); any = true; }
        } else if (ev.type == EVENT_TOUCH) {
            handleTouch(ev.data.touch_single.direction, ev.data.touch_single.p1_x, ev.data.touch_single.p1_y);
            if (ev.data.touch_single.direction == TOUCH_DOWN) any = true;
        } else if (ev.type == EVENT_NONE) {
            break;
        } else if (wait && n == 0) {
            continue;        // timer / action-bar event: keep waiting for real input
        }
        if (quit) break;
    }
    return any;
}

bool animating() { return screen == SC_RUN && !paused; }

void draw() {
    switch (screen) {
        case SC_TITLE: drawTitle(); break;
        case SC_GARAGE: drawGarage(); break;
        case SC_MAPS: drawMaps(); break;
        case SC_RUN: drawRun(); break;
        case SC_RESULT: drawResult(); break;
        case SC_SETTINGS: drawSettings(); break;
        case SC_HELP: drawHelp(); break;
    }
}

}  // namespace

int main() {
    gfx::init();
    if (!save::load(data)) save::defaults(data);

    while (!quit) {
        if (animating()) {
            pollEvents(false);
            if (quit) break;
            if (animating()) runFrame();
            draw();
            LCD_Refresh();
            continue;
        }
        // Static screens: draw once, then sleep until something happens.
        draw();
        LCD_Refresh();
        msgTimer = 0;
        while (!pollEvents(true) && !quit) {}
        if (animating() && data.set.timer) rtcLast = rtcRead();   // do not count the idle time
    }
    if (screen == SC_RUN) { if (!run.over) run.over = END_QUIT; finishRun(run, P()); }
    persist();
    return 0;
}
