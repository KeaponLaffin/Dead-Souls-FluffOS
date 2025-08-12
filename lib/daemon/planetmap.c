// /daemon/planetmap.c
// Procedural overland planet generator with delta layers
// Written for Dead Souls Mudlib

#include <lib.h>
#include <save.h>
#include <daemons.h>

inherit LIB_DAEMON;

#define PERMA_SAVE_DIR "/save/planetmap_perma/"
#define TEMP_SAVE_DIR  "/save/planetmap_temp/"

// Planet definitions
mapping Planets = ([]); // planet_id : ([ params ])
mapping PermanentDeltas = ([]); // planet_id : ([ "x:y" : mapping ])
mapping TemporaryDeltas = ([]); // planet_id : ([ "x:y" : mapping ])

// Utility — make sure dirs exist
static void CheckDirs() {
    if (file_size(PERMA_SAVE_DIR) != -2) mkdir(PERMA_SAVE_DIR);
    if (file_size(TEMP_SAVE_DIR) != -2) mkdir(TEMP_SAVE_DIR);
}

// Hash planet type + seed for save filenames
string HashPlanet(string type, int seed) {
    string raw = type + ":" + seed;
    string hashed = crypt(raw, "pm");
    return lower_case(replace_string(hashed[2..9], "/", "x"));
}

// Save + load helpers
void SavePlanetData(string planet_id) {
    string perma_file = PERMA_SAVE_DIR + planet_id + ".o";
    string temp_file  = TEMP_SAVE_DIR  + planet_id + ".o";
    unguarded( (: save_object, perma_file, PermanentDeltas[planet_id] :) );
    unguarded( (: save_object, temp_file,  TemporaryDeltas[planet_id] :) );
}

void LoadPlanetData(string planet_id) {
    string perma_file = PERMA_SAVE_DIR + planet_id + ".o";
    string temp_file  = TEMP_SAVE_DIR  + planet_id + ".o";
    if (file_size(perma_file) > 0) {
        unguarded( (: restore_object, perma_file, PermanentDeltas[planet_id] :) );
    } else PermanentDeltas[planet_id] = ([]);
    if (file_size(temp_file) > 0) {
        unguarded( (: restore_object, temp_file, TemporaryDeltas[planet_id] :) );
    } else TemporaryDeltas[planet_id] = ([]);
}

// Register a new planet
void NewPlanet(string type, int seed, mapping params) {
    string pid = HashPlanet(type, seed);
    params["type"] = type;
    params["seed"] = seed;
    Planets[pid] = params;
    LoadPlanetData(pid);
}

// Placeholder noise functions — replace with Perlin later
float Noise2D(int x, int y, int seed) {
    return to_float((x * 92837111 ^ y * 689287499 ^ seed) & 0x7fffffff) / 2147483647.0;
}

float SmoothNoise(int x, int y, int seed) {
    return (Noise2D(x-1,y-1,seed) + Noise2D(x+1,y-1,seed) +
            Noise2D(x-1,y+1,seed) + Noise2D(x+1,y+1,seed)) / 4.0;
}

// Get elevation (-1.0 to 1.0)
float GetElevation(string pid, int x, int y) {
    int seed = Planets[pid]["seed"];
    return (SmoothNoise(x,y,seed) * 2.0) - 1.0;
}

// Get moisture (0.0 to 1.0)
float GetMoisture(string pid, int x, int y) {
    int seed = Planets[pid]["seed"] + 99999;
    return Noise2D(x,y,seed);
}

// Get latitude (-90 to 90)
float GetLatitude(string pid, int y) {
    int radius = Planets[pid]["radius"];
    return ((to_float(y) / radius) * 180.0) - 90.0;
}

// Get base temperature from latitude & axial tilt
float GetTemperature(string pid, int y) {
    float axial = Planets[pid]["axial_tilt"];
    float lat = GetLatitude(pid, y);
    // simple cosine-based temp curve
    float temp = cos((lat + axial) * 3.14159 / 180.0);
    return temp; // normalized 0-1
}

// Determine biome based on elevation, moisture, temperature
string ProceduralBiome(string pid, int x, int y) {
    float elev = GetElevation(pid,x,y);
    float moist = GetMoisture(pid,x,y);
    float temp = GetTemperature(pid,y);

    // Simple rules — tweak later
    if (elev < -0.2) return "ocean";
    if (elev < 0.0) return "coast";
    if (temp > 0.8) {
        if (moist < 0.3) return "desert";
        else return "tropical_forest";
    }
    if (temp > 0.5) {
        if (moist < 0.4) return "savanna";
        else return "temperate_forest";
    }
    if (temp > 0.3) return "taiga";
    return "tundra";
}

// Get biome with deltas applied
string GetBiome(string pid, int x, int y) {
    string key = x + ":" + y;
    if (PermanentDeltas[pid] && PermanentDeltas[pid][key] && PermanentDeltas[pid][key]["biome"]) {
        return PermanentDeltas[pid][key]["biome"];
    }
    // Seasonal modifier hook could go here
    return ProceduralBiome(pid,x,y);
}

// Get room data (biome, elevation, moisture, temp, deltas)
mapping GetRoomData(string pid, int x, int y) {
    mapping data = ([
        "biome" : GetBiome(pid,x,y),
        "elev"  : GetElevation(pid,x,y),
        "moist" : GetMoisture(pid,x,y),
        "temp"  : GetTemperature(pid,y),
        "perma" : PermanentDeltas[pid][x+":"+y],
        "tempd" : TemporaryDeltas[pid][x+":"+y],
    ]);
    return data;
}

// Set permanent delta (terrain/building changes)
void SetPermanentDelta(string pid, int x, int y, mapping change) {
    string key = x + ":" + y;
    if (!PermanentDeltas[pid]) PermanentDeltas[pid] = ([]);
    PermanentDeltas[pid][key] = change;
    SavePlanetData(pid);
}

// Remove permanent delta
void RemovePermanentDelta(string pid, int x, int y) {
    string key = x + ":" + y;
    if (PermanentDeltas[pid]) map_delete(PermanentDeltas[pid], key);
    SavePlanetData(pid);
}

// Set temporary delta (mobs/items)
void SetTemporaryDelta(string pid, int x, int y, mapping change) {
    string key = x + ":" + y;
    if (!TemporaryDeltas[pid]) TemporaryDeltas[pid] = ([]);
    TemporaryDeltas[pid][key] = change;
    SavePlanetData(pid);
}

// Remove temporary delta
void RemoveTemporaryDelta(string pid, int x, int y) {
    string key = x + ":" + y;
    if (TemporaryDeltas[pid]) map_delete(TemporaryDeltas[pid], key);
    SavePlanetData(pid);
}

static void create() {
    daemon::create();
    CheckDirs();
}
