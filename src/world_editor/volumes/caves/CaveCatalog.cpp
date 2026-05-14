#include "src/world_editor/volumes/caves/CaveCatalog.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::editor::world::volumes::caves
{
	namespace
	{
		void SkipWs(const std::string& s, size_t& p)
		{
			while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
				++p;
		}

		bool ExpectChar(const std::string& s, size_t& p, char c)
		{
			SkipWs(s, p);
			if (p >= s.size() || s[p] != c) return false;
			++p;
			return true;
		}

		/// Lit une chaîne JSON entre guillemets. Le curseur démarre au "
		/// d'ouverture, finit après le " de fermeture.
		bool ReadJsonString(const std::string& s, size_t& p, std::string& out)
		{
			SkipWs(s, p);
			if (p >= s.size() || s[p] != '"') return false;
			++p;
			out.clear();
			while (p < s.size() && s[p] != '"')
			{
				if (s[p] == '\\' && p + 1 < s.size())
				{
					const char esc = s[p + 1];
					if (esc == 'n')      out.push_back('\n');
					else if (esc == 't') out.push_back('\t');
					else if (esc == 'r') out.push_back('\r');
					else                  out.push_back(esc);
					p += 2;
				}
				else
				{
					out.push_back(s[p]);
					++p;
				}
			}
			if (p >= s.size()) return false;
			++p;
			return true;
		}

		bool ReadJsonNumber(const std::string& s, size_t& p, float& out)
		{
			SkipWs(s, p);
			const size_t start = p;
			if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
			while (p < s.size() && (
				(s[p] >= '0' && s[p] <= '9') || s[p] == '.' ||
				s[p] == 'e' || s[p] == 'E' || s[p] == '-' || s[p] == '+'))
			{
				++p;
			}
			if (p == start) return false;
			try {
				out = std::stof(s.substr(start, p - start));
			} catch (...) {
				return false;
			}
			return true;
		}

		/// Lit un tableau de 3 floats `[a, b, c]`. Curseur après `]`.
		bool ReadVec3(const std::string& s, size_t& p, engine::math::Vec3& out)
		{
			if (!ExpectChar(s, p, '[')) return false;
			if (!ReadJsonNumber(s, p, out.x)) return false;
			if (!ExpectChar(s, p, ',')) return false;
			if (!ReadJsonNumber(s, p, out.y)) return false;
			if (!ExpectChar(s, p, ',')) return false;
			if (!ReadJsonNumber(s, p, out.z)) return false;
			if (!ExpectChar(s, p, ']')) return false;
			return true;
		}

		/// Cherche la clé `"key"` puis le `:` et avance le curseur à la valeur.
		/// Retourne false si non trouvée.
		bool SeekKey(const std::string& s, size_t startScope, size_t endScope,
			const std::string& key, size_t& outValuePos)
		{
			const std::string needle = "\"" + key + "\"";
			size_t pos = s.find(needle, startScope);
			while (pos != std::string::npos && pos < endScope)
			{
				size_t after = pos + needle.size();
				SkipWs(s, after);
				if (after < s.size() && s[after] == ':')
				{
					++after;
					SkipWs(s, after);
					outValuePos = after;
					return true;
				}
				pos = s.find(needle, after);
			}
			return false;
		}

		/// Trouve le `{` à `p`, retourne la position du `}` correspondant
		/// (compte des accolades imbriquées).
		size_t MatchObjectEnd(const std::string& s, size_t p)
		{
			if (p >= s.size() || s[p] != '{') return std::string::npos;
			int depth = 0;
			bool inString = false;
			for (size_t i = p; i < s.size(); ++i)
			{
				const char c = s[i];
				if (c == '"' && (i == 0 || s[i - 1] != '\\')) inString = !inString;
				if (inString) continue;
				if (c == '{') ++depth;
				else if (c == '}') { --depth; if (depth == 0) return i; }
			}
			return std::string::npos;
		}
	}

	bool CaveCatalog::LoadFromContent(const std::string& contentRoot, std::string& outError)
	{
		m_entries.clear();
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "meshes" / "caves" / "catalog.json";
		std::ifstream f(path);
		if (!f.good())
		{
			// Fichier absent : catalogue vide, pas une erreur.
			return true;
		}
		std::stringstream buf;
		buf << f.rdbuf();
		return ParseJson(buf.str(), outError);
	}

	bool CaveCatalog::ParseJson(const std::string& jsonText, std::string& outError)
	{
		m_entries.clear();
		size_t cavesPos = 0u;
		if (!SeekKey(jsonText, 0u, jsonText.size(), "caves", cavesPos))
		{
			// Pas de clé "caves" : catalogue vide, accepté.
			return true;
		}
		SkipWs(jsonText, cavesPos);
		if (cavesPos >= jsonText.size() || jsonText[cavesPos] != '[')
		{
			outError = "CaveCatalog: 'caves' is not an array";
			return false;
		}
		++cavesPos;

		while (cavesPos < jsonText.size())
		{
			SkipWs(jsonText, cavesPos);
			if (cavesPos < jsonText.size() && jsonText[cavesPos] == ']')
			{
				++cavesPos;
				break;
			}
			if (cavesPos < jsonText.size() && jsonText[cavesPos] == ',')
			{
				++cavesPos;
				SkipWs(jsonText, cavesPos);
			}
			if (cavesPos >= jsonText.size() || jsonText[cavesPos] != '{')
			{
				outError = "CaveCatalog: expected '{'";
				return false;
			}
			const size_t objStart = cavesPos;
			const size_t objEnd   = MatchObjectEnd(jsonText, objStart);
			if (objEnd == std::string::npos)
			{
				outError = "CaveCatalog: unmatched '{'";
				return false;
			}

			CaveCatalogEntry entry;
			size_t kp = 0u;
			auto getStr = [&](const std::string& key, std::string& dst, bool optional = false) {
				if (SeekKey(jsonText, objStart, objEnd, key, kp))
				{
					return ReadJsonString(jsonText, kp, dst);
				}
				return optional;
			};
			auto getVec3 = [&](const std::string& key, engine::math::Vec3& dst, bool optional = true) {
				if (SeekKey(jsonText, objStart, objEnd, key, kp))
				{
					return ReadVec3(jsonText, kp, dst);
				}
				return optional;
			};

			if (!getStr("id", entry.id))           { outError = "CaveCatalog: missing id"; return false; }
			if (!getStr("gltf", entry.gltfRelativePath, true)) entry.gltfRelativePath.clear();
			if (!getStr("displayName", entry.displayName, true)) entry.displayName = entry.id;
			if (!getStr("thumbnail", entry.thumbnailPath, true)) entry.thumbnailPath.clear();
			getVec3("aabbMin", entry.aabbMin);
			getVec3("aabbMax", entry.aabbMax);
			getVec3("entrancePoint", entry.entrancePoint);
			getVec3("interiorAabbMin", entry.interiorAabbMin);
			getVec3("interiorAabbMax", entry.interiorAabbMax);

			m_entries.push_back(std::move(entry));
			cavesPos = objEnd + 1u;
		}
		return true;
	}

	const CaveCatalogEntry* CaveCatalog::FindById(const std::string& id) const
	{
		auto it = std::find_if(m_entries.begin(), m_entries.end(),
			[&id](const CaveCatalogEntry& e) { return e.id == id; });
		return (it == m_entries.end()) ? nullptr : &*it;
	}
}
