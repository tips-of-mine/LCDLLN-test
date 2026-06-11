#include "src/shardd/gameplay/creature/CreatureArchetypeLibrary.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::server
{
	namespace
	{
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

		/// Parseur JSON minimal local au catalogue d'archétypes — même convention
		/// que SpawnerRuntime : parseur par module, aucune dépendance nouvelle.
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
		bool TryGetFloat(const JsonValue& value, float& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue))
			{
				return false;
			}

			outValue = static_cast<float>(value.numberValue);
			return true;
		}
	}

	CreatureArchetypeLibrary::CreatureArchetypeLibrary(const engine::core::Config& config)
		: m_config(config)
	{
		LOG_INFO(Net, "[CreatureArchetypeLibrary] Constructed");
	}

	bool CreatureArchetypeLibrary::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[CreatureArchetypeLibrary] Init ignored: already initialized");
			return true;
		}

		const std::string relativePath = "creatures/archetypes.json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
		if (jsonText.empty())
		{
			LOG_ERROR(Net, "[CreatureArchetypeLibrary] Init FAILED: empty or missing file ({})", relativePath);
			return false;
		}

		std::string loadError;
		if (!LoadFromText(jsonText, loadError))
		{
			LOG_ERROR(Net, "[CreatureArchetypeLibrary] Init FAILED: {} ({})", loadError, relativePath);
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[CreatureArchetypeLibrary] Init OK (archetypes={})", m_archetypes.size());
		return true;
	}

	const CreatureArchetype* CreatureArchetypeLibrary::Find(uint32_t archetypeId) const
	{
		const auto it = m_archetypes.find(archetypeId);
		if (it == m_archetypes.end())
		{
			return nullptr;
		}

		return &it->second;
	}

	bool CreatureArchetypeLibrary::LoadFromText(std::string_view jsonText, std::string& outError)
	{
		m_archetypes.clear();

		JsonValue root;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, outError))
		{
			return false;
		}

		const JsonValue* archetypesValue = FindObjectMember(root, "archetypes");
		if (archetypesValue == nullptr || archetypesValue->type != JsonType::Array || archetypesValue->arrayValue.empty())
		{
			outError = "root.archetypes must be a non-empty array";
			return false;
		}

		for (size_t index = 0; index < archetypesValue->arrayValue.size(); ++index)
		{
			const JsonValue& entry = archetypesValue->arrayValue[index];
			if (entry.type != JsonType::Object)
			{
				outError = "archetypes[" + std::to_string(index) + "] must be an object";
				m_archetypes.clear();
				return false;
			}

			CreatureArchetype archetype{};

			const JsonValue* idValue = FindObjectMember(entry, "id");
			if (idValue == nullptr || !TryGetUint(*idValue, archetype.archetypeId) || archetype.archetypeId == 0)
			{
				outError = "archetypes[" + std::to_string(index) + "].id must be a positive integer";
				m_archetypes.clear();
				return false;
			}

			const std::string entryLabel = "archetype id=" + std::to_string(archetype.archetypeId);

			const JsonValue* nameValue = FindObjectMember(entry, "name");
			if (nameValue == nullptr || nameValue->type != JsonType::String || nameValue->stringValue.empty())
			{
				outError = entryLabel + ": name must be a non-empty string";
				m_archetypes.clear();
				return false;
			}
			archetype.name = nameValue->stringValue;

			const JsonValue* levelValue = FindObjectMember(entry, "level");
			if (levelValue == nullptr || !TryGetUint(*levelValue, archetype.level) || archetype.level == 0)
			{
				outError = entryLabel + ": level must be a positive integer";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* statsValue = FindObjectMember(entry, "stats");
			if (statsValue == nullptr || statsValue->type != JsonType::Object)
			{
				outError = entryLabel + ": stats must be an object";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* hpValue = FindObjectMember(*statsValue, "hp");
			if (hpValue == nullptr || !TryGetUint(*hpValue, archetype.hp) || archetype.hp == 0)
			{
				outError = entryLabel + ": stats.hp must be a positive integer";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* damageValue = FindObjectMember(*statsValue, "damage");
			if (damageValue == nullptr || !TryGetUint(*damageValue, archetype.damage))
			{
				outError = entryLabel + ": stats.damage must be an unsigned integer";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* accuracyValue = FindObjectMember(*statsValue, "accuracy");
			if (accuracyValue == nullptr || !TryGetFloat(*accuracyValue, archetype.accuracy))
			{
				outError = entryLabel + ": stats.accuracy must be a finite number";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* rangeValue = FindObjectMember(*statsValue, "rangeMeters");
			if (rangeValue == nullptr || !TryGetFloat(*rangeValue, archetype.rangeMeters) || archetype.rangeMeters <= 0.0f)
			{
				outError = entryLabel + ": stats.rangeMeters must be a positive number";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* critRateValue = FindObjectMember(*statsValue, "critRate");
			if (critRateValue == nullptr || !TryGetFloat(*critRateValue, archetype.critRate))
			{
				outError = entryLabel + ": stats.critRate must be a finite number";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* critMultValue = FindObjectMember(*statsValue, "critMult");
			if (critMultValue == nullptr || !TryGetFloat(*critMultValue, archetype.critMult))
			{
				outError = entryLabel + ": stats.critMult must be a finite number";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* attackPeriodValue = FindObjectMember(*statsValue, "attackPeriodMs");
			if (attackPeriodValue == nullptr
				|| !TryGetUint(*attackPeriodValue, archetype.attackPeriodMs)
				|| archetype.attackPeriodMs == 0)
			{
				outError = entryLabel + ": stats.attackPeriodMs must be a positive integer";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* xpRewardValue = FindObjectMember(entry, "xpReward");
			if (xpRewardValue == nullptr || !TryGetUint(*xpRewardValue, archetype.xpReward))
			{
				outError = entryLabel + ": xpReward must be an unsigned integer";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* modelValue = FindObjectMember(entry, "model");
			if (modelValue == nullptr || modelValue->type != JsonType::Object)
			{
				outError = entryLabel + ": model must be an object";
				m_archetypes.clear();
				return false;
			}

			const JsonValue* meshValue = FindObjectMember(*modelValue, "mesh");
			if (meshValue == nullptr || meshValue->type != JsonType::String || meshValue->stringValue.empty())
			{
				outError = entryLabel + ": model.mesh must be a non-empty string";
				m_archetypes.clear();
				return false;
			}
			archetype.meshKey = meshValue->stringValue;

			const JsonValue* scaleValue = FindObjectMember(*modelValue, "scale");
			if (scaleValue != nullptr)
			{
				if (!TryGetFloat(*scaleValue, archetype.scale) || archetype.scale <= 0.0f)
				{
					outError = entryLabel + ": model.scale must be a positive number";
					m_archetypes.clear();
					return false;
				}
			}

			const uint32_t archetypeId = archetype.archetypeId;
			if (!m_archetypes.emplace(archetypeId, std::move(archetype)).second)
			{
				outError = "duplicate archetype id " + std::to_string(archetypeId);
				m_archetypes.clear();
				return false;
			}

			LOG_INFO(Net,
				"[CreatureArchetypeLibrary] Loaded archetype (id={}, name={}, level={}, hp={}, damage={}, xp_reward={})",
				archetypeId,
				m_archetypes.at(archetypeId).name,
				m_archetypes.at(archetypeId).level,
				m_archetypes.at(archetypeId).hp,
				m_archetypes.at(archetypeId).damage,
				m_archetypes.at(archetypeId).xpReward);
		}

		return true;
	}
}
