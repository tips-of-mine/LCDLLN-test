#include "src/client/gameplay/ClassSkillCatalog.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"
#include "src/client/gameplay/SpellKitCatalog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <utility>

namespace engine::client
{
	namespace
	{
		// -------------------------------------------------------------------------
		// Parseur JSON minimal local — copié verbatim depuis SpellKitCatalog.cpp
		// (convention du repo : parseur par module, zéro dépendance externe).
		// -------------------------------------------------------------------------

		enum class JsonType
		{
			Null,
			Bool,
			Number,
			String,
			Object,
			Array
		};

		struct JsonValue
		{
			JsonType type = JsonType::Null;
			bool boolValue = false;
			double numberValue = 0.0;
			std::string stringValue;
			std::unordered_map<std::string, JsonValue> objectValue;
			std::vector<JsonValue> arrayValue;
		};

		/// Parseur JSON minimal local au catalogue — convention du repo
		/// (SpawnerRuntime/CreatureCatalog) : parseur par module, zéro dépendance.
		class JsonParser final
		{
		public:
			explicit JsonParser(std::string_view input)
				: m_input(input)
			{
			}

			/// Parse le document complet ; rejette tout caractère résiduel.
			bool Parse(JsonValue& outRoot, std::string& outError)
			{
				SkipWhitespace();
				if (!ParseValue(outRoot, outError))
				{
					return false;
				}

				SkipWhitespace();
				if (m_pos != m_input.size())
				{
					outError = "unexpected trailing characters";
					return false;
				}

				return true;
			}

