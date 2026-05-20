#include "src/shared/Character/CustomizationJson.h"

#include "src/shared/Character/Json.h"

#include <string>

namespace engine::character
{
    namespace
    {
        std::string Escape(const std::string& in)
        {
            std::string out;
            out.reserve(in.size() + 2);
            for (char ch : in)
            {
                if (ch == '"' || ch == '\\') out.push_back('\\');
                out.push_back(ch);
            }
            return out;
        }

        uint8_t AsU8(const engine::json::Value* v)
        {
            if (!v || v->type != engine::json::Value::Type::Number) return 0u;
            double d = v->n;
            if (d < 0.0) d = 0.0;
            if (d > 255.0) d = 255.0;
            return static_cast<uint8_t>(d);
        }
    }

    std::string CustomizationToJson(const engine::network::CharacterCustomization& c)
    {
        std::string out = "{\"v\":1";
        out += ",\"face\":"       + std::to_string(c.faceType);
        out += ",\"hair\":"       + std::to_string(c.hairStyle);
        out += ",\"skin\":"       + std::to_string(c.skinColorIdx);
        out += ",\"hairColor\":"  + std::to_string(c.hairColorIdx);
        out += ",\"eye\":"        + std::to_string(c.eyeColorIdx);
        out += ",\"frame\":"      + std::to_string(c.bodyFrame);
        out += ",\"body\":"       + std::to_string(c.bodyType);
        out += ",\"facialHair\":" + std::to_string(c.facialHair);
        out += ",\"features\":{";
        for (size_t i = 0; i < c.racialFeatures.size(); ++i)
        {
            if (i) out += ",";
            out += "\"" + Escape(c.racialFeatures[i].first) + "\":" + std::to_string(c.racialFeatures[i].second);
        }
        out += "}}";
        return out;
    }

    engine::network::CharacterCustomization CustomizationFromJson(const std::string& jsonStr)
    {
        engine::network::CharacterCustomization c;
        engine::json::Value root;
        if (!engine::json::Parse(jsonStr, root) || root.type != engine::json::Value::Type::Object)
            return c;

        c.faceType     = AsU8(root.Find("face"));
        c.hairStyle    = AsU8(root.Find("hair"));
        c.skinColorIdx = AsU8(root.Find("skin"));
        c.hairColorIdx = AsU8(root.Find("hairColor"));
        c.eyeColorIdx  = AsU8(root.Find("eye"));
        c.bodyFrame    = AsU8(root.Find("frame"));
        c.bodyType     = AsU8(root.Find("body"));
        c.facialHair   = AsU8(root.Find("facialHair"));

        if (const engine::json::Value* feats = root.Find("features");
            feats && feats->type == engine::json::Value::Type::Object)
        {
            for (const auto& [key, val] : feats->o)
            {
                if (val.type == engine::json::Value::Type::Number)
                    c.racialFeatures.emplace_back(key, AsU8(&val));
            }
        }
        return c;
    }
}
