#include "src/shared/Character/CustomizationValidator.h"

#include <iostream>

namespace
{
    int s_fail = 0;
    void Assert(bool cond, const char* msg)
    {
        if (!cond) { ++s_fail; std::cerr << "[FAIL] " << msg << std::endl; }
    }

    // Construit un catalogue déterministe en mémoire (pas de fichiers).
    engine::character::CustomizationCatalog MakeCatalog()
    {
        using namespace engine::character;
        CustomizationCatalog cat;
        RaceCustomization rc;
        rc.frames = { "masculine", "feminine" };
        FrameModules m;
        m.bodyTypes  = { "base", "muscular" };
        m.faces      = { "f0", "f1", "f2" };
        m.hair       = { "h0", "h1" };
        m.facialHair = { "none", "goatee" };
        rc.modules["masculine"] = m;
        rc.modules["feminine"]  = m;
        rc.racialFeatures["horns"] = { "none", "curved_01" };
        rc.skinColorCount = 4;
        rc.hairColorCount = 3;
        rc.eyeColorCount  = 2;
        cat.Set("demons", std::move(rc));
        return cat;
    }
}

using engine::network::CharacterCustomization;

int main()
{
    const auto cat = MakeCatalog();

    {
        CharacterCustomization c;
        c.bodyFrame = 0; c.bodyType = 1; c.faceType = 2; c.hairStyle = 1; c.facialHair = 1;
        c.skinColorIdx = 3; c.hairColorIdx = 2; c.eyeColorIdx = 1;
        c.racialFeatures = { {"horns", 1} };
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(r.ok, "valid customization accepted");
    }
    {
        CharacterCustomization c;
        auto r = engine::character::ValidateCustomization(cat, "unknown_race", c);
        Assert(!r.ok, "unknown race rejected");
    }
    {
        CharacterCustomization c; c.bodyFrame = 5; // hors frames
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "bad bodyFrame rejected");
    }
    {
        CharacterCustomization c; c.faceType = 9; // > faces.size()
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "bad faceType rejected");
    }
    {
        CharacterCustomization c; c.skinColorIdx = 4; // == count -> hors borne
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "bad skinColorIdx rejected");
    }
    {
        CharacterCustomization c; c.racialFeatures = { {"wings", 0} }; // clé inconnue
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "unknown feature key rejected");
    }
    {
        CharacterCustomization c; c.racialFeatures = { {"horns", 9} }; // index hors borne
        auto r = engine::character::ValidateCustomization(cat, "demons", c);
        Assert(!r.ok, "out-of-range feature index rejected");
    }

    return s_fail == 0 ? 0 : 1;
}
