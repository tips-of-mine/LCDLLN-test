#include "src/world_editor/prefs/UserPrefsStore.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace engine::editor::world::prefs
{
	namespace
	{
		// --- Mini-parseur JSON tolérant (pattern hand-rolled du repo,
		// cf. WorldMapIo / CaveCatalog). Suffisant pour le schéma plat de
		// user_prefs.json — pas de dépendance lib externe.

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

		/// Cherche `"key"` au niveau racine et positionne le curseur sur la
		/// valeur (après le `:`). Tolérant : false si non trouvé.
		bool SeekKey(const std::string& s, const std::string& key, size_t& outPos)
		{
			const std::string needle = "\"" + key + "\"";
			size_t pos = s.find(needle);
			while (pos != std::string::npos)
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

		/// Lit un objet `{ "k": "v", ... }` de valeurs string. Curseur sur
		/// le `{`. Tolérant : s'arrête au `}` correspondant.
		void ReadStringMap(const std::string& s, size_t p,
			std::unordered_map<std::string, std::string>& out)
		{
			SkipWs(s, p);
			if (p >= s.size() || s[p] != '{') return;
			++p;
			while (p < s.size())
			{
				SkipWs(s, p);
				if (p < s.size() && s[p] == '}') return;
				std::string key;
				if (!ReadString(s, p, key)) return;
				SkipWs(s, p);
				if (p >= s.size() || s[p] != ':') return;
				++p;
				std::string val;
				if (!ReadString(s, p, val)) return;
				out[key] = val;
				SkipWs(s, p);
				if (p < s.size() && s[p] == ',') { ++p; continue; }
				if (p < s.size() && s[p] == '}') return;
			}
		}

		/// Lit un objet `{ "k": true/false, ... }`. Curseur sur le `{`.
		void ReadBoolMap(const std::string& s, size_t p,
			std::unordered_map<std::string, bool>& out)
		{
			SkipWs(s, p);
			if (p >= s.size() || s[p] != '{') return;
			++p;
			while (p < s.size())
			{
				SkipWs(s, p);
				if (p < s.size() && s[p] == '}') return;
				std::string key;
				if (!ReadString(s, p, key)) return;
				SkipWs(s, p);
				if (p >= s.size() || s[p] != ':') return;
				++p;
				SkipWs(s, p);
				bool val = false;
				if (s.compare(p, 4, "true") == 0)  { val = true;  p += 4; }
				else if (s.compare(p, 5, "false") == 0) { val = false; p += 5; }
				else return;
				out[key] = val;
				SkipWs(s, p);
				if (p < s.size() && s[p] == ',') { ++p; continue; }
				if (p < s.size() && s[p] == '}') return;
			}
		}

		/// Lit un tableau `[ "a", "b", ... ]` de strings. Curseur sur le `[`.
		/// Tolérant : s'arrête au `]` ou à la première anomalie (le contenu
		/// déjà lu est conservé).
		void ReadStringArray(const std::string& s, size_t p,
			std::vector<std::string>& out)
		{
			SkipWs(s, p);
			if (p >= s.size() || s[p] != '[') return;
			++p;
			while (p < s.size())
			{
				SkipWs(s, p);
				if (p < s.size() && s[p] == ']') return;
				std::string val;
				if (!ReadString(s, p, val)) return;
				out.push_back(std::move(val));
				SkipWs(s, p);
				if (p < s.size() && s[p] == ',') { ++p; continue; }
				if (p < s.size() && s[p] == ']') return;
			}
		}

		std::string EscapeJson(const std::string& in)
		{
			std::string out;
			out.reserve(in.size() + 8u);
			for (char c : in)
			{
				switch (c)
				{
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\t': out += "\\t";  break;
					case '\r': out += "\\r";  break;
					default:   out.push_back(c); break;
				}
			}
			return out;
		}
	}

	UserPrefsStore& UserPrefsStore::Instance()
	{
		static UserPrefsStore instance;
		return instance;
	}

	bool UserPrefsStore::Init(const std::string& contentRoot)
	{
		m_path = std::filesystem::path(contentRoot) / "editor" / "user_prefs.json";
		std::error_code ec;
		const bool existed = std::filesystem::exists(m_path, ec) && !ec;
		if (existed)
		{
			LoadFromDisk();
		}
		else
		{
			// Premier lancement : crée le fichier avec les défauts.
			m_prefs = UserPrefs{};
			(void)SaveToDisk();
		}
		m_initialized = true;
		return existed;
	}

	void UserPrefsStore::LoadFromDisk()
	{
		std::ifstream f(m_path);
		if (!f.good())
		{
			m_prefs = UserPrefs{};
			return;
		}
		std::stringstream buf;
		buf << f.rdbuf();
		const std::string json = buf.str();

		UserPrefs parsed; // démarre sur les défauts ; chaque champ absent les conserve

		size_t pos = 0u;
		if (SeekKey(json, "version", pos))
		{
			try { parsed.version = std::stoi(json.substr(pos, 16)); }
			catch (...) { parsed.version = kUserPrefsSchemaVersion; }
		}
		if (SeekKey(json, "editorMode", pos))
		{
			std::string modeStr;
			if (ReadString(json, pos, modeStr))
				parsed.editorMode = modes::FromString(modeStr.c_str());
		}
		if (SeekKey(json, "showAdvancedTooltips", pos))
		{
			SkipWs(json, pos);
			parsed.showAdvancedTooltips = (json.compare(pos, 4, "true") == 0);
		}
		if (SeekKey(json, "lastPresetByTool", pos))
		{
			ReadStringMap(json, pos, parsed.lastPresetByTool);
		}
		if (SeekKey(json, "tutorialCompletionFlags", pos))
		{
			ReadBoolMap(json, pos, parsed.tutorialCompletionFlags);
		}
		if (SeekKey(json, "recentMapIds", pos))
		{
			ReadStringArray(json, pos, parsed.recentMapIds);
			if (parsed.recentMapIds.size() > kMaxRecentMaps)
			{
				parsed.recentMapIds.resize(kMaxRecentMaps);
			}
		}

		m_prefs = std::move(parsed);
	}

	bool UserPrefsStore::SaveToDisk() const
	{
		if (m_path.empty()) return false;
		std::error_code ec;
		std::filesystem::create_directories(m_path.parent_path(), ec);
		if (ec) return false;

		std::ostringstream out;
		out << "{\n";
		out << "  \"version\": " << kUserPrefsSchemaVersion << ",\n";
		out << "  \"editorMode\": \"" << modes::ToString(m_prefs.editorMode) << "\",\n";
		out << "  \"showAdvancedTooltips\": "
		    << (m_prefs.showAdvancedTooltips ? "true" : "false") << ",\n";

		out << "  \"lastPresetByTool\": {";
		{
			bool first = true;
			for (const auto& kv : m_prefs.lastPresetByTool)
			{
				out << (first ? "\n" : ",\n");
				out << "    \"" << EscapeJson(kv.first) << "\": \""
				    << EscapeJson(kv.second) << "\"";
				first = false;
			}
			out << (first ? "}" : "\n  }") << ",\n";
		}

		out << "  \"tutorialCompletionFlags\": {";
		{
			bool first = true;
			for (const auto& kv : m_prefs.tutorialCompletionFlags)
			{
				out << (first ? "\n" : ",\n");
				out << "    \"" << EscapeJson(kv.first) << "\": "
				    << (kv.second ? "true" : "false");
				first = false;
			}
			out << (first ? "}" : "\n  }") << ",\n";
		}

		out << "  \"recentMapIds\": [";
		{
			bool first = true;
			for (const std::string& id : m_prefs.recentMapIds)
			{
				out << (first ? "\n" : ",\n");
				out << "    \"" << EscapeJson(id) << "\"";
				first = false;
			}
			out << (first ? "]" : "\n  ]") << "\n";
		}
		out << "}\n";

		// Sauvegarde atomique : .tmp puis rename.
		const std::filesystem::path tmp = m_path.string() + ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			if (!f.good()) return false;
			const std::string payload = out.str();
			f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
			f.flush();
			if (!f.good()) return false;
		}
		std::filesystem::rename(tmp, m_path, ec);
		if (ec)
		{
			// rename inter-filesystem improbable ici, mais on retombe sur
			// une copie + remove pour ne pas perdre la sauvegarde.
			std::filesystem::copy_file(tmp, m_path,
				std::filesystem::copy_options::overwrite_existing, ec);
			std::filesystem::remove(tmp, ec);
			return !ec;
		}
		return true;
	}

	void UserPrefsStore::Set(const UserPrefs& prefs)
	{
		m_prefs = prefs;
		(void)SaveToDisk();
	}

	void UserPrefsStore::SetEditorMode(modes::EditorMode mode)
	{
		if (m_prefs.editorMode == mode) return;
		m_prefs.editorMode = mode;
		(void)SaveToDisk();
	}

	std::string UserPrefsStore::GetLastPresetForTool(const std::string& toolId) const
	{
		auto it = m_prefs.lastPresetByTool.find(toolId);
		return (it == m_prefs.lastPresetByTool.end()) ? std::string{} : it->second;
	}

	void UserPrefsStore::SetLastPresetForTool(const std::string& toolId,
		const std::string& presetId)
	{
		auto it = m_prefs.lastPresetByTool.find(toolId);
		if (it != m_prefs.lastPresetByTool.end() && it->second == presetId) return;
		m_prefs.lastPresetByTool[toolId] = presetId;
		(void)SaveToDisk();
	}

	bool UserPrefsStore::GetTutorialFlag(const std::string& flagId) const
	{
		auto it = m_prefs.tutorialCompletionFlags.find(flagId);
		return (it != m_prefs.tutorialCompletionFlags.end()) && it->second;
	}

	void UserPrefsStore::SetTutorialFlag(const std::string& flagId, bool value)
	{
		auto it = m_prefs.tutorialCompletionFlags.find(flagId);
		if (it != m_prefs.tutorialCompletionFlags.end() && it->second == value) return;
		m_prefs.tutorialCompletionFlags[flagId] = value;
		(void)SaveToDisk();
	}

	void UserPrefsStore::PushRecentMap(const std::string& zoneId)
	{
		if (zoneId.empty()) return;
		std::vector<std::string>& recents = m_prefs.recentMapIds;
		// Dédoublonnage : une occurrence existante est retirée avant la
		// réinsertion en tête (la carte « remonte »).
		for (auto it = recents.begin(); it != recents.end(); )
		{
			if (*it == zoneId) it = recents.erase(it);
			else ++it;
		}
		recents.insert(recents.begin(), zoneId);
		if (recents.size() > kMaxRecentMaps)
		{
			recents.resize(kMaxRecentMaps);
		}
		(void)SaveToDisk();
	}

	void UserPrefsStore::ResetForTesting()
	{
		m_prefs = UserPrefs{};
		m_path.clear();
		m_initialized = false;
	}
}
