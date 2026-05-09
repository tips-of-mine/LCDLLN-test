// engine/world/surface/SurfaceTable.cpp
#include "engine/world/surface/SurfaceTable.h"

#include <fstream>
#include <sstream>

namespace engine::world::surface
{
    namespace
    {
        /// Pattern LayerPalette : extracteur string linéaire pour un objet JSON
        /// minimal (pas d'imbrication, pas d'escapes). `obj` est la sous-chaîne
        /// "{...}" d'une entrée du tableau "surfaces".
        std::string ExtractStringField(const std::string& obj, const std::string& key,
            const std::string& fallback = "")
        {
            const std::string searchKey = "\"" + key + "\"";
            size_t keyPos = obj.find(searchKey);
            if (keyPos == std::string::npos) return fallback;
            size_t colonPos = obj.find(':', keyPos);
            if (colonPos == std::string::npos) return fallback;
            size_t startQuote = obj.find('"', colonPos + 1);
            if (startQuote == std::string::npos) return fallback;
            size_t endQuote = obj.find('"', startQuote + 1);
            if (endQuote == std::string::npos) return fallback;
            return obj.substr(startQuote + 1, endQuote - startQuote - 1);
        }

        float ExtractFloatField(const std::string& obj, const std::string& key,
            float fallback = 0.0f)
        {
            const std::string searchKey = "\"" + key + "\"";
            size_t keyPos = obj.find(searchKey);
            if (keyPos == std::string::npos) return fallback;
            size_t colonPos = obj.find(':', keyPos);
            if (colonPos == std::string::npos) return fallback;
            std::string remainder = obj.substr(colonPos + 1);
            try { return std::stof(remainder); } catch (...) { return fallback; }
        }
    }

    bool SurfaceTable::LoadFromJson(const std::filesystem::path& path, std::string& outError)
    {
        m_loaded = false;
        std::ifstream f(path);
        if (!f.good())
        { outError = "SurfaceTable: cannot open " + path.string(); return false; }

        std::stringstream buf;
        buf << f.rdbuf();
        const std::string content = buf.str();

        const std::string surfacesKey = "\"surfaces\"";
        size_t pos = content.find(surfacesKey);
        if (pos == std::string::npos)
        { outError = "SurfaceTable: missing 'surfaces' field"; return false; }
        size_t arrStart = content.find('[', pos);
        if (arrStart == std::string::npos)
        { outError = "SurfaceTable: 'surfaces' is not an array"; return false; }

        const size_t expected = static_cast<size_t>(SurfaceType::_Count);
        bool seen[static_cast<size_t>(SurfaceType::_Count)] = {};
        size_t cursor = arrStart;
        size_t parsed = 0;

        for (size_t i = 0; i < expected; ++i)
        {
            size_t objStart = content.find('{', cursor);
            if (objStart == std::string::npos)
            {
                outError = "SurfaceTable: " + std::to_string(expected)
                    + " entries expected, got " + std::to_string(parsed);
                return false;
            }
            size_t objEnd = content.find('}', objStart);
            if (objEnd == std::string::npos)
            {
                outError = "SurfaceTable: unterminated entry " + std::to_string(i);
                return false;
            }
            std::string obj = content.substr(objStart, objEnd - objStart + 1);

            const std::string typeStr = ExtractStringField(obj, "type");
            SurfaceType type = SurfaceType::Dirt;
            if (!ParseSurfaceType(typeStr, type))
            {
                outError = "SurfaceTable: unknown type '" + typeStr + "' at index "
                    + std::to_string(i);
                return false;
            }
            const auto idx = static_cast<size_t>(type);
            if (seen[idx])
            {
                outError = "SurfaceTable: duplicate type '" + typeStr + "'";
                return false;
            }
            seen[idx] = true;

            SurfaceTableEntry& e = m_entries[idx];
            e.type      = type;
            e.baseSpeed = ExtractFloatField(obj, "baseSpeed", 1.0f);
            if (e.baseSpeed < 0.0f)
            { outError = "SurfaceTable: negative baseSpeed for '" + typeStr + "'"; return false; }
            e.audioStep = ExtractStringField(obj, "audioStep");
            e.visualTag = ExtractStringField(obj, "visualTag");

            cursor = objEnd + 1;
            ++parsed;
        }

        // Vérifie que toutes les surfaces sont présentes.
        for (size_t i = 0; i < expected; ++i)
        {
            if (!seen[i])
            {
                outError = "SurfaceTable: " + std::to_string(expected)
                    + " entries expected, missing index " + std::to_string(i);
                return false;
            }
        }

        m_loaded = true;
        return true;
    }

    const SurfaceTableEntry& SurfaceTable::Get(SurfaceType t) const noexcept
    {
        return m_entries[static_cast<size_t>(t)];
    }
}
