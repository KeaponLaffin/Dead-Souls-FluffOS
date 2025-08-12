// /domains/overland/virtual/server.c
#include <lib.h>
inherit LIB_VIRT_SERVER;

object GetVirtualObject(string file, string args) {
    string planet;
    int x, y;

    if (sscanf(args, "%d,%d,%s", x, y, planet) != 3) return 0;

    object room = new("/domains/overland/virtual/room");
    room->SetCoords(x, y, planet);

    mapping P = PLANETMAP_D->GetPlanet(planet);

    room->SetShort(capitalize(PLANETMAP_D->GetBiome(x, y, planet)));
    room->SetLong(room->GetLong());

    // 8 directions
    room->AddExit("north",     GetVirtualName(x, y-1, planet));
    room->AddExit("south",     GetVirtualName(x, y+1, planet));
    room->AddExit("east",      GetVirtualName(x+1, y, planet));
    room->AddExit("west",      GetVirtualName(x-1, y, planet));
    room->AddExit("northeast", GetVirtualName(x+1, y-1, planet));
    room->AddExit("northwest", GetVirtualName(x-1, y-1, planet));
    room->AddExit("southeast", GetVirtualName(x+1, y+1, planet));
    room->AddExit("southwest", GetVirtualName(x-1, y+1, planet));

    return room;
}

string GetVirtualName(int x, int y, string planet) {
    mapping P = PLANETMAP_D->GetPlanet(planet);
    int w = P["width"];
    int h = P["height"];

    // Wrap on sphere
    x = (x % w + w) % w;
    y = (y % h + h) % h;

    return "/domains/overland/virtual/server:" + x + "," + y + "," + planet;
}
