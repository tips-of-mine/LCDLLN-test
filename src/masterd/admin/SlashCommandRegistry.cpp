// Implementation SlashCommandRegistry : parsing JSON minimal embarque
// (le projet n'a pas de bibliotheque JSON publique exposee, et le parser
// dans Config.cpp est interne anonymous-namespace). On reste minimaliste :
// on n'a besoin que d'objets, arrays, strings, et booleens. Pas de support
// numbers ni escapes complexes : les valeurs utilisees dans
// slash_commands.json sont strings + arrays + booleans + un seul int
// "_version" qu'on ignore.

#include "src/masterd/admin/SlashCommandRegistry.h"

#include "src/shared/core/Log.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace engine::server
{
	namespace
	{
		// ---- Mini parser JSON ------------------------------------------
		// Subset suffisant pour slash_commands.json : object, array, string,
		// number (skipped), bool, null. Les escapes pris en charge sont \"
		// \\ \/ \b \f \n \r \t.

		struct JsonNode;
		using JsonObject = std::unordered_map<std::string, JsonNode>;
		using JsonArray  = std::vector<JsonNode>;

		struct JsonNode
		{
			enum class Type { Null, Bool, Number, String, Array, Object };
			Type        type = Type::Null;
			bool        b    = false;
			double      n    = 0.0;
			std::string s;
			JsonArray   a;
			JsonObject  o;
		};

		/// Avance le curseur sur les espaces (incluant tabs / newlines).
		void SkipWs(const std::string& src, size_t& pos)
		{
			while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
				++pos;
		}

		bool ParseValue(const std::string& src, size_t& pos, JsonNode& out);

		/// Parse une chaine JSON entre guillemets (escapes basiques).
		bool ParseStringLit(const std::string& src, size_t& pos, std::string& out)
		{
			if (pos >= src.size() || src[pos] != '"') return false;
			++pos;
			out.clear();
			while (pos < src.size())
			{
				const char c = src[pos++];
				if (c == '"') return true;
				if (c == '\\')
				{
					if (pos >= src.size()) return false;
					const char e = src[pos++];
					switch (e)
					{
					case '"':  out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/':  out.push_back('/'); break;
					case 'b':  out.push_back('\b'); break;
					case 'f':  out.push_back('\f'); break;
					case 'n':  out.push_back('\n'); break;
					case 'r':  out.push_back('\r'); break;
					case 't':  out.push_back('\t'); break;
					case 'u':
						// On skip les 4 hex chars (pas de support unicode complet).
						if (pos + 4 > src.size()) return false;
						pos += 4;
						out.push_back('?');
						break;
					default:   return false;
					}
				}
				else
				{
					out.push_back(c);
				}
			}
			return false;
		}

		/// Parse un object {"key":value, ...}.
		bool ParseObject(const std::string& src, size_t& pos, JsonNode& out)
		{
			if (pos >= src.size() || src[pos] != '{') return false;
			++pos;
			out.type = JsonNode::Type::Object;
			SkipWs(src, pos);
			if (pos < src.size() && src[pos] == '}') { ++pos; return true; }
			while (pos < src.size())
			{
				SkipWs(src, pos);
				std::string key;
				if (!ParseStringLit(src, pos, key)) return false;
				SkipWs(src, pos);
				if (pos >= src.size() || src[pos] != ':') return false;
				++pos;
				SkipWs(src, pos);
				JsonNode val;
				if (!ParseValue(src, pos, val)) return false;
				out.o.emplace(std::move(key), std::move(val));
				SkipWs(src, pos);
				if (pos < src.size() && src[pos] == ',') { ++pos; continue; }
				if (pos < src.size() && src[pos] == '}') { ++pos; return true; }
				return false;
			}
			return false;
		}

		/// Parse un array [val, val, ...].
		bool ParseArray(const std::string& src, size_t& pos, JsonNode& out)
		{
			if (pos >= src.size() || src[pos] != '[') return false;
			++pos;
			out.type = JsonNode::Type::Array;
			SkipWs(src, pos);
			if (pos < src.size() && src[pos] == ']') { ++pos; return true; }
			while (pos < src.size())
			{
				SkipWs(src, pos);
				JsonNode val;
				if (!ParseValue(src, pos, val)) return false;
				out.a.push_back(std::move(val));
				SkipWs(src, pos);
				if (pos < src.size() && src[pos] == ',') { ++pos; continue; }
				if (pos < src.size() && src[pos] == ']') { ++pos; return true; }
				return false;
			}
			return false;
		}

		/// Parse un nombre JSON (entier ou decimal). Accepte le signe negatif
		/// et la notation scientifique 'e'/'E'. La valeur n'est pas utilisee
		/// par les consommateurs actuels mais on remplit \c n.
		bool ParseNumber(const std::string& src, size_t& pos, JsonNode& out)
		{
			const size_t start = pos;
			if (pos < src.size() && src[pos] == '-') ++pos;
			bool any = false;
			while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
			{
				any = true;
				++pos;
			}
			if (pos < src.size() && src[pos] == '.')
			{
				++pos;
				while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
				{
					any = true;
					++pos;
				}
			}
			if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E'))
			{
				++pos;
				if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
				while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
				{
					any = true;
					++pos;
				}
			}
			if (!any) return false;
			out.type = JsonNode::Type::Number;
			try { out.n = std::stod(src.substr(start, pos - start)); }
			catch (...) { return false; }
			return true;
		}

		/// Parse n'importe quelle valeur JSON.
		bool ParseValue(const std::string& src, size_t& pos, JsonNode& out)
		{
			SkipWs(src, pos);
			if (pos >= src.size()) return false;
			const char c = src[pos];
			if (c == '{') return ParseObject(src, pos, out);
			if (c == '[') return ParseArray(src, pos, out);
			if (c == '"')
			{
				out.type = JsonNode::Type::String;
				return ParseStringLit(src, pos, out.s);
			}
			if (c == 't' && src.compare(pos, 4, "true") == 0)
			{
				pos += 4; out.type = JsonNode::Type::Bool; out.b = true; return true;
			}
			if (c == 'f' && src.compare(pos, 5, "false") == 0)
			{
				pos += 5; out.type = JsonNode::Type::Bool; out.b = false; return true;
			}
			if (c == 'n' && src.compare(pos, 4, "null") == 0)
			{
				pos += 4; out.type = JsonNode::Type::Null; return true;
			}
			return ParseNumber(src, pos, out);
		}

		/// Lit un fichier complet en memoire.
		bool ReadFile(const std::string& path, std::string& out)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) return false;
			std::ostringstream ss;
			ss << f.rdbuf();
			out = ss.str();
			return true;
		}

		/// Extrait le prefixe canonique d'une commande declaree :
		/// "/sky moon <phase 0..15>" -> "/sky moon"
		/// "/ban <player> <reason>"  -> "/ban"
		/// "/who"                    -> "/who"
		///
		/// Regle : on coupe au premier espace suivi par '<' ou par toute autre
		/// chose qui n'est pas un mot. En pratique on coupe au premier '<'
		/// (en remontant le dernier espace pour ne pas garder l'espace).
		std::string ExtractCanonical(const std::string& declared)
		{
			const size_t lt = declared.find('<');
			if (lt == std::string::npos)
				return declared; // pas de placeholder, c'est deja canonique
			// Remonte le dernier espace avant le '<' pour exclure le separateur.
			size_t end = lt;
			while (end > 0 && std::isspace(static_cast<unsigned char>(declared[end - 1])))
				--end;
			return declared.substr(0, end);
		}

		/// Lookup helper : retourne pointeur vers l'entree d'un objet
		/// JSON-Object ou nullptr si la cle est absente / pas du bon type.
		const JsonNode* FindString(const JsonObject& o, const char* key)
		{
			auto it = o.find(key);
			if (it == o.end() || it->second.type != JsonNode::Type::String) return nullptr;
			return &it->second;
		}
		const JsonNode* FindArray(const JsonObject& o, const char* key)
		{
			auto it = o.find(key);
			if (it == o.end() || it->second.type != JsonNode::Type::Array) return nullptr;
			return &it->second;
		}
	}

	bool SlashCommandRegistry::LoadFromFile(const std::string& jsonPath)
	{
		std::string raw;
		if (!ReadFile(jsonPath, raw))
		{
			LOG_WARN(Net, "[SlashCommandRegistry] cannot open {}", jsonPath);
			return false;
		}

		size_t pos = 0;
		JsonNode root;
		if (!ParseValue(raw, pos, root) || root.type != JsonNode::Type::Object)
		{
			LOG_WARN(Net, "[SlashCommandRegistry] JSON parse failed for {}", jsonPath);
			return false;
		}

		const JsonNode* commandsArr = FindArray(root.o, "commands");
		if (!commandsArr)
		{
			LOG_WARN(Net, "[SlashCommandRegistry] missing 'commands' array in {}", jsonPath);
			return false;
		}

		m_entries.clear();
		m_byName.clear();
		m_entries.reserve(commandsArr->a.size());

		for (const JsonNode& item : commandsArr->a)
		{
			if (item.type != JsonNode::Type::Object) continue;
			const JsonNode* cmdNode = FindString(item.o, "command");
			if (!cmdNode || cmdNode->s.empty()) continue;

			SlashCommandEntry entry;
			entry.command = ExtractCanonical(cmdNode->s);

			if (const JsonNode* aliasArr = FindArray(item.o, "aliases"))
			{
				for (const JsonNode& aliasItem : aliasArr->a)
				{
					if (aliasItem.type == JsonNode::Type::String && !aliasItem.s.empty())
						entry.aliases.push_back(aliasItem.s);
				}
			}

			AccountRole minRole = AccountRole::Player;
			if (const JsonNode* roleNode = FindString(item.o, "minRole"))
				minRole = ParseRole(roleNode->s);
			entry.minRole = minRole;

			if (const JsonNode* statusNode = FindString(item.o, "status"))
				entry.status = statusNode->s;

			const size_t idx = m_entries.size();
			m_entries.push_back(std::move(entry));

			// Index par canonique + alias.
			m_byName[m_entries[idx].command] = idx;
			for (const auto& alias : m_entries[idx].aliases)
				m_byName[alias] = idx;
		}

		LOG_INFO(Net, "[SlashCommandRegistry] loaded {} commands from {}",
			m_entries.size(), jsonPath);
		return !m_entries.empty();
	}

	const SlashCommandEntry* SlashCommandRegistry::Lookup(const std::string& command) const
	{
		auto it = m_byName.find(command);
		if (it == m_byName.end()) return nullptr;
		return &m_entries[it->second];
	}

	SlashCommandRegistry::CheckResult SlashCommandRegistry::Check(
		const std::string& command, AccountRole userRole) const
	{
		CheckResult r;
		const SlashCommandEntry* e = Lookup(command);
		if (!e)
		{
			r.found = false;
			r.allowed = false;
			r.minRequired = AccountRole::Player;
			return r;
		}
		r.found = true;
		r.minRequired = e->minRole;
		r.allowed = (userRole >= e->minRole);
		return r;
	}
}
