// /daemon/planetmap.c
// Procedural spherical planet map daemon for Dead Souls MUD
// Deterministic height + moisture + climate zones

#include <lib.h>
inherit LIB_DAEMON;

#define DEFAULT_PLANET "earthlike"

mapping planets = ([
    "earthlike" : ([
        "width"       : 200,
        "height"      : 100,
        "seed"        : 42,
        "axial_tilt"  : 23.5,
        "sea_level"   : 0.50,   // normalized height (0.0-1.0)
        "temp_offset" : 0.0,    // global temp shift
    ]),
    "desertworld" : ([
        "width"       : 150,
        "height"      : 75,
        "seed"        : 777,
        "axial_tilt"  : 10.0,
        "sea_level"   : 0.25,
        "temp_offset" : 5.0,
    ]),
]);

// --- Utility: simple deterministic pseudo-random ---
float pnoise(int x, int y, int seed, int prime) {
    int n = x + y * 57 + seed * 131;
    n = (n<<13) ^ n;
    return (1.0 - ((n * (n * n * prime + 15731) + 789221) & 0x7fffffff) / 1073741824.0);
}

// --- Smooth noise ---
float smooth_noise(float x, float y, int seed, int prime) {
    int xi = to_int(x);
    int yi = to_int(y);
    float fracX = x - xi;
    float fracY = y - yi;

    float v1 = pnoise(xi,     yi,     seed, prime);
    float v2 = pnoise(xi + 1, yi,     seed, prime);
    float v3 = pnoise(xi,     yi + 1, seed, prime);
    float v4 = pnoise(xi + 1, yi + 1, seed, prime);

    float i1 = v1 + fracX * (v2 - v1);
    float i2 = v3 + fracX * (v4 - v3);

    return i1 + fracY * (i2 - i1);
}

// --- Fractal noise (Perlin-ish) ---
float perlin_noise(float x, float y, int seed, int prime) {
    float total = 0.0;
    float freq  = 0.02; // scale of features
    float amp   = 1.0;
    float maxVal = 0.0;

    int octaves = 4;
    float persistence = 0.5;

    for (int i = 0; i < octaves; i++) {
        total += smooth_noise(x * freq, y * freq, seed, prime) * amp;
        maxVal += amp;
        amp *= persistence;
        freq *= 2.0;
    }

    return (total / maxVal + 1.0) / 2.0; // normalize 0-1
}

// --- Coordinate wrapping ---
int wrap_x(int x, int w) { return (x % w + w) % w; }
int wrap_y(int y, int h) { return (y % h + h) % h; }

// --- Convert Y to latitude ---
float get_latitude(int y, int height) {
    return ((float)y / height - 0.5) * 180.0; // -90 to +90
}

// --- Climate zone calculation ---
string get_climate_zone(float lat, float axial_tilt) {
    float tropic = axial_tilt;
    if (abs(lat) <= tropic) return "tropical";
    if (abs(lat) <= (90 - tropic)) return "temperate";
    return "polar";
}

// --- Main API: GetHeight ---
float GetHeight(int x, int y, string planet) {
    mapping P = planets[planet];
    x = wrap_x(x, P["width"]);
    y = wrap_y(y, P["height"]);
    return perlin_noise(x, y, P["seed"], 15731);
}

// --- Main API: GetMoisture ---
float GetMoisture(int x, int y, string planet) {
    mapping P = planets[planet];
    x = wrap_x(x, P["width"]);
    y = wrap_y(y, P["height"]);
    return perlin_noise(x, y, P["seed"] + 9999, 31337); // offset seed for different pattern
}

// --- Main API: GetBiome ---
string GetBiome(int x, int y, string planet) {
    mapping P = planets[planet];
    if (!P) P = planets[DEFAULT_PLANET];

    float height   = GetHeight(x, y, planet);
    float moisture = GetMoisture(x, y, planet);
    float lat      = get_latitude(y, P["height"]);
    string climate = get_climate_zone(lat, P["axial_tilt"]);

    float sea = P["sea_level"];

    // Below sea level
    if (height < sea - 0.05) return "deep ocean";
    if (height < sea) return "coast";

    // Above sea level - determine biome from climate + moisture
    switch(climate) {
        case "tropical":
            if (moisture > 0.7) return "rainforest";
            if (moisture > 0.4) return "savanna";
            return "desert";
        case "temperate":
            if (moisture > 0.7) return "forest";
            if (moisture > 0.4) return "grassland";
            return "steppe";
        case "polar":
            if (height > sea + 0.3) return "ice cap";
            return "tundra";
    }
    return "unknown";
}

mapping GetPlanet(string planet) {
    if (!planets[planet]) planet = DEFAULT_PLANET;
    return planets[planet];
}

