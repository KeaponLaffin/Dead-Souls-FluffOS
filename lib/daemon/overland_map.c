/* /lib/daemons/overland_map.c
   Simple deterministic chunked map daemon.
   API:
     object MakeRoom(int x, int y);      // create and return a room object for given coords
     int QueryElevation(int x, int y);   // elevation 0..MAX_ELEV
     string QueryBiome(int x, int y);    // biome name
     int QuerySlope(int x, int y);       // rough slope measure
*/
inherit "/lib/daemon";   // adapt if your DeadSouls has a different daemon parent

private int world_seed = 1234567;
private mapping chunk_cache = ([]);    // "cx:cy" -> mapping of "ox,oy" -> ({elev,biome})
private int CHUNK = 32;
private int MAX_ELEV = 200;            // vertical resolution (tweak)
private int CHUNK_LRU_LIMIT = 200;

void create() {
    daemon::create();
    // optionally seed from config or env
}

/* ---------------------
   cheap deterministic hash -> 0..1000
   --------------------- */
int coord_hash(int x, int y, int n) {
    // mix coords with seed and octave
    long h = (long)x * 374761393L + (long)y * 668265263L + (long)world_seed * 1274126177L + (long)n * 2654435761L;
    h = (h ^ (h >> 13)) * 1274126177L;
    if (h < 0) h = -h;
    return to_int(h % 1001); // 0..1000
}

/* bilinear interpolation between four integer samples (0..1000),
   returns 0..1000 */
int bilerp(int n00, int n10, int n01, int n11, int sx, int sy) {
    // sx,sy scaled 0..1000
    int ix0 = n00 + ((n10 - n00) * sx) / 1000;
    int ix1 = n01 + ((n11 - n01) * sx) / 1000;
    return ix0 + ((ix1 - ix0) * sy) / 1000;
}

/* value-noise style sample for arbitrary fx,fy using integer arithmetic.
   fx,fy are grid coordinates scaled by 1 (integers). For fractal we pass
   fractional coords by dividing. We emulate fractional using integer math:
   call with base grid positions and fraction as separate ints (ugly but
   keeps no floats). We'll expose a wrapper fractal that uses integer math. */
int value_noise_scaled(int gx, int gy, int fracx, int fracy, int octave) {
    // gx,gy are integer grid coords for bottom-left
    // fracx,fracy range 0..1000
    int n00 = coord_hash(gx,   gy,   octave);
    int n10 = coord_hash(gx+1, gy,   octave);
    int n01 = coord_hash(gx,   gy+1, octave);
    int n11 = coord_hash(gx+1, gy+1, octave);
    return bilerp(n00, n10, n01, n11, fracx, fracy);
}

/* fractal fBm using integer math:
   returns 0..1000 */
int fractal_height_int(int x, int y) {
    int octaves = 5;
    int persistence_scaled = 500; // 0..1000 => 0.5
    int total = 0;
    int amplitude = 1000;     // scaled amplitude
    int frequency = 1;
    int maxAmplitude = 0;

    for (int i = 0; i < octaves; i++) {
        // produce sample at scale (we'll treat frequency as integer scaling of coords)
        int gx = x / frequency;
        int gy = y / frequency;
        int fracx = ( (x % frequency) * (1000 / (frequency==0?1:frequency)) );
        int fracy = ( (y % frequency) * (1000 / (frequency==0?1:frequency)) );
        // to avoid div-by-zero for frequency==1, frac becomes 0..999
        // (this crude approach is fine for a demo)
        int sample = value_noise_scaled(gx, gy, fracx, fracy, i);
        total += (sample * amplitude) / 1000;
        maxAmplitude += amplitude;
        amplitude = to_int((amplitude * persistence_scaled) / 1000);
        frequency = frequency * 2;
        if (frequency < 1) frequency = 1;
    }
    if (maxAmplitude == 0) return 0;
    return to_int((total * 1000) / maxAmplitude); // normalize to 0..1000
}

/* map height 0..1000 -> elevation 0..MAX_ELEV */
int get_elevation(int x, int y) {
    int h = fractal_height_int(x,y); // 0..1000
    return to_int((h * MAX_ELEV) / 1000);
}

/* biome classification */
string classify_biome(int x, int y) {
    int elev = get_elevation(x,y);
    int sea_level = to_int(MAX_ELEV * 0.20); // 20% sea by default
    if (elev <= sea_level) return "ocean";
    if (elev > to_int(MAX_ELEV * 0.85)) return "snow_mountain";
    if (elev > to_int(MAX_ELEV * 0.60)) return "rocky_mountain";
    if (elev > to_int(MAX_ELEV * 0.45)) return "hills";
    int moist = coord_hash(x, y, 999);
    if (moist < 150) return "desert";
    if (moist < 500) return "grassland";
    return "forest";
}

/* slope estimate = max abs diff to 4 neighbours */
int get_slope(int x, int y) {
    int h = get_elevation(x,y);
    int maxdiff = 0;
    int dx[4] = ({1,-1,0,0});
    int dy[4] = ({0,0,1,-1});
    for (int i = 0; i < 4; i++) {
        int hh = get_elevation(x + dx[i], y + dy[i]);
        int d = (h > hh) ? (h - hh) : (hh - h);
        if (d > maxdiff) maxdiff = d;
    }
    return maxdiff;
}

/* chunk functions */
string chunk_key(int cx, int cy) { return sprintf("%d:%d", cx, cy); }

mapping generate_chunk(int cx, int cy) {
    mapping chunk = ([]);
    for (int ox = 0; ox < CHUNK; ox++) {
        for (int oy = 0; oy < CHUNK; oy++) {
            int gx = cx * CHUNK + ox;
            int gy = cy * CHUNK + oy;
            chunk[sprintf("%d,%d", ox, oy)] = ([
                "elev": get_elevation(gx, gy),
                "biome": classify_biome(gx, gy)
            ]);
        }
    }
    return chunk;
}

mapping GetChunk(int cx, int cy) {
    string key = chunk_key(cx, cy);
    if (!mapp(chunk_cache[key])) {
        chunk_cache[key] = generate_chunk(cx, cy);
        // LRU naive trimming
        if (sizeof(keys(chunk_cache)) > CHUNK_LRU_LIMIT) {
            string *k = keys(chunk_cache);
            map_delete(chunk_cache, k[0]); // naive: remove first key
        }
    }
    return chunk_cache[key];
}

/* public queries */
int QueryElevation(int x, int y) {
    int cx = x / CHUNK;
    int cy = y / CHUNK;
    mapping c = GetChunk(cx, cy);
    int ox = x - cx * CHUNK;
    int oy = y - cy * CHUNK;
    return c[sprintf("%d,%d", ox, oy)]["elev"];
}

string QueryBiome(int x, int y) {
    int cx = x / CHUNK;
    int cy = y / CHUNK;
    mapping c = GetChunk(cx, cy);
    int ox = x - cx * CHUNK;
    int oy = y - cy * CHUNK;
    return c[sprintf("%d,%d", ox, oy)]["biome"];
}

int QuerySlope(int x, int y) { return get_slope(x,y); }

/* MakeRoom: clone the virtual room template and call setup */
object MakeRoom(int x, int y) {
    object room;
    string path = "/domains/overland/virtual/overland_room";
    // clone template
    room = clone_object(path);
    if (!room) return 0;
    room->setup_room(x, y);
    return room;
}

/* helper to set seed if you want reproducible worlds */
void SetSeed(int s) { world_seed = s; }
int GetSeed() { return world_seed; }
