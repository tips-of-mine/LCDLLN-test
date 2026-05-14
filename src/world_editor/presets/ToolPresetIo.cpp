#include "src/world_editor/presets/ToolPresetIo.h"

namespace engine::editor::world::presets
{
	namespace
	{
		void SkipWs(const std::string& s, size_t& p)
		{
			while (p < s.size() &&
				(s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r'))
				++p;
		}

		bool ReadString(const std::string& s, size_t& p, std::string& out)
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

		bool ReadNumber(const std::string& s, size_t& p, double& out)
		{
			SkipWs(s, p);
			const size_t start = p;
			if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
			while (p < s.size() && (
				(s[p] >= '0' && s[p] <= '9') || s[p] == '.' ||
				s[p] == 'e' || s[p] == 'E' || s[p] == '-' || s[p] == '+'))
				++p;
			if (p == start) return false;
			try { out = std::stod(s.substr(start, p - start)); }
			catch (...) { return false; }
			return true;
		}

		/// Position du `}` fermant l'objet ouvert à `p` (`{` à `p`).
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

		size_t MatchArrayEnd(const std::string& s, size_t p)
		{
			if (p >= s.size() || s[p] != '[') return std::string::npos;
			int depth = 0;
			bool inString = false;
			for (size_t i = p; i < s.size(); ++i)
			{
				const char c = s[i];
				if (c == '"' && (i == 0 || s[i - 1] != '\\')) inString = !inString;
				if (inString) continue;
				if (c == '[') ++depth;
				else if (c == ']') { --depth; if (depth == 0) return i; }
			}
			return std::string::npos;
		}

		/// Cherche `"key"` dans `[startScope, endScope)` et positionne le
		/// curseur sur la valeur (après le `:`).
		bool SeekKey(const std::string& s, size_t startScope, size_t endScope,
			const std::string& key, size_t& outPos)
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
					outPos = after;
					return true;
				}
				pos = s.find(needle, after);
			}
			return false;
		}

		/// Parse l'objet `parameters` (clés → nombres). Curseur sur le `{`.
		void ParseParameters(const std::string& s, size_t p,
			std::unordered_map<std::string, double>& out)
		{
			const size_t end = MatchObjectEnd(s, p);
			if (end == std::string::npos) return;
			++p; // après le '{'
			while (p < end)
			{
				SkipWs(s, p);
				if (p >= end || s[p] == '}') break;
				std::string key;
				if (!ReadString(s, p, key)) break;
				SkipWs(s, p);
				if (p >= end || s[p] != ':') break;
				++p;
				double val = 0.0;
				if (!ReadNumber(s, p, val)) break;
				out[key] = val;
				SkipWs(s, p);
				if (p < end && s[p] == ',') { ++p; continue; }
				break;
			}
		}
	}

	bool ParseToolPresetJson(const std::string& jsonText, ToolPresetFile& out,
		std::string& outError)
	{
		out = ToolPresetFile{};

		size_t pos = 0u;
		if (!SeekKey(jsonText, 0u, jsonText.size(), "toolId", pos)
			|| !ReadString(jsonText, pos, out.toolId)
			|| out.toolId.empty())
		{
			outError = "ToolPresetIo: 'toolId' manquant ou invalide";
			return false;
		}

		if (SeekKey(jsonText, 0u, jsonText.size(), "defaultPreset", pos))
		{
			(void)ReadString(jsonText, pos, out.defaultPreset);
		}

		size_t arrPos = 0u;
		if (!SeekKey(jsonText, 0u, jsonText.size(), "presets", arrPos))
		{
			// Pas de tableau presets : toléré, fichier vide de presets.
			return true;
		}
		SkipWs(jsonText, arrPos);
		if (arrPos >= jsonText.size() || jsonText[arrPos] != '[')
		{
			outError = "ToolPresetIo: 'presets' n'est pas un tableau";
			return false;
		}
		const size_t arrEnd = MatchArrayEnd(jsonText, arrPos);
		if (arrEnd == std::string::npos)
		{
			outError = "ToolPresetIo: tableau 'presets' non terminé";
			return false;
		}
		++arrPos; // après le '['

		while (arrPos < arrEnd)
		{
			SkipWs(jsonText, arrPos);
			if (arrPos >= arrEnd || jsonText[arrPos] == ']') break;
			if (jsonText[arrPos] == ',') { ++arrPos; continue; }
			if (jsonText[arrPos] != '{')
			{
				outError = "ToolPresetIo: élément de 'presets' non-objet";
				return false;
			}
			const size_t objStart = arrPos;
			const size_t objEnd   = MatchObjectEnd(jsonText, objStart);
			if (objEnd == std::string::npos)
			{
				outError = "ToolPresetIo: objet preset non terminé";
				return false;
			}

			ToolPreset preset;
			size_t kp = 0u;
			if (SeekKey(jsonText, objStart, objEnd, "id", kp))
				(void)ReadString(jsonText, kp, preset.id);
			if (SeekKey(jsonText, objStart, objEnd, "displayName", kp))
				(void)ReadString(jsonText, kp, preset.displayName);
			if (SeekKey(jsonText, objStart, objEnd, "description", kp))
				(void)ReadString(jsonText, kp, preset.description);
			if (SeekKey(jsonText, objStart, objEnd, "parameters", kp))
				ParseParameters(jsonText, kp, preset.parameters);

			// Preset sans id : ignoré (tolérance — pas d'échec global).
			if (!preset.id.empty())
			{
				if (preset.displayName.empty()) preset.displayName = preset.id;
				out.presets.push_back(std::move(preset));
			}

			arrPos = objEnd + 1u;
		}
		return true;
	}
}
