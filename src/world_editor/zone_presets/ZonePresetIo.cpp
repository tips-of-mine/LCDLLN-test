#include "src/world_editor/zone_presets/ZonePresetIo.h"

namespace engine::editor::world::zone_presets
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

		/// Position du `}` fermant l'objet ouvert à `p` (`{` à `p`), ou
		/// du `]` fermant le tableau ouvert à `p` (`[` à `p`). Gère
		/// l'imbrication et ignore les accolades dans les chaînes.
		size_t MatchSpanEnd(const std::string& s, size_t p)
		{
			if (p >= s.size()) return std::string::npos;
			const char open  = s[p];
			const char close = (open == '{') ? '}' : (open == '[') ? ']' : '\0';
			if (close == '\0') return std::string::npos;
			int depth = 0;
			bool inString = false;
			for (size_t i = p; i < s.size(); ++i)
			{
				const char c = s[i];
				if (c == '"' && (i == 0 || s[i - 1] != '\\')) inString = !inString;
				if (inString) continue;
				if (c == '{' || c == '[') ++depth;
				else if (c == '}' || c == ']')
				{
					--depth;
					if (depth == 0) return (c == close) ? i : std::string::npos;
				}
			}
			return std::string::npos;
		}

		/// Cherche `"key"` dans `[startScope, endScope)` et positionne le
		/// curseur sur sa valeur (après le `:`).
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

		/// Lit un tableau `["a", "b", ...]` de chaînes. Curseur sur le `[`.
		void ReadStringArray(const std::string& s, size_t p,
			std::vector<std::string>& out)
		{
			const size_t end = MatchSpanEnd(s, p);
			if (end == std::string::npos) return;
			++p; // après le '['
			while (p < end)
			{
				SkipWs(s, p);
				if (p >= end || s[p] == ']') break;
				if (s[p] == ',') { ++p; continue; }
				std::string v;
				if (!ReadString(s, p, v)) break;
				out.push_back(std::move(v));
			}
		}

		/// Lit `{ "fr": "...", "en": "..." }`. Curseur sur le `{`.
		void ReadLocalized(const std::string& s, size_t p, LocalizedString& out)
		{
			const size_t end = MatchSpanEnd(s, p);
			if (end == std::string::npos) return;
			size_t kp = 0u;
			if (SeekKey(s, p, end, "fr", kp)) (void)ReadString(s, kp, out.fr);
			if (SeekKey(s, p, end, "en", kp)) (void)ReadString(s, kp, out.en);
		}

		/// Compte les éléments objets (`{`) de niveau 1 dans le tableau
		/// ouvert à `p` (`[` à `p`). Suffisant pour `decoration` (entrées
		/// = objets selon le spec). `[]` → 0.
		size_t CountArrayObjects(const std::string& s, size_t p)
		{
			const size_t end = MatchSpanEnd(s, p);
			if (end == std::string::npos) return 0u;
			size_t count = 0u;
			int depth = 0;
			bool inString = false;
			for (size_t i = p; i <= end && i < s.size(); ++i)
			{
				const char c = s[i];
				if (c == '"' && (i == 0 || s[i - 1] != '\\')) inString = !inString;
				if (inString) continue;
				if (c == '{')
				{
					if (depth == 1) ++count;  // objet direct dans le tableau
					++depth;
				}
				else if (c == '}') --depth;
				else if (c == '[') ++depth;
				else if (c == ']') --depth;
			}
			return count;
		}
	}

	bool ParseZonePresetJson(const std::string& jsonText, ZonePreset& out,
		std::string& outError)
	{
		out = ZonePreset{};
		const size_t n = jsonText.size();
		size_t pos = 0u;

		// --- Champs simples du top-level ---
		if (SeekKey(jsonText, 0u, n, "version", pos))
		{
			double v = 1.0;
			if (ReadNumber(jsonText, pos, v)) out.version = static_cast<int>(v);
		}
		if (!SeekKey(jsonText, 0u, n, "id", pos) || !ReadString(jsonText, pos, out.id)
			|| out.id.empty())
		{
			outError = "ZonePresetIo: 'id' manquant ou invalide";
			return false;
		}
		if (SeekKey(jsonText, 0u, n, "displayName", pos))
			ReadLocalized(jsonText, pos, out.displayName);
		if (SeekKey(jsonText, 0u, n, "description", pos))
			ReadLocalized(jsonText, pos, out.description);
		if (SeekKey(jsonText, 0u, n, "thumbnail", pos))
			(void)ReadString(jsonText, pos, out.thumbnailPath);
		if (SeekKey(jsonText, 0u, n, "tags", pos))
			ReadStringArray(jsonText, pos, out.tags);
		if (SeekKey(jsonText, 0u, n, "estimatedExecutionSeconds", pos))
		{
			double v = 0.0;
			if (ReadNumber(jsonText, pos, v)) out.estimatedExecutionSeconds = static_cast<float>(v);
		}

		// --- operations[] ---
		size_t opsPos = 0u;
		if (!SeekKey(jsonText, 0u, n, "operations", opsPos))
		{
			outError = "ZonePresetIo[" + out.id + "]: 'operations' manquant";
			return false;
		}
		SkipWs(jsonText, opsPos);
		if (opsPos >= n || jsonText[opsPos] != '[')
		{
			outError = "ZonePresetIo[" + out.id + "]: 'operations' n'est pas un tableau";
			return false;
		}
		const size_t opsEnd = MatchSpanEnd(jsonText, opsPos);
		if (opsEnd == std::string::npos)
		{
			outError = "ZonePresetIo[" + out.id + "]: tableau 'operations' non terminé";
			return false;
		}
		size_t cur = opsPos + 1u;
		while (cur < opsEnd)
		{
			SkipWs(jsonText, cur);
			if (cur >= opsEnd || jsonText[cur] == ']') break;
			if (jsonText[cur] == ',') { ++cur; continue; }
			if (jsonText[cur] != '{')
			{
				outError = "ZonePresetIo[" + out.id + "]: élément 'operations' non-objet";
				return false;
			}
			const size_t objStart = cur;
			const size_t objEnd   = MatchSpanEnd(jsonText, objStart);
			if (objEnd == std::string::npos)
			{
				outError = "ZonePresetIo[" + out.id + "]: objet opération non terminé";
				return false;
			}

			ZonePresetOperation op;
			op.rawJson = jsonText.substr(objStart, objEnd - objStart + 1u);
			size_t kp = 0u;
			if (SeekKey(jsonText, objStart, objEnd, "type", kp))
				(void)ReadString(jsonText, kp, op.type);
			if (SeekKey(jsonText, objStart, objEnd, "preset", kp))
				(void)ReadString(jsonText, kp, op.toolPresetId);
			if (SeekKey(jsonText, objStart, objEnd, "affectedBy", kp))
				ReadStringArray(jsonText, kp, op.affectedBy);

			if (op.type.empty())
			{
				outError = "ZonePresetIo[" + out.id + "]: opération sans 'type'";
				return false;
			}
			out.operations.push_back(std::move(op));
			cur = objEnd + 1u;
		}

		// --- decoration[] (réservée Phase 13 — on compte les entrées) ---
		size_t decoPos = 0u;
		if (SeekKey(jsonText, 0u, n, "decoration", decoPos))
		{
			SkipWs(jsonText, decoPos);
			if (decoPos < n && jsonText[decoPos] == '[')
			{
				out.decorationEntryCount = CountArrayObjects(jsonText, decoPos);
			}
		}

		return true;
	}
}
