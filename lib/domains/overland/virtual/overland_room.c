/* /domains/overland/virtual/overland_room.c
   Virtual overland room template.  The daemon clones this and calls setup_room(x,y).
   Movement is handled by action handlers which create/cloned neighboring rooms on demand.
*/

inherit "/lib/room";

int X;
int Y;

void setup_room(int x, int y) {
    X = x; Y = y;

    // get data from daemon
    object MAP = find_object("/lib/daemons/overland_map");
    if (!MAP) MAP = load_object("/lib/daemons/overland_map");
    if (!MAP) {
        SetShort("An empty void");
        SetLong("The map daemon is missing. Contact admin.\n");
        return;
    }

    int elev = MAP->QueryElevation(x,y);
    string biome = MAP->QueryBiome(x,y);
    int slope = MAP->QuerySlope(x,y);

    SetProperty("coordinates", ({ X, Y }));

    SetShort(capitalize(biome) + " (" + X + "," + Y + ")");
    string desc = "";
    switch(biome) {
        case "ocean":
            desc = "You are at the edge of the wide ocean. Salt air and waves surround you.\n";
            break;
        case "desert":
            desc = "You stand on dry, hot sands stretching to the horizon.\n";
            break;
        case "grassland":
            desc = "A broad grassy plain rolls away in all directions.\n";
            break;
        case "forest":
            desc = "Trees crowd around you; shafts of light break through the canopy.\n";
            break;
        case "hills":
            desc = "Gentle hills rise and fall here.\n";
            break;
        case "rocky_mountain":
            desc = "Sharp rock and cliffs make progress difficult here.\n";
            break;
        case "snow_mountain":
            desc = "Snow and ice dominate; the air is thin and bitter.\n";
            break;
        default:
            desc = "You are on an indistinct stretch of land.\n";
    }

    desc += sprintf("Elevation: %d. Slope: %d.\n", elev, slope);
    desc += "You can travel: north, south, east, west.\n";
    SetLong(desc);

    // clear normal exits — movement via actions
    SetNoClean(1); // keep around while players are here
}

/* add movement commands */
void init() {
    ::init();
    add_action("go_dir", "north");
    add_action("go_dir", "n");
    add_action("go_dir", "south");
    add_action("go_dir", "s");
    add_action("go_dir", "east");
    add_action("go_dir", "e");
    add_action("go_dir", "west");
    add_action("go_dir", "w");
    add_action("do_map", "map");
    add_action("do_coords", "coords");
}

/* mapping of dir -> dx,dy */
int *dir_to_delta(string dir) {
    switch(dir) {
        case "north":
        case "n": return ({0, 1});
        case "south":
        case "s": return ({0, -1});
        case "east":
        case "e": return ({1, 0});
        case "west":
        case "w": return ({-1, 0});
    }
    return ({0,0});
}

/* movement handler */
int go_dir(string arg) {
    string verb = query_verb();
    int *d = dir_to_delta(verb);
    int tx = X + d[0];
    int ty = Y + d[1];

    object MAP = find_object("/lib/daemons/overland_map");
    if (!MAP) MAP = load_object("/lib/daemons/overland_map");
    if (!MAP) {
        write("The map daemon is unavailable. You cannot move.\n");
        return 1;
    }

    // basic impassability by slope or deep ocean
    string tb = MAP->QueryBiome(tx, ty);
    int tslope = MAP->QuerySlope(tx, ty);

    if (tb == "ocean") {
        write("The sea blocks your way. You need a boat to cross.\n");
        return 1;
    }
    if (tslope > 60) { // threshold: tweak to taste
        write("The terrain ahead is too steep to cross.\n");
        return 1;
    }

    // create or fetch the target room and move player
    object target = MAP->MakeRoom(tx, ty);
    if (!target) {
        write("You can't move there right now.\n");
        return 1;
    }

    this_player()->eventMove(target);
    return 1;
}

/* show a simple 3x3 mini-map centered on current tile */
int do_map(string arg) {
    object MAP = find_object("/lib/daemons/overland_map");
    if (!MAP) MAP = load_object("/lib/daemons/overland_map");
    if (!MAP) { write("No map daemon.\n"); return 1; }

    string out = "";
    for (int dy = 1; dy >= -1; dy--) {
        for (int dx = -1; dx <= 1; dx++) {
            int sx = X + dx;
            int sy = Y + dy;
            string b = MAP->QueryBiome(sx, sy);
            string ch = "?";
            switch(b) {
                case "ocean": ch = "~"; break;
                case "desert": ch = ":"; break;
                case "grassland": ch = "."; break;
                case "forest": ch = "T"; break;
                case "hills": ch = "h"; break;
                case "rocky_mountain": ch = "M"; break;
                case "snow_mountain": ch = "^"; break;
            }
            if (dx == 0 && dy == 0) ch = "[" + ch + "]";
            else ch = " " + ch + " ";
            out += ch;
        }
        out += "\n";
    }
    this_player()->eventPrint("Mini-map:\n" + out);
    return 1;
}

/* quick show coords */
int do_coords(string arg) {
    write(sprintf("Coordinates: %d,%d\n", X, Y));
    return 1;
}
