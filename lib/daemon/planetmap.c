/* /daemon/planetmap.c
   Procedural planet generator with deltas + hydrology (rivers/lakes)
   For Dead Souls MUD

   Features:
   - Per-planet procedural height, moisture, temperature, biome.
   - Permanent + temporary delta layers saved per-planet.
   - Hydrology: flow target, flow accumulation, river and lake detection.
   - Simple memoization caches for performance.
   - Admin helpers to inspect tiles.

   Notes:
   - This version uses simple Perlin-like noise functions (good for prototyping).
   - Hydrology algorithm: each tile points to the lowest adjacent neighbor (8-way).
     Flow accumulation is the number of upstream tiles whose path passes through that tile.
     If a flow path reaches sea -> drained. If flow path enters a loop/not reaching sea -> basin/lake.
     River tiles are those whose accumulation >= RIVER_ACCUM_THRESHOLD.
   - For very large planets consider chunking / LRU caching later.
*/

#include <lib.h>
inherit LIB_DAEMON;

#define PI 3.141592653589793
#define SAVE_DIR "/save/planetmap/"
#define PERMA_PREFIX "perma_"
#define TEMP_PREFIX  "temp_"

/* tuning constants */
#define DEFAULT_OCTAVES 5
#define RIVER_ACCUM_THRESHOLD 40  /* upstream tiles required to mark tile as a river */
#define MAX_FLOW_RECURSION 10000  /* protect DFS from pathological loops */

/* persistent storage */
private mapping Planets;         /* planet_id -> params mapping */
private mapping PermanentDeltas; /* planet_hash -> mapping("x,y" -> mapping) */
private mapping TemporaryDeltas; /* planet_hash -> mapping("x,y" -> mapping) */

/* runtime caches (not persisted) */
private mapping value_cache;     /* key -> numeric value (height/temp/moist/...) */
private mapping flow_cache;      /* "ph:x,y" -> ({ tx, ty }) or "sea" or "loop" or "pool" */
private mapping accum_cache;     /* "ph:x,y" -> integer accumulation count */
private mapping water_mask;      /* "ph:x,y" -> "river"/"lake"/0 */

static void create() {
    daemon::create();
    Planets = ([]);
    PermanentDeltas = ([]);
    TemporaryDeltas = ([]);
    value_cache = ([]);
    flow_cache = ([]);
    accum_cache = ([]);
    water_mask = ([]);

    /* make save dir if missing */
    if (file_size(SAVE_DIR) != -2) mkdir(SAVE_DIR);

    /* default planets (example); use AddPlanet to add your own */
    AddPlanet("earthlike", ([
        "type"        : "earthlike",
        "seed"        : 42,
        "width"       : 400,
        "height"      : 200,
        "axial_tilt"  : 23.5,
        "sea_level"   : 0.50,
        "base_temp"   : 15.0,
        "max_elev_m"  : 8000,
        "lapse_rate"  : 6.5,
        "noise_scale" : 0.007,
        "moisture_scale": 0.02,
        "moisture_sea_influence_radius": 24,
    ]));

    AddPlanet("hot_desert", ([
        "type"        : "hot_desert",
        "seed"        : 777,
        "width"       : 300,
        "height"      : 150,
        "axial_tilt"  : 10.0,
        "sea_level"   : 0.20,
        "base_temp"   : 30.0,
        "max_elev_m"  : 6000,
        "lapse_rate"  : 6.0,
        "noise_scale" : 0.01,
        "moisture_scale": 0.03,
        "moisture_sea_influence_radius": 18,
    ]));
}

/* -------------------------
   Persistence helpers: per-planet saves for deltas
   ------------------------- */

string planet_hash(mapping P) {
    if (!mapp(P)) return "none";
    string raw = P["type"] + ":" + to_string(P["seed"]);
    unsigned long h = 2166136261UL;
    for (int i = 0; i < strlen(raw); i++) {
        h ^= (unsigned long)raw[i];
        h *= 16777619UL;
    }
    return sprintf("%08x", h & 0xffffffff);
}

string perma_file_for(string phash) { return SAVE_DIR + PERMA_PREFIX + phash; }
string temp_file_for(string phash)  { return SAVE_DIR + TEMP_PREFIX + phash; }

void save_planet_deltas(string phash) {
    if (!stringp(phash)) return;
    if (!mapp(PermanentDeltas[phash])) PermanentDeltas[phash] = ([]);
    if (!mapp(TemporaryDeltas[phash])) TemporaryDeltas[phash] = ([]);
    save_object(perma_file_for(phash));
    save_object(temp_file_for(phash));
}

void load_planet_deltas(string phash) {
    string pf = perma_file_for(phash) + ".o";
    string tf = temp_file_for(phash) + ".o";
    if (file_size(pf) > 0) restore_object(perma_file_for(phash));
    else PermanentDeltas[phash] = ([]);
    if (file_size(tf) > 0) restore_object(temp_file_for(phash));
    else TemporaryDeltas[phash] = ([]);
}

/* -------------------------
   Planet management
   ------------------------- */

mixed AddPlanet(string name, mapping params) {
    if (!stringp(name) || !mapp(params)) return 0;
    /* normalize params: require seed,width,height */
    if (!params["seed"]) params["seed"] = 0;
    if (!params["width"] || !params["height"]) return 0;
    params["type"] = name;
    Planets[name] = params;
    string ph = planet_hash(params);
    if (!mapp(PermanentDeltas[ph])) PermanentDeltas[ph] = ([]);
    if (!mapp(TemporaryDeltas[ph])) TemporaryDeltas[ph] = ([]);
    load_planet_deltas(ph);
    /* clear caches touching this planet */
    foreach (string k in keys(value_cache)) {
        if (strsrch(k, ph + ":") == 0) map_delete(value_cache, k);
    }
    foreach (string k in keys(flow_cache)) {
        if (strsrch(k, ph + ":") == 0) map_delete(flow_cache, k);
    }
    foreach (string k in keys(accum_cache)) {
        if (strsrch(k, ph + ":") == 0) map_delete(accum_cache, k);
    }
    foreach (string k in keys(water_mask)) {
        if (strsrch(k, ph + ":") == 0) map_delete(water_mask, k