		private:
			void SkipWhitespace()
			{
				while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos])) != 0)
				{
					++m_pos;
				}
			}

			bool Consume(char expected)
			{
				if (m_pos >= m_input.size() || m_input[m_pos] != expected)
				{
					return false;
				}

				++m_pos;
				return true;
			}

			bool StartsWith(std::string_view token) const
			{
				return m_input.substr(m_pos, token.size()) == token;
			}

			bool ParseValue(JsonValue& outValue, std::string& outError)
			{
				if (m_pos >= m_input.size())
				{
					outError = "unexpected end of input";
					return false;
				}

				switch (m_input[m_pos])
				{
				case '{':
					return ParseObject(outValue, outError);
				case '[':
					return ParseArray(outValue, outError);
				case '"':
					outValue = JsonValue{};
					outValue.type = JsonType::String;
					return ParseString(outValue.stringValue, outError);
				default:
					break;
				}

				if (StartsWith("true"))
				{
					m_pos += 4;
					outValue = JsonValue{};
					outValue.type = JsonType::Bool;
					outValue.boolValue = true;
					return true;
				}

				if (StartsWith("false"))
				{
					m_pos += 5;
					outValue = JsonValue{};
					outValue.type = JsonType::Bool;
					outValue.boolValue = false;
					return true;
				}

				if (StartsWith("null"))
				{
					m_pos += 4;
					outValue = JsonValue{};
					outValue.type = JsonType::Null;
					return true;
				}

				return ParseNumber(outValue, outError);
			}

			bool ParseObject(JsonValue& outValue, std::string& outError)
			{
				if (!Consume('{'))
				{
					outError = "expected '{'";
					return false;
				}

				outValue = JsonValue{};
				outValue.type = JsonType::Object;
				SkipWhitespace();
				if (Consume('}'))
				{
					return true;
				}

				while (true)
				{
					std::string key;
					if (!ParseString(key, outError))
					{
						return false;
					}

					SkipWhitespace();
					if (!Consume(':'))
					{
						outError = "expected ':' after object key";
						return false;
					}

					SkipWhitespace();
					JsonValue child;
					if (!ParseValue(child, outError))
					{
						return false;
					}

					outValue.objectValue.emplace(std::move(key), std::move(child));
					SkipWhitespace();
					if (Consume('}'))
					{
						return true;
					}

					if (!Consume(','))
					{
						outError = "expected ',' between object members";
						return false;
					}

					SkipWhitespace();
				}
			}

			bool ParseArray(JsonValue& outValue, std::string& outError)
			{
				if (!Consume('['))
				{
					outError = "expected '['";
					return false;
				}

				outValue = JsonValue{};
				outValue.type = JsonType::Array;
				SkipWhitespace();
				if (Consume(']'))
				{
					return true;
				}

				while (true)
				{
					JsonValue child;
					if (!ParseValue(child, outError))
					{
						return false;
					}

					outValue.arrayValue.emplace_back(std::move(child));
					SkipWhitespace();
					if (Consume(']'))
					{
						return true;
					}

					if (!Consume(','))
					{
						outError = "expected ',' between array entries";
						return false;
					}

					SkipWhitespace();
				}
			}

			bool ParseString(std::string& outValue, std::string& outError)
			{
				if (!Consume('"'))
				{
					outError = "expected string";
					return false;
				}

				outValue.clear();
				while (m_pos < m_input.size())
				{
					const char current = m_input[m_pos++];
					if (current == '"')
					{
						return true;
					}

					if (current != '\\')
					{
						outValue.push_back(current);
						continue;
					}

					if (m_pos >= m_input.size())
					{
						outError = "unterminated escape sequence";
						return false;
					}

					const char escaped = m_input[m_pos++];
					switch (escaped)
					{
					case '"': outValue.push_back('"'); break;
					case '\\': outValue.push_back('\\'); break;
					case '/': outValue.push_back('/'); break;
					case 'b': outValue.push_back('\b'); break;
					case 'f': outValue.push_back('\f'); break;
					case 'n': outValue.push_back('\n'); break;
					case 'r': outValue.push_back('\r'); break;
					case 't': outValue.push_back('\t'); break;
					default:
						outError = "unsupported escape sequence";
						return false;
					}
				}

				outError = "unterminated string";
				return false;
			}

			bool ParseNumber(JsonValue& outValue, std::string& outError)
			{
				const size_t start = m_pos;
				if (m_input[m_pos] == '-')
				{
					++m_pos;
				}

				bool hasDigit = false;
				while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])) != 0)
				{
					hasDigit = true;
					++m_pos;
				}

				if (m_pos < m_input.size() && m_input[m_pos] == '.')
				{
					++m_pos;
					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])) != 0)
					{
						hasDigit = true;
						++m_pos;
					}
				}

				if (m_pos < m_input.size() && (m_input[m_pos] == 'e' || m_input[m_pos] == 'E'))
				{
					++m_pos;
					if (m_pos < m_input.size() && (m_input[m_pos] == '+' || m_input[m_pos] == '-'))
					{
						++m_pos;
					}

					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])) != 0)
					{
						hasDigit = true;
						++m_pos;
					}
				}

				if (!hasDigit)
				{
					outError = "expected number";
					return false;
				}

				const std::string token(m_input.substr(start, m_pos - start));
				char* endPtr = nullptr;
				const double parsedValue = std::strtod(token.c_str(), &endPtr);
				if (endPtr == nullptr || *endPtr != '\0')
				{
					outError = "invalid number";
					return false;
				}

				outValue = JsonValue{};
				outValue.type = JsonType::Number;
				outValue.numberValue = parsedValue;
				return true;
			}

			std::string_view m_input;
			size_t m_pos = 0;
		};

		/// Retourne un membre d'objet, ou nullptr si la clé est absente.
		const JsonValue* FindObjectMember(const JsonValue& object, std::string_view key)
		{
			if (object.type != JsonType::Object)
			{
				return nullptr;
			}

			const auto it = object.objectValue.find(std::string(key));
			if (it == object.objectValue.end())
			{
				return nullptr;
			}

			return &it->second;
		}

		/// Convertit un nombre JSON en uint32 validé (entier, borné, fini).
		bool TryGetUint(const JsonValue& value, uint32_t& outValue)
		{
			if (value.type != JsonType::Number
				|| !std::isfinite(value.numberValue)
				|| value.numberValue < 0.0
				|| value.numberValue > static_cast<double>(std::numeric_limits<uint32_t>::max()))
			{
				return false;
			}

			const double truncated = std::floor(value.numberValue);
			if (std::abs(truncated - value.numberValue) > 0.000001)
			{
				return false;
			}

			outValue = static_cast<uint32_t>(truncated);
			return true;
		}

		/// Convertit un nombre JSON en float fini validé.
		/// Accepte les entiers JSON (ex. "powerValue": 1) car le générateur peut
		/// émettre des entiers sans point décimal.
		bool TryGetFloat(const JsonValue& value, float& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue))
			{
				return false;
			}

			outValue = static_cast<float>(value.numberValue);
			return true;
		}

	} // namespace anonyme

	SpellDisplay ToSpellDisplay(const ClassSkillDisplay& s, std::string_view classId)
	{
		SpellDisplay out{};
		out.spellId              = s.skillId;
		out.name                 = s.name;
		out.slot                 = 1u; // résolu par ResolveActionBarLayout
		out.castTimeMs           = s.castTimeMs;
		out.cooldownMs           = s.cooldownMs;
		out.resourceCostPercent  = s.resourceCostPercent;
		out.needsEnemyTarget     = (s.target == "SingleEnemy");
		out.targetsAlly          = (s.target == "SingleAlly");
		// Chemin d'icône relatif à paths.content, ou "" si pas d'icône / classId vide.
		if (!s.iconFile.empty() && !classId.empty())
		{
			out.iconPath = "icons/skills/" + std::string(classId) + "/" + s.iconFile;
		}
		return out;
	}

	const std::vector<ClassSkillDisplay>* ClassSkillCatalog::GetClassSkills(std::string_view classId) const
	{
		const auto it = m_classes.find(std::string(classId));
		if (it == m_classes.end()) { return nullptr; }
		return &it->second;
	}

	bool ClassSkillCatalog::LoadClassFromText(std::string_view jsonText, std::string& outError)
	{
		JsonParser parser(jsonText);
		JsonValue root;
		if (!parser.Parse(root, outError) || root.type != JsonType::Object)
		{
			if (outError.empty()) { outError = "class_skills: JSON racine invalide"; }
			return false;
		}

		const JsonValue* classIdValue = FindObjectMember(root, "classId");
		if (classIdValue == nullptr || classIdValue->type != JsonType::String || classIdValue->stringValue.empty())
		{
			outError = "class_skills: 'classId' manquant";
			return false;
		}
		const std::string classId = classIdValue->stringValue;

		const JsonValue* skillsValue = FindObjectMember(root, "skills");
		if (skillsValue == nullptr || skillsValue->type != JsonType::Array || skillsValue->arrayValue.empty())
		{
			outError = "class_skills: 'skills' vide ou absent";
			return false;
		}

		std::vector<ClassSkillDisplay> skills;
		skills.reserve(skillsValue->arrayValue.size());
		for (const JsonValue& entry : skillsValue->arrayValue)
		{
			if (entry.type != JsonType::Object) { outError = "class_skills: entrée non-objet"; return false; }
			ClassSkillDisplay disp{};

			const JsonValue* idV = FindObjectMember(entry, "id");
			const JsonValue* nameV = FindObjectMember(entry, "name");
			const JsonValue* branchV = FindObjectMember(entry, "branch");
			const JsonValue* effV = FindObjectMember(entry, "effectKind");
			const JsonValue* tgtV = FindObjectMember(entry, "target");
			const JsonValue* descV = FindObjectMember(entry, "description");

			if (idV == nullptr || idV->type != JsonType::String || idV->stringValue.empty()
				|| nameV == nullptr || nameV->type != JsonType::String
				|| branchV == nullptr || branchV->type != JsonType::String
				|| effV == nullptr || effV->type != JsonType::String
				|| tgtV == nullptr || tgtV->type != JsonType::String)
			{
				outError = "class_skills: champ string manquant";
				return false;
			}

			disp.skillId = idV->stringValue;
			disp.name = nameV->stringValue;
			disp.branch = branchV->stringValue;
			// effectKind et target restent des strings brutes (client tolérant, pas d'enum).
			disp.effectKind = effV->stringValue;
			disp.target = tgtV->stringValue;
			disp.description = (descV != nullptr && descV->type == JsonType::String) ? descV->stringValue : "";
			// iconFile : optionnel et tolérant (absent/non-string → "" → repli texte).
			const JsonValue* iconV = FindObjectMember(entry, "iconFile");
			disp.iconFile = (iconV != nullptr && iconV->type == JsonType::String) ? iconV->stringValue : "";

			const JsonValue* tierV = FindObjectMember(entry, "tier");
			const JsonValue* levelV = FindObjectMember(entry, "level");
			const JsonValue* castV = FindObjectMember(entry, "castTimeMs");
			const JsonValue* cdV = FindObjectMember(entry, "cooldownMs");
			const JsonValue* costV = FindObjectMember(entry, "resourceCostPercent");

			if (tierV == nullptr || !TryGetUint(*tierV, disp.tier)
				|| levelV == nullptr || !TryGetUint(*levelV, disp.level)
				|| castV == nullptr || !TryGetUint(*castV, disp.castTimeMs)
				|| cdV == nullptr || !TryGetUint(*cdV, disp.cooldownMs)
				|| costV == nullptr || !TryGetUint(*costV, disp.resourceCostPercent))
			{
				outError = "class_skills: champ entier invalide";
				return false;
			}

			const JsonValue* powV = FindObjectMember(entry, "powerValue");
			const JsonValue* rngV = FindObjectMember(entry, "rangeMeters");
			const JsonValue* radV = FindObjectMember(entry, "areaRadiusMeters");

			if (powV == nullptr || !TryGetFloat(*powV, disp.powerValue)
				|| rngV == nullptr || !TryGetFloat(*rngV, disp.rangeMeters)
				|| radV == nullptr || !TryGetFloat(*radV, disp.areaRadiusMeters))
			{
				outError = "class_skills: champ flottant invalide";
				return false;
			}

			skills.push_back(std::move(disp));
		}

		std::sort(skills.begin(), skills.end(),
			[](const ClassSkillDisplay& a, const ClassSkillDisplay& b) { return a.level < b.level; });

		m_classes[classId] = std::move(skills);
		return true;
	}

	bool ClassSkillCatalog::Init(const engine::core::Config& config)
	{
		// Même mécanisme de scan que SpellKitCatalog::Init (tolérant) :
		// ResolveContentPath + ListDirectory + ReadAllTextContent.
		const std::filesystem::path classSkillsDirectory =
			engine::platform::FileSystem::ResolveContentPath(config, "gameplay/class_skills");
		const std::vector<std::filesystem::path> entries =
			engine::platform::FileSystem::ListDirectory(classSkillsDirectory);

		if (entries.empty())
		{
			LOG_WARN(Core, "[ClassSkillCatalog] Init skipped: no class skill files ({}) — catalogue vide",
				classSkillsDirectory.string());
			return false;
		}

		size_t loadedCount = 0;
		for (const std::filesystem::path& entry : entries)
		{
			if (entry.extension() != ".json")
			{
				continue;
			}

			const std::string relativePath = "gameplay/class_skills/" + entry.filename().string();
			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(config, relativePath);
			if (jsonText.empty())
			{
				LOG_WARN(Core, "[ClassSkillCatalog] Fichier ignoré: vide ou illisible ({})", relativePath);
				continue;
			}

			std::string loadError;
			if (!LoadClassFromText(jsonText, loadError))
			{
				LOG_WARN(Core, "[ClassSkillCatalog] Fichier ignoré: {} ({})", loadError, relativePath);
				continue;
			}
			++loadedCount;
		}

		LOG_INFO(Core, "[ClassSkillCatalog] Init OK (classes={})", loadedCount);
		return loadedCount > 0;
	}

} // namespace engine::client
