# stardew3ds

A Nintendo 3DS homebrew farming engine that plays using **your own copy of
Stardew Valley's assets**. The engine is original code. It contains no code,
art, maps or data from the game. You convert your own Steam install's content
on your PC, and the 3DS reads the result from the SD card. This is the same
model as ScummVM or OpenMW. Personal use only.

## Milestone 1: the farm loop (this build)

- The real Farm map with your tilesheets, seasonal swaps and animated water
- Tilling, watering (refill at the pond), planting, overnight growth and
  harvesting, all driven by your `Data/Crops.json` (phases, seasons, regrowth)
- Weeds, stones and twigs to clear with the scythe, pickaxe and axe (drops
  fiber, stone, wood)
- Clock (7 seconds per 10 minutes), days of the week, 28-day seasons, years,
  rain (waters crops), night tint, passing out at 2 am
- Energy, eating, shipping bin (paid overnight), gold
- 36-slot inventory on the touch screen: tap to select, tap two slots to swap
- Temporary seed stall (Seeds tab) until Pierre's shop exists
- Saves automatically every time you sleep, plus Save and Save & quit in the menu
- **3D slider:** ground sits back, buildings and farmer at screen level,
  roofs and tree tops pop forward, HUD floats on top

## Setup

### 1. Unpack your game content (PC, once)
1. Download **StardewXnbHack** (Pathoschild) for your OS from its GitHub
   releases.
2. Unzip it into your Stardew Valley game folder (next to `Stardew Valley.dll`)
   and run it. You get a `Content (unpacked)` folder.

### 2. Convert for the 3DS
```bash
pip install pillow numpy
python3 tools/convert.py "<game folder>/Content (unpacked)" out/
```
Copy `out/3ds` onto the root of the SD card, merging with the existing `3ds`
folder. You end up with `sdmc:/3ds/stardew3ds/{tex,maps,data,layout.txt}`.

### 3. Build
```bash
# devkitPro with 3ds-dev installed
make
```
Put `stardew3ds.3dsx` in `sdmc:/3ds/` and launch it from the Homebrew Launcher.

## Controls

| | |
|---|---|
| Circle pad / D-pad | Move (hold **B** to walk) |
| **A** / **Y** | Harvest, ship, use the door, otherwise use the held tool or item (hold to keep swinging) |
| **L / R** | Previous / next hotbar slot |
| Touch | Inventory, Seeds, Menu tabs |
| **SELECT** | Cycle bottom-screen tabs |
| **START** | Menu tab |

Walk up to the farmhouse door and press A to sleep.

## Things to check on first run

These were set from memory and can't be checked without real assets, so tell
me what looks off:

1. **Farmhouse and shipping bin position.** They live in
   `sdmc:/3ds/stardew3ds/layout.txt` and can be edited without rebuilding:
   sprite rectangle, tile position, footprint, door, spawn point, bin tiles.
2. **Tilled soil borders.** If the edges of tilled patches join up wrong, the
   neighbour table in `hoeDirtIndex()` (main.cpp) needs remapping.
3. **Farmer sprite.** For now it's the base body plus hairstyle 0 with a brown
   tint. Shirts, pants, arms and tool-swing animations are still to come.
4. **Crop frames.** Seeds, growth stages and the ready frame come from
   `crops.png`. If a crop shows the wrong stage, tell me which crop and day.

## Verified so far (without hardware)

- The texture swizzle matches devkitPro's tex3ds: same Morton order, byte
  order and row flip. Round-trip tested.
- Converter tested against a synthetic Content folder (CSV map, seasonal
  sheets, animated tiles, tile properties, data JSON).
- Game rules have host unit tests: `tests/core_test.cpp` covers tilling,
  planting, growth, harvest, shipping, pass-out and save/load.
- Every source file compiles warning-free for ARM11 against the real
  libctru, citro3d and citro2d headers. A full link needs devkitARM, which
  wasn't reachable from the build machine.

```bash
python3 tests/make_fake_content.py && python3 tools/convert.py "fake/Content (unpacked)" out
g++ -std=c++17 -Isource/core tests/core_test.cpp source/core/*.cpp -o core_test && ./core_test out/3ds/stardew3ds/
```

## Roadmap

1. **Farm loop**: done
2. **World**: warps, farmhouse interior with bed, Pelican Town, Pierre's
   shop (replaces the seed stall), bus stop, more maps through the converter
3. **People**: villagers with schedules and pathfinding, dialogue from your
   `Characters/Dialogue`, portraits, gifts and friendship, birthdays
4. **Systems**: tool upgrades, crafting, artisan machines, cooking, trees and
   foraging, fishing minigame
5. **Mines and combat**: floors, monsters, weapons, ore
6. **Animals and buildings**: coops, barns, Robin's construction
7. **Community Center**: bundles, Joja, festivals, events
8. **Save sync**: a small service on the homelab converts between the PC save
   and the 3DS save. The 3DS syncs over Wi-Fi, and Steam Cloud syncs the PC
   side as usual.

## Layout

```
source/core/      game rules, map, data (pure C++17, host-testable)
source/platform/  3DS texture loading
source/main.cpp   rendering, stereo 3D, touch UI, main loop
tools/convert.py  PC asset converter
tests/            host tests + fake content generator
```
