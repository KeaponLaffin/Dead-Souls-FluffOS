// /domains/overland/virtual/room.c
#include <lib.h>
#include <rooms.h>

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

string GetLong() {
    string biome = PLANETMAP_D->GetBiome(XCoord, YCoord, Planet);
    string desc = "You are in a " + biome + ".\n";

    desc += "Coordinates: (" + XCoord + ", " + YCoord + ")\n";
    desc += "Planet: " + Planet + "\n";
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
