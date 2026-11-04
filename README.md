# Hill Climber for the CP400

A physics hill-climbing driving game for the Casio fx-CP400 / ClassPad II, written for
[Hollyhock 3](https://github.com/classpaddev/hollyhock-3). Copy **`HillClimber.hh3`** to the calculator and start it from the launcher.

**Hold the calculator sideways** (keypad on the right): the game uses the screen in landscape, 528 x 320.

Drive as far as you can over endless procedurally generated hills. Fuel runs out, so grab the red cans (they get rarer the
further you go), and land on your wheels, not on the driver's head. Coins from pickups, flips, long jumps and distance
milestones buy upgrades, new vehicles and new maps. All vehicles, maps and art are original.

## What is in it

* **Real-time soft-body physics**: every vehicle is a set of Verlet particles with swing-arm suspension, springs and dampers,
  wheel spin and tyre grip (wheelspin on ice, wheelies under power, flips in the air)
* **6 vehicles**: Buggy, Scrambler (dirt bike), Tractor, Rally Car (4WD), Monster truck (4WD), Moon Rover (4WD), each with its own
  geometry, mass, power, top speed, grip, suspension, fuel tank and air control
* **5 upgrades per vehicle**, 8 levels each: engine, tires, suspension, fuel tank, air control
* **7 maps** with their own terrain generator, gravity, grip, colours, parallax backgrounds and scenery:
  Clover Hills, Dune Sea (sharp sand crests, soft sand), Frostbite Pass (ice), Red Canyon (rock shelves),
  Moon Craters (low gravity), Magma Ridge (jagged peaks), Stunt Park (kickers and tabletops)
* Terrain gets steeper and bumpier the further you drive; roads are 30 km long
* Coins (5 / 25 / 100 / 500), fuel cans, distance signs, your record flag on the track, flip and air-time bonuses,
  +coins every 500 m
* Records per map, total stats, everything saved to `\\fls0\HillClimber.sav`

## Controls

| Action | Touch | Keys |
| --- | --- | --- |
| Gas | hold the right half of the screen | arrow DOWN (points right when held sideways), `EXE`, `1` `2` `3`, `+` |
| Brake / reverse | hold the left half | arrow UP (points left), `7` `8` `9`, `-` |
| In the air | gas tilts the nose up (backflip), brake tilts it down | same keys |
| Pause | II button (top right) | `CLEAR` (then `EXE` resume, `1` restart, `CLEAR` end run - coins are kept) |
| Menus | buttons | title: `EXE` drive, `1` garage, `2` maps, `=` setup, `.` help, `CLEAR` quit |
| Garage | tap a vehicle / BUY / upgrade | arrows pick a vehicle, `1`-`5` buy upgrades, `EXE` drive (or buy), `CLEAR` back |
| Maps | tap a map twice | arrows pick, `EXE` drive (or buy), `CLEAR` back |

## Game speed

The Hollyhock SDK has no timer, so the game counts frames by default. In **SETUP**:

* **TICKS PER FRAME** (FRAME timer, default 4): every frame advances the physics by that many 1/120 s steps. 4 is right for
  30 frames per second. If the game feels slow on your calculator, raise it; if it feels too fast, lower it.
* **TIMER: RTC** (experimental): reads the CPU's 128 Hz real-time clock counter (`0xA413FEC0`, as in Pulse Dash and
  Pocket Gridiron), so the game runs at real speed whatever the frame rate. The game checks that the counter moves before
  switching and falls back to the frame timer if it stops. **Not tested on a real calculator yet.**
* **AUTO ZOOM**: the camera zooms out a little at speed.

## Optimisation notes

* No floating point anywhere (`make fpucheck` fails the build if soft-float helpers are linked). Positions are Q12 fixed point.
* The physics is 5 particles per vehicle, 3 constraint passes per 1/120 s tick: a few dozen integer square roots per tick.
* The world is drawn column by column: a logical column is one contiguous VRAM row in landscape, so every column is three
  spans - a copy of a prebuilt sky gradient, the parallax hills, and a copy of a prebuilt ground strip (grass edge, soil,
  strata) that starts at the surface. Every pixel is written exactly once, with 32-bit moves.
* The terrain is a pure function of (map, seed, metre), cached in a 512-sample ring buffer around the car: no level data,
  random access in both directions, cheap enough to generate a few samples per frame.
* Vehicles are small convex polygons rotated with the chassis and filled with sub-pixel vertices; no rotated sprite tables.
* Menus, the garage, results and pause draw once and then block in `GetInput` (no busy loop, saves battery).
* The rules are pure C++ (no SDK), so a bot can drive thousands of runs per second on a PC for testing and balancing.

## Building

You need the Hollyhock 3 toolchain and SDK; the `ghcr.io/classpaddev/hollyhock-3` image has both.

```sh
export SDK_DIR=/sdk        # default
make                       # -> HillClimber.elf and HillClimber.hh3
make fpucheck              # fails if soft-float code was linked (the CPU has no FPU)
```

GitHub Actions (`.github/workflows/build.yml`) runs the PC tests, builds `HillClimber.hh3` in that image and commits it back
to the branch.

## Tests (run on the PC, not on the calculator)

```sh
make -C test test SDK_DIR=/path/to/hollyhock-3/sdk     # unit tests + bot table + bot career
make -C test bot BOT_ARGS="--veh 2 --map 3 --level 4"  # one bot run with a trace
make -C test shots SDK_DIR=...                         # runs the real UI headlessly, screenshots in test/out/
make -C test film SDK_DIR=...                          # a bot drives every map, frames in test/out/film_*.png
```

* `test/test_game.cpp`: fixed-point maths, terrain continuity on all 30 km of every map, parked vehicles do not creep,
  driving/braking/reversing, fuel and fuel cans, crashes, coin pickups, flips, purchases, the save format.
* `test/bot.cpp`: a bot that only uses gas and brake drives every vehicle on every map (stock and fully upgraded) and checks the
  physics never explodes; it also plays a whole career (run, bank coins, shop) as a balance check.
* `test/host_app.cpp` runs `main.cpp` unchanged against a fake LCD and scripted input (`test/script.txt`).

`cp400.md` is a field guide to writing games for the CP400 with Hollyhock 3 (shared with the other CP400 projects, with a
section on what this game taught).
