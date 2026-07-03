#include "src/client/quest/QuestTextCatalog.h"

#include "src/shared/platform/FileSystem.h"

#include <cctype>
#include <cstdlib>
#include <string>
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

		/// Analyseur JSON minimal, dédié au catalogue de textes de quête pour
		/// éviter une dépendance externe (miroir de QuestRuntime::JsonParser
		/// côté serveur, dupliqué volontairement : pas de lib JSON partagée
		/// client/serveur aujourd'hui).
		class JsonParser final
		{
		public:
			explicit JsonParser(std::string_view input)
				: m_input(input)
			{
			}

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

		/// Renvoie un membre d'objet JSON, ou `nullptr` si la clé est absente.
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

		/// Remplace toutes les occurrences de `{current}`/`{required}` dans
		/// \p stepTemplate par les valeurs numériques données. Substitution
		/// texte simple (pas de format complexe requis pour l'instant).
		std::string SubstituteStepTemplate(const std::string& stepTemplate, uint32_t current, uint32_t required)
		{
			std::string result = stepTemplate;

			auto replaceAll = [](std::string& text, std::string_view token, const std::string& value)
			{
				size_t pos = 0;
				while ((pos = text.find(token, pos)) != std::string::npos)
				{
					text.replace(pos, token.size(), value);
					pos += value.size();
				}
			};

			replaceAll(result, "{current}", std::to_string(current));
			replaceAll(result, "{required}", std::to_string(required));
			return result;
		}
	}

	bool QuestTextCatalog::Load(const engine::core::Config& cfg, std::string_view locale)
	{
		const std::string localeStr(locale);
		const std::string primaryPath = "quests/quest_texts." + localeStr + ".json";
		if (LoadFromContentPath(cfg, primaryPath))
		{
			return true;
		}

		// Repli sur le français si la locale demandée est absente/invalide.
		if (localeStr == "fr")
		{
			return false;
		}

		const std::string fallbackPath = "quests/quest_texts.fr.json";
		return LoadFromContentPath(cfg, fallbackPath);
	}

	bool QuestTextCatalog::LoadFromContentPath(const engine::core::Config& cfg, const std::string& relativeContentPath)
	{
		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(cfg, relativeContentPath);
		if (jsonText.empty())
		{
			return false;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, parseError) || root.type != JsonType::Object)
		{
			return false;
		}

		std::unordered_map<std::string, QuestTextEntry> parsedEntries;
		for (const auto& [questId, questValue] : root.objectValue)
		{
			if (questValue.type != JsonType::Object)
			{
				continue;
			}

			QuestTextEntry entry{};

			if (const JsonValue* titleValue = FindObjectMember(questValue, "title");
				titleValue != nullptr && titleValue->type == JsonType::String)
			{
				entry.title = titleValue->stringValue;
			}

			if (const JsonValue* descriptionValue = FindObjectMember(questValue, "description");
				descriptionValue != nullptr && descriptionValue->type == JsonType::String)
			{
				entry.description = descriptionValue->stringValue;
			}

			if (const JsonValue* stepsValue = FindObjectMember(questValue, "steps");
				stepsValue != nullptr && stepsValue->type == JsonType::Array)
			{
				for (const JsonValue& stepValue : stepsValue->arrayValue)
				{
					entry.stepTemplates.push_back(stepValue.type == JsonType::String ? stepValue.stringValue : std::string());
				}
			}

			parsedEntries.emplace(questId, std::move(entry));
		}

		m_entries = std::move(parsedEntries);
		return true;
	}

	std::string QuestTextCatalog::Title(std::string_view questId) const
	{
		const auto it = m_entries.find(std::string(questId));
		if (it == m_entries.end() || it->second.title.empty())
		{
			return std::string(questId);
		}

		return it->second.title;
	}

	std::string QuestTextCatalog::Description(std::string_view questId) const
	{
		const auto it = m_entries.find(std::string(questId));
		if (it == m_entries.end())
		{
			return {};
		}

		return it->second.description;
	}

	std::string QuestTextCatalog::StepLabel(std::string_view questId, size_t stepIndex, uint32_t current, uint32_t required) const
	{
		const auto it = m_entries.find(std::string(questId));
		if (it != m_entries.end() && stepIndex < it->second.stepTemplates.size() && !it->second.stepTemplates[stepIndex].empty())
		{
			return SubstituteStepTemplate(it->second.stepTemplates[stepIndex], current, required);
		}

		return std::to_string(current) + "/" + std::to_string(required);
	}
}
