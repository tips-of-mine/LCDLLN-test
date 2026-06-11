#include "src/client/gameplay/SpellKitCatalog.h"

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

namespace engine::client
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
	}

	bool SpellKitCatalog::Init(const engine::core::Config& config)
	{
		const std::filesystem::path spellsDirectory =
			engine::platform::FileSystem::ResolveContentPath(config, "gameplay/spells");
		const std::vector<std::filesystem::path> entries =
			engine::platform::FileSystem::ListDirectory(spellsDirectory);
		if (entries.empty())
		{
			LOG_WARN(Core, "[SpellKitCatalog] Init skipped: no spell kit files ({}) — barre d'action masquée",
				spellsDirectory.string());
			return false;
		}

		size_t loadedCount = 0;
		for (const std::filesystem::path& entry : entries)
		{
			if (entry.extension() != ".json")
			{
				continue;
			}

			const std::string relativePath = "gameplay/spells/" + entry.filename().string();
			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(config, relativePath);
			if (jsonText.empty())
			{
				LOG_WARN(Core, "[SpellKitCatalog] Kit skipped: empty or unreadable ({})", relativePath);
				continue;
			}

			std::string loadError;
			if (!LoadKitFromText(jsonText, loadError))
			{
				LOG_WARN(Core, "[SpellKitCatalog] Kit skipped: {} ({})", loadError, relativePath);
				continue;
			}
			++loadedCount;
		}

		LOG_INFO(Core, "[SpellKitCatalog] Init OK (kits={})", loadedCount);
		return loadedCount > 0;
	}

	const std::vector<SpellDisplay>* SpellKitCatalog::FindKit(std::string_view profile) const
	{
		const auto it = m_kits.find(std::string(profile));
		if (it == m_kits.end())
		{
			return nullptr;
		}

		return &it->second;
	}

	std::string SpellKitCatalog::ResolveSpellName(std::string_view spellId) const
	{
		for (const auto& [profile, kit] : m_kits)
		{
			for (const SpellDisplay& spell : kit)
			{
				if (spell.spellId == spellId)
				{
					return spell.name;
				}
			}
		}

		return std::string(spellId);
	}

	bool SpellKitCatalog::LoadKitFromText(std::string_view jsonText, std::string& outError)
	{
		JsonValue root;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, outError))
		{
			return false;
		}

		const JsonValue* profileValue = FindObjectMember(root, "profile");
		if (profileValue == nullptr || profileValue->type != JsonType::String || profileValue->stringValue.empty())
		{
			outError = "root.profile must be a non-empty string";
			return false;
		}

		const JsonValue* spellsValue = FindObjectMember(root, "spells");
		if (spellsValue == nullptr || spellsValue->type != JsonType::Array || spellsValue->arrayValue.empty())
		{
			outError = "root.spells must be a non-empty array";
			return false;
		}

		std::vector<SpellDisplay> kit;
		for (size_t index = 0; index < spellsValue->arrayValue.size(); ++index)
		{
			const JsonValue& entry = spellsValue->arrayValue[index];
			if (entry.type != JsonType::Object)
			{
				outError = "spells[" + std::to_string(index) + "] must be an object";
				return false;
			}

			SpellDisplay spell{};
			const JsonValue* idValue = FindObjectMember(entry, "id");
			const JsonValue* nameValue = FindObjectMember(entry, "name");
			const JsonValue* slotValue = FindObjectMember(entry, "slot");
			const JsonValue* castValue = FindObjectMember(entry, "castTimeMs");
			const JsonValue* cooldownValue = FindObjectMember(entry, "cooldownMs");
			const JsonValue* costValue = FindObjectMember(entry, "resourceCostPercent");
			const JsonValue* targetValue = FindObjectMember(entry, "targetType");
			if (idValue == nullptr || idValue->type != JsonType::String || idValue->stringValue.empty()
				|| nameValue == nullptr || nameValue->type != JsonType::String || nameValue->stringValue.empty()
				|| slotValue == nullptr || !TryGetUint(*slotValue, spell.slot) || spell.slot < 1 || spell.slot > 4
				|| castValue == nullptr || !TryGetUint(*castValue, spell.castTimeMs)
				|| cooldownValue == nullptr || !TryGetUint(*cooldownValue, spell.cooldownMs)
				|| costValue == nullptr || !TryGetUint(*costValue, spell.resourceCostPercent)
				|| targetValue == nullptr || targetValue->type != JsonType::String)
			{
				outError = "spells[" + std::to_string(index) + "]: missing or invalid display field";
				return false;
			}
			spell.spellId = idValue->stringValue;
			spell.name = nameValue->stringValue;
			spell.needsEnemyTarget = (targetValue->stringValue == "SingleEnemy");
			kit.push_back(std::move(spell));
		}

		std::sort(kit.begin(), kit.end(),
			[](const SpellDisplay& a, const SpellDisplay& b) { return a.slot < b.slot; });
		m_kits[profileValue->stringValue] = std::move(kit);
		return true;
	}
}
