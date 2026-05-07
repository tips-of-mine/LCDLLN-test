#include "engine/world/terrain/LayerPalette.h"

#include "engine/world/surface/SurfaceType.h"

#include <cassert>
#include <fstream>
#include <sstream>

namespace engine::world::terrain
{
	namespace
	{
		/// Extrait la valeur string associée à `"key"` dans un objet JSON
		/// linéaire `obj`. Ex. avec `obj = "{\"name\": \"dirt\", ...}"` et
		/// `key = "name"`, retourne `"dirt"`. Pas de support escape sequences
		/// ni objets imbriqués (suffisant pour le format fixé M100.9).
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

		/// Idem pour une valeur float (ex. `"tilingMeters": 4.0` → 4.0f).
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

		/// Idem pour uint32 (ex. `"index": 0` → 0u).
		uint32_t ExtractUintField(const std::string& obj, const std::string& key,
			uint32_t fallback = 0u)
		{
			const std::string searchKey = "\"" + key + "\"";
			size_t keyPos = obj.find(searchKey);
			if (keyPos == std::string::npos) return fallback;
			size_t colonPos = obj.find(':', keyPos);
			if (colonPos == std::string::npos) return fallback;
			std::string remainder = obj.substr(colonPos + 1);
			try { return static_cast<uint32_t>(std::stoul(remainder)); }
			catch (...) { return fallback; }
		}
	}

	bool LoadLayerPalette(const std::filesystem::path& path,
		LayerPalette& outPalette, std::string& outError)
	{
		std::ifstream f(path);
		if (!f.good())
		{ outError = "LayerPalette: cannot open " + path.string(); return false; }

		std::stringstream buf;
		buf << f.rdbuf();
		const std::string content = buf.str();

		outPalette.version = ExtractUintField(content, "version", 1u);

		// Trouve le début du tableau "layers": [ ... ].
		const std::string layersKey = "\"layers\"";
		size_t layersKeyPos = content.find(layersKey);
		if (layersKeyPos == std::string::npos)
		{ outError = "LayerPalette: missing 'layers' field"; return false; }
		size_t arrStart = content.find('[', layersKeyPos);
		if (arrStart == std::string::npos)
		{ outError = "LayerPalette: 'layers' is not an array"; return false; }

		// Parse 8 objets séquentiels.
		size_t cursor = arrStart;
		for (uint32_t i = 0; i < 8u; ++i)
		{
			size_t objStart = content.find('{', cursor);
			if (objStart == std::string::npos)
			{ outError = "LayerPalette: missing layer entry " + std::to_string(i); return false; }
			size_t objEnd = content.find('}', objStart);
			if (objEnd == std::string::npos)
			{ outError = "LayerPalette: unterminated layer entry " + std::to_string(i); return false; }
			std::string obj = content.substr(objStart, objEnd - objStart + 1);

			LayerEntry& e = outPalette.layers[i];
			e.index        = ExtractUintField(obj, "index", i);
			e.name         = ExtractStringField(obj, "name");
			e.albedoPath   = ExtractStringField(obj, "albedo");
			e.normalPath   = ExtractStringField(obj, "normal");
			e.armPath      = ExtractStringField(obj, "arm");
			e.tilingMeters = ExtractFloatField(obj, "tilingMeters", 4.0f);
			e.surfaceTypeName = ExtractStringField(obj, "surfaceType", "Dirt");
			if (!engine::world::surface::ParseSurfaceType(e.surfaceTypeName, e.surfaceType))
			{
				e.surfaceType = engine::world::surface::SurfaceType::Dirt;
				// Pas de LOG_WARN ici : pour les fixtures de test on tolère, le warn
				// serait noise. Le caller (Engine::Init) peut logguer s'il veut.
			}

			cursor = objEnd + 1;
		}
		return true;
	}

	engine::world::surface::SurfaceType
	LayerPalette::GetSurfaceTypeForLayer(uint8_t layer) const noexcept
	{
		assert(layer < 8u);
		return layers[layer].surfaceType;
	}
}
