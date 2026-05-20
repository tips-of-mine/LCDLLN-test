#include "src/shared/Character/CustomizationCatalog.h"

#include "src/shared/Character/Json.h"
#include "src/shared/core/Log.h"

#include <fstream>
#include <sstream>

namespace engine::character
{
    namespace
    {
        bool ReadFile(const std::string& path, std::string& out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::ostringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        // Remplit \p dst avec les chaînes d'un array JSON (ignore les non-strings).
        void ReadStringArray(const engine::json::Value* arr, std::vector<std::string>& dst)
        {
            if (!arr || arr->type != engine::json::Value::Type::Array) return;
            for (const auto& item : arr->a)
                if (item.type == engine::json::Value::Type::String)
                    dst.push_back(item.s);
        }

        uint32_t ArraySize(const engine::json::Value* arr)
        {
            return (arr && arr->type == engine::json::Value::Type::Array)
                ? static_cast<uint32_t>(arr->a.size()) : 0u;
        }
    }

    void CustomizationCatalog::Set(const std::string& raceId, RaceCustomization rc)
    {
        m_races[raceId] = std::move(rc);
    }

    const RaceCustomization* CustomizationCatalog::Find(const std::string& raceId) const
    {
        auto it = m_races.find(raceId);
        return it == m_races.end() ? nullptr : &it->second;
    }

    bool CustomizationCatalog::LoadFromDir(const std::string& racesDir)
    {
        m_races.clear();

        // 1. races.json : ids + tailles de palettes couleurs.
        std::string racesRaw;
        if (!ReadFile(racesDir + "/races.json", racesRaw))
        {
            LOG_WARN(Auth, "[CustomizationCatalog] cannot read {}/races.json", racesDir);
            return false;
        }
        engine::json::Value racesRoot;
        if (!engine::json::Parse(racesRaw, racesRoot) || racesRoot.type != engine::json::Value::Type::Object)
        {
            LOG_WARN(Auth, "[CustomizationCatalog] races.json parse failed");
            return false;
        }
        const engine::json::Value* racesArr = racesRoot.Find("races");
        if (!racesArr || racesArr->type != engine::json::Value::Type::Array)
        {
            LOG_WARN(Auth, "[CustomizationCatalog] races.json missing 'races' array");
            return false;
        }

        for (const auto& raceNode : racesArr->a)
        {
            if (raceNode.type != engine::json::Value::Type::Object) continue;
            const engine::json::Value* idNode = raceNode.Find("id");
            if (!idNode || idNode->type != engine::json::Value::Type::String) continue;
            const std::string raceId = idNode->s;

            RaceCustomization rc;
            rc.skinColorCount = ArraySize(raceNode.Find("defaultSkinColors"));
            rc.hairColorCount = ArraySize(raceNode.Find("defaultHairColors"));
            rc.eyeColorCount  = ArraySize(raceNode.Find("defaultEyeColors"));

            // 2. customization/<id>.json : frames + modules + features.
            std::string custRaw;
            if (!ReadFile(racesDir + "/customization/" + raceId + ".json", custRaw))
            {
                LOG_WARN(Auth, "[CustomizationCatalog] missing customization file for race {}", raceId);
                continue;
            }
            engine::json::Value custRoot;
            if (!engine::json::Parse(custRaw, custRoot) || custRoot.type != engine::json::Value::Type::Object)
            {
                LOG_WARN(Auth, "[CustomizationCatalog] customization parse failed for {}", raceId);
                continue;
            }

            ReadStringArray(custRoot.Find("frames"), rc.frames);

            if (const engine::json::Value* modules = custRoot.Find("modules");
                modules && modules->type == engine::json::Value::Type::Object)
            {
                for (const auto& [frame, modVal] : modules->o)
                {
                    if (modVal.type != engine::json::Value::Type::Object) continue;
                    FrameModules fm;
                    ReadStringArray(modVal.Find("bodyTypes"),  fm.bodyTypes);
                    ReadStringArray(modVal.Find("faces"),      fm.faces);
                    ReadStringArray(modVal.Find("hair"),       fm.hair);
                    ReadStringArray(modVal.Find("facialHair"), fm.facialHair);
                    rc.modules.emplace(frame, std::move(fm));
                }
            }

            if (const engine::json::Value* feats = custRoot.Find("racialFeatures");
                feats && feats->type == engine::json::Value::Type::Object)
            {
                for (const auto& [key, idsVal] : feats->o)
                {
                    std::vector<std::string> ids;
                    ReadStringArray(&idsVal, ids);
                    rc.racialFeatures.emplace(key, std::move(ids));
                }
            }

            m_races.emplace(raceId, std::move(rc));
        }

        LOG_INFO(Auth, "[CustomizationCatalog] loaded {} races from {}", m_races.size(), racesDir);
        return !m_races.empty();
    }
}
