#include "src/shared/Character/CustomizationJson.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }
}

using engine::network::CharacterCustomization;

int main()
{
    CharacterCustomization c;
    c.faceType = 2; c.hairStyle = 4; c.skinColorIdx = 1; c.hairColorIdx = 3; c.eyeColorIdx = 5;
    c.bodyFrame = 1; c.bodyType = 2; c.facialHair = 3;
    c.racialFeatures = { {"horns", 2}, {"tails", 1} };

    const std::string json = engine::character::CustomizationToJson(c);
    Assert(!json.empty() && json.front() == '{', "json starts with brace");

    const CharacterCustomization r = engine::character::CustomizationFromJson(json);
    Assert(r.faceType == 2 && r.hairStyle == 4 && r.skinColorIdx == 1 &&
           r.hairColorIdx == 3 && r.eyeColorIdx == 5, "base fields round-trip");
    Assert(r.bodyFrame == 1 && r.bodyType == 2 && r.facialHair == 3, "identity fields round-trip");
    Assert(r.racialFeatures.size() == 2, "2 features round-trip");

    // Robustesse : entrée vide / '{}' → défauts, pas de crash.
    const CharacterCustomization empty1 = engine::character::CustomizationFromJson("");
    const CharacterCustomization empty2 = engine::character::CustomizationFromJson("{}");
    Assert(empty1.faceType == 0 && empty1.racialFeatures.empty(), "empty string -> defaults");
    Assert(empty2.faceType == 0 && empty2.racialFeatures.empty(), "'{}' -> defaults");

    return s_fail == 0 ? 0 : 1;
}
