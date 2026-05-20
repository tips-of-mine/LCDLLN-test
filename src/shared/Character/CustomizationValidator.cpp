#include "src/shared/Character/CustomizationValidator.h"

#include <utility>

namespace engine::character
{
    ValidationResult ValidateCustomization(const CustomizationCatalog& catalog,
                                           const std::string& raceId,
                                           const engine::network::CharacterCustomization& c)
    {
        ValidationResult res;
        auto fail = [&res](std::string msg) { res.ok = false; res.errors.push_back(std::move(msg)); };

        const RaceCustomization* rc = catalog.Find(raceId);
        if (!rc)
        {
            fail("unknown race: " + raceId);
            return res; // impossible de continuer sans config de race
        }

        if (c.bodyFrame >= rc->frames.size())
        {
            fail("bodyFrame out of range");
            return res; // le frame conditionne les modules suivants
        }
        const std::string& frame = rc->frames[c.bodyFrame];

        auto it = rc->modules.find(frame);
        if (it == rc->modules.end())
        {
            fail("no modules for frame: " + frame);
            return res;
        }
        const FrameModules& m = it->second;

        if (c.bodyType   >= m.bodyTypes.size())  fail("bodyType out of range");
        if (c.faceType   >= m.faces.size())      fail("faceType out of range");
        if (c.hairStyle  >= m.hair.size())       fail("hairStyle out of range");
        if (c.facialHair >= m.facialHair.size()) fail("facialHair out of range");

        if (c.skinColorIdx >= rc->skinColorCount) fail("skinColorIdx out of range");
        if (c.hairColorIdx >= rc->hairColorCount) fail("hairColorIdx out of range");
        if (c.eyeColorIdx  >= rc->eyeColorCount)  fail("eyeColorIdx out of range");

        for (const auto& [key, idx] : c.racialFeatures)
        {
            auto fit = rc->racialFeatures.find(key);
            if (fit == rc->racialFeatures.end())
            {
                fail("unknown racial feature: " + key);
                continue;
            }
            if (idx >= fit->second.size())
                fail("racial feature index out of range: " + key);
        }

        return res;
    }
}
