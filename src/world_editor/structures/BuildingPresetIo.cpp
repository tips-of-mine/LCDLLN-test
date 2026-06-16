#include "src/world_editor/structures/BuildingPresetIo.h"

#include <cstdio>

namespace engine::editor::world::structures
{
	namespace
	{
		void SkipWs(const std::string& s, size_t& p)
		{
			while (p < s.size() &&
				(s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
		}
		bool ReadString(const std::string& s, size_t& p, std::string& out)
		{
			SkipWs(s, p);
			if (p >= s.size() || s[p] != '"') return false;
			++p; out.clear();
			while (p < s.size() && s[p] != '"')
			{
				if (s[p] == '\\' && p + 1 < s.size())
				{
					const char esc = s[p + 1];
					if (esc == 'n') out.push_back('\n');
					else if (esc == 't') out.push_back('\t');
					else out.push_back(esc);
					p += 2;
				}
				else { out.push_back(s[p]); ++p; }
			}
			if (p >= s.size()) return false;
			++p; return true;
		}
		bool ReadNumber(const std::string& s, size_t& p, double& out)
		{
			SkipWs(s, p);
			const size_t start = p;
			if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
			while (p < s.size() && ((s[p] >= '0' && s[p] <= '9') || s[p] == '.' ||
				s[p] == 'e' || s[p] == 'E' || s[p] == '-' || s[p] == '+')) ++p;
			if (p == start) return false;
			try { out = std::stod(s.substr(start, p - start)); }
			catch (...) { return false; }
			return true;
		}
		bool ReadBool(const std::string& s, size_t& p, bool& out)
		{
			SkipWs(s, p);
			if (s.compare(p, 4, "true") == 0) { out = true; p += 4; return true; }
			if (s.compare(p, 5, "false") == 0) { out = false; p += 5; return true; }
			return false;
		}
		size_t MatchEnd(const std::string& s, size_t p, char open, char close)
		{
			if (p >= s.size() || s[p] != open) return std::string::npos;
			int depth = 0; bool inStr = false;
			for (size_t i = p; i < s.size(); ++i)
			{
				const char c = s[i];
				if (c == '"' && (i == 0 || s[i - 1] != '\\')) inStr = !inStr;
				if (inStr) continue;
				if (c == open) ++depth;
				else if (c == close) { --depth; if (depth == 0) return i; }
			}
			return std::string::npos;
		}
		bool SeekKey(const std::string& s, size_t start, size_t end,
			const std::string& key, size_t& outPos)
		{
			const std::string needle = "\"" + key + "\"";
			size_t pos = s.find(needle, start);
			while (pos != std::string::npos && pos < end)
			{
				size_t after = pos + needle.size();
				SkipWs(s, after);
				if (after < s.size() && s[after] == ':')
				{
					++after; SkipWs(s, after); outPos = after; return true;
				}
				pos = s.find(needle, after);
			}
			return false;
		}
		float SeekNum(const std::string& s, size_t start, size_t end,
			const std::string& key, float fallback)
		{
			size_t kp = 0;
			if (!SeekKey(s, start, end, key, kp)) return fallback;
			double v = fallback;
			if (!ReadNumber(s, kp, v)) return fallback;
			return static_cast<float>(v);
		}
	}

	bool ParseBuildingPresetJson(const std::string& s, BuildingPreset& out,
		std::string& outError)
	{
		out = BuildingPreset{};
		size_t pos = 0;
		if (!SeekKey(s, 0, s.size(), "id", pos) || !ReadString(s, pos, out.id) ||
			out.id.empty())
		{
			outError = "BuildingPresetIo: 'id' manquant"; return false;
		}
		if (SeekKey(s, 0, s.size(), "displayName", pos))
			(void)ReadString(s, pos, out.displayName);
		if (out.displayName.empty()) out.displayName = out.id;

		size_t anchorPos = 0;
		if (SeekKey(s, 0, s.size(), "spawnAnchor", anchorPos))
		{
			SkipWs(s, anchorPos);
			const size_t aEnd = MatchEnd(s, anchorPos, '{', '}');
			if (aEnd != std::string::npos)
			{
				out.spawnAnchor.x = SeekNum(s, anchorPos, aEnd, "x", 0.0f);
				out.spawnAnchor.y = SeekNum(s, anchorPos, aEnd, "y", 0.0f);
				out.spawnAnchor.z = SeekNum(s, anchorPos, aEnd, "z", 0.0f);
			}
		}

		size_t arrPos = 0;
		if (!SeekKey(s, 0, s.size(), "elements", arrPos)) return true; // toléré
		SkipWs(s, arrPos);
		const size_t arrEnd = MatchEnd(s, arrPos, '[', ']');
		if (arrEnd == std::string::npos)
		{
			outError = "BuildingPresetIo: 'elements' non terminé"; return false;
		}
		++arrPos;
		while (arrPos < arrEnd)
		{
			SkipWs(s, arrPos);
			if (arrPos >= arrEnd || s[arrPos] == ']') break;
			if (s[arrPos] == ',') { ++arrPos; continue; }
			if (s[arrPos] != '{') { outError = "BuildingPresetIo: element non-objet"; return false; }
			const size_t objStart = arrPos;
			const size_t objEnd = MatchEnd(s, objStart, '{', '}');
			if (objEnd == std::string::npos)
			{
				outError = "BuildingPresetIo: element non terminé"; return false;
			}
			BuildingPresetElement el;
			size_t mp = 0;
			if (SeekKey(s, objStart, objEnd, "mesh", mp)) (void)ReadString(s, mp, el.meshPath);
			el.offset.x = SeekNum(s, objStart, objEnd, "x", 0.0f);
			el.offset.y = SeekNum(s, objStart, objEnd, "y", 0.0f);
			el.offset.z = SeekNum(s, objStart, objEnd, "z", 0.0f);
			el.yawDeg = SeekNum(s, objStart, objEnd, "yaw_deg", 0.0f);
			el.scale = SeekNum(s, objStart, objEnd, "scale", 1.0f);
			el.collisionRadius = SeekNum(s, objStart, objEnd, "collision_radius", 0.0f);
			size_t sp = 0;
			if (SeekKey(s, objStart, objEnd, "solid", sp)) (void)ReadBool(s, sp, el.solid);
			if (!el.meshPath.empty()) out.elements.push_back(std::move(el));
			arrPos = objEnd + 1;
		}
		return true;
	}

	std::string SerializeBuildingPresetJson(const BuildingPreset& p)
	{
		char buf[512];
		std::string out;
		out += "{\n";
		out += "  \"id\": \"" + p.id + "\",\n";
		out += "  \"displayName\": \"" + p.displayName + "\",\n";
		std::snprintf(buf, sizeof(buf),
			"  \"spawnAnchor\": { \"x\": %.3f, \"y\": %.3f, \"z\": %.3f },\n",
			p.spawnAnchor.x, p.spawnAnchor.y, p.spawnAnchor.z);
		out += buf;
		out += "  \"elements\": [\n";
		for (size_t i = 0; i < p.elements.size(); ++i)
		{
			const BuildingPresetElement& e = p.elements[i];
			std::snprintf(buf, sizeof(buf),
				"    { \"mesh\": \"%s\", \"x\": %.3f, \"y\": %.3f, \"z\": %.3f, "
				"\"yaw_deg\": %.3f, \"scale\": %.3f, \"collision_radius\": %.3f, \"solid\": %s }%s\n",
				e.meshPath.c_str(), e.offset.x, e.offset.y, e.offset.z, e.yawDeg,
				e.scale, e.collisionRadius, e.solid ? "true" : "false",
				(i + 1 < p.elements.size()) ? "," : "");
			out += buf;
		}
		out += "  ]\n}\n";
		return out;
	}
}
