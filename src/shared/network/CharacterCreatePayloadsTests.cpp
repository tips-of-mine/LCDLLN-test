// Round-trip du payload CHARACTER_CREATE incluant le bloc customization versionné.
#include "src/shared/network/CharacterPayloads.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

using namespace engine::network;

int main()
{
    CharacterCustomization c;
    c.faceType = 2; c.hairStyle = 4; c.skinColorIdx = 1; c.hairColorIdx = 3; c.eyeColorIdx = 5;
    c.bodyFrame = 1; c.bodyType = 2; c.facialHair = 3;
    c.racialFeatures = { {"horns", 2}, {"tails", 1} };

    auto buf = BuildCharacterCreateRequestPayload("Alyx", "demons", "warrior", c);
    Assert(!buf.empty(), "build create payload not empty");

    auto parsed = ParseCharacterCreateRequestPayload(buf.data(), buf.size());
    Assert(parsed.has_value(), "parse create payload ok");
    if (parsed)
    {
        Assert(parsed->name == "Alyx", "name round-trips");
        Assert(parsed->raceId == "demons", "raceId round-trips");
        Assert(parsed->classId == "warrior", "classId round-trips");
        const auto& g = parsed->customization;
        Assert(g.faceType == 2 && g.hairStyle == 4 && g.skinColorIdx == 1 &&
               g.hairColorIdx == 3 && g.eyeColorIdx == 5, "5 base fields round-trip");
        Assert(g.bodyFrame == 1 && g.bodyType == 2 && g.facialHair == 3, "identity fields round-trip");
        Assert(g.racialFeatures.size() == 2, "2 features");
        Assert(g.racialFeatures.size() == 2 && g.racialFeatures[0].first == "horns" &&
               g.racialFeatures[0].second == 2, "feature 0 == horns:2");
        Assert(g.racialFeatures.size() == 2 && g.racialFeatures[1].first == "tails" &&
               g.racialFeatures[1].second == 1, "feature 1 == tails:1");
    }

    // Pas de customization : valeurs par défaut.
    auto buf2 = BuildCharacterCreateRequestPayload("Bob", "humains", "mage", CharacterCustomization{});
    auto parsed2 = ParseCharacterCreateRequestPayload(buf2.data(), buf2.size());
    Assert(parsed2.has_value() && parsed2->customization.racialFeatures.empty(), "default has no features");

    return s_fail == 0 ? 0 : 1;
}
