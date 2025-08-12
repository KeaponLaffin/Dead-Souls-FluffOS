/* /daemon/planetmap.c
   Procedural planet generator with deltas + hydrology (rivers/lakes)
   + BakeHydrology and export helpers.

   Place this file in /daemon/planetmap.c in your Dead Souls fork.
   WARNING: Baking entire large planets can be CPU intensive.
*/

#include <lib.h>
inherit LIB_DAEMON;

#define PI 3.141592653589793
#define SAVE_DIR "/save/planetmap/"
#define PERMA_PREFIX "perma_"
#define TEMP_PREFIX  "temp_"

/* tuning */
#define DEFAULT_OCTAVES 5
#define RIVER_ACCUM_THRESHOLD 40
#define MAX_FLOW_RECURSION 10000

/* persistent */
private mapping Planets;
private mapping PermanentDeltas;
private mapping TemporaryDeltas;

/* runtime caches */
private mapping value_cache;
private mapping flow_cache;
private mapping accum_cache;
private mapping water_mask;

/* neighbour offsets */
static int *NX = ({ -1, 1, 0, 0, -1, 1, -1, 1 });
static int *NY = ({ 0, 0, -1, 1, -1, -1, 1, 1 });

/* -------------------------
   create()
   ------------------------- */
static void create() {
    daemon::create();
    Planets = ([]);
    PermanentDeltas = ([]);
    TemporaryDeltas = ([]);
    value_cache = ([]);
    flow_cache = ([]);
    accum_cache = ([]);
    water_mask = ([]);

    if (file_size(SAVE_DIR) != -2) mkdir(SAVE_DIR);

    /* example planets */
    AddPlanet("earthlike", ([
        "type"        : "earthlike",
        "seed"        : 42,
        "width"       : 200,
        "height"      : 100,
        "axial_tilt"  : 23.5,
        "sea_level"   : 0.50,
        "base_temp"   : 15.0,
        "max_elev_m"  : 8000,
        "lapse_rate"  : 6.5,
        "noise_scale" : 0.007,
        "moisture_scale": 0.02,
        "moisture_sea_influence_radius": 24,
    ]));
}

/* -------------------------
   Persistence helpers
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
string perma_file_for(string phash) { return SAVE_DIR + "perma_" + phash; }
string temp_file_for(string phash)  { return SAVE_DIR + "temp_" + phash; }

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
    if (!params["seed"]) params["seed"] = 0;
    if (!params["width"] || !params["height"]) return 0;
    params["type"] = name;
    Planets[name] = params;
    string ph = planet_hash(params);
    if (!mapp(PermanentDeltas[ph])) PermanentDeltas[ph] = ([]);
    if (!mapp(TemporaryDeltas[ph])) TemporaryDeltas[ph] = ([]);
    load_planet_deltas(ph);
    ClearCaches(name);
    return ph;
}
mapping GetPlanet(string name) { return Planets[name]; }
string *ListPlanets() { return keys(Planets); }

/* -------------------------
   Perlin-ish noise utilities
   ------------------------- */
