// /domains/overland/virtual/server.c
inherit "/lib/virtual/server";

string base = "/domains/overland/virtual/room";

string GetVirtual(string arg) {
    // arg will be like "earthlike:123,45"
    string planet; int x; int y;
    if(sscanf(arg, "%s:%d,%d", planet, x, y) != 3) return 0;
    return base;
}
