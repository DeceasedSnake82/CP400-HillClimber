// Settings + player progress file (\\fls0\HillClimber.sav). The record format is pure (no SDK)
// so it can be unit-tested on a PC. A file with a wrong magic, version, struct size or
// checksum, or with out-of-range values, is ignored (the caller gets defaults).
//
// Record layout (multi-byte header fields big-endian):
//   'H' 'C' 'S' VERSION | sizeof(Profile) (16 bit) | pace timer showFps autoZoom | Profile (raw) | checksum (16 bit)
// Profile is a POD struct written and read by the same binary; the size field rejects a file
// written by a build with a different layout.
#pragma once

#include <stdint.h>
#include "game.h"

namespace save {

constexpr uint8_t VERSION = 1;

struct Settings {
    uint8_t pace;           // ticks per frame in frame-timer mode, 1..8 (default 4 = 30 fps)
    uint8_t timer;          // 0 = frame based, 1 = RTC
    uint8_t showFps;        // 0 / 1
    uint8_t autoZoom;       // 0 / 1: zoom out at speed
};

struct Data {
    Settings set;
    hc::Profile prof;
};

constexpr int HEADER_SIZE = 4 + 2;
constexpr int RECORD_SIZE = HEADER_SIZE + 4 + (int)sizeof(hc::Profile) + 2;

void defaults(Data& d);
bool valid(const Data& d);
void encode(const Data& d, uint8_t out[RECORD_SIZE]);
bool decode(const uint8_t in[RECORD_SIZE], Data& d);   // false: d holds defaults

bool load(Data& d);         // false: no (valid) file, d holds defaults
bool store(const Data& d);  // false: could not write it

}  // namespace save