static float hash_noise(int x, int y, int seed) {
    int n = x + y * 57 + seed * 131;
    n = (n << 13) ^ n;
    float v = 1.0 - (( (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0);
    return v;
}
static float smooth_noise(float x, float y, int seed) {
    int xi = to_int(floor(x));
    int yi = to_int(floor(y));
    float fx = x - xi;
    float fy = y - yi;

    float v1 = hash_noise(xi, yi, seed);
    float v2 = hash_noise(xi+1, yi, seed);
    float v3 = hash_noise(xi, yi+1, seed);
    float v4 = hash_noise(xi+1, yi+1, seed);

    float i1 = v1 + fx * (v2 - v1);
    float i2 = v3 + fx * (v4 - v3);
    return i1 + fy * (i2 - i1);
}
static float fractal_noise(float x, float y, int seed, float base_scale, int octaves) {
    float total = 0.0, freq = base_scale, amp = 1.0, maxAmp = 0.0, persistence = 0.5;
    for (int i = 0; i < octaves; i++) {
        total += smooth_noise(x * freq, y * freq, seed) * amp;
        maxAmp += amp;
        amp *= persistence;
        freq *= 2.0;
    }
    if (maxAmp == 0.0) return 0.0;
    return (total / maxAmp + 1.0) / 2.0;
}

/* -------------------------
   Coordinate helpers
   ------------------------- */
static int wrap_x(int x, mapping P) {
    int w = P["width"];
    if (!w) return x;
    x %= w; if (x < 0) x += w;
    return x;
}
static int wrap_y(int y, mapping P) {
    int h = P["height"];
    if (!h) return y;
    y %= h; if (y < 0) y += h;
    return y;
}
static float latitude_for_y(int y, mapping P) {
    float h = (float)P["height"];
    return ((float)y / h - 0.5) * 180.0;
}

/* -------------------------
   Cached tile properties
   ------------------------- */
static string value_key(string ph, string kind, int x, int y) {
    return ph + ":" + kind + ":" + to_string(x) + "," + to_string(y);
}

float GetHeight(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0.0;
    int x = wrap_x(x_in, P), y = wrap_y(y_in, P);
    string ph = planet_hash(P);
    string ck = value_key(ph, "h", x, y);
    if (!undefinedp(value_cache[ck])) return value_cache[ck];

    float scale = P["noise_scale"] ? P["noise_scale"] : 0.008;
    int seed = P["seed"];
    float raw = fractal_noise((float)x, (float)y, seed, scale, DEFAULT_OCTAVES);

    float lat = latitude_for_y(y, P) / 90.0;
    float latfactor = cos(lat * PI / 2.0);
    float height = raw * (0.6 + 0.4 * latfactor);
    if (height < 0.0) height = 0.0;
    if (height > 1.0) height = 1.0;
    value_cache[ck] = height;
    return height;
}

float GetTemperature(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0.0;
    int x = wrap_x(x_in, P), y = wrap_y(y_in, P);
    string ph = planet_hash(P);
    string ck = value_key(ph, "t", x, y);
    if (!undefinedp(value_cache[ck])) return value_cache[ck];

    float base = P["base_temp"] ? P["base_temp"] : 15.0;
    float lat = latitude_for_y(y, P);
    float latrad = lat * PI / 180.0;
    float latfactor = cos(latrad);
    float temp = base * (0.5 + 0.5 * latfactor);

    float elevNorm = GetHeight(planet_name, x, y);
    float max_elev = P["max_elev_m"] ? P["max_elev_m"] : 8000.0;
    float elevm = elevNorm * max_elev;
    float lapse = P["lapse_rate"] ? P["lapse_rate"] : 6.5;
    temp -= (lapse * (elevm / 1000.0));

    if (elevNorm <= (P["sea_level"] ? P["sea_level"] : 0.5)) {
        temp = temp * 0.85 + base * 0.15;
    }

    value_cache[ck] = temp;
    return temp;
}

float GetMoisture(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0.0;
    int x = wrap_x(x_in, P), y = wrap_y(y_in, P);
    string ph = planet_hash(P);
    string ck = value_key(ph, "m", x, y);
    if (!undefinedp(value_cache[ck])) return value_cache[ck];

    float mscale = P["moisture_scale"] ? P["moisture_scale"] : 0.02;
    int seed = P["seed"] + 10000;
    float raw = fractal_noise((float)x, (float)y, seed, mscale, 4);

    int prox = P["moisture_sea_influence_radius"] ? P["moisture_sea_influence_radius"] : 20;
    int dsea = distance_to_sea(planet_name, x, y, prox);
    if (dsea <= prox) {
        float sea_infl = (float)(prox - dsea) / (float)prox;
        raw += (0.5 * sea_infl);
    }

    float elev = GetHeight(planet_name, x, y);
    raw *= (1.0 - 0.4 * elev);

    float temp = GetTemperature(planet_name, x, y);
    float tf = (temp + 40.0) / 80.0;
    if (tf < 0.1) tf = 0.1;
    if (tf > 1.5) tf = 1.5;
    raw *= tf;

    if (raw < 0.0) raw = 0.0;
    if (raw > 1.0) raw = 1.0;
    value_cache[ck] = raw;
    return raw;
}

/* distance to sea */
int distance_to_sea(string planet_name, int sx, int sy, int radius_limit) {
    mapping P = Planets[planet_name];
    float sea = P["sea_level"] ? P["sea_level"] : 0.5;
    for (int r = 0; r <= radius_limit; r++) {
        for (int dx = -r; dx <= r; dx++) {
            int dy = r;
            int tx = wrap_x(sx + dx, P);
            int ty = wrap_y(sy + dy, P);
            if (GetHeight(planet_name, tx, ty) <= sea) return r;
            ty = wrap_y(sy - dy, P);
            if (GetHeight(planet_name, tx, ty) <= sea) return r;
        }
        for (int dy2 = -r+1; dy2 <= r-1; dy2++) {
            int dx2 = r;
            int tx = wrap_x(sx + dx2, P);
            int ty = wrap_y(sy + dy2, P);
            if (GetHeight(planet_name, tx, ty) <= sea) return r;
            tx = wrap_x(sx - dx2, P);
            if (GetHeight(planet_name, tx, ty) <= sea) return r;
        }
    }
    return radius_limit + 1;
}

/* -------------------------
   Biome classification
   ------------------------- */
string GetClimateZone(string planet_name, int x, int y) {
    mapping P = Planets[planet_name];
    float lat = latitude_for_y(y, P);
    float tilt = P["axial_tilt"] ? P["axial_tilt"] : 23.5;
    float a = (lat < 0.0) ? -lat : lat;
    if (a <= tilt) return "tropical";
    if (a <= (90.0 - tilt)) return "temperate";
    return "polar";
}

string GetBiome(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return "unknown";
    int x = wrap_x(x_in, P);
    int y = wrap_y(y_in, P);
    string ph = planet_hash(P);

    /* check permanent delta override */
    if (mapp(PermanentDeltas[ph])) {
        string dk = sprintf("%d,%d", x, y);
        if (mapp(PermanentDeltas[ph][dk]) && PermanentDeltas[ph][dk]["biome"]) {
            return PermanentDeltas[ph][dk]["biome"];
        }
    }

    float elev = GetHeight(planet_name, x, y);
    float moist = GetMoisture(planet_name, x, y);
    float temp = GetTemperature(planet_name, x, y);
    float sea = P["sea_level"] ? P["sea_level"] : 0.5;
    string climate = GetClimateZone(planet_name, x, y);

    /* water first */
    if (elev <= sea) {
        float depth = (sea - elev) / (sea > 0 ? sea : 1.0);
        if (depth > 0.5) return "deep_ocean";
        return "coastal_water";
    }

    /* water mask (river/lake) overrides non-aquatic biomes */
    string wmkey = ph + ":wm:" + sprintf("%d,%d", x, y);
    if (!undefinedp(water_mask[wmkey]) && water_mask[wmkey]) {
        return water_mask[wmkey]; /* "river" or "lake" */
    }

    /* mountains */
    if (elev > 0.78) {
        if (temp < -8.0) return "snow_peak";
        return "alpine";
    }

    /* polar/cold */
    if (temp <= -12.0) return "polar_ice";
    if (temp <= 0.0) {
        if (moist < 0.25) return "tundra";
        return "taiga";
    }

    if (climate == "tropical") {
        if (moist > 0.75) return "tropical_rainforest";
        if (moist > 0.45) return "savanna";
        return "hot_desert";
    }

    if (climate == "temperate") {
        if (moist > 0.7) return "temperate_rainforest";
        if (moist > 0.45) return "temperate_forest";
        if (moist > 0.25) return "grassland";
        return "temperate_steppe";
    }

    return "unknown";
}

/* -------------------------
   Hydrology core (flow, end, accumulation, water mask)
   ------------------------- */

mixed ComputeFlowTarget(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    int x = wrap_x(x_in, P);
    int y = wrap_y(y_in, P);
    string ph = planet_hash(P);
    string fkey = ph + ":flow:" + sprintf("%d,%d", x, y);
    if (!undefinedp(flow_cache[fkey])) return flow_cache[fkey];

    float h = GetHeight(planet_name, x, y);
    float sea = P["sea_level"] ? P["sea_level"] : 0.5;
    if (h <= sea) { flow_cache[fkey] = "sea"; return "sea"; }

    float best_h = h;
    int best_tx = x, best_ty = y;
    for (int i = 0; i < sizeof(NX); i++) {
        int tx = wrap_x(x + NX[i], P);
        int ty = wrap_y(y + NY[i], P);
        float th = GetHeight(planet_name, tx, ty);
        if (th < best_h) { best_h = th; best_tx = tx; best_ty = ty; }
    }

    if (best_tx == x && best_ty == y) {
        flow_cache[fkey] = "pool";
        return "pool";
    }

    flow_cache[fkey] = ({ best_tx, best_ty });
    return ({ best_tx, best_ty });
}

mixed DetermineFlowEnd(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    int x0 = wrap_x(x_in, P);
    int y0 = wrap_y(y_in, P);
    string ph = planet_hash(P);
    string endkey = ph + ":end:" + sprintf("%d,%d", x0, y0);
    if (!undefinedp(flow_cache[endkey])) return flow_cache[endkey];

    int steps = 0;
    int cx = x0, cy = y0;
    mapping visited = ([]);
    string result = 0;
    while (1) {
        steps++;
        if (steps > MAX_FLOW_RECURSION) { result = "loop"; break; }
        string seq_key = sprintf("%d,%d", cx, cy);
        if (visited[seq_key]) { result = "loop"; break; }
        visited[seq_key] = steps;

        mixed t = ComputeFlowTarget(planet_name, cx, cy);
        if (stringp(t)) { result = t; break; }
        if (arrayp(t) && sizeof(t) == 2) {
            int nx = t[0], ny = t[1];
            string nk = ph + ":end:" + sprintf("%d,%d", nx, ny);
            if (!undefinedp(flow_cache[nk])) { result = flow_cache[nk]; break; }
            cx = nx; cy = ny; continue;
        }
        result = "loop"; break;
    }

    foreach (string pcoord in keys(visited)) {
        flow_cache[ph + ":end:" + pcoord] = result;
    }
    flow_cache[endkey] = result;
    return result;
}

int ComputeAccumulation(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    int x = wrap_x(x_in, P), y = wrap_y(y_in, P);
    string ph = planet_hash(P);
    string akey = ph + ":acc:" + sprintf("%d,%d", x, y);
    if (!undefinedp(accum_cache[akey])) return accum_cache[akey];

    int acc = 1;
    for (int i = 0; i < sizeof(NX); i++) {
        int ux = wrap_x(x + NX[i], P);
        int uy = wrap_y(y + NY[i], P);
        mixed t = ComputeFlowTarget(planet_name, ux, uy);
        if (arrayp(t) && t[0] == x && t[1] == y) {
            acc += ComputeAccumulation(planet_name, ux, uy);
        }
    }

    accum_cache[akey] = acc;
    return acc;
}

string DetermineWaterMask(string planet_name, int x_in, int y_in) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    int x = wrap_x(x_in, P), y = wrap_y(y_in, P);
    string ph = planet_hash(P);
    string wmkey = ph + ":wm:" + sprintf("%d,%d", x, y);
    if (!undefinedp(water_mask[wmkey])) return water_mask[wmkey];

    float sea = P["sea_level"] ? P["sea_level"] : 0.5;
    float h = GetHeight(planet_name, x, y);
    if (h <= sea) { water_mask[wmkey] = "ocean"; return "ocean"; }

    mixed end = DetermineFlowEnd(planet_name, x, y);
    if (stringp(end) && end == "sea") {
        int acc = ComputeAccumulation(planet_name, x, y);
        if (acc >= RIVER_ACCUM_THRESHOLD) { water_mask[wmkey] = "river"; return "river"; }
    } else if (stringp(end) && (end == "pool" || end == "loop")) {
        int cx = x, cy = y, steps = 0;
        while (1) {
            steps++;
            if (steps > MAX_FLOW_RECURSION) break;
            mixed t = ComputeFlowTarget(planet_name, cx, cy);
            if (stringp(t) && (t == "pool")) {
                string pkey = ph + ":wm:" + sprintf("%d,%d", cx, cy);
                water_mask[pkey] = "lake";
                if (pkey == wmkey) { water_mask[wmkey] = "lake"; return "lake"; }
                break;
            } else if (arrayp(t)) { cx = t[0]; cy = t[1]; continue; }
            else break;
        }
        int acc = ComputeAccumulation(planet_name, x, y);
        if (acc >= 6) { water_mask[wmkey] = "lake"; return "lake"; }
    }

    water_mask[wmkey] = 0;
    return 0;
}

mapping GetHydrology(string planet_name, int x, int y) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    int xx = wrap_x(x, P), yy = wrap_y(y, P);
    string ph = planet_hash(P);
    string wmkey = ph + ":wm:" + sprintf("%d,%d", xx, yy);
    string w = DetermineWaterMask(planet_name, xx, yy);
    int acc = ComputeAccumulation(planet_name, xx, yy);
    mixed end = DetermineFlowEnd(planet_name, xx, yy);
    return ([ "water": w, "acc": acc, "end": end ]);
}

