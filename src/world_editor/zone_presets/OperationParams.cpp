#include "src/world_editor/zone_presets/OperationParams.h"

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

		/// Position du `}`/`]` fermant le span ouvert à `p`, gère
		/// l'imbrication et ignore les délimiteurs dans les chaînes.
		size_t MatchSpanEnd(const std::string& s, size_t p)
		{
			if (p >= s.size()) return std::string::npos;
			const char open = s[p];
			if (open != '{' && open != '[') return std::string::npos;
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
					if (depth == 0) return i;
				}
			}
			return std::string::npos;
		}

		/// Aplatit récursivement un tableau de nombres / tableaux de
		/// nombres en `out`. Curseur sur le `[`. \return false si une
		/// valeur non numérique/non-tableau est rencontrée.
		bool FlattenNumberArray(const std::string& s, size_t p, std::vector<double>& out)
		{
			const size_t end = MatchSpanEnd(s, p);
			if (end == std::string::npos) return false;
			++p; // après le '['
			while (p < end)
			{
				SkipWs(s, p);
				if (p >= end || s[p] == ']') break;
				if (s[p] == ',') { ++p; continue; }
				if (s[p] == '[')
				{
					if (!FlattenNumberArray(s, p, out)) return false;
					p = MatchSpanEnd(s, p) + 1u;
				}
				else
				{
					double v = 0.0;
					if (!ReadNumber(s, p, v)) return false;
					out.push_back(v);
				}
			}
			return true;
		}
	}

	OperationParams OperationParams::Parse(const std::string& rawJson)
	{
		OperationParams params;
		const std::string& s = rawJson;
		size_t p = 0u;
		SkipWs(s, p);
		if (p >= s.size() || s[p] != '{') return params;
		const size_t objEnd = MatchSpanEnd(s, p);
		if (objEnd == std::string::npos) return params;
		++p; // après le '{'

		while (p < objEnd)
		{
			SkipWs(s, p);
			if (p >= objEnd || s[p] == '}') break;
			if (s[p] == ',') { ++p; continue; }

			std::string key;
			if (!ReadString(s, p, key)) break;
			SkipWs(s, p);
			if (p >= objEnd || s[p] != ':') break;
			++p;
			SkipWs(s, p);
			if (p >= objEnd) break;

			const char c = s[p];
			// Clés structurelles déjà parsées côté ZonePresetOperation :
			// on les saute (mais on consomme quand même leur valeur).
			// P1 (audit 2026-06-05, 6.4) : "preset" N'EST PLUS filtrée — c'est
			// `MaybeApplyToolPreset` (OperationDispatcher) qui la lit via
			// `GetString("preset")` ; la filtrer ici rendait l'overlay de
			// tool-preset silencieusement inopérant (les sims tournaient
			// toujours avec les defaults + overrides scalaires seulement).
			const bool structural =
				(key == "type" || key == "affectedBy");

			if (c == '"')
			{
				std::string v;
				if (!ReadString(s, p, v)) break;
				if (!structural) params.m_values[key] = v;
			}
			else if (c == '[')
			{
				const size_t arrEnd = MatchSpanEnd(s, p);
				if (arrEnd == std::string::npos) break;
				std::vector<double> flat;
				if (!structural && FlattenNumberArray(s, p, flat))
				{
					params.m_values[key] = std::move(flat);
				}
				p = arrEnd + 1u;
			}
			else if (c == '{')
			{
				// Objet imbriqué : ignoré pour cet incrément (aucun param
				// d'opération MVP n'en utilise). On saute proprement.
				const size_t nestedEnd = MatchSpanEnd(s, p);
				if (nestedEnd == std::string::npos) break;
				p = nestedEnd + 1u;
			}
			else if (s.compare(p, 4, "true") == 0)
			{
				if (!structural) params.m_values[key] = true;
				p += 4u;
			}
			else if (s.compare(p, 5, "false") == 0)
			{
				if (!structural) params.m_values[key] = false;
				p += 5u;
			}
			else if (s.compare(p, 4, "null") == 0)
			{
				p += 4u; // valeur null ignorée
			}
			else
			{
				double v = 0.0;
				if (!ReadNumber(s, p, v)) break;
				if (!structural) params.m_values[key] = v;
			}
		}
		return params;
	}

	bool OperationParams::Has(const std::string& key) const
	{
		return m_values.find(key) != m_values.end();
	}

	bool OperationParams::GetNumber(const std::string& key, double& out) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return false;
		if (const double* v = std::get_if<double>(&it->second))
		{
			out = *v;
			return true;
		}
		return false;
	}

	bool OperationParams::GetBool(const std::string& key, bool& out) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return false;
		if (const bool* v = std::get_if<bool>(&it->second))
		{
			out = *v;
			return true;
		}
		return false;
	}

	bool OperationParams::GetString(const std::string& key, std::string& out) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return false;
		if (const std::string* v = std::get_if<std::string>(&it->second))
		{
			out = *v;
			return true;
		}
		return false;
	}

	bool OperationParams::GetNumberList(const std::string& key,
		std::vector<double>& out) const
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return false;
		if (const std::vector<double>* v = std::get_if<std::vector<double>>(&it->second))
		{
			out = *v;
			return true;
		}
		return false;
	}

	void OperationParams::ScaleNumber(const std::string& key, double factor)
	{
		auto it = m_values.find(key);
		if (it == m_values.end()) return;
		if (double* v = std::get_if<double>(&it->second))
		{
			*v *= factor;
		}
	}
}
