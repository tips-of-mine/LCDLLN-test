#include "src/shardd/gameplay/spell/ClassSkillLibrary.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <utility>

namespace engine::server
{
	namespace
	{
		// -------------------------------------------------------------------------
		// Parseur JSON minimal local — copié verbatim depuis SpellKitLibrary.cpp
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

		/// Parseur JSON minimal local aux kits de sorts — même convention que
		/// SpawnerRuntime/CreatureArchetypeLibrary : parseur par module, zéro dépendance.
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

		/// Mappe la chaîne effectKind vers l'enum (false si inconnue).
		bool ParseEffectKind(const std::string& s, ClassSkillEffectKind& out)
		{
			if (s == "Damage") { out = ClassSkillEffectKind::Damage; return true; }
			if (s == "Heal") { out = ClassSkillEffectKind::Heal; return true; }
			if (s == "Defense") { out = ClassSkillEffectKind::Defense; return true; }
			return false;
		}

		/// Mappe la chaîne target vers l'enum (false si inconnue).
		bool ParseTarget(const std::string& s, ClassSkillTarget& out)
		{
			if (s == "SingleEnemy") { out = ClassSkillTarget::SingleEnemy; return true; }
			if (s == "AreaAroundSelf") { out = ClassSkillTarget::AreaAroundSelf; return true; }
			if (s == "SingleAlly") { out = ClassSkillTarget::SingleAlly; return true; }
			return false;
		}

	} // namespace anonyme

	ClassSkillLibrary::ClassSkillLibrary(const engine::core::Config& config)
		: m_config(config)
	{
		LOG_INFO(Net, "[ClassSkillLibrary] Constructed");
	}

	const std::vector<ClassSkillDef>* ClassSkillLibrary::GetClassSkills(std::string_view classId) const
	{
		const auto it = m_classes.find(std::string(classId));
		if (it == m_classes.end()) { return nullptr; }
		return &it->second;
	}

	const ClassSkillDef* ClassSkillLibrary::FindSkill(std::string_view classId, std::string_view skillId) const
	{
		const std::vector<ClassSkillDef>* skills = GetClassSkills(classId);
		if (skills == nullptr) { return nullptr; }
		for (const ClassSkillDef& s : *skills)
		{
			if (s.skillId == skillId) { return &s; }
		}
		return nullptr;
	}

	bool ClassSkillLibrary::LoadClassFromText(std::string_view jsonText, std::string& outError)
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

		std::vector<ClassSkillDef> skills;
		skills.reserve(skillsValue->arrayValue.size());
		for (const JsonValue& entry : skillsValue->arrayValue)
		{
			if (entry.type != JsonType::Object) { outError = "class_skills: entrée non-objet"; return false; }
			ClassSkillDef def{};

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

			def.skillId = idV->stringValue;
			def.name = nameV->stringValue;
			def.branch = branchV->stringValue;
			def.description = (descV != nullptr && descV->type == JsonType::String) ? descV->stringValue : "";

			if (!ParseEffectKind(effV->stringValue, def.effectKind))
			{
				outError = "class_skills: effectKind invalide (" + effV->stringValue + ")";
				return false;
			}
			if (!ParseTarget(tgtV->stringValue, def.target))
			{
				outError = "class_skills: target invalide (" + tgtV->stringValue + ")";
				return false;
			}

			const JsonValue* tierV = FindObjectMember(entry, "tier");
			const JsonValue* levelV = FindObjectMember(entry, "level");
			const JsonValue* castV = FindObjectMember(entry, "castTimeMs");
			const JsonValue* cdV = FindObjectMember(entry, "cooldownMs");
			const JsonValue* costV = FindObjectMember(entry, "resourceCostPercent");

			if (tierV == nullptr || !TryGetUint(*tierV, def.tier)
				|| levelV == nullptr || !TryGetUint(*levelV, def.level)
				|| castV == nullptr || !TryGetUint(*castV, def.castTimeMs)
				|| cdV == nullptr || !TryGetUint(*cdV, def.cooldownMs)
				|| costV == nullptr || !TryGetUint(*costV, def.resourceCostPercent)
				|| def.resourceCostPercent > 100u)
			{
				outError = "class_skills: champ entier invalide";
				return false;
			}

			const JsonValue* powV = FindObjectMember(entry, "powerValue");
			const JsonValue* rngV = FindObjectMember(entry, "rangeMeters");
			const JsonValue* radV = FindObjectMember(entry, "areaRadiusMeters");

			if (powV == nullptr || !TryGetFloat(*powV, def.powerValue) || def.powerValue < 1.0f
				|| rngV == nullptr || !TryGetFloat(*rngV, def.rangeMeters) || def.rangeMeters < 0.0f
				|| radV == nullptr || !TryGetFloat(*radV, def.areaRadiusMeters) || def.areaRadiusMeters < 0.0f)
			{
				outError = "class_skills: champ flottant invalide";
				return false;
			}

			skills.push_back(std::move(def));
		}

		std::sort(skills.begin(), skills.end(),
			[](const ClassSkillDef& a, const ClassSkillDef& b) { return a.level < b.level; });

		m_classes[classId] = std::move(skills);
		return true;
	}

	bool ClassSkillLibrary::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[ClassSkillLibrary] Init ignored: already initialized");
			return true;
		}

		// Même mécanisme de scan que SpellKitLibrary::Init :
		// ResolveContentPath + ListDirectory + ReadAllTextContent.
		const std::filesystem::path classSkillsDirectory =
			engine::platform::FileSystem::ResolveContentPath(m_config, "gameplay/class_skills");
		const std::vector<std::filesystem::path> entries =
			engine::platform::FileSystem::ListDirectory(classSkillsDirectory);

		if (entries.empty())
		{
			LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: no class skill files found ({})",
				classSkillsDirectory.string());
			return false;
		}

		for (const std::filesystem::path& entry : entries)
		{
			if (entry.extension() != ".json")
			{
				continue;
			}

			const std::string relativePath = "gameplay/class_skills/" + entry.filename().string();
			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
			if (jsonText.empty())
			{
				LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: empty or unreadable ({})", relativePath);
				m_classes.clear();
				return false;
			}

			std::string loadError;
			if (!LoadClassFromText(jsonText, loadError))
			{
				LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: {} ({})", loadError, relativePath);
				m_classes.clear();
				return false;
			}
		}

		if (m_classes.empty())
		{
			LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: no class skills loaded");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[ClassSkillLibrary] Init OK ({} classes)", m_classes.size());
		return true;
	}

} // namespace engine::server
