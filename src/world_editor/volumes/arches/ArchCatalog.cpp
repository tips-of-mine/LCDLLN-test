#include "src/world_editor/volumes/arches/ArchCatalog.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace engine::editor::world::volumes::arches
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
				else { out.push_back(s[p]); ++p; }
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
				++p;
			if (p == start) return false;
			try { out = std::stof(s.substr(start, p - start)); }
			catch (...) { return false; }
			return true;
		}
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

	bool ArchCatalog::LoadFromContent(const std::string& contentRoot, std::string& outError)
	{
		m_entries.clear();
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "meshes" / "arches" / "catalog.json";
		std::ifstream f(path);
		if (!f.good()) return true;
		std::stringstream buf;
		buf << f.rdbuf();
		return ParseJson(buf.str(), outError);
	}

	bool ArchCatalog::ParseJson(const std::string& jsonText, std::string& outError)
	{
		m_entries.clear();
		size_t arrPos = 0u;
		if (!SeekKey(jsonText, 0u, jsonText.size(), "arches", arrPos)) return true;
		SkipWs(jsonText, arrPos);
		if (arrPos >= jsonText.size() || jsonText[arrPos] != '[')
		{
			outError = "ArchCatalog: 'arches' is not an array";
			return false;
		}
		++arrPos;
		while (arrPos < jsonText.size())
		{
			SkipWs(jsonText, arrPos);
			if (arrPos < jsonText.size() && jsonText[arrPos] == ']') { ++arrPos; break; }
			if (arrPos < jsonText.size() && jsonText[arrPos] == ',') { ++arrPos; SkipWs(jsonText, arrPos); }
			if (arrPos >= jsonText.size() || jsonText[arrPos] != '{')
			{
				outError = "ArchCatalog: expected '{'";
				return false;
			}
			const size_t objStart = arrPos;
			const size_t objEnd   = MatchObjectEnd(jsonText, objStart);
			if (objEnd == std::string::npos)
			{
				outError = "ArchCatalog: unmatched '{'";
				return false;
			}

			ArchCatalogEntry entry;
			size_t kp = 0u;
			auto getStr = [&](const std::string& key, std::string& dst, bool optional = false) {
				if (SeekKey(jsonText, objStart, objEnd, key, kp))
					return ReadJsonString(jsonText, kp, dst);
				return optional;
			};
			auto getVec3 = [&](const std::string& key, engine::math::Vec3& dst, bool optional = true) {
				if (SeekKey(jsonText, objStart, objEnd, key, kp))
					return ReadVec3(jsonText, kp, dst);
				return optional;
			};
			auto getF = [&](const std::string& key, float& dst, bool optional = true) {
				if (SeekKey(jsonText, objStart, objEnd, key, kp))
					return ReadJsonNumber(jsonText, kp, dst);
				return optional;
			};

			if (!getStr("id", entry.id))  { outError = "ArchCatalog: missing id"; return false; }
			if (!getStr("gltf", entry.gltfRelativePath, true)) entry.gltfRelativePath.clear();
			if (!getStr("displayName", entry.displayName, true)) entry.displayName = entry.id;
			if (!getStr("thumbnail", entry.thumbnailPath, true)) entry.thumbnailPath.clear();
			getVec3("aabbMin", entry.aabbMin);
			getVec3("aabbMax", entry.aabbMax);
			getVec3("archAnchorA", entry.archAnchorA);
			getVec3("archAnchorB", entry.archAnchorB);
			getF("archHeight", entry.archHeight);

			m_entries.push_back(std::move(entry));
			arrPos = objEnd + 1u;
		}
		return true;
	}

	const ArchCatalogEntry* ArchCatalog::FindById(const std::string& id) const
	{
		auto it = std::find_if(m_entries.begin(), m_entries.end(),
			[&id](const ArchCatalogEntry& e) { return e.id == id; });
		return (it == m_entries.end()) ? nullptr : &*it;
	}
}
