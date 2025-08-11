// /domains/overland/virtual/overland_room.c
inherit "/lib/virtual";

int x,y;

void setup_room(int px, int py) {
    x = px; y = py;
    string biome = MAP_D->QueryBiome(x,y);
    int elev = MAP_D->QueryElevation(x,y);
    int slope = MAP_D->QuerySlope(x,y);

    SetShort(capitalize(biome)+" at "+x+","+y);
    string desc = "You stand on a "+biome+" at elevation "+elev+".\n";
    if (slope > 15) desc += "The ground is steep here.";
    SetLong(desc);

    // movement rules
    mapping exits = ([]);
    if (slope <= 40) exits["north"] = "/domains/overland/virtual/room"->GetVirtual(x, y+1);
    // similar checks for other directions...
    SetExits(exits);

    // movement cost — override walk messages or use stamina system
    SetProperty("overland_coords", ({x,y}));
}
