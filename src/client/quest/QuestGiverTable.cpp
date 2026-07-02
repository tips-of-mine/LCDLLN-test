#include "src/client/quest/QuestGiverTable.h"

#include "src/shared/platform/FileSystem.h"

#include <cctype>
#include <cstdlib>
#include <iostream>
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

		/// Analyseur JSON minimal, dédié à la table PNJ->quêtes pour éviter une
		/// dépendance externe (miroir de QuestTextCatalog::JsonParser, dupliqué
		/// volontairement : pas de lib JSON partagée client/serveur aujourd'hui).
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
	}

	bool QuestGiverTable::Load(const engine::core::Config& cfg)
	{
		const std::string relativeContentPath = "quests/quest_givers.json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(cfg, relativeContentPath);
		if (jsonText.empty())
		{
			std::cerr << "[QuestGiverTable] fichier introuvable ou vide : " << relativeContentPath << "\n";
			return false;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, parseError) || root.type != JsonType::Object)
		{
			std::cerr << "[QuestGiverTable] JSON invalide (" << relativeContentPath << ") : " << parseError << "\n";
			return false;
		}

		std::unordered_map<std::string, std::vector<QuestGiverLink>> parsedLinks;
		for (const auto& [npcTargetId, linksValue] : root.objectValue)
		{
			if (linksValue.type != JsonType::Array)
			{
				std::cerr << "[QuestGiverTable] valeur non-tableau pour \"" << npcTargetId << "\" dans "
					<< relativeContentPath << "\n";
				return false;
			}

			std::vector<QuestGiverLink> links;
			links.reserve(linksValue.arrayValue.size());

			for (const JsonValue& linkValue : linksValue.arrayValue)
			{
				const JsonValue* questIdValue = FindObjectMember(linkValue, "questId");
				const JsonValue* roleValue = FindObjectMember(linkValue, "role");

				if (questIdValue == nullptr || questIdValue->type != JsonType::String || questIdValue->stringValue.empty())
				{
					std::cerr << "[QuestGiverTable] questId manquant/vide pour \"" << npcTargetId << "\" dans "
						<< relativeContentPath << "\n";
					return false;
				}

				if (roleValue == nullptr || roleValue->type != JsonType::Number)
				{
					std::cerr << "[QuestGiverTable] role manquant/invalide pour \"" << npcTargetId
						<< "\" (quête \"" << questIdValue->stringValue << "\") dans " << relativeContentPath << "\n";
					return false;
				}

				const double roleNumber = roleValue->numberValue;
				if (roleNumber != 0.0 && roleNumber != 1.0)
				{
					std::cerr << "[QuestGiverTable] role hors {0,1} (" << roleNumber << ") pour \"" << npcTargetId
						<< "\" (quête \"" << questIdValue->stringValue << "\") dans " << relativeContentPath << "\n";
					return false;
				}

				QuestGiverLink link{};
				link.questId = questIdValue->stringValue;
				link.role = static_cast<uint8_t>(roleNumber);
				links.push_back(std::move(link));
			}

			parsedLinks.emplace(npcTargetId, std::move(links));
		}

		m_linksByNpc = std::move(parsedLinks);
		return true;
	}

	const std::vector<QuestGiverLink>* QuestGiverTable::ForNpc(std::string_view npcTargetId) const
	{
		const auto it = m_linksByNpc.find(std::string(npcTargetId));
		if (it == m_linksByNpc.end())
		{
			return nullptr;
		}

		return &it->second;
	}
}
