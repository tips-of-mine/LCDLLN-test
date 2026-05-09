#include "src/world_editor/ui/TreeSpeciesCatalog.h"

#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <filesystem>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace engine::editor
{
	namespace
	{
		void SkipWs(std::string_view s, size_t& i)
		{
			while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0)
			{
				++i;
			}
		}

		std::string UnescapeJsonString(std::string_view inner)
		{
			std::string o;
			o.reserve(inner.size());
			for (size_t i = 0; i < inner.size(); ++i)
			{
				if (inner[i] != '\\' || i + 1 >= inner.size())
				{
					o.push_back(inner[i]);
					continue;
				}
				switch (inner[++i])
				{
				case '"': o.push_back('"'); break;
				case '\\': o.push_back('\\'); break;
				case '/': o.push_back('/'); break;
				case 'b': o.push_back('\b'); break;
				case 'f': o.push_back('\f'); break;
				case 'n': o.push_back('\n'); break;
				case 'r': o.push_back('\r'); break;
				case 't': o.push_back('\t'); break;
				default: o.push_back(inner[i]); break;
				}
			}
			return o;
		}

		bool ReadStringLiteral(std::string_view json, size_t& p, std::string& out, std::string& err, const char* ctx)
		{
			SkipWs(json, p);
			if (p >= json.size() || json[p] != '"')
			{
				err = std::string(ctx) + ": guillemet attendu";
				return false;
			}
			++p;
			const size_t start = p;
			while (p < json.size())
			{
				if (json[p] == '\\')
				{
					if (p + 1 >= json.size())
					{
						err = std::string(ctx) + ": échappement incomplet";
						return false;
					}
					p += 2;
					continue;
				}
				if (json[p] == '"')
				{
					out = UnescapeJsonString(json.substr(start, p - start));
					++p;
					return true;
				}
				++p;
			}
			err = std::string(ctx) + ": chaîne non terminée";
			return false;
		}

		bool ParseJsonStringMember(std::string_view obj, std::string_view key, std::string& out)
		{
			const std::string needle = std::string("\"") + std::string(key) + "\"";
			size_t p = obj.find(needle);
			if (p == std::string::npos)
			{
				return false;
			}
			p = obj.find(':', p + needle.size());
			if (p == std::string::npos)
			{
				return false;
			}
			++p;
			std::string err;
			return ReadStringLiteral(obj, p, out, err, "string member");
		}

		bool ParseOptionalDoubleMember(std::string_view obj, std::string_view key, double& out, bool& found)
		{
			found = false;
			const std::string needle = std::string("\"") + std::string(key) + "\"";
			size_t p = obj.find(needle);
			if (p == std::string::npos)
			{
				return true;
			}
			p = obj.find(':', p + needle.size());
			if (p == std::string::npos)
			{
				return false;
			}
			++p;
			SkipWs(obj, p);
			size_t q = p;
			while (q < obj.size())
			{
				const char c = obj[q];
				if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
				{
					++q;
					continue;
				}
				break;
			}
			if (q == p)
			{
				return false;
			}
			const std::string num(obj.substr(p, q - p));
			char* endPtr = nullptr;
			out = std::strtod(num.c_str(), &endPtr);
			if (endPtr == num.c_str() || static_cast<size_t>(endPtr - num.c_str()) != num.size())
			{
				return false;
			}
			found = true;
			return true;
		}

		bool ParseGltfInShapeObject(std::string_view obj, std::string& gltfOut, std::string& err)
		{
			if (!ParseJsonStringMember(obj, "gltf", gltfOut) || gltfOut.empty())
			{
				err = "shape: gltf manquant";
				return false;
			}
			return true;
		}

		bool ParseShapesArray(std::string_view speciesObj, std::vector<TreeSpeciesShapeSpec>& shapesOut, std::string& err)
		{
			shapesOut.clear();
			const std::string needle = "\"shapes\"";
			size_t p = speciesObj.find(needle);
			if (p == std::string::npos)
			{
				err = "espèce: shapes manquant";
				return false;
			}
			p = speciesObj.find(':', p + needle.size());
			if (p == std::string::npos)
			{
				return false;
			}
			++p;
			SkipWs(speciesObj, p);
			if (p >= speciesObj.size() || speciesObj[p] != '[')
			{
				err = "shapes: '[' attendu";
				return false;
			}
			++p;
			for (;;)
			{
				SkipWs(speciesObj, p);
				if (p < speciesObj.size() && speciesObj[p] == ']')
				{
					return true;
				}
				if (p >= speciesObj.size())
				{
					err = "shapes: fin inattendue";
					return false;
				}
				if (speciesObj[p] != '{')
				{
					err = "shapes: '{' attendu";
					return false;
				}
				const size_t objStart = p;
				int depth = 0;
				do
				{
					if (p >= speciesObj.size())
					{
						err = "shapes: accolade non fermée";
						return false;
					}
					if (speciesObj[p] == '{')
					{
						++depth;
					}
					else if (speciesObj[p] == '}')
					{
						--depth;
					}
					++p;
				} while (depth > 0);
				const std::string_view one = speciesObj.substr(objStart, p - objStart);
				TreeSpeciesShapeSpec sh{};
				if (!ParseGltfInShapeObject(one, sh.gltfContentRelativePath, err))
				{
					return false;
				}
				for (char& c : sh.gltfContentRelativePath)
				{
					if (c == '\\')
					{
						c = '/';
					}
				}
				while (!sh.gltfContentRelativePath.empty() && sh.gltfContentRelativePath.front() == '/')
				{
					sh.gltfContentRelativePath.erase(sh.gltfContentRelativePath.begin());
				}
				shapesOut.push_back(std::move(sh));
				SkipWs(speciesObj, p);
				if (p < speciesObj.size() && speciesObj[p] == ',')
				{
					++p;
					continue;
				}
				if (p < speciesObj.size() && speciesObj[p] == ']')
				{
					return true;
				}
				err = "shapes: ',' ou ']' attendu";
				return false;
			}
		}

		bool ExtractNextObjectInArray(std::string_view json, size_t& p, std::string_view& outObj, std::string& err)
		{
			SkipWs(json, p);
			if (p < json.size() && json[p] == ']')
			{
				outObj = std::string_view();
				return true;
			}
			if (p >= json.size() || json[p] != '{')
			{
				err = "tableau: '{' attendu";
				return false;
			}
			const size_t objStart = p;
			int depth = 0;
			do
			{
				if (p >= json.size())
				{
					err = "tableau: accolade non fermée";
					return false;
				}
				if (json[p] == '{')
				{
					++depth;
				}
				else if (json[p] == '}')
				{
					--depth;
				}
				++p;
			} while (depth > 0);
			outObj = json.substr(objStart, p - objStart);
			SkipWs(json, p);
			if (p < json.size() && json[p] == ',')
			{
				++p;
			}
			return true;
		}
	} // namespace

	bool TreeSpeciesCatalog::LoadFromFile(const engine::core::Config& cfg, std::string_view relativeCatalogPath, std::string& outError)
	{
		m_species.clear();
		const std::filesystem::path abs = engine::platform::FileSystem::ResolveContentPath(cfg, relativeCatalogPath);
		std::ifstream in(abs, std::ios::binary);
		if (!in.is_open())
		{
			outError = std::string("catalogue arbres introuvable: ") + abs.string();
			LOG_WARN(Core, "[TreeSpeciesCatalog] {}", outError);
			return false;
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		const std::string json = ss.str();
		const std::string needle = "\"tree_species\"";
		size_t keyPos = json.find(needle);
		if (keyPos == std::string::npos)
		{
			outError = "clé tree_species absente";
			LOG_WARN(Core, "[TreeSpeciesCatalog] {}", outError);
			return false;
		}
		size_t p = json.find(':', keyPos + needle.size());
		if (p == std::string::npos)
		{
			outError = "tree_species: ':' manquant";
			return false;
		}
		++p;
		SkipWs(json, p);
		if (p >= json.size() || json[p] != '[')
		{
			outError = "tree_species: '[' manquant";
			return false;
		}
		++p;
		for (;;)
		{
			SkipWs(json, p);
			if (p < json.size() && json[p] == ']')
			{
				LOG_INFO(Core, "[TreeSpeciesCatalog] {} espèce(s) chargée(s) depuis {}", m_species.size(), abs.string());
				return true;
			}
			std::string_view oneObj;
			std::string err;
			if (!ExtractNextObjectInArray(json, p, oneObj, err))
			{
				outError = err;
				return false;
			}
			if (oneObj.empty())
			{
				LOG_INFO(Core, "[TreeSpeciesCatalog] {} espèce(s) chargée(s) depuis {}", m_species.size(), abs.string());
				return true;
			}
			TreeSpeciesSpec sp{};
			if (!ParseJsonStringMember(oneObj, "id", sp.id) || sp.id.empty())
			{
				LOG_WARN(Core, "[TreeSpeciesCatalog] espèce ignorée (id manquant)");
				continue;
			}
			bool hasMin = false;
			bool hasMax = false;
			double smin = 0.8;
			double smax = 1.2;
			if (!ParseOptionalDoubleMember(oneObj, "scale_min", smin, hasMin))
			{
				LOG_WARN(Core, "[TreeSpeciesCatalog] espèce '{}' ignorée (scale_min invalide)", sp.id);
				continue;
			}
			if (!ParseOptionalDoubleMember(oneObj, "scale_max", smax, hasMax))
			{
				LOG_WARN(Core, "[TreeSpeciesCatalog] espèce '{}' ignorée (scale_max invalide)", sp.id);
				continue;
			}
			if (!hasMin)
			{
				smin = 0.8;
			}
			if (!hasMax)
			{
				smax = 1.2;
			}
			if (smin > smax)
			{
				std::swap(smin, smax);
			}
			sp.scaleMin = smin;
			sp.scaleMax = smax;
			if (!ParseShapesArray(oneObj, sp.shapes, err))
			{
				LOG_WARN(Core, "[TreeSpeciesCatalog] espèce '{}' ignorée: {}", sp.id, err);
				continue;
			}
			std::vector<TreeSpeciesShapeSpec> validShapes;
			validShapes.reserve(sp.shapes.size());
			for (const TreeSpeciesShapeSpec& sh : sp.shapes)
			{
				const std::filesystem::path gltfAbs = engine::platform::FileSystem::ResolveContentPath(cfg, sh.gltfContentRelativePath);
				std::error_code ec;
				if (!std::filesystem::exists(gltfAbs, ec))
				{
					LOG_WARN(Core, "[TreeSpeciesCatalog] forme ignorée (fichier manquant) espèce={} path={}", sp.id, gltfAbs.string());
					continue;
				}
				validShapes.push_back(sh);
			}
			if (validShapes.size() < 2u)
			{
				LOG_WARN(Core, "[TreeSpeciesCatalog] espèce '{}' ignorée (moins de 2 formes glTF valides)", sp.id);
				continue;
			}
			sp.shapes = std::move(validShapes);
			m_species.push_back(std::move(sp));
		}
	}

	const TreeSpeciesSpec* TreeSpeciesCatalog::FindById(std::string_view id) const
	{
		for (const TreeSpeciesSpec& s : m_species)
		{
			if (s.id == id)
			{
				return &s;
			}
		}
		return nullptr;
	}

	int TreeSpeciesCatalog::IndexOfId(std::string_view id) const
	{
		for (size_t i = 0; i < m_species.size(); ++i)
		{
			if (m_species[i].id == id)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void TreeSpeciesCatalog::SanitizeLayoutInstance(WorldMapEditLayoutInstance& inst) const
	{
		if (inst.speciesId.empty())
		{
			return;
		}
		const TreeSpeciesSpec* sp = FindById(inst.speciesId);
		if (sp == nullptr || sp->shapes.empty())
		{
			return;
		}
		const size_t n = sp->shapes.size();
		size_t idx = static_cast<size_t>(inst.shapeVariantIndex);
		if (idx >= n)
		{
			idx = n - 1u;
			inst.shapeVariantIndex = static_cast<uint32_t>(idx);
		}
		inst.gltfContentRelativePath = sp->shapes[idx].gltfContentRelativePath;
		inst.uniformScale = std::clamp(inst.uniformScale, sp->scaleMin, sp->scaleMax);
	}

} // namespace engine::editor