/* -------------------------
   Delta layer APIs
   ------------------------- */
void SetPermanentDelta(string planet_name, int x, int y, mapping change) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return;
    string ph = planet_hash(P);
    if (!mapp(PermanentDeltas[ph])) PermanentDeltas[ph] = ([]);
    string key = sprintf("%d,%d", wrap_x(x,P), wrap_y(y,P));
    PermanentDeltas[ph][key] = change;
    save_planet_deltas(ph);
    map_delete(value_cache, ph + ":b:" + key);
    map_delete(flow_cache, ph + ":flow:" + key);
    map_delete(flow_cache, ph + ":end:" + key);
    map_delete(accum_cache, ph + ":acc:" + key);
    map_delete(water_mask, ph + ":wm:" + key);
}

void RemovePermanentDelta(string planet_name, int x, int y) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return;
    string ph = planet_hash(P);
    string key = sprintf("%d,%d", wrap_x(x,P), wrap_y(y,P));
    if (mapp(PermanentDeltas[ph])) map_delete(PermanentDeltas[ph], key);
    save_planet_deltas(ph);
    map_delete(value_cache, ph + ":b:" + key);
}

/* temporary deltas */
void SetTemporaryDelta(string planet_name, int x, int y, mapping change) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return;
    string ph = planet_hash(P);
    if (!mapp(TemporaryDeltas[ph])) TemporaryDeltas[ph] = ([]);
    string key = sprintf("%d,%d", wrap_x(x,P), wrap_y(y,P));
    TemporaryDeltas[ph][key] = change;
    save_planet_deltas(ph);
}
void RemoveTemporaryDelta(string planet_name, int x, int y) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return;
    string ph = planet_hash(P);
    string key = sprintf("%d,%d", wrap_x(x,P), wrap_y(y,P));
    if (mapp(TemporaryDeltas[ph])) map_delete(TemporaryDeltas[ph], key);
    save_planet_deltas(ph);
}
mapping QueryTemporaryDelta(string planet_name, int x, int y) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    string ph = planet_hash(P);
    string key = sprintf("%d,%d", wrap_x(x,P), wrap_y(y,P));
    if (mapp(TemporaryDeltas[ph]) && mapp(TemporaryDeltas[ph][key])) return TemporaryDeltas[ph][key];
    return 0;
}

