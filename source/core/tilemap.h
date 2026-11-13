// Tile map loaded from a converted .smap file.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sv {

enum MapLayer : int { L_BACK = 0, L_BUILDINGS, L_PATHS, L_FRONT, L_ALWAYSFRONT, L_COUNT };

enum CellFlag : uint8_t {
    CF_DIGGABLE = 1,
    CF_WATER = 2,
    CF_BLOCKED = 4,
    CF_NOSPAWN = 8,
    CF_PASSABLE = 16,
    CF_GRASS = 32,
    CF_DIRT = 64,
    CF_SPAWNABLE = 128,
};

struct TileAnim {
    uint16_t durationMs = 250;
    std::vector<uint16_t> frames;
};

struct Tileset {
    std::string texture;        // may contain "{S}" for the season
    int columns = 0;
    int tileCount = 0;
    std::vector<int16_t> animIndex;   // per tile: index into anims or -1
    std::vector<TileAnim> anims;
    std::string textureFor(int season) const;
};

static const uint16_t NO_TILE = 0xFFFF;

// A map-edge or door warp: stepping onto (x, y) sends you to target at (tx, ty).
struct Warp {
    int x = 0, y = 0;
    std::string target;
    int tx = 0, ty = 0;
};

class TileMap {
public:
    bool load(const std::string& path, std::string* err);

    int width() const { return w; }
    int height() const { return h; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    uint16_t tile(int layer, int x, int y) const { return tiles[layer][y * w + x]; }
    uint8_t tileset(int layer, int x, int y) const { return tsIdx[layer][y * w + x]; }
    uint8_t flags(int x, int y) const { return inBounds(x, y) ? cellFlags[y * w + x] : (uint8_t)CF_BLOCKED; }
    void setFlags(int x, int y, uint8_t f) { if (inBounds(x, y)) cellFlags[y * w + x] = f; }

    // Tile index to draw right now (resolves animations).
    uint16_t animatedTile(int ts, uint16_t t, uint32_t timeMs) const;

    // Tile "Action" property at a cell ("" if none), e.g. "Warp 6 29 SeedShop".
    const std::string& action(int x, int y) const;
    const Warp* warpAt(int x, int y) const;
    const Warp* warpTo(const std::string& target) const;   // first warp leading there

    // The map's own properties (DayTiles, NightTiles, Light, Doors...), if the
    // converter wrote them; "" when missing.
    const std::string& prop(const std::string& key) const;
    // swap a tile, keeping the cell's tilesheet (DayTiles/NightTiles)
    void setTile(int layer, int x, int y, uint16_t index) {
        if (inBounds(x, y) && layer >= 0 && layer < L_COUNT && tiles[layer][y * w + x] != index) {
            tiles[layer][y * w + x] = index;
            rev++;
        }
    }
    void setTile(int layer, int x, int y, uint16_t index, int ts) {
        if (!inBounds(x, y) || layer < 0 || layer >= L_COUNT) return;
        tsIdx[layer][y * w + x] = (uint8_t)ts;
        tiles[layer][y * w + x] = index;
        rev++;
    }
    void setAction(int x, int y, const std::string& a);
    // Lay a smaller map over this one at (dx, dy): tiles, walls, actions and warps. Returns false if it doesn't fit.
    bool stamp(const TileMap& patch, int dx, int dy);
    // An extra tile on a layer group (0 Back, 1 Buildings, 2 Front, 3 AlwaysFront), over what's there.
    void addOverlay(int x, int y, uint16_t tile, int ts, int group);
    // Take over the cells marked in `mask` (w*h, nonzero = take) from a map of the same size: every layer,
    // walls, actions and numbered-layer tiles (a room of the Community Center restored from its refurbished map).
    bool copyCells(const TileMap& src, const std::vector<uint8_t>& mask);
    // The part of this map that has anything on it (a patch drawn on a full-size canvas), and where that part starts.
    TileMap trimmed(int& ox, int& oy) const;
    // Where a patch map belongs: the offset where most of its ground already matches. ok=false if nothing fits well.
    bool findFit(const TileMap& patch, int& dx, int& dy, int* fitScore = nullptr) const;
    uint32_t rev = 0;   // bumps whenever a tile changes (cached drawings go stale)
    bool isAnimated(int ts, uint16_t t) const {
        if (ts < 0 || ts >= (int)tilesets.size()) return false;
        const Tileset& s = tilesets[ts];
        return t < s.animIndex.size() && s.animIndex[t] >= 0;
    }
    // apply "Layer x y index ..." (DayTiles / NightTiles)
    void applyTileSwaps(const std::string& list);

    // Tiles from numbered layers (Back2, AlwaysFront3...), drawn over the layer they belong to.
    // Sorted by group (0 Back, 1 Buildings, 2 Front, 3 AlwaysFront), then row, then column.
    struct Overlay { uint16_t x, y, tile; uint8_t ts, group; };
    std::vector<Overlay> overlays;
    std::vector<uint32_t> overlayRow[4];   // per group: index of each row's first overlay (h + 1 entries)

    std::vector<Tileset> tilesets;
    std::vector<Warp> warps;
    std::map<std::string, std::string> props;
    bool outdoors = true;
    std::string name;

private:
    void rowRuns();
    std::vector<std::pair<uint32_t, std::string>> actions;   // sorted by cell
    int w = 0, h = 0;
    std::vector<uint16_t> tiles[L_COUNT];
    std::vector<uint8_t> tsIdx[L_COUNT];
    std::vector<uint8_t> cellFlags;
};

} // namespace sv
