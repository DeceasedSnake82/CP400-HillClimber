#include "save.h"

namespace save {

namespace {
uint16_t checksum(const uint8_t* p, int n) {
    uint16_t a = 0x48C1, b = 0;
    for (int i = 0; i < n; ++i) { a = (uint16_t)(a + p[i]); b = (uint16_t)(b + a); }
    return (uint16_t)(a ^ (b << 3) ^ (b >> 5));
}

void copy(void* dst, const void* src, unsigned n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (unsigned i = 0; i < n; ++i) d[i] = s[i];
}

constexpr uint16_t PROFILE_SIZE = (uint16_t)sizeof(hc::Profile);
static_assert(sizeof(hc::Profile) < 65536, "size field is 16 bit");
}  // namespace

void defaults(Data& d) {
    uint8_t* q = (uint8_t*)&d;
    for (unsigned i = 0; i < sizeof d; ++i) q[i] = 0;
    d.set.pace = 4;
    d.set.timer = 0;
    d.set.showFps = 0;
    d.set.autoZoom = 1;
    hc::profileNew(d.prof);
}

bool valid(const Data& d) {
    const Settings& s = d.set;
    if (s.pace < 1 || s.pace > 8 || s.timer > 1 || s.showFps > 1 || s.autoZoom > 1) return false;
    const hc::Profile& p = d.prof;
    if (p.coins < 0 || p.veh >= hc::NVEH || p.map >= hc::NMAPS) return false;
    if (!(p.vehOwned & 1) || !(p.mapOwned & 1)) return false;
    if (p.vehOwned >= (1 << hc::NVEH) || p.mapOwned >= (1 << hc::NMAPS)) return false;
    if (!hc::ownsVeh(p, p.veh) || !hc::ownsMap(p, p.map)) return false;
    for (int v = 0; v < hc::NVEH; ++v)
        for (int u = 0; u < hc::U_COUNT; ++u)
            if (p.level[v][u] > hc::MAX_LEVEL) return false;
    for (int m = 0; m < hc::NMAPS; ++m)
        if (p.best[m] < 0 || p.best[m] > hc::MAX_M) return false;
    return true;
}

void encode(const Data& d, uint8_t out[RECORD_SIZE]) {
    out[0] = 'H'; out[1] = 'C'; out[2] = 'S'; out[3] = VERSION;
    out[4] = (uint8_t)(PROFILE_SIZE >> 8); out[5] = (uint8_t)PROFILE_SIZE;
    uint8_t* p = out + HEADER_SIZE;
    p[0] = d.set.pace; p[1] = d.set.timer; p[2] = d.set.showFps; p[3] = d.set.autoZoom;
    copy(p + 4, &d.prof, PROFILE_SIZE);
    const uint16_t c = checksum(out, RECORD_SIZE - 2);
    out[RECORD_SIZE - 2] = (uint8_t)(c >> 8);
    out[RECORD_SIZE - 1] = (uint8_t)c;
}

bool decode(const uint8_t in[RECORD_SIZE], Data& d) {
    defaults(d);
    if (in[0] != 'H' || in[1] != 'C' || in[2] != 'S' || in[3] != VERSION) return false;
    if (((in[4] << 8) | in[5]) != PROFILE_SIZE) return false;
    const uint16_t c = checksum(in, RECORD_SIZE - 2);
    if (in[RECORD_SIZE - 2] != (uint8_t)(c >> 8) || in[RECORD_SIZE - 1] != (uint8_t)c) return false;
    Data t;
    defaults(t);
    const uint8_t* p = in + HEADER_SIZE;
    t.set.pace = p[0]; t.set.timer = p[1]; t.set.showFps = p[2]; t.set.autoZoom = p[3];
    copy(&t.prof, p + 4, PROFILE_SIZE);
    if (!valid(t)) return false;
    d = t;
    return true;
}

}  // namespace save

#ifdef HC_HOST
// PC tests: a plain file.
#include <stdio.h>
#ifndef HC_SAVE_PATH
#define HC_SAVE_PATH "out/hillclimber.sav"
#endif

namespace save {

bool load(Data& d) {
    defaults(d);
    FILE* f = fopen(HC_SAVE_PATH, "rb");
    if (!f) return false;
    static uint8_t rec[RECORD_SIZE];
    const size_t n = fread(rec, 1, sizeof rec, f);
    fclose(f);
    return n == sizeof rec && decode(rec, d);
}

bool store(const Data& d) {
    static uint8_t rec[RECORD_SIZE];
    encode(d, rec);
    FILE* f = fopen(HC_SAVE_PATH, "wb");
    if (!f) return false;
    const bool ok = fwrite(rec, 1, sizeof rec, f) == sizeof rec;
    return fclose(f) == 0 && ok;
}

}  // namespace save

#else
#include <sdk/os/file.h>

namespace save {

namespace {
const char* const PATH = "\\\\fls0\\HillClimber.sav";     // = \\fls0\HillClimber.sav
uint8_t rec[RECORD_SIZE];                                     // static: keep it off the stack
}

bool load(Data& d) {
    defaults(d);
    const int fd = File_Open(PATH, FILE_OPEN_READ);
    if (fd < 0) return false;
    const int n = File_Read(fd, rec, RECORD_SIZE);
    File_Close(fd);
    return n == RECORD_SIZE && decode(rec, d);
}

bool store(const Data& d) {
    encode(d, rec);
    // Fixed-size record: overwriting from the start is enough whether or not the OS truncates.
    int fd = File_Open(PATH, FILE_OPEN_WRITE);
    if (fd < 0) fd = File_Open(PATH, FILE_OPEN_WRITE | FILE_OPEN_CREATE);
    if (fd < 0) return false;
    const int n = File_Write(fd, rec, RECORD_SIZE);
    const int c = File_Close(fd);
    return n == RECORD_SIZE && c >= 0;
}

}  // namespace save
#endif
