// Your pet: a cat or a dog, adopted in Marnie's cutscene. It wanders the farm by day, comes inside
// when it rains and to sleep, likes being petted once a day and having its bowl filled.
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sv {

static const int TILE = 16;

void Game::adoptPet(int type) {
    pet = Pet();
    pet.type = type;
    pet.breed = type == me.petKind ? me.petBreed : 0;
    pet.name = type == 2 ? "Buddy" : "Whiskers";
    if (keyboard) {
        std::string n = keyboard(type == 2 ? "Name your dog" : "Name your cat", pet.name, 12);
        if (!n.empty()) pet.name = n;
    }
    mailReceived.insert("hasPet");
    petHome();
    ensurePetBowl();
    setDialogueToken("%pet", pet.name);
    setDialogueToken("%pet%", pet.name);
    say(pet.name + " is part of the farm now!");
}

// Where the pet starts the day: out by the door, or inside when it's wet.
void Game::petHome() {
    if (!pet.type) return;
    bool inside = raining || snowing;
    int dx = 0, dy = 0;
    if (inside && houseDoor(houseLevel, dx, dy)) {
        pet.loc = "FarmHouse";
        pet.x = (dx + 2) * TILE + 8.0f;
        pet.y = (dy - 2) * TILE + 14.0f;
    } else {
        pet.loc = "Farm";
        pet.x = (data.layout.doorX + 2) * TILE + 8.0f;
        pet.y = (data.layout.doorY + 2) * TILE + 14.0f;
    }
    pet.state = 0;
    pet.timer = 3;
    pet.facing = FACE_DOWN;
}

// A Pet Bowl by the house if Robin's list has one and you don't.
void Game::ensurePetBowl() {
    const BuildingType* t = data.buildingType("Pet Bowl");
    if (!t || !farm) return;
    for (auto& b : buildings) if (b.type == "Pet Bowl") return;
    for (int r = 3; r < 14; r++)
        for (int dy = 1; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int x = data.layout.doorX + dx, y = data.layout.doorY + dy;
                if (!farmSpotFree(x - 1, y - 1, t->w + 2, t->h + 2)) continue;
                FarmBuilding b;
                b.id = nextBuildingId++;
                b.type = "Pet Bowl";
                b.x = x; b.y = y;
                buildings.push_back(b);
                return;
            }
}

void Game::petMorning() {
    if (!pet.type) {
        if (mailReceived.count("hasPet")) { pet.type = 1; pet.name = "Whiskers"; }   // adopted before pets had names
        else return;
    }
    if (pet.watered || raining) pet.friendship = std::min(1000, pet.friendship + 6);
    pet.watered = raining;          // the rain fills the bowl
    pet.petted = false;
    petHome();
    setDialogueToken("%pet", pet.name);
    setDialogueToken("%pet%", pet.name);
    if (pet.friendship >= 1000 && !mailReceived.count("s3d_petLoves")) {
        mailReceived.insert("s3d_petLoves");
        if (morningNote.empty()) morningNote = pet.name + " loves you as much as a pet can.";
    }
}

void Game::petUpdate(float dt) {
    if (!pet.type || !cur || pet.loc != cur->name) return;
    if (pet.heart > 0) pet.heart -= dt;
    pet.anim += dt;
    bool night = timeOfDay >= 20 * 60;
    if (night && pet.loc == "Farm") {          // bedtime: in it goes
        int dx, dy;
        if (houseDoor(houseLevel, dx, dy)) {
            pet.loc = "FarmHouse";
            pet.x = (dx + 2) * TILE + 8.0f;
            pet.y = (dy - 2) * TILE + 14.0f;
        }
        pet.state = 2;
        return;
    }
    if (night) { pet.state = 2; return; }
    if (pet.loc != "Farm") { pet.state = 0; return; }   // indoors on a wet day: it just sits
    pet.timer -= dt;
    if (pet.state == 1) {                       // walking to a spot
        float dx = pet.tx - pet.x, dy = pet.ty - pet.y, d = std::sqrt(dx * dx + dy * dy);
        if (d < 1.5f || pet.timer <= 0) { pet.state = 0; pet.timer = 3 + (float)(rand32() % 60) / 10.0f; return; }
        float sp = (pet.type == 2 ? 34.0f : 26.0f) * dt;
        float nx = pet.x + dx / d * sp, ny = pet.y + dy / d * sp;
        int tx = (int)std::floor(nx / TILE), ty = (int)std::floor((ny - 4) / TILE);
        bool same = tx == (int)std::floor(pet.x / TILE) && ty == (int)std::floor((pet.y - 4) / TILE);
        if (!same && !farmSpotFree(tx, ty, 1, 1)) { pet.state = 0; pet.timer = 2; return; }
        pet.x = nx; pet.y = ny;
        pet.facing = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? FACE_RIGHT : FACE_LEFT) : (dy > 0 ? FACE_DOWN : FACE_UP);
        return;
    }
    if (pet.timer > 0) return;
    // dogs trot over when you're near; otherwise a wander that stays around the house
    float hx = (data.layout.doorX + 2) * TILE + 8.0f, hy = (data.layout.doorY + 3) * TILE + 8.0f;
    float gx, gy;
    float pdx = px - pet.x, pdy = py - pet.y;
    if (pet.type == 2 && pdx * pdx + pdy * pdy < 90.0f * 90.0f && pdx * pdx + pdy * pdy > 24.0f * 24.0f && rand32() % 2) {
        gx = px - (pdx > 0 ? 14 : -14);
        gy = py;
    } else {
        gx = pet.x + (float)((int)(rand32() % 97) - 48);
        gy = pet.y + (float)((int)(rand32() % 97) - 48);
        if (std::fabs(gx - hx) > 150 || std::fabs(gy - hy) > 120) { gx = hx + (float)((int)(rand32() % 61) - 30); gy = hy + (float)((int)(rand32() % 41) - 20); }
    }
    pet.tx = gx; pet.ty = gy;
    pet.state = 1;
    pet.timer = 6;
}

bool Game::petInteract(int tx, int ty) {
    if (!pet.type || !cur || pet.loc != cur->name) return false;
    int ptx = (int)std::floor(pet.x / TILE), pty = (int)std::floor((pet.y - 4) / TILE);
    if (tx != ptx || (ty != pty && ty != pty - 1)) return false;
    pet.heart = 1.5f;
    if (pet.state == 1) { pet.state = 0; pet.timer = 3; }
    sfx(pet.type == 2 ? "dog_bark" : "cat");
    if (!pet.petted) {
        pet.petted = true;
        pet.friendship = std::min(1000, pet.friendship + 12);
        say(pet.name + (pet.type == 2 ? " wags its tail." : " purrs.") + " (" + std::to_string(pet.friendship / 200) + " of 5 hearts)");
    }
    return true;
}

// A on the pet bowl: fill it if you're carrying a watering can with water in it.
bool Game::petBowlInteract() {
    if (!pet.type) { say("An empty pet bowl."); return true; }
    if (pet.watered) { say(pet.name + "'s bowl is full."); return true; }
    bool can = false;
    for (int i = 0; i < invSlots; i++)
        if (inv[i].type == SLOT_TOOL)
            if (const ToolDef* t = data.tool(inv[i].id)) can |= t->kind == TOOL_CAN;
    if (!can || water <= 0) { say(pet.name + "'s bowl is empty. Bring your watering can (with water in it)."); return true; }
    water--;
    pet.watered = true;
    sfx("slosh");
    say("Filled " + pet.name + "'s bowl.");
    return true;
}

} // namespace sv