/* -------------------------
   High-level room API
   ------------------------- */
mapping GetRoomData(string planet_name, int x, int y) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    int xx = wrap_x(x, P), yy = wrap_y(y, P);
    mapping out = ([]);
    out["height"] = GetHeight(planet_name, xx, yy);
    out["temperature"] = GetTemperature(planet_name, xx, yy);
    out["moisture"] = GetMoisture(planet_name, xx, yy);
    out["permanent"] = (PermanentDeltas[planet_hash(P)] && PermanentDeltas[planet_hash(P)][sprintf("%d,%d",xx,yy)]) ? PermanentDeltas[planet_hash(P)][sprintf("%d,%d",xx,yy)] : 0;
    out["temporary"] = QueryTemporaryDelta(planet_name, xx, yy);
    out["hydrology"] = GetHydrology(planet_name, xx, yy);
    out["biome"] = GetBiome(planet_name, xx, yy);
    return out;
}

/* -------------------------
   BakeHydrology + Export helpers
   ------------------------- */

/* BakeHydrology computes flow/accumulation/watermask for all tiles on a planet.
   If export_to_file is non-zero, writes an ASCII water mask to /tmp/<planet>_water.txt
   Returns number of tiles processed, or -1 on error.
*/
int BakeHydrology(string planet_name, int export_to_file) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return -1;
    int w = P["width"], h = P["height"];
    string ph = planet_hash(P);

    /* clear planet caches first */
    foreach (string k in keys(value_cache)) if (strsrch(k, ph + ":") == 0) map_delete(value_cache, k);
    foreach (string k in keys(flow_cache)) if (strsrch(k, ph + ":") == 0) map_delete(flow_cache, k);
    foreach (string k in keys(accum_cache)) if (strsrch(k, ph + ":") == 0) map_delete(accum_cache, k);
    foreach (string k in keys(water_mask)) if (strsrch(k, ph + ":") == 0) map_delete(water_mask, k);

    int total = 0;
    /* iterate all tiles and force hydrology computations */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* compute flow end (memoized inside), accumulation and water mask */
            DetermineFlowEnd(planet_name, x, y);
            ComputeAccumulation(planet_name, x, y);
            DetermineWaterMask(planet_name, x, y);
            total++;
        }
    }

    /* optionally export to file for debugging */
    if (export_to_file) {
        string fname = "/tmp/" + planet_name + "_water.txt";
        string out = ExportWaterMask(planet_name, fname);
        if (!out) return total; /* baked but export failed */
    }

    return total;
}

