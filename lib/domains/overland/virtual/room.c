// /domains/overland/virtual/room.c
#include <lib.h>
#include <rooms.h>
#include <daemons.h>  // For PLANETMAP_D define

inherit LIB_ROOM;

static int XCoord, YCoord;
static string Planet;

void SetCoords(int x, int y, string planet) {
    XCoord = x;
    YCoord = y;
    Planet = planet;
}

static void create() {
    room::create();
    SetClimate("outdoors");
    SetShort("Somewhere on a planet");
    SetLong("You are somewhere overland.");
}

// Checks if the given biome blocks LOS
int BlocksLOS(string biome) {
    switch (biome) {
        case "mountains": return 1;
        case "forest": return 1; // could make partial later
        default: return 0;
    }
}

// Simple LOS algorithm (Bresenham-like) to check if we can see a cell
int HasLineOfSight(int tx, int ty) {
    // Wizard exemption placeholder
    if (this_player() && creatorp(this_player())) return 1;

    int dx = tx - XCoord;
    int dy = ty - YCoord;
    int steps = max(abs(dx), abs(dy));
    float x = XCoord, y = YCoord;
    float xInc = dx / (float)steps;
    float yInc = dy / (float)steps;

    for (int i = 1; i <= steps; i++) {
        x += xInc;
        y += yInc;
        int ix = to_int(round(x));
        int iy = to_int(round(y));

        string biome = PLANETMAP_D->GetBiome(ix, iy, Planet);
        if (i < steps && BlocksLOS(biome)) return 0;
    }
    return 1;
}

string RenderMiniMap(int radius) {
    string out = "";
    mapping P = PLANETMAP_D->GetPlanet(Planet);
    int w = P["width"];
    int h = P["height"];

    // Keep north up: we don't actually rotate coords here, just maintain same axis
    // For more complex rotations, spherical trig can be applied later.

    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int nx = (XCoord + dx) % w; if (nx < 0) nx += w;
            int ny = (YCoord + dy) % h; if (ny < 0) ny += h;

            string ch;
            if (dx == 0 && dy == 0) {
                ch = "@";
            } else if (!HasLineOfSight(nx, ny)) {
                ch = " ";
            } else {
                string biome = PLANETMAP_D->GetBiome(nx, ny, Planet);
                switch (biome) {
                    case "ocean": ch = "~"; break;
                    case "shore": ch = ":"; break;
                    case "plains": ch = "."; break;
                    case "forest": ch = "♣"; break;
                    case "desert": ch = "░"; break;
                    case "hills": ch = "^"; break;
                    case "mountains": ch = "▲"; break;
                    case "tundra": ch = "*"; break;
                    case "ice": ch = "#"; break;
                    default: ch = "?"; break;
                }
            }
            out += ch;
        }
        out += "\n";
    }
    return out;
}

string GetLong() {
    string biome = PLANETMAP_D->GetBiome(XCoord, YCoord, Planet);
    string desc = "You are in a " + biome + ".\n";
    desc += "Coordinates: (" + XCoord + ", " + YCoord + ")\n";
    desc += "Planet: " + Planet + "\n";
    desc += "\n" + RenderMiniMap(5); // radius = 5 → 11x11 map
    return desc;
}

void init() {
    ::init();
}

mixed CanGo(string dir) { return 1; }

string GetExitRoom(string dir) {
    mapping P = PLANETMAP_D->GetPlanet(Planet);
    int w = P["width"];
    int h = P["height"];

    int nx = XCoord;
    int ny = YCoord;

    switch (dir) {
        case "north": ny--; break;
        case "south": ny++; break;
        case "east":  nx++; break;
        case "west":  nx--; break;
        case "northeast": ny--; nx++; break;
        case "northwest": ny--; nx--; break;
        case "southeast": ny++; nx++; break;
        case "southwest": ny++; nx--; break;
    }

    // Wrap on globe
    nx = (nx % w + w) % w;
    ny = (ny % h + h) % h;

    return "/domains/overland/virtual/server:" + nx + "," + ny + "," + Planet;
}
