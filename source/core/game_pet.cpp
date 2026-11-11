// Your pets: a cat, dog or turtle. The first comes with Marnie's cutscene; more from her shop once the
// first one loves you. They wander the farm by day, come inside when it's wet and to sleep, like being
// petted once a day and having the bowl filled, and bring you things when they love you.
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sv {

static const int TILE = 16;
static const char* kindWord(int t) { return t == 2 ? "dog" : t == 3 ? "turtle" : "cat"; }

void Game::petTokens() {
    setDialogueToken("%pet", pets.empty() ? "your pet" : pets[0].name);
    setDialogueToken("%pet%", pets.empty() ? "your pet" : pets[0].name);
}

void Game::adoptPet(int type, int breed) {
    Pet p;
    p.type = type;
    p.breed = breed >= 0 ? breed : type == me.petKind ? me.petBreed : 0;
    static const char* defaults[3][3] = {{"Whiskers", "Mittens", "Pepper"}, {"Buddy", "Biscuit", "Scout"}, {"Shelly", "Pebble", "Moss"}};
    p.name = defaults[std::max(0, std::min(2, type - 1))][pets.size() % 3];
    if (keyboard) {
        std::string n = keyboard(std::string("Name your ") + kindWord(type), p.name, 12);
        if (!n.empty()) p.name = n;
    }
    mailReceived.insert("hasPet");
    pets.push_back(p);
    petHome(pets.back(), (int)pets.size() - 1);
    ensurePetBowl();
    petTokens();
    say(p.name + " is part of the farm now!");
}

// Another pet from Marnie: once your first one loves you. 40,000g.
bool Game::buyPet(int type, int breed) {
    if (pets.empty()) { say("Marnie: \"A stray's been hanging around. I'll bring it by your farm one morning.\""); return false; }
    if (pets[0].friendship < 1000) { say("Marnie: \"Let " + pets[0].name + " settle in first. Come back when you two are close.\""); return false; }
    if (pets.size() >= 6) { say("Marnie: \"I think you've got your hands full!\""); return false; }
    if (money < 40000) { say("Not enough money"); return false; }
    money -= 40000;
    sfx("purchase");
    adoptPet(type, breed);
    return true;
}

// Where a pet starts the day: out by the door, or inside when it's wet.
void Game::petHome(Pet& pet, int idx) {
    bool inside = raining || snowing;
    int dx = 0, dy = 0;
    if (inside && houseDoor(houseLevel, dx, dy)) {
        pet.loc = "FarmHouse";
        pet.x = (dx + 2 + idx % 3) * TILE + 8.0f;
        pet.y = (dy - 2 - idx / 3) * TILE + 14.0f;
    } else {
        pet.loc = "Farm";
        pet.x = (data.layout.doorX + 2 + idx % 3) * TILE + 8.0f;
        pet.y = (data.layout.doorY + 2 + idx / 3) * TILE + 14.0f;
    }
    pet.state = 0;
    pet.timer = 2 + idx;
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

// What a pet that loves you might bring (Data/Pets Gifts, by kind).
int Game::petGiftFor(const Pet& pet) {
    if (!petGiftsLoaded) {
        petGiftsLoaded = true;
        std::vector<std::string> lines;
        if (data.vfs.readTable("data/petgifts.dat", lines))
            for (auto& l : lines) {
                int kind = 0, item = 0;
                float w = 1;
                if (sscanf(l.c_str(), "%d\t%d\t%f", &kind, &item, &w) == 3 && data.object(item)) petGifts.push_back({kind, item, w});
            }
    }
    float total = 0;
    for (auto& g : petGifts) if (g.kind == pet.type) total += g.weight;
    if (total <= 0) return -1;
    float roll = (float)(rand32() % 10000) / 10000.0f * total;
    for (auto& g : petGifts) {
        if (g.kind != pet.type) continue;
        roll -= g.weight;
        if (roll <= 0) return g.item;
    }
    return -1;
}

void Game::petMorning() {
    if (pets.empty() && mailReceived.count("hasPet")) {   // adopted before pets had names
        Pet p;
        p.type = 1;
        p.name = "Whiskers";
        pets.push_back(p);
    }
    if (pets.empty()) return;
    bool full = bowlFull || raining;
    for (size_t i = 0; i < pets.size(); i++) {
        Pet& pet = pets[i];
        if (full) pet.friendship = std::min(1000, pet.friendship + 6);
        pet.petted = false;
        if (pet.friendship >= 1000 && pet.gift < 0 && rand32() % 100 < 20) pet.gift = petGiftFor(pet);
        petHome(pet, (int)i);
    }
    bowlFull = raining;             // the rain fills the bowl
    petTokens();
    if (pets[0].friendship >= 1000 && !mailReceived.count("s3d_petLoves")) {
        mailReceived.insert("s3d_petLoves");
        if (morningNote.empty()) morningNote = pets[0].name + " loves you as much as a pet can.";
    }
}

void Game::petUpdate(float dt) {
    if (!cur) return;
    for (auto& pet : pets) {
        if (pet.loc != cur->name) continue;
        if (pet.heart > 0) pet.heart -= dt;
        pet.anim += dt;
        bool night = timeOfDay >= 20 * 60;
        if (night && pet.loc == "Farm") {          // bedtime: in it goes
            int dx, dy;
            int idx = (int)(&pet - &pets[0]);
            if (houseDoor(houseLevel, dx, dy)) {
                pet.loc = "FarmHouse";
                pet.x = (dx + 2 + idx % 3) * TILE + 8.0f;
                pet.y = (dy - 2 - idx / 3) * TILE + 14.0f;
            }
            pet.state = 2;
            continue;
        }
        if (night) { pet.state = 2; continue; }
        if (pet.loc != "Farm") { pet.state = 0; continue; }   // indoors on a wet day: it just sits
        pet.timer -= dt;
        if (pet.state == 1) {                       // walking to a spot
            float dx = pet.tx - pet.x, dy = pet.ty - pet.y, d = std::sqrt(dx * dx + dy * dy);
            if (d < 1.5f || pet.timer <= 0) { pet.state = 0; pet.timer = 3 + (float)(rand32() % 60) / 10.0f; continue; }
            float sp = (pet.type == 2 ? 34.0f : pet.type == 3 ? 10.0f : 26.0f) * dt;
            float nx = pet.x + dx / d * sp, ny = pet.y + dy / d * sp;
            int tx = (int)std::floor(nx / TILE), ty = (int)std::floor((ny - 4) / TILE);
            bool same = tx == (int)std::floor(pet.x / TILE) && ty == (int)std::floor((pet.y - 4) / TILE);
            if (!same && !farmSpotFree(tx, ty, 1, 1)) { pet.state = 0; pet.timer = 2; continue; }
            pet.x = nx; pet.y = ny;
            pet.facing = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? FACE_RIGHT : FACE_LEFT) : (dy > 0 ? FACE_DOWN : FACE_UP);
            continue;
        }
        if (pet.timer > 0) continue;
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
        pet.timer = pet.type == 3 ? 14 : 6;
    }
}

bool Game::petInteract(int tx, int ty) {
    if (!cur) return false;
    for (size_t i = 0; i < pets.size(); i++) {
        Pet& pet = pets[i];
        if (pet.loc != cur->name) continue;
        int ptx = (int)std::floor(pet.x / TILE), pty = (int)std::floor((pet.y - 4) / TILE);
        if (tx != ptx || (ty != pty && ty != pty - 1)) continue;
        pet.heart = 1.5f;
        if (pet.state == 1) { pet.state = 0; pet.timer = 3; }
        sfx(pet.type == 2 ? "dog_bark" : pet.type == 3 ? "croak" : "cat");
        if (pet.gift >= 0) {                        // it brought you something
            const ObjectDef* o = data.object(pet.gift);
            if (o && addItem(pet.gift, 1)) {
                itemFlies(pet.gift, pet.x, pet.y - 12);
                say(pet.name + " brought you a " + o->name + "!");
                pet.gift = -1;
            }
            pet.petted = true;
            return true;
        }
        if (!pet.petted) {
            pet.petted = true;
            pet.friendship = std::min(1000, pet.friendship + 12);
            say(pet.name + (pet.type == 2 ? " wags its tail." : pet.type == 3 ? " pokes its head out." : " purrs.") + " (" +
                std::to_string(pet.friendship / 200) + " of 5 hearts)");
            return true;
        }
        // already petted today: its name tag
        DialoguePage p;
        p.text = pet.name + " the " + kindWord(pet.type) + ". " + std::to_string(pet.friendship / 200) + " of 5 hearts.";
        DialogueChoice c;
        c.label = "Rename"; c.responseKey = "pet:rename:" + std::to_string(i); p.choices.push_back(c);
        c.label = "Good " + std::string(kindWord(pet.type)); c.responseKey = ""; p.choices.push_back(c);
        dialogue = DialogueBox();
        dialogue.active = true;
        dialogue.pages.push_back(p);
        pendingChoice = "pet";
        return true;
    }
    return false;
}

bool Game::petChoice(const std::string& what, const std::string& key) {
    if (what != "pet") return false;
    if (key.rfind("pet:rename:", 0) == 0) {
        size_t i = (size_t)atoi(key.c_str() + 11);
        if (i < pets.size()) {
            if (keyboard) {
                std::string n = keyboard("A new name", pets[i].name, 12);
                if (!n.empty()) pets[i].name = n;
                say("It answers to " + pets[i].name + " now.");
                petTokens();
            } else {
                say("No keyboard here to type a name with.");
            }
        }
    } else if (key.rfind("pet:adopt:", 0) == 0) {
        int kind = atoi(key.c_str() + 10);
        if (kind >= 1 && kind <= 3) adoptPet(kind);
        else mailReceived.insert("s3d_noPet");
    }
    return true;
}

// A on the pet bowl: fill it if you're carrying a watering can with water in it.
bool Game::petBowlInteract() {
    if (pets.empty()) { say("An empty pet bowl."); return true; }
    if (bowlFull) { say("The bowl is full."); return true; }
    bool can = false;
    for (int i = 0; i < invSlots; i++)
        if (inv[i].type == SLOT_TOOL)
            if (const ToolDef* t = data.tool(inv[i].id)) can |= t->kind == TOOL_CAN;
    if (!can || water <= 0) { say("The bowl is empty. Bring your watering can (with water in it)."); return true; }
    water--;
    bowlFull = true;
    sfx("slosh");
    say("Filled the bowl" + (pets.size() == 1 ? " for " + pets[0].name + "." : std::string(".")));
    return true;
}

} // namespace sv
