// /lib/daemons/overland_map.c
inherit LIB_DAEMON;

private int world_seed = 1234567; // settable
private mapping chunk_cache = ([]); // key -> chunk mapping
private int CHUNK = 32;

void SetSeed(int s) { world_seed = s; }
int GetSeed() { return world_seed; }

/* --- simple deterministic hash RNG from coords --- */
/* returns pseudo-random float 0..1 for integer inputs */
float hash01(int x, int y, int n) {
    // linear congruential combination
    long h = (long)(x*73856093) ^ (long)(y*19349663) ^ (long)(world_seed*83492791) ^ (n*374761393);
    h = (h ^ (h >> 13)) * 1274126177;
    // keep positive
    if (h < 0) h = -h;
    return (float)(h % 1000000) / 1000000.0;
}

/* linear interpolation */
float lerp(float a, float b, float t) { return a + (b - a) * t; }

/* value noise at integer grid points with bilinear interpolation */
float value_noise(float fx, float fy, int octave) {
    int x0 = to_int(floor(fx));
    int y0 = to_int(floor(fy));
    float sx = fx - x0;
    float sy = fy - y0;

    float n00 = hash01(x0, y0, octave);
    float n10 = hash01(x0+1, y0, octave);
    float n01 = hash01(x0, y0+1, octave);
    float n11 = hash01(x0+1, y0+1, octave);

    float ix0 = lerp(n00, n10, sx);
    float ix1 = lerp(n01, n11, sx);
    return lerp(ix0, ix1, sy);
}

/* fractal sum of value noise (fBm) */
float fractal_height(int x, int y) {
    float total = 0.0;
    float amplitude = 1.0;
    float frequency = 1.0;
    float persistence = 0.5; // amplitude multiplier per octave
    float lacunarity = 2.0;  // frequency multiplier per octave
    int octaves = 5;
    for (int i = 0; i < octaves; i++) {
        float nx = x / (float) (CHUNK * 1) * frequency;
        float ny = y / (float) (CHUNK * 1) * frequency;
        total += value_noise(nx, ny, i) * amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    // normalize: octaves produce in (0..sum(amplitudes)), but since base noise ~[0,1], we roughly normalize:
    return total / (1.0 - pow(persistence, octaves)) * (1.0 - persistence);
}

/* map height to elevation integer (0..MAX_ELEV) */
int get_elevation(int x, int y) {
    float h = fractal_height(x,y);
    int MAX_ELEV = 200; // your vertical resolution
    return to_int(h * MAX_ELEV);
}

/* biome classification */
string get_biome(int x, int y) {
    int elev = get_elevation(x,y);
    int sea_level = 40;
    if (elev <= sea_level) return "ocean";
    if (elev > 160) return "snow_mountain";
    if (elev > 120) return "rocky_mountain";
    if (elev > 90) return "hills";
    // simple moisture via another hash
    float moist = hash01(x, y, 999);
    if (moist < 0.2) return "desert";
    if (moist < 0.45) return "grassland";
    return "forest";
}

/* slope estimation — difference to neighbor heights */
int get_slope(int x, int y) {
    int h = get_elevation(x,y);
    int maxdiff = 0;
    int dx[4] = ({1,-1,0,0});
    int dy[4] = ({0,0,1,-1});
    for (int i=0;i<4;i++) {
        int hh = get_elevation(x+dx[i], y+dy[i]);
        int d = abs(h - hh);
        if (d > maxdiff) maxdiff = d;
    }
    return maxdiff;
}

/* chunking helpers */
string chunk_key(int cx, int cy) { return ""+cx+":"+cy; }
mixed generate_chunk(int cx, int cy) {
    mapping chunk = ([]);
    for (int ox=0; ox<CHUNK; ox++) {
        for (int oy=0; oy<CHUNK; oy++) {
            int x = cx*CHUNK + ox;
            int y = cy*CHUNK + oy;
            chunk[ox + "," + oy] = ({
                "elev": get_elevation(x,y),
                "biome": get_biome(x,y)
            });
        }
    }
    return chunk;
}

mapping GetChunk(int cx, int cy) {
    string key = chunk_key(cx,cy);
    if (!chunk_cache[key]) {
        chunk_cache[key] = generate_chunk(cx,cy);
    }
    return chunk_cache[key];
}

/* API for virtual rooms */
int QueryElevation(int x, int y) {
    int cx = to_int(floor(x/CHUNK));
    int cy = to_int(floor(y/CHUNK));
    mapping c = GetChunk(cx, cy);
    int ox = x - cx*CHUNK;
    int oy = y - cy*CHUNK;
    return c[ox + "," + oy]["elev"];
}
string QueryBiome(int x, int y) {
    int cx = to_int(floor(x/CHUNK));
    int cy = to_int(floor(y/CHUNK));
    mapping c = GetChunk(cx, cy);
    int ox = x - cx*CHUNK;
    int oy = y - cy*CHUNK;
    return c[ox + "," + oy]["biome"];
}
int QuerySlope(int x, int y) { return get_slope(x,y); }
