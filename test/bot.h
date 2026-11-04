// A bot driver that only uses the human controls (gas / brake). Shared by the bot tests and
// the film tool. It keeps the nose near the slope of the ground below and eases off the gas
// when a wheelie gets dangerous - roughly what a careful player does.
#pragma once

#include "game.h"

namespace bot {

inline int sang(int a) { a &= 1023; return a >= 512 ? a - 1024 : a; }

// skill 0..2: 0 = sloppy (late corrections), 2 = careful
inline hc::Input drive(hc::Run& r, int skill = 2) {
    using namespace hc;
    Input in;
    in.gas = false;
    in.brake = false;
    const int a = sang(bodyAngle(r));                      // + = nose down
    // slope of the ground ahead of the car (angle units, + = downhill)
    const fx x = carX(r);
    const fx g0 = groundAt(r.terr, x - (24 << FXS)), g1 = groundAt(r.terr, x + (40 << FXS));
    const int slope = iatan2(g1 - g0, 64 << FXS);
    const int s = sang(slope);
    const int rel = a - s;                                 // body angle relative to the ground
    const int tol = skill == 0 ? 110 : skill == 1 ? 80 : 55;
    // rotation speed from the previous positions (angle units per tick)
    const int aOld = sang(iatan2(r.p[P_B1].oy - r.p[P_B0].oy, r.p[P_B1].ox - r.p[P_B0].ox));
    int angV = a - aOld;
    if (angV > 512) angV -= 1024;
    if (angV < -512) angV += 1024;
    if (r.onGround == 0 && r.airT > 4) {
        const int look = skill == 0 ? 6 : skill == 1 ? 14 : 22;
        const int pred = rel + angV * look;                // where the nose will be soon
        if (pred < -tol) in.brake = true;                  // nose too high: brake tilts it down
        else if (pred > tol) in.gas = true;                // nose too low: gas lifts it
    } else {
        const int lim = skill == 0 ? -190 : skill == 1 ? -160 : -130;
        in.gas = rel + angV * 10 > lim;                    // ease off before flipping over backwards
        if (rel < lim - 60) in.brake = true;
        // careful drivers lift off before a crest at high speed
        if (skill >= 1 && speedKmh(r) > 55) {
            const fx g2 = groundAt(r.terr, x + (160 << FXS));
            const int ahead = sang(iatan2(g2 - g1, 120 << FXS));
            if (ahead - s > 60) in.gas = false;
        }
    }
    return in;
}

}  // namespace bot
