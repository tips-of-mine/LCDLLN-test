#include "src/world_editor/quests/QuestEditIo.h"

#include "src/shared/platform/FileSystem.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace engine::editor::world::quests
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

		/// Analyseur JSON minimal dédié à l'éditeur de quêtes, pour éviter une
		/// dépendance externe (miroir volontairement dupliqué de
		/// `QuestRuntime::JsonParser` côté serveur / `QuestTextCatalog::JsonParser`
		/// côté client : pas de lib JSON partagée aujourd'hui).
		class JsonParser final
		{
		public:
			/// Construit un analyseur sur \p input (non copié, doit rester valide
			/// pendant tout l'appel à Parse).
			explicit JsonParser(std::string_view input)
				: m_input(input)
			{
			}

			/// Analyse l'intégralité de `m_input` comme une unique valeur JSON.
			/// \param outRoot reçoit la valeur racine analysée.
			/// \param outError message d'erreur lisible en cas d'échec.
			/// \return false si le texte n'est pas un JSON valide (y compris
			///         caractères en trop après la valeur racine).
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
			/// Avance `m_pos` au-delà des espaces/retours à la ligne courants.
			void SkipWhitespace()
			{
				while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos])) != 0)
				{
					++m_pos;
				}
			}

			/// Consomme le caractère \p expected s'il est au curseur courant.
			/// \return true et avance si le caractère correspond, false sinon (sans avancer).
			bool Consume(char expected)
			{
				if (m_pos >= m_input.size() || m_input[m_pos] != expected)
				{
					return false;
				}

				++m_pos;
				return true;
			}

			/// \return true si le texte restant commence par \p token (sans avancer).
			bool StartsWith(std::string_view token) const
			{
				return m_input.substr(m_pos, token.size()) == token;
			}

			/// Analyse une valeur JSON quelconque (objet, tableau, chaîne, bool,
			/// null ou nombre) au curseur courant.
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

			/// Analyse un objet JSON `{ "clé": valeur, ... }` au curseur courant.
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

			/// Analyse un tableau JSON `[ valeur, ... ]` au curseur courant.
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

			/// Analyse une chaîne JSON entre guillemets (avec échappements standards)
			/// au curseur courant.
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

			/// Analyse un nombre JSON (entier ou flottant, notation exponentielle
			/// comprise) au curseur courant.
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

		/// Renvoie un membre d'objet JSON, ou `nullptr` si \p object n'est pas un
		/// objet ou si la clé \p key est absente.
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

		/// Convertit un nombre JSON en entier non signé 32 bits validé (fini,
		/// positif ou nul, sans partie fractionnaire significative).
		/// \return false si \p value n'est pas un nombre entier représentable.
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

		/// Analyse une étape éditable (`{type, target, requiredCount}`) depuis
		/// \p stepValue et la pousse dans `quest.steps`.
		/// \return false si `type`/`target` sont absents ou de mauvais type
		///         (échec bloquant : la quête entière est rejetée).
		bool ParseEditedStep(const JsonValue& stepValue, EditedQuest& quest, std::string& outError)
		{
			if (stepValue.type != JsonType::Object)
			{
				outError = "quest '" + quest.id + "': step must be an object";
				return false;
			}

			const JsonValue* typeValue = FindObjectMember(stepValue, "type");
			const JsonValue* targetValue = FindObjectMember(stepValue, "target");
			if (typeValue == nullptr || typeValue->type != JsonType::String || typeValue->stringValue.empty())
			{
				outError = "quest '" + quest.id + "': step.type must be a non-empty string";
				return false;
			}
			if (targetValue == nullptr || targetValue->type != JsonType::String || targetValue->stringValue.empty())
			{
				outError = "quest '" + quest.id + "': step.target must be a non-empty string";
				return false;
			}

			EditedStep step{};
			step.type = typeValue->stringValue;
			step.target = targetValue->stringValue;
			step.requiredCount = 1;

			if (const JsonValue* requiredCountValue = FindObjectMember(stepValue, "requiredCount");
				requiredCountValue != nullptr)
			{
				if (!TryGetUint(*requiredCountValue, step.requiredCount) || step.requiredCount == 0)
				{
					outError = "quest '" + quest.id + "': step.requiredCount must be a positive integer";
					return false;
				}
			}

			quest.steps.push_back(std::move(step));
			return true;
		}

		/// Analyse le bloc `rewards` optionnel (`{xp, gold, items:[{itemId,quantity}]}`)
		/// et remplit les champs de récompense de \p quest.
		/// \return false si un champ présent est de mauvais type (échec bloquant).
		bool ParseEditedRewards(const JsonValue& questValue, EditedQuest& quest, std::string& outError)
		{
			const JsonValue* rewardsValue = FindObjectMember(questValue, "rewards");
			if (rewardsValue == nullptr)
			{
				return true;
			}

			if (rewardsValue->type != JsonType::Object)
			{
				outError = "quest '" + quest.id + "': rewards must be an object";
				return false;
			}

			if (const JsonValue* xpValue = FindObjectMember(*rewardsValue, "xp"); xpValue != nullptr)
			{
				if (!TryGetUint(*xpValue, quest.rewardXp))
				{
					outError = "quest '" + quest.id + "': rewards.xp must be a non-negative integer";
					return false;
				}
			}

			if (const JsonValue* goldValue = FindObjectMember(*rewardsValue, "gold"); goldValue != nullptr)
			{
				if (!TryGetUint(*goldValue, quest.rewardGold))
				{
					outError = "quest '" + quest.id + "': rewards.gold must be a non-negative integer";
					return false;
				}
			}

			const JsonValue* itemsValue = FindObjectMember(*rewardsValue, "items");
			if (itemsValue == nullptr)
			{
				return true;
			}

			if (itemsValue->type != JsonType::Array)
			{
				outError = "quest '" + quest.id + "': rewards.items must be an array";
				return false;
			}

			for (const JsonValue& itemValue : itemsValue->arrayValue)
			{
				if (itemValue.type != JsonType::Object)
				{
					outError = "quest '" + quest.id + "': rewards.items entry must be an object";
					return false;
				}

				const JsonValue* itemIdValue = FindObjectMember(itemValue, "itemId");
				const JsonValue* quantityValue = FindObjectMember(itemValue, "quantity");
				EditedRewardItem item{};
				if (itemIdValue == nullptr || quantityValue == nullptr
					|| !TryGetUint(*itemIdValue, item.itemId)
					|| !TryGetUint(*quantityValue, item.quantity)
					|| item.itemId == 0
					|| item.quantity == 0)
				{
					outError = "quest '" + quest.id + "': rewards.items entry must define positive itemId and quantity";
					return false;
				}

				quest.rewardItems.push_back(item);
			}

			return true;
		}

		/// Analyse une quête complète (`{id, giver, turnIn, prereqs, steps, rewards}`)
		/// depuis \p questValue et la pousse dans \p out si valide.
		/// \return false si un champ requis (`id`/`giver`/`turnIn`/`steps` non
		///         vide) est absent ou invalide (échec bloquant pour tout le
		///         chargement, cf. `QuestEditIo::Load`).
		bool ParseEditedQuest(const JsonValue& questValue, std::vector<EditedQuest>& out, std::string& outError)
		{
			if (questValue.type != JsonType::Object)
			{
				outError = "quests[] entry must be an object";
				return false;
			}

			const JsonValue* idValue = FindObjectMember(questValue, "id");
			if (idValue == nullptr || idValue->type != JsonType::String || idValue->stringValue.empty())
			{
				outError = "quest entry: id must be a non-empty string";
				return false;
			}

			EditedQuest quest{};
			quest.id = idValue->stringValue;

			const JsonValue* giverValue = FindObjectMember(questValue, "giver");
			const JsonValue* turnInValue = FindObjectMember(questValue, "turnIn");
			if (giverValue == nullptr || giverValue->type != JsonType::String || giverValue->stringValue.empty())
			{
				outError = "quest '" + quest.id + "': giver must be a non-empty string";
				return false;
			}
			if (turnInValue == nullptr || turnInValue->type != JsonType::String || turnInValue->stringValue.empty())
			{
				outError = "quest '" + quest.id + "': turnIn must be a non-empty string";
				return false;
			}
			quest.giver = giverValue->stringValue;
			quest.turnIn = turnInValue->stringValue;

			if (const JsonValue* prereqsValue = FindObjectMember(questValue, "prereqs");
				prereqsValue != nullptr)
			{
				if (prereqsValue->type != JsonType::Array)
				{
					outError = "quest '" + quest.id + "': prereqs must be an array";
					return false;
				}

				for (const JsonValue& prereqValue : prereqsValue->arrayValue)
				{
					if (prereqValue.type != JsonType::String || prereqValue.stringValue.empty())
					{
						outError = "quest '" + quest.id + "': prereqs entry must be a non-empty string";
						return false;
					}

					quest.prereqs.push_back(prereqValue.stringValue);
				}
			}

			const JsonValue* stepsValue = FindObjectMember(questValue, "steps");
			if (stepsValue == nullptr || stepsValue->type != JsonType::Array || stepsValue->arrayValue.empty())
			{
				outError = "quest '" + quest.id + "': steps must be a non-empty array";
				return false;
			}

			for (const JsonValue& stepValue : stepsValue->arrayValue)
			{
				if (!ParseEditedStep(stepValue, quest, outError))
				{
					return false;
				}
			}

			if (!ParseEditedRewards(questValue, quest, outError))
			{
				return false;
			}

			out.push_back(std::move(quest));
			return true;
		}

		/// Fusionne les textes lisibles de `quest_texts.fr.json` (titre,
		/// description, libellés d'étape) dans \p quests, appariés par `id` de
		/// quête. Silencieux si \p jsonText est vide/invalide ou si une quête
		/// n'a pas d'entrée de texte (les champs restent vides, ce n'est pas
		/// une erreur pour `Load`).
		void MergeQuestTexts(const std::string& jsonText, std::vector<EditedQuest>& quests)
		{
			if (jsonText.empty())
			{
				return;
			}

			JsonValue root;
			std::string parseError;
			JsonParser parser(jsonText);
			if (!parser.Parse(root, parseError) || root.type != JsonType::Object)
			{
				return;
			}

			for (EditedQuest& quest : quests)
			{
				const JsonValue* entryValue = FindObjectMember(root, quest.id);
				if (entryValue == nullptr || entryValue->type != JsonType::Object)
				{
					continue;
				}

				if (const JsonValue* titleValue = FindObjectMember(*entryValue, "title");
					titleValue != nullptr && titleValue->type == JsonType::String)
				{
					quest.title = titleValue->stringValue;
				}

				if (const JsonValue* descriptionValue = FindObjectMember(*entryValue, "description");
					descriptionValue != nullptr && descriptionValue->type == JsonType::String)
				{
					quest.description = descriptionValue->stringValue;
				}

				if (const JsonValue* stepsValue = FindObjectMember(*entryValue, "steps");
					stepsValue != nullptr && stepsValue->type == JsonType::Array)
				{
					for (const JsonValue& stepValue : stepsValue->arrayValue)
					{
						quest.stepLabels.push_back(stepValue.type == JsonType::String ? stepValue.stringValue : std::string());
					}
				}
			}
		}
	}

	bool QuestEditIo::Load(const std::string& contentRoot, std::vector<EditedQuest>& out, std::string& outError) const
	{
		out.clear();
		outError.clear();

		const std::filesystem::path definitionsPath = std::filesystem::path(contentRoot) / "quests" / "quest_definitions.json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllText(definitionsPath);
		if (jsonText.empty())
		{
			outError = "cannot read quest_definitions.json (path=" + definitionsPath.string() + ")";
			return false;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, parseError))
		{
			outError = "quest_definitions.json parse error: " + parseError;
			return false;
		}

		const JsonValue* questsValue = FindObjectMember(root, "quests");
		if (questsValue == nullptr || questsValue->type != JsonType::Array)
		{
			outError = "quest_definitions.json: root.quests must be an array";
			return false;
		}

		std::vector<EditedQuest> parsedQuests;
		for (const JsonValue& questValue : questsValue->arrayValue)
		{
			if (!ParseEditedQuest(questValue, parsedQuests, outError))
			{
				return false;
			}
		}

		const std::filesystem::path textsPath = std::filesystem::path(contentRoot) / "quests" / "quest_texts.fr.json";
		const std::string textsJson = engine::platform::FileSystem::ReadAllText(textsPath);
		MergeQuestTexts(textsJson, parsedQuests);

		out = std::move(parsedQuests);
		return true;
	}
}
