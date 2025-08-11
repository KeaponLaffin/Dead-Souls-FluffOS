// /domains/overland/virtual/room.c
inherit "/lib/virtual";
#include <lib.h>

void create() {
    ::create();
    SetClimate("temperate");
}

void Setup(string arg) {
    string planet; int x; int y;
    if(sscanf(arg, "%s:%d,%d", planet, x, y) != 3) return;

    mapping P = PLANETMAP_D->GetPlanet(planet);
    string biome = PLANETMAP_D->GetBiome(x, y, planet);

    SetShort(capitalize(biome));
    SetLong("You are in a region of " + biome + ". Coordinates: ("+x+","+y+") on planet "+planet+".\n");

    // Wrap exits
    int w = P["width"];
    int h = P["height"];
    int nx, ny;

    mapping exits = ([]);
    exits["north"]     = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, x, (y-1+h)%h);
    exits["south"]     = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, x, (y+1)%h);
    exits["east"]      = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, (x+1)%w, y);
    exits["west"]      = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, (x-1+w)%w, y);

    // Diagonals
    exits["northeast"] = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, (x+1)%w, (y-1+h)%h);
    exits["northwest"] = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, (x-1+w)%w, (y-1+h)%h);
    exits["southeast"] = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, (x+1)%w, (y+1)%h);
    exits["southwest"] = sprintf("/domains/overland/virtual/server.c:%s:%d,%d", planet, (x-1+w)%w, (y+1)%h);

    SetExits(exits);
}
