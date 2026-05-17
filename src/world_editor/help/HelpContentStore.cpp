#include "src/world_editor/help/HelpContentStore.h"

#include "src/shared/core/Log.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace engine::editor::world::help
{
	namespace
	{
		// Parser JSON hand-rolled, miroir de ToolPresetIo.cpp. Spécialisé
		// pour les tooltips (clés string-uniquement, sauf `svgInline` qu'on
		// ignore en MVP).

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

		/// Cherche `"key"` dans `[startScope, endScope)` et positionne le
		/// curseur sur la valeur (après le `:`). Idem ToolPresetIo.
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

		/// Parse un sous-objet tooltip (entre `{` et `}`). Renseigne les
		/// champs string reconnus. Les clés inconnues sont ignorées
		/// (tolérance ; permet d'enrichir le format sans casser le reader).
		void ParseTooltipObject(const std::string& s, size_t objStart,
			size_t objEnd, TooltipDefinition& out)
		{
			size_t kp = 0u;
			auto readStringField = [&](const std::string& key, std::string& dst) {
				if (SeekKey(s, objStart, objEnd, key, kp))
					(void)ReadString(s, kp, dst);
			};
			readStringField("label",                out.label);
			readStringField("description_simple",   out.descriptionSimple);
			readStringField("description_advanced", out.descriptionAdvanced);
			readStringField("defaultValue",         out.defaultValue);
			readStringField("range",                out.range);
			readStringField("docSectionId",         out.docSectionId);
			// `svgInline` non chargé en MVP (cf. docstring du header).
		}
	}

	HelpContentStore& HelpContentStore::Instance()
	{
		static HelpContentStore instance;
		return instance;
	}

	bool ParseTooltipFileJson(const std::string& jsonText,
		TooltipFileContents& out, std::string& outError)
	{
		out = TooltipFileContents{};

		size_t pos = 0u;
		if (!SeekKey(jsonText, 0u, jsonText.size(), "toolId", pos)
			|| !ReadString(jsonText, pos, out.toolId)
			|| out.toolId.empty())
		{
			outError = "HelpContentIo: 'toolId' manquant ou invalide";
			return false;
		}

		size_t tooltipsPos = 0u;
		if (!SeekKey(jsonText, 0u, jsonText.size(), "tooltips", tooltipsPos))
		{
			// Pas de bloc tooltips : toléré, fichier vide de tooltips.
			return true;
		}
		SkipWs(jsonText, tooltipsPos);
		if (tooltipsPos >= jsonText.size() || jsonText[tooltipsPos] != '{')
		{
			outError = "HelpContentIo: 'tooltips' n'est pas un objet";
			return false;
		}
		const size_t tooltipsEnd = MatchObjectEnd(jsonText, tooltipsPos);
		if (tooltipsEnd == std::string::npos)
		{
			outError = "HelpContentIo: objet 'tooltips' non terminé";
			return false;
		}

		// Itère sur les paires `"paramName": { ... }` dans tooltips.
		size_t p = tooltipsPos + 1u;
		while (p < tooltipsEnd)
		{
			SkipWs(jsonText, p);
			if (p >= tooltipsEnd || jsonText[p] == '}') break;
			if (jsonText[p] == ',') { ++p; continue; }

			std::string paramName;
			if (!ReadString(jsonText, p, paramName)) break;
			if (paramName.empty()) break;

			SkipWs(jsonText, p);
			if (p >= tooltipsEnd || jsonText[p] != ':') break;
			++p;
			SkipWs(jsonText, p);
			if (p >= tooltipsEnd || jsonText[p] != '{') break;

			const size_t childStart = p;
			const size_t childEnd   = MatchObjectEnd(jsonText, childStart);
			if (childEnd == std::string::npos || childEnd >= tooltipsEnd) break;

			TooltipDefinition def;
			ParseTooltipObject(jsonText, childStart, childEnd, def);

			// `id` final n'est PAS reconstruit ici (le store le fera côté
			// LoadFromContentPath comme `toolId + "." + paramName`).
			out.tooltips.emplace(paramName, std::move(def));

			p = childEnd + 1u;
		}
		return true;
	}

	size_t HelpContentStore::LoadFromContentPath(const std::string& contentRoot)
	{
		m_lastContentRoot = contentRoot;
		m_tooltips.clear();

		const std::filesystem::path dir =
			std::filesystem::path(contentRoot) / "editor" / "tooltips";
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec) || ec)
		{
			// Dossier absent : pas une erreur (catalogue vide).
			return 0u;
		}

		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (ec) break;
			if (!entry.is_regular_file()) continue;
			if (entry.path().extension() != ".json") continue;

			std::ifstream f(entry.path());
			if (!f.good())
			{
				LOG_WARN(EditorWorld, "[HelpContentStore] illisible : {}",
					entry.path().string());
				continue;
			}
			std::stringstream buf;
			buf << f.rdbuf();

			TooltipFileContents file;
			std::string err;
			if (!ParseTooltipFileJson(buf.str(), file, err))
			{
				LOG_WARN(EditorWorld, "[HelpContentStore] {} ignoré (parse) : {}",
					entry.path().filename().string(), err);
				continue;
			}
			for (auto& kv : file.tooltips)
			{
				const std::string fullId = file.toolId + "." + kv.first;
				kv.second.id = fullId;
				// Si un id en doublon existe (ex. deux fichiers déclarant le
				// même tool), on garde le premier rencontré + warning.
				if (m_tooltips.find(fullId) != m_tooltips.end())
				{
					LOG_WARN(EditorWorld,
						"[HelpContentStore] tooltip id '{}' dupliqué ; conserve la 1ère version",
						fullId);
					continue;
				}
				m_tooltips.emplace(fullId, std::move(kv.second));
			}
		}

		LOG_INFO(EditorWorld, "[HelpContentStore] {} tooltip(s) chargé(s) depuis {}",
			m_tooltips.size(), dir.string());
		return m_tooltips.size();
	}

	size_t HelpContentStore::Reload()
	{
		if (m_lastContentRoot.empty()) return 0u;
		return LoadFromContentPath(m_lastContentRoot);
	}

	const TooltipDefinition* HelpContentStore::FindTooltip(const std::string& id) const
	{
		auto it = m_tooltips.find(id);
		return (it == m_tooltips.end()) ? nullptr : &it->second;
	}

	void HelpContentStore::ResetForTesting()
	{
		m_tooltips.clear();
		m_lastContentRoot.clear();
	}
}
