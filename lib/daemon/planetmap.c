// /daemon/planetmap.c
// Procedural spherical planet terrain generator for Dead Souls
// Works with virtual room system

inherit DAEMON;

mapping planets = ([
    "earthlike" : ([
        "width"       : 200,     // tiles east-west
        "height"      : 100,     // tiles north-south
        "seed"        : 42,      // random seed for terrain generation
        "axial_tilt"  : 23.5,    // degrees
        "sea_level"   : 0.5,     // 0.0-1.0, fraction of height
        "temp"        : 15.0,    // avg surface temp C
    ]),
    "desert" : ([
        "width"       : 150,
        "height"      : 75,
        "seed"        : 777,
        "axial_tilt"  : 10.0,
        "sea_level"   : 0.2,
        "temp"        : 35.0,
    ]),
]);

// --- Simple deterministic noise function (Perlin-like) ---
float hash_noise(int x, int y, int seed) {
    int n = x + y * 57 + seed * 131;
    n = (n<<13) ^ n;
    return (1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0);
}

float smooth_noise(int x, int y, int seed) {
    float corners = (hash_noise(x-1,y-1,seed)+hash_noise(x+1,y-1,seed)+hash_noise(x-1,y+1,seed)+hash_noise(x+1,y+1,seed)) / 16;
    float sides   = (hash_noise(x-1,y,seed)+hash_noise(x+1,y,seed)+hash_noise(x,y-1,seed)+hash_noise(x,y+1,seed)) / 8;
    float center  = hash_noise(x,y,seed) / 4;
    return corners + sides + center;
}

float interpolate(float a, float b, float x) {
    float ft = x * 3.1415927;
    float f = (1 - cos(ft)) * 0.5;
    return  a*(1-f) + b*f;
}

float interpolated_noise(float x, float y, int seed) {
    int intX = to_int(x);
    float fracX = x - intX;
    int intY = to_int(y);
    float fracY = y - intY;

    float v1 = smooth_noise(intX, intY, seed);
    float v2 = smooth_noise(intX+1, intY, seed);
    float v3 = smooth_noise(intX, intY+1, seed);
    float v4 = smooth_noise(intX+1, intY+1, seed);

    float i1 = interpolate(v1, v2, fracX);
    float i2 = interpolate(v3, v4, fracX);

    return interpolate(i1, i2, fracY);
}

float perlin_noise(float x, float y, int seed) {
    float total = 0;
    float persistence = 0.5;
    int octaves = 6;
    for(int i=0; i<octaves; i++) {
        float frequency = pow(2, i);
        float amplitude = pow(persistence, i);
        total += interpolated_noise(x * frequency, y * frequency, seed) * amplitude;
    }
    return total;
}

// Wrap coordinates for spherical mapping
int wrap_x(int x, string planet) {
    int w = planets[planet]["width"];
    if(x < 0) return x + w;
    if(x >= w) return x - w;
    return x;
}

int wrap_y(int y, string planet) {
    int h = planets[planet]["height"];
    if(y < 0) return y + h;
    if(y >= h) return y - h;
    return y;
}

// Get height at coordinate
float GetHeight(int x, int y, string planet) {
    if(undefinedp(planets[planet])) return 0.0;
    x = wrap_x(x, planet);
    y = wrap_y(y, planet);

    mapping P = planets[planet];
    float nx = (float)x / P["width"] - 0.5;
    float ny = (float)y / P["height"] - 0.5;

    // Adjust for latitude (simulate polar regions)
    float lat_factor = cos(ny * 3.1415927);

    float noise = perlin_noise(x, y, P["seed"]);
    noise = (noise + 1.0) / 2.0; // normalize 0..1
    return noise * lat_factor;
}

// Map height to biome string
string GetBiome(int x, int y, string planet) {
    float h = GetHeight(x, y, planet);
    float sea = planets[planet]["sea_level"];

    if(h < sea * 0.6) return "deep ocean";
    if(h < sea) return "coastal waters";
    if(h < sea + 0.05) return "beach";
    if(h < sea + 0.3) return "plains";
    if(h < sea + 0.5) return "hills";
    return "mountains";
}

mapping GetPlanet(string planet) { return planets[planet]; }
