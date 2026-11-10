# stardew3ds

A Nintendo 3DS homebrew farming engine that plays using **your own copy of
Stardew Valley's assets**. The engine is original code. It contains no code,
art, maps or data from the game. You convert your own Steam install's content
on your PC, and the 3DS reads the result from the SD card. This is the same
model as ScummVM or OpenMW. Personal use only.

## Milestone 2: the world (this build)

- **Every map gets converted**, along with its warps, door actions and
  indoor/outdoor flag. Walk off the edge of the farm to the Bus Stop and
  on into Pelican Town. That means any map from your files (or SVE later)
  just works.
- **You wake up in the farmhouse.** Walk out the door and in again. You can
  only sleep at home: walk into the bed, or use Sleep. Pass out at 2 am
  anywhere and you still wake up at home.
- **Doors** open by walking into them or pressing A. Locked doors keep their
  real opening hours ("Closed. Open 9:00 am to ...").
- **Pierre's shop** opens from the counter and uses your `Data/Shops.json`
  (season stock, prices). It's a bottom-screen takeover with Buy (+/- cart)
  and Sell tabs. This replaces milestone 1's seed stall.
- **New bottom screen layout:** the hotbar is pinned on top, with Items, Map
  and Options tabs. The Map tab shows a live mini-map of where you are, plus
  the exits.
- **Screenshots on PC:** `tools/preview` runs the real game code through a
  scripted walk and writes 3DS-shaped PNGs, using the same drawing code as
  the console.

## Milestone 1: the farm loop

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
5. **Bed and wake-up spot** (`bed` / `wake` in `layout.txt`). The 1.6
   farmhouse bed is furniture rather than map tiles, so these are guesses.
   The game picks the nearest open tile if the wake spot is blocked.
6. **Pierre's counter.** The shop opens from any tile whose Action is
   `OpenShop <id>` (or the older `Buy General`). If the counter does nothing
   on your files, tell me and I'll look at what action your map uses.
   Pierre's Wednesday closing isn't handled yet; it comes with villager
   schedules.

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
# placeholder world (my own simple art, not the game's) + host tests
python3 tests/make_fake_content.py fake
python3 tools/convert.py "fake/Content (unpacked)" out
g++ -std=c++17 -Isource -Isource/core tests/core_test.cpp source/core/*.cpp -o core_test
./core_test out/3ds/stardew3ds/

# screenshots (works on your real converted files too)
make -C tools/preview
mkdir -p shots && tools/preview/preview out/3ds/stardew3ds/ shots
```

## Roadmap

1. **Farm loop**: done
2. **World**: done (warps, farmhouse, town, Pierre's, mini-map)
3. **People**: villagers with schedules and pathfinding, dialogue from your
   `Characters/Dialogue`, portraits, gifts and friendship, birthdays
4. **Systems**: tool upgrades, crafting, artisan machines, cooking, trees and
   foraging, fishing minigame
5. **Mines and combat**: floors, monsters, weapons, ore
6. **Animals and buildings**: coops, barns, Robin's construction
7. **Community Center**: bundles, Joja, festivals, events
8. **Stardew Valley Expanded, built in**: the converter merges your own
   downloaded copy of SVE into the base content by default (`--vanilla` to
   skip it). The 3DS has no idea it's a mod: SVE's maps, NPCs, events and
   farms load like any other game data, and the Social tab, map and
   calendar include them. Systems from milestone 2 on are written with this
   in mind: everything comes from data, nothing is tied to the vanilla
   cast or map list.
9. **Save sync**: a small service on the homelab converts between the PC save
   and the 3DS save. The 3DS syncs over Wi-Fi, and Steam Cloud syncs the PC
   side as usual.

## Layout

```
source/core/      game rules, maps, warps, shops, data (pure C++17, host-testable)
source/render/    all drawing + touch UI, through a Canvas interface
source/platform/  3DS: textures and the citro2d Canvas
source/main.cpp   3DS main loop, input, stereo 3D
tools/preview/    PC Canvas (PNG) + scripted screenshot run
tools/convert.py  PC asset converter
tests/            host tests + fake content generator
```