/* Export the water mask as ASCII to provided filename.
   Format: rows top-to-bottom (y=0..h-1), columns left-to-right (x=0..w-1).
   Characters: '~' ocean, 'r' river, 'l' lake, '.' land.
   Returns filename on success, 0 on error.
*/
string ExportWaterMask(string planet_name, string filename) {
    mapping P = Planets[planet_name];
    if (!mapp(P)) return 0;
    int w = P["width"], h = P["height"];
    string ph = planet_hash(P);

    string buf = "";
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            string wm = DetermineWaterMask(planet_name, x, y);
            string ch = ".";
            if (wm == "ocean") ch = "~";
            else if (wm == "river") ch = "r";
            else if (wm == "lake") ch = "l";
            buf += ch;
        }
        buf += "\n";
    }

    if (!filename) filename = "/tmp/" + planet_name + "_water.txt";
    if (catch(write_file(filename, buf))) return 0;
    return filename;
}

void BakeAndSave(string planet_name) {
    int count = BakeHydrology(planet_name, 1);
    mapping P = Planets[planet_name];
    if (!mapp(P)) return;
    string ph = planet_hash(P);
    save_planet_deltas(ph);
    return;
}

/* -------------------------
   Cache & admin helpers
   ------------------------- */
void ClearCaches(string planet_name) {
    if (!stringp(planet_name)) {
        value_cache = ([]); flow_cache = ([]); accum_cache = ([]); water_mask = ([]);
        return;
    }
    mapping P = Planets[planet_name];
    if (!mapp(P)) return;
    string ph = planet_hash(P);
    foreach (string k in keys(value_cache)) if (strsrch(k, ph + ":") == 0) map_delete(value_cache, k);
    foreach (string k in keys(flow_cache)) if (strsrch(k, ph + ":") == 0) map_delete(flow_cache, k);
    foreach (string k in keys(accum_cache)) if (strsrch(k, ph + ":") == 0) map_delete(accum_cache, k);
    foreach (string k in keys(water_mask)) if (strsrch(k, ph + ":") == 0) map_delete(water_mask, k);
}

void ShowTile(string planet_name, int x, int y) {
    mapping d = GetRoomData(planet_name, x, y);
    if (!d) { write("No such planet: " + planet_name + "\n"); return; }
    write(sprintf("Tile %s:%d,%d\n", planet_name, x, y));
    write(sprintf(" Height: %.3f  Temp: %.2fC  Moist: %.3f\n", d["height"], d["temperature"], d["moisture"]));
    write(" Biome: " + d["biome"] + "\n");
    if (mapp(d["permanent"])) write(" Permanent delta: " + identify(d["permanent"]) + "\n");
    if (mapp(d["temporary"])) write(" Temporary delta: " + identify(d["temporary"]) + "\n");
    mapping hyd = d["hydrology"];
    if (mapp(hyd)) {
        write(" Hydrology: water=" + hyd["water"] + " acc=" + hyd["acc"] + " end=" + identify(hyd["end"]) + "\n");
    }
}

/* -------------------------
   End of planetmap.c
   ------------------------- */
