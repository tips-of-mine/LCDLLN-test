#include "src/world_editor/quests/QuestEditIo.h"

#include "src/shared/platform/FileSystem.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

		/// Lit une valeur JSON booléenne. \return false si \p value n'est pas de
		/// type `Bool` (l'éditeur n'ayant pas d'autre accesseur bool, on lit
		/// directement le type de la `JsonValue`).
		bool TryGetBool(const JsonValue& value, bool& outValue)
		{
			if (value.type != JsonType::Bool)
			{
				return false;
			}

			outValue = value.boolValue;
			return true;
		}

		/// Convertit la chaîne JSON `"repeat"` en `QuestRepeatMode` (EXT-2).
		/// Les 5 tokens acceptés (minuscules) sont ceux attendus par le parseur
		/// shard : none/repeatable/daily/weekly/cooldown.
		/// \return false si \p token n'est aucun des 5 modes valides (mode rejeté).
		bool ParseRepeatMode(const std::string& token, QuestRepeatMode& outMode)
		{
			if (token == "none") { outMode = QuestRepeatMode::None; return true; }
			if (token == "repeatable") { outMode = QuestRepeatMode::Repeatable; return true; }
			if (token == "daily") { outMode = QuestRepeatMode::Daily; return true; }
			if (token == "weekly") { outMode = QuestRepeatMode::Weekly; return true; }
			if (token == "cooldown") { outMode = QuestRepeatMode::Cooldown; return true; }
			return false;
		}

		/// Convertit un `QuestRepeatMode` en son token JSON minuscule (EXT-2),
		/// exactement dans le vocabulaire attendu par le shard. `None` mappe sur
		/// `"none"` (émis explicitement à la sérialisation pour un JSON complet).
		const char* RepeatModeToString(QuestRepeatMode mode)
		{
			switch (mode)
			{
			case QuestRepeatMode::Repeatable: return "repeatable";
			case QuestRepeatMode::Daily: return "daily";
			case QuestRepeatMode::Weekly: return "weekly";
			case QuestRepeatMode::Cooldown: return "cooldown";
			case QuestRepeatMode::None:
			default: return "none";
			}
		}

		/// Analyse les champs EXT-2 optionnels de re-réalisation d'une quête
		/// (`"repeat"`, `"cooldownHours"`, `"autoComplete"`) depuis \p questValue
		/// (niveau quête, PAS sous `rewards`) et remplit les champs correspondants
		/// de \p quest. Tous optionnels (défaut None/0/false → rétro-compat).
		/// \return false si `"repeat"` n'est pas une chaîne d'un des 5 modes, si
		///         `"cooldownHours"` n'est pas un entier, si `"autoComplete"`
		///         n'est pas un booléen, ou si le mode `cooldown` est demandé sans
		///         `cooldownHours > 0` (échec bloquant pour la quête).
		bool ParseEditedRepeat(const JsonValue& questValue, EditedQuest& quest, std::string& outError)
		{
			if (const JsonValue* repeatValue = FindObjectMember(questValue, "repeat"); repeatValue != nullptr)
			{
				if (repeatValue->type != JsonType::String
					|| !ParseRepeatMode(repeatValue->stringValue, quest.repeatMode))
				{
					outError = "quest '" + quest.id
						+ "': repeat must be one of none/repeatable/daily/weekly/cooldown";
					return false;
				}
			}

			if (const JsonValue* cooldownValue = FindObjectMember(questValue, "cooldownHours"); cooldownValue != nullptr)
			{
				if (!TryGetUint(*cooldownValue, quest.cooldownHours))
				{
					outError = "quest '" + quest.id + "': cooldownHours must be a non-negative integer";
					return false;
				}
			}

			if (const JsonValue* autoValue = FindObjectMember(questValue, "autoComplete"); autoValue != nullptr)
			{
				if (!TryGetBool(*autoValue, quest.autoComplete))
				{
					outError = "quest '" + quest.id + "': autoComplete must be a boolean";
					return false;
				}
			}

			if (const JsonValue* partySharedValue = FindObjectMember(questValue, "partyShared"); partySharedValue != nullptr)
			{
				if (!TryGetBool(*partySharedValue, quest.partyShared))
				{
					outError = "quest '" + quest.id + "': partyShared must be a boolean";
					return false;
				}
			}

			// Contrainte inter-champs : le mode cooldown exige un délai strictement
			// positif (0 heure n'aurait pas de sens et casserait le reset shard).
			if (quest.repeatMode == QuestRepeatMode::Cooldown && quest.cooldownHours == 0)
			{
				outError = "quest '" + quest.id + "': cooldownHours must be > 0 when repeat is 'cooldown'";
				return false;
			}

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

		/// Analyse une quête complète (`{id, giver, turnIn, prereqs, excludes,
		/// steps, rewards}` + champs EXT-2 `repeat`/`cooldownHours`/`autoComplete`)
		/// depuis \p questValue et la pousse dans \p out si valide.
		/// \return false si un champ requis (`id`/`giver`/`turnIn`/`steps` non
		///         vide) est absent ou invalide, ou si un champ EXT-2 est mal
		///         formé (échec bloquant pour tout le chargement, cf.
		///         `QuestEditIo::Load`).
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

			// EXT-1 : champ `excludes` OPTIONNEL (quêtes mutuellement exclusives),
			// miroir exact du bloc `prereqs` ci-dessus. Absent = OK (rétro-compat) ;
			// la validation d'existence / anti-auto-exclusion est faite dans Validate.
			if (const JsonValue* excludesValue = FindObjectMember(questValue, "excludes");
				excludesValue != nullptr)
			{
				if (excludesValue->type != JsonType::Array)
				{
					outError = "quest '" + quest.id + "': excludes must be an array";
					return false;
				}

				for (const JsonValue& excludeValue : excludesValue->arrayValue)
				{
					if (excludeValue.type != JsonType::String || excludeValue.stringValue.empty())
					{
						outError = "quest '" + quest.id + "': excludes entry must be a non-empty string";
						return false;
					}

					quest.excludes.push_back(excludeValue.stringValue);
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

			// EXT-2 : champs de re-réalisation optionnels (repeat/cooldownHours/
			// autoComplete), au niveau quête (PAS sous rewards).
			if (!ParseEditedRepeat(questValue, quest, outError))
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

		/// Couleur d'un sommet du graphe `prereqs` pendant la détection de cycle
		/// par DFS (coloriage blanc/gris/noir classique).
		enum class VisitColor
		{
			White, ///< pas encore visité.
			Grey,  ///< en cours de visite (sur la pile d'appel courante).
			Black  ///< visite terminée, aucun cycle trouvé sous ce sommet.
		};

		/// Visite en profondeur le sommet `questId` dans le graphe `prereqs`
		/// (index construit dans `Validate`) pour détecter un cycle.
		/// \param questId id de quête à visiter (doit exister dans \p byId).
		/// \param byId index `id -> EditedQuest*` de l'ensemble complet.
		/// \param colors coloriage courant de chaque sommet visité (modifié en place).
		/// \return true si un cycle est atteignable depuis `questId` (un sommet
		///         gris est réatteint pendant la descente).
		///
		/// Effet de bord : met à jour \p colors (Grey à l'entrée, Black à la
		/// sortie normale). Les prereqs inconnus (dangling, déjà signalés par
		/// ailleurs) sont ignorés ici pour ne pas fausser la détection de cycle.
		bool HasCycle(const std::string& questId, const std::unordered_map<std::string, const EditedQuest*>& byId,
			std::unordered_map<std::string, VisitColor>& colors)
		{
			colors[questId] = VisitColor::Grey;

			const auto it = byId.find(questId);
			if (it != byId.end())
			{
				for (const std::string& prereqId : it->second->prereqs)
				{
					const auto colorIt = colors.find(prereqId);
					const VisitColor prereqColor = (colorIt != colors.end()) ? colorIt->second : VisitColor::White;

					if (prereqColor == VisitColor::Grey)
					{
						return true;
					}

					if (prereqColor == VisitColor::White && byId.find(prereqId) != byId.end())
					{
						if (HasCycle(prereqId, byId, colors))
						{
							return true;
						}
					}
				}
			}

			colors[questId] = VisitColor::Black;
			return false;
		}

		/// Échappe une chaîne pour insertion dans un littéral JSON (guillemets +
		/// antislash + retour à la ligne). Patron dupliqué volontairement depuis
		/// `BuildingTemplateLibrary.cpp` (pas de lib JSON partagée côté éditeur).
		std::string JsonEscape(const std::string& s)
		{
			std::string out;
			out.reserve(s.size() + 8);
			for (char c : s)
			{
				if (c == '"' || c == '\\')
				{
					out.push_back('\\');
					out.push_back(c);
				}
				else if (c == '\n')
				{
					out += "\\n";
				}
				else
				{
					out.push_back(c);
				}
			}
			return out;
		}

		/// Formate un entier non signé pour un littéral JSON (pas de zéros
		/// parasites, pas de séparateur de milliers).
		std::string Num(uint32_t v)
		{
			return std::to_string(v);
		}

		/// Sérialise une étape éditable (`{type, target, requiredCount}`) en JSON
		/// pur, indentée à \p indent espaces, sans retour à la ligne final.
		std::string SerializeStep(const EditedStep& step, int indent)
		{
			const std::string pad(static_cast<size_t>(indent), ' ');
			std::ostringstream os;
			os << pad << "{ \"type\": \"" << JsonEscape(step.type) << "\", \"target\": \"" << JsonEscape(step.target)
			   << "\", \"requiredCount\": " << Num(step.requiredCount) << " }";
			return os.str();
		}

		/// Sérialise le bloc `rewards` (`{xp, gold, items:[{itemId,quantity}]}`)
		/// d'une quête éditée en JSON pur, indentée à \p indent espaces.
		std::string SerializeRewards(const EditedQuest& quest, int indent)
		{
			const std::string pad(static_cast<size_t>(indent), ' ');
			const std::string itemPad(static_cast<size_t>(indent) + 2, ' ');
			std::ostringstream os;
			os << pad << "{\n";
			os << itemPad << "\"xp\": " << Num(quest.rewardXp) << ",\n";
			os << itemPad << "\"gold\": " << Num(quest.rewardGold) << ",\n";
			os << itemPad << "\"items\": [";
			for (size_t i = 0; i < quest.rewardItems.size(); ++i)
			{
				const EditedRewardItem& item = quest.rewardItems[i];
				os << (i == 0 ? "\n" : ",\n") << itemPad << "  { \"itemId\": " << Num(item.itemId)
				   << ", \"quantity\": " << Num(item.quantity) << " }";
			}
			if (!quest.rewardItems.empty())
			{
				os << "\n" << itemPad;
			}
			os << "]\n";
			os << pad << "}";
			return os.str();
		}

		/// Sérialise une quête éditée complète (`{id, giver, turnIn, prereqs,
		/// excludes, steps, rewards}` + champs EXT-2 `repeat`/`cooldownHours`/
		/// `autoComplete`) en JSON pur, tableau, indentée à \p indent espaces
		/// (élément de `quests[]` dans `quest_definitions.json`). Les 3 champs
		/// EXT-2 sont TOUJOURS émis (défaut `"none"`/`0`/`false`) : JSON pur
		/// relisible par le shard.
		std::string SerializeQuestDefinition(const EditedQuest& quest, int indent)
		{
			const std::string pad(static_cast<size_t>(indent), ' ');
			const std::string fieldPad(static_cast<size_t>(indent) + 2, ' ');
			std::ostringstream os;
			os << pad << "{\n";
			os << fieldPad << "\"id\": \"" << JsonEscape(quest.id) << "\",\n";
			os << fieldPad << "\"giver\": \"" << JsonEscape(quest.giver) << "\",\n";
			os << fieldPad << "\"turnIn\": \"" << JsonEscape(quest.turnIn) << "\",\n";

			os << fieldPad << "\"prereqs\": [";
			for (size_t i = 0; i < quest.prereqs.size(); ++i)
			{
				os << (i == 0 ? "" : ", ") << "\"" << JsonEscape(quest.prereqs[i]) << "\"";
			}
			os << "],\n";

			// EXT-1 : `excludes` sérialisé à l'identique de `prereqs` — tableau
			// TOUJOURS émis (vide -> `[]`), JSON pur relisible par le shard.
			os << fieldPad << "\"excludes\": [";
			for (size_t i = 0; i < quest.excludes.size(); ++i)
			{
				os << (i == 0 ? "" : ", ") << "\"" << JsonEscape(quest.excludes[i]) << "\"";
			}
			os << "],\n";

			os << fieldPad << "\"steps\": [";
			for (size_t i = 0; i < quest.steps.size(); ++i)
			{
				os << (i == 0 ? "\n" : ",\n") << SerializeStep(quest.steps[i], indent + 4);
			}
			if (!quest.steps.empty())
			{
				os << "\n" << fieldPad;
			}
			os << "],\n";

			os << fieldPad << "\"rewards\": " << SerializeRewards(quest, indent + 2) << ",\n";

			// EXT-2 : re-réalisation. `repeat` en chaîne minuscule (vocabulaire
			// shard), `cooldownHours` en entier, `autoComplete` en bool JSON.
			// EXT-3 : `partyShared` en bool JSON (dernier champ, sans virgule).
			os << fieldPad << "\"repeat\": \"" << RepeatModeToString(quest.repeatMode) << "\",\n";
			os << fieldPad << "\"cooldownHours\": " << Num(quest.cooldownHours) << ",\n";
			os << fieldPad << "\"autoComplete\": " << (quest.autoComplete ? "true" : "false") << ",\n";
			os << fieldPad << "\"partyShared\": " << (quest.partyShared ? "true" : "false") << "\n";
			os << pad << "}";
			return os.str();
		}

		/// Sérialise `quest_definitions.json` en entier (`{ "quests": [...] }`,
		/// JSON pur, tableaux — PAS le format `count`-indexé de `Config`).
		std::string SerializeQuestDefinitions(const std::vector<EditedQuest>& quests)
		{
			std::ostringstream os;
			os << "{\n  \"quests\": [";
			for (size_t i = 0; i < quests.size(); ++i)
			{
				os << (i == 0 ? "\n" : ",\n") << SerializeQuestDefinition(quests[i], 4);
			}
			if (!quests.empty())
			{
				os << "\n  ";
			}
			os << "]\n}\n";
			return os.str();
		}

		/// Sérialise `quest_texts.fr.json` (`{ "<id>": {title, description,
		/// steps:[...]} }`) à partir des textes portés par chaque `EditedQuest`.
		/// N'écrit une entrée que pour les quêtes ayant au moins un champ texte
		/// non vide (title/description/stepLabels), pour ne pas polluer le
		/// fichier de traductions avec des entrées entièrement vides.
		std::string SerializeQuestTexts(const std::vector<EditedQuest>& quests)
		{
			std::ostringstream os;
			os << "{";
			bool first = true;
			for (const EditedQuest& quest : quests)
			{
				if (quest.title.empty() && quest.description.empty() && quest.stepLabels.empty())
				{
					continue;
				}

				os << (first ? "\n" : ",\n") << "  \"" << JsonEscape(quest.id) << "\": {\n";
				os << "    \"title\": \"" << JsonEscape(quest.title) << "\",\n";
				os << "    \"description\": \"" << JsonEscape(quest.description) << "\",\n";
				os << "    \"steps\": [";
				for (size_t i = 0; i < quest.stepLabels.size(); ++i)
				{
					os << (i == 0 ? "\n" : ",\n") << "      \"" << JsonEscape(quest.stepLabels[i]) << "\"";
				}
				if (!quest.stepLabels.empty())
				{
					os << "\n    ";
				}
				os << "]\n  }";
				first = false;
			}
			if (!first)
			{
				os << "\n";
			}
			os << "}\n";
			return os.str();
		}

		/// Une entrée de `quest_givers.json` régénérée : quête + rôle du PNJ
		/// pour cette quête (0 = donneur `giver`, 1 = receveur `turnIn`).
		struct GiverEntry
		{
			std::string questId;
			int role = 0;
		};

		/// Sérialise `quest_givers.json` régénéré à partir de `giver`/`turnIn`
		/// de chaque quête de \p quests : pour chaque quête, ajoute `{questId,
		/// role:0}` sous la clé PNJ `giver`, et `{questId, role:1}` sous la clé
		/// PNJ `turnIn`, puis groupe par PNJ (`std::map` pour un ordre de clés
		/// stable et déterministe entre deux appels, utile pour les diffs git).
		/// Un même PNJ peut recevoir plusieurs entrées (plusieurs quêtes, ou
		/// donneur+receveur d'une même quête) : toutes conservées, dans l'ordre
		/// de parcours de \p quests.
		std::string SerializeQuestGivers(const std::vector<EditedQuest>& quests)
		{
			std::map<std::string, std::vector<GiverEntry>> byNpc;
			for (const EditedQuest& quest : quests)
			{
				if (!quest.giver.empty())
				{
					byNpc[quest.giver].push_back(GiverEntry{ quest.id, 0 });
				}
				if (!quest.turnIn.empty())
				{
					byNpc[quest.turnIn].push_back(GiverEntry{ quest.id, 1 });
				}
			}

			std::ostringstream os;
			os << "{";
			bool first = true;
			for (const auto& npcAndEntries : byNpc)
			{
				os << (first ? "\n" : ",\n") << "  \"" << JsonEscape(npcAndEntries.first) << "\": [";
				const std::vector<GiverEntry>& entries = npcAndEntries.second;
				for (size_t i = 0; i < entries.size(); ++i)
				{
					os << (i == 0 ? "\n" : ",\n") << "    { \"questId\": \"" << JsonEscape(entries[i].questId)
					   << "\", \"role\": " << entries[i].role << " }";
				}
				os << "\n  ]";
				first = false;
			}
			if (!first)
			{
				os << "\n";
			}
			os << "}\n";
			return os.str();
		}

		/// Écrit \p content dans \p path (binaire, tronque un fichier existant).
		/// \return false et remplit \p outError si le fichier n'a pas pu être ouvert en écriture.
		///
		/// Effet de bord : écriture disque (création/écrasement de \p path).
		bool WriteTextFile(const std::filesystem::path& path, const std::string& content, std::string& outError)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				outError = "cannot write file (path=" + path.string() + ")";
				return false;
			}

			file << content;
			if (!file)
			{
				outError = "write failed (path=" + path.string() + ")";
				return false;
			}

			return true;
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

	bool QuestEditIo::Validate(const std::vector<EditedQuest>& quests, std::vector<std::string>& outErrors) const
	{
		outErrors.clear();

		// Index id -> quête, et détection des doublons d'id au passage.
		std::unordered_map<std::string, const EditedQuest*> byId;
		byId.reserve(quests.size());
		for (const EditedQuest& quest : quests)
		{
			if (quest.id.empty())
			{
				outErrors.push_back("quête sans id (id vide)");
				continue;
			}

			if (byId.find(quest.id) != byId.end())
			{
				outErrors.push_back("id de quête dupliqué : '" + quest.id + "'");
				continue;
			}

			byId.emplace(quest.id, &quest);
		}

		static const std::unordered_set<std::string> kValidStepTypes = { "kill", "collect", "talk", "enter" };

		for (const EditedQuest& quest : quests)
		{
			const std::string label = quest.id.empty() ? std::string("<sans id>") : quest.id;

			if (quest.giver.empty())
			{
				outErrors.push_back("quête '" + label + "': giver ne doit pas être vide");
			}
			if (quest.turnIn.empty())
			{
				outErrors.push_back("quête '" + label + "': turnIn ne doit pas être vide");
			}

			if (quest.steps.empty())
			{
				outErrors.push_back("quête '" + label + "': doit avoir au moins une étape");
			}
			for (size_t stepIndex = 0; stepIndex < quest.steps.size(); ++stepIndex)
			{
				const EditedStep& step = quest.steps[stepIndex];
				if (kValidStepTypes.find(step.type) == kValidStepTypes.end())
				{
					outErrors.push_back("quête '" + label + "': étape " + std::to_string(stepIndex)
						+ ": type '" + step.type + "' invalide (attendu kill/collect/talk/enter)");
				}
				if (step.target.empty())
				{
					outErrors.push_back("quête '" + label + "': étape " + std::to_string(stepIndex) + ": target ne doit pas être vide");
				}
				if (step.requiredCount < 1)
				{
					outErrors.push_back("quête '" + label + "': étape " + std::to_string(stepIndex) + ": requiredCount doit être ≥ 1");
				}
			}

			for (const std::string& prereqId : quest.prereqs)
			{
				if (byId.find(prereqId) == byId.end())
				{
					outErrors.push_back("quête '" + label + "': prereq '" + prereqId + "' introuvable dans l'ensemble");
				}
			}

			// EXT-1 : chaque `exclude` doit exister dans l'ensemble (dangling ->
			// erreur, même politique que les prereqs) et ne peut pas viser la
			// quête elle-même (auto-exclusion interdite). PAS de détection de
			// cycle : l'exclusion mutuelle A<->B est autorisée.
			for (const std::string& excludeId : quest.excludes)
			{
				if (excludeId == quest.id)
				{
					outErrors.push_back("quête '" + label + "': ne peut pas s'exclure elle-même ('" + excludeId + "')");
				}
				else if (byId.find(excludeId) == byId.end())
				{
					outErrors.push_back("quête '" + label + "': exclude '" + excludeId + "' introuvable dans l'ensemble");
				}
			}

			for (size_t itemIndex = 0; itemIndex < quest.rewardItems.size(); ++itemIndex)
			{
				const EditedRewardItem& item = quest.rewardItems[itemIndex];
				if (item.itemId == 0 || item.quantity < 1)
				{
					outErrors.push_back("quête '" + label + "': reward item " + std::to_string(itemIndex)
						+ ": itemId doit être > 0 et quantity ≥ 1");
				}
			}

			// EXT-2 : le mode cooldown exige un délai strictement positif (règle
			// inter-champs, miroir de la validation au parse).
			if (quest.repeatMode == QuestRepeatMode::Cooldown && quest.cooldownHours == 0)
			{
				outErrors.push_back("quête '" + label + "': cooldownHours doit être > 0 en mode 'cooldown'");
			}
		}

		// Détection de cycle sur le graphe prereqs, uniquement entre id connus
		// (les dangling prereqs sont déjà signalés ci-dessus et ignorés ici).
		std::unordered_map<std::string, VisitColor> colors;
		colors.reserve(byId.size());
		for (const auto& idAndQuest : byId)
		{
			const std::string& questId = idAndQuest.first;
			if (colors.find(questId) == colors.end() && HasCycle(questId, byId, colors))
			{
				outErrors.push_back("cycle détecté dans le graphe prereqs impliquant la quête '" + questId + "'");
			}
		}

		return outErrors.empty();
	}

	bool QuestEditIo::Save(const std::string& contentRoot, const std::vector<EditedQuest>& quests, std::string& outError) const
	{
		outError.clear();

		const std::filesystem::path questsDir = std::filesystem::path(contentRoot) / "quests";
		std::error_code ec;
		std::filesystem::create_directories(questsDir, ec);
		if (ec)
		{
			outError = "cannot create directory (path=" + questsDir.string() + "): " + ec.message();
			return false;
		}

		if (!WriteTextFile(questsDir / "quest_definitions.json", SerializeQuestDefinitions(quests), outError))
		{
			return false;
		}

		if (!WriteTextFile(questsDir / "quest_texts.fr.json", SerializeQuestTexts(quests), outError))
		{
			return false;
		}

		if (!WriteTextFile(questsDir / "quest_givers.json", SerializeQuestGivers(quests), outError))
		{
			return false;
		}

		return true;
	}
}
