#include "src/shared/Character/CustomizationCatalog.h"

#include <iostream>
#include <string>

#ifndef LCDLLN_DATA_DIR
#define LCDLLN_DATA_DIR "game/data"
#endif

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

int main()
{
    engine::character::CustomizationCatalog cat;
    const std::string racesDir = std::string(LCDLLN_DATA_DIR) + "/races";
    const bool ok = cat.LoadFromDir(racesDir);
    Assert(ok, "catalog loads from data dir");
    Assert(cat.Size() == 8u, "8 races loaded");

    const auto* humains = cat.Find("humains");
    Assert(humains != nullptr, "humains present");
    if (humains)
    {
        Assert(humains->frames.size() == 2u, "humains has 2 frames");
        auto it = humains->modules.find("masculine");
        Assert(it != humains->modules.end(), "humains masculine modules present");
        Assert(it != humains->modules.end() && it->second.faces.size() == 5u, "humains masculine 5 faces");
        Assert(it != humains->modules.end() && it->second.bodyTypes.size() == 3u, "humains masculine 3 body types");
        Assert(humains->skinColorCount == 4u, "humains 4 skin colors (races.json)");
        Assert(humains->hairColorCount == 6u, "humains 6 hair colors (races.json)");
        Assert(humains->eyeColorCount == 4u, "humains 4 eye colors (races.json)");
        Assert(humains->racialFeatures.empty(), "humains has no racial features");
    }

    const auto* demons = cat.Find("demons");
    Assert(demons != nullptr, "demons present");
    if (demons)
    {
        auto h = demons->racialFeatures.find("horns");
        Assert(h != demons->racialFeatures.end() && h->second.size() == 4u, "demons horns has 4 ids");
    }

    Assert(cat.Find("orkh") == nullptr, "unknown race not found");

    return s_fail == 0 ? 0 : 1;
}
