#include "engine/server/QuestRuntime.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

		/// Minimal JSON parser kept local to the quest runtime to avoid adding new dependencies.
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

		/// Return one object member or `nullptr` when the key is absent.
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

		/// Convert one JSON number into a validated unsigned 32-bit integer.
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

		/// Parse one JSON string into a quest step type token.
		bool TryParseQuestStepType(std::string_view text, QuestStepType& outType)
		{
			if (text == "kill")
			{
				outType = QuestStepType::Kill;
				return true;
			}
			if (text == "collect")
			{
				outType = QuestStepType::Collect;
				return true;
			}
			if (text == "talk")
			{
				outType = QuestStepType::Talk;
				return true;
			}
			if (text == "enter")
			{
				outType = QuestStepType::Enter;
				return true;
			}

			return false;
		}

		/// Return true when every step on the quest reached its required count.
		bool AreAllStepsComplete(const QuestDefinition& definition, const QuestState& state)
		{
			if (definition.steps.size() != state.stepProgressCounts.size())
			{
				return false;
			}

			for (size_t index = 0; index < definition.steps.size(); ++index)
			{
				if (state.stepProgressCounts[index] < definition.steps[index].requiredCount)
				{
					return false;
				}
			}

			return true;
		}

		/// Find one quest state by id and return its index, or `npos` when missing.
		size_t FindQuestStateIndex(const std::vector<QuestState>& states, std::string_view questId)
		{
			for (size_t index = 0; index < states.size(); ++index)
			{
				if (states[index].questId == questId)
				{
					return index;
				}
			}

			return std::string::npos;
		}

		/// Append one delta, replacing the previous version when the same quest already changed earlier in the tick.
		void UpsertDelta(std::vector<QuestProgressDelta>& deltas, QuestProgressDelta delta)
		{
			for (QuestProgressDelta& existing : deltas)
			{
				if (existing.questId == delta.questId)
				{
					existing = std::move(delta);
					return;
				}
			}

			deltas.push_back(std::move(delta));
		}
	}

	const char* GetQuestStepTypeName(QuestStepType type)
	{
		switch (type)
		{
		case QuestStepType::Kill:
			return "kill";
		case QuestStepType::Collect:
			return "collect";
		case QuestStepType::Talk:
			return "talk";
		case QuestStepType::Enter:
			return "enter";
		default:
			return "kill";
		}
	}

	const char* GetQuestStatusName(QuestStatus status)
	{
		switch (status)
		{
		case QuestStatus::Locked:
			return "locked";
		case QuestStatus::Active:
			return "active";
		case QuestStatus::Completed:
			return "completed";
		default:
			return "locked";
		}
	}

	QuestRuntime::QuestRuntime(const engine::core::Config& config)
		: m_config(config)
		, m_questDefinitionsRelativePath(m_config.GetString("server.quest_definitions_path", "quests/quest_definitions.json"))
	{
		LOG_INFO(Net, "[QuestRuntime] Constructed");
	}

	QuestRuntime::~QuestRuntime()
	{
		Shutdown();
	}

	bool QuestRuntime::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[QuestRuntime] Init ignored: already initialized");
			return true;
		}

		if (!LoadDefinitions())
		{
			LOG_ERROR(Net, "[QuestRuntime] Init FAILED: definition load failed");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[QuestRuntime] Init OK (path={}, quests={})", m_questDefinitionsRelativePath, m_definitions.size());
		return true;
	}

	void QuestRuntime::Shutdown()
	{
		if (!m_initialized && m_definitions.empty())
		{
			return;
		}

		const size_t definitionCount = m_definitions.size();
		m_definitions.clear();
		m_initialized = false;
		LOG_INFO(Net, "[QuestRuntime] Destroyed (quests={})", definitionCount);
	}

	bool QuestRuntime::SyncQuestStates(std::vector<QuestState>& states, std::vector<QuestProgressDelta>& outDeltas) const
	{
		outDeltas.clear();
		if (!m_initialized)
		{
			LOG_WARN(Net, "[QuestRuntime] SyncQuestStates FAILED: runtime not initialized");
			return false;
		}

		std::unordered_set<std::string> changedQuestIds;
		for (const QuestDefinition& definition : m_definitions)
		{
			const size_t stateIndex = FindQuestStateIndex(states, definition.questId);
			if (stateIndex == std::string::npos)
			{
				QuestState state{};
				state.questId = definition.questId;
				state.status = QuestStatus::Locked;
				state.stepProgressCounts.assign(definition.steps.size(), 0u);
				states.push_back(std::move(state));
				changedQuestIds.insert(definition.questId);
				LOG_INFO(Net, "[QuestRuntime] Quest state created (quest_id={})", definition.questId);
				continue;
			}

			QuestState& state = states[stateIndex];
			if (state.stepProgressCounts.size() != definition.steps.size())
			{
				const size_t previousStepCount = state.stepProgressCounts.size();
				state.stepProgressCounts.resize(definition.steps.size(), 0u);
				changedQuestIds.insert(definition.questId);
				LOG_WARN(Net,
					"[QuestRuntime] Quest state resized to match definition (quest_id={}, previous_steps={}, new_steps={})",
					definition.questId,
					previousStepCount,
					state.stepProgressCounts.size());
			}
		}

		for (const QuestDefinition& definition : m_definitions)
		{
			const size_t stateIndex = FindQuestStateIndex(states, definition.questId);
			if (stateIndex == std::string::npos)
			{
				continue;
			}

			QuestState& state = states[stateIndex];
			if (state.status == QuestStatus::Completed)
			{
				continue;
			}

			QuestStatus desiredStatus = QuestStatus::Locked;
			bool prerequisitesComplete = true;
			for (const std::string& prerequisiteQuestId : definition.prerequisiteQuestIds)
			{
				const size_t prerequisiteIndex = FindQuestStateIndex(states, prerequisiteQuestId);
				if (prerequisiteIndex == std::string::npos || states[prerequisiteIndex].status != QuestStatus::Completed)
				{
					prerequisitesComplete = false;
					break;
				}
			}

			if (prerequisitesComplete)
			{
				desiredStatus = QuestStatus::Active;
			}

			if (state.status != desiredStatus)
			{
				state.status = desiredStatus;
				changedQuestIds.insert(definition.questId);
				LOG_INFO(Net, "[QuestRuntime] Quest status updated (quest_id={}, status={})",
					definition.questId,
					GetQuestStatusName(state.status));
			}
		}

		for (const QuestDefinition& definition : m_definitions)
		{
			if (!changedQuestIds.contains(definition.questId))
			{
				continue;
			}

			const size_t stateIndex = FindQuestStateIndex(states, definition.questId);
			if (stateIndex == std::string::npos)
			{
				continue;
			}

			QuestProgressDelta delta{};
			delta.questId = definition.questId;
			delta.status = states[stateIndex].status;
			delta.stepProgressCounts = states[stateIndex].stepProgressCounts;
			outDeltas.push_back(std::move(delta));
		}

		if (!outDeltas.empty())
		{
			LOG_INFO(Net, "[QuestRuntime] Quest state sync OK (changed_quests={})", outDeltas.size());
		}
		return true;
	}

	bool QuestRuntime::ApplyEvent(
		std::vector<QuestState>& states,
		QuestStepType eventType,
		std::string_view targetId,
		uint32_t amount,
		std::vector<QuestProgressDelta>& outDeltas) const
	{
		outDeltas.clear();
		if (!m_initialized)
		{
			LOG_WARN(Net, "[QuestRuntime] ApplyEvent FAILED: runtime not initialized");
			return false;
		}

		if (targetId.empty() || amount == 0)
		{
			LOG_WARN(Net, "[QuestRuntime] ApplyEvent ignored: invalid payload (type={}, target={}, amount={})",
				GetQuestStepTypeName(eventType),
				targetId,
				amount);
			return false;
		}

		std::vector<QuestProgressDelta> syncDeltas;
		(void)SyncQuestStates(states, syncDeltas);
		for (QuestProgressDelta& delta : syncDeltas)
		{
			UpsertDelta(outDeltas, std::move(delta));
		}

		for (const QuestDefinition& definition : m_definitions)
		{
			const size_t stateIndex = FindQuestStateIndex(states, definition.questId);
			if (stateIndex == std::string::npos)
			{
				continue;
			}

			QuestState& state = states[stateIndex];
			if (state.status != QuestStatus::Active)
			{
				continue;
			}

			bool questChanged = false;
			for (size_t stepIndex = 0; stepIndex < definition.steps.size(); ++stepIndex)
			{
				const QuestStepDefinition& step = definition.steps[stepIndex];
				if (step.type != eventType || step.targetId != targetId)
				{
					continue;
				}

				const uint32_t previousCount = state.stepProgressCounts[stepIndex];
				const uint32_t nextCount = std::min(step.requiredCount, previousCount + amount);
				if (nextCount == previousCount)
				{
					continue;
				}

				state.stepProgressCounts[stepIndex] = nextCount;
				questChanged = true;
				LOG_INFO(Net,
					"[QuestRuntime] Quest progress updated (quest_id={}, step={}, type={}, target={}, progress={}/{})",
					definition.questId,
					stepIndex,
					GetQuestStepTypeName(step.type),
					step.targetId,
					nextCount,
					step.requiredCount);
			}

			if (!questChanged)
			{
				continue;
			}

			QuestProgressDelta delta{};
			delta.questId = definition.questId;
			delta.status = state.status;
			delta.stepProgressCounts = state.stepProgressCounts;
			if (AreAllStepsComplete(definition, state))
			{
				state.status = QuestStatus::Completed;
				delta.status = QuestStatus::Completed;
				delta.rewardExperience = definition.rewards.experience;
				delta.rewardGold = definition.rewards.gold;
				delta.rewardItems = definition.rewards.items;
				LOG_INFO(Net,
					"[QuestRuntime] Quest completed (quest_id={}, reward_xp={}, reward_gold={}, reward_items={})",
					definition.questId,
					delta.rewardExperience,
					delta.rewardGold,
					delta.rewardItems.size());
			}

			UpsertDelta(outDeltas, std::move(delta));
		}

		std::vector<QuestProgressDelta> unlockDeltas;
		(void)SyncQuestStates(states, unlockDeltas);
		for (QuestProgressDelta& delta : unlockDeltas)
		{
			UpsertDelta(outDeltas, std::move(delta));
		}

		if (!outDeltas.empty())
		{
			LOG_INFO(Net, "[QuestRuntime] ApplyEvent OK (type={}, target={}, deltas={})",
				GetQuestStepTypeName(eventType),
				targetId,
				outDeltas.size());
			return true;
		}

		LOG_DEBUG(Net, "[QuestRuntime] ApplyEvent produced no quest changes (type={}, target={})",
			GetQuestStepTypeName(eventType),
			targetId);
		return false;
	}

	const QuestDefinition* QuestRuntime::FindQuestDefinition(std::string_view questId) const
	{
		for (const QuestDefinition& definition : m_definitions)
		{
			if (definition.questId == questId)
			{
				return &definition;
			}
		}

		return nullptr;
	}

	bool QuestRuntime::LoadDefinitions()
	{
		m_definitions.clear();

		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, m_questDefinitionsRelativePath);
		if (jsonText.empty())
		{
			LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: empty or missing file (path={})", m_questDefinitionsRelativePath);
			return false;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, parseError))
		{
			LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: parse error '{}' (path={})", parseError, m_questDefinitionsRelativePath);
			return false;
		}

		const JsonValue* questsValue = FindObjectMember(root, "quests");
		if (questsValue == nullptr || questsValue->type != JsonType::Array)
		{
			LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: root.quests must be an array");
			return false;
		}

		std::unordered_set<std::string> questIds;
		for (size_t questIndex = 0; questIndex < questsValue->arrayValue.size(); ++questIndex)
		{
			const JsonValue& questValue = questsValue->arrayValue[questIndex];
			if (questValue.type != JsonType::Object)
			{
				LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quests[{}] must be an object", questIndex);
				m_definitions.clear();
				return false;
			}

			const JsonValue* idValue = FindObjectMember(questValue, "id");
			const JsonValue* stepsValue = FindObjectMember(questValue, "steps");
			if (idValue == nullptr || idValue->type != JsonType::String || idValue->stringValue.empty())
			{
				LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quests[{}].id must be a non-empty string", questIndex);
				m_definitions.clear();
				return false;
			}
			if (stepsValue == nullptr || stepsValue->type != JsonType::Array || stepsValue->arrayValue.empty())
			{
				LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}' must define at least one step", idValue->stringValue);
				m_definitions.clear();
				return false;
			}
			if (!questIds.emplace(idValue->stringValue).second)
			{
				LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: duplicate quest id '{}'", idValue->stringValue);
				m_definitions.clear();
				return false;
			}

			QuestDefinition definition{};
			definition.questId = idValue->stringValue;

			if (const JsonValue* prereqsValue = FindObjectMember(questValue, "prereqs");
				prereqsValue != nullptr)
			{
				if (prereqsValue->type != JsonType::Array)
				{
					LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.prereqs must be an array", definition.questId);
					m_definitions.clear();
					return false;
				}

				for (size_t prereqIndex = 0; prereqIndex < prereqsValue->arrayValue.size(); ++prereqIndex)
				{
					const JsonValue& prereqValue = prereqsValue->arrayValue[prereqIndex];
					if (prereqValue.type != JsonType::String || prereqValue.stringValue.empty())
					{
						LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.prereqs[{}] must be a non-empty string",
							definition.questId,
							prereqIndex);
						m_definitions.clear();
						return false;
					}

					definition.prerequisiteQuestIds.push_back(prereqValue.stringValue);
				}
			}

			for (size_t stepIndex = 0; stepIndex < stepsValue->arrayValue.size(); ++stepIndex)
			{
				const JsonValue& stepValue = stepsValue->arrayValue[stepIndex];
				if (stepValue.type != JsonType::Object)
				{
					LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.steps[{}] must be an object",
						definition.questId,
						stepIndex);
					m_definitions.clear();
					return false;
				}

				const JsonValue* typeValue = FindObjectMember(stepValue, "type");
				const JsonValue* targetValue = FindObjectMember(stepValue, "target");
				const JsonValue* requiredCountValue = FindObjectMember(stepValue, "requiredCount");
				if (typeValue == nullptr || typeValue->type != JsonType::String)
				{
					LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.steps[{}].type must be a string",
						definition.questId,
						stepIndex);
					m_definitions.clear();
					return false;
				}
				if (targetValue == nullptr || targetValue->type != JsonType::String || targetValue->stringValue.empty())
				{
					LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.steps[{}].target must be a non-empty string",
						definition.questId,
						stepIndex);
					m_definitions.clear();
					return false;
				}

				QuestStepDefinition step{};
				if (!TryParseQuestStepType(typeValue->stringValue, step.type))
				{
					LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.steps[{}].type '{}' is invalid",
						definition.questId,
						stepIndex,
						typeValue->stringValue);
					m_definitions.clear();
					return false;
				}

				step.targetId = targetValue->stringValue;
				step.requiredCount = 1;
				if (requiredCountValue != nullptr)
				{
					if (!TryGetUint(*requiredCountValue, step.requiredCount) || step.requiredCount == 0)
					{
						LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.steps[{}].requiredCount must be a positive integer",
							definition.questId,
							stepIndex);
						m_definitions.clear();
						return false;
					}
				}

				definition.steps.push_back(std::move(step));
			}

			if (const JsonValue* rewardsValue = FindObjectMember(questValue, "rewards");
				rewardsValue != nullptr)
			{
				if (rewardsValue->type != JsonType::Object)
				{
					LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.rewards must be an object", definition.questId);
					m_definitions.clear();
					return false;
				}

				if (const JsonValue* xpValue = FindObjectMember(*rewardsValue, "xp"); xpValue != nullptr)
				{
					if (!TryGetUint(*xpValue, definition.rewards.experience))
					{
						LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.rewards.xp must be a non-negative integer",
							definition.questId);
						m_definitions.clear();
						return false;
					}
				}

				if (const JsonValue* goldValue = FindObjectMember(*rewardsValue, "gold"); goldValue != nullptr)
				{
					if (!TryGetUint(*goldValue, definition.rewards.gold))
					{
						LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.rewards.gold must be a non-negative integer",
							definition.questId);
						m_definitions.clear();
						return false;
					}
				}

				if (const JsonValue* itemsValue = FindObjectMember(*rewardsValue, "items"); itemsValue != nullptr)
				{
					if (itemsValue->type != JsonType::Array)
					{
						LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.rewards.items must be an array",
							definition.questId);
						m_definitions.clear();
						return false;
					}

					for (size_t itemIndex = 0; itemIndex < itemsValue->arrayValue.size(); ++itemIndex)
					{
						const JsonValue& itemValue = itemsValue->arrayValue[itemIndex];
						if (itemValue.type != JsonType::Object)
						{
							LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.rewards.items[{}] must be an object",
								definition.questId,
								itemIndex);
							m_definitions.clear();
							return false;
						}

						const JsonValue* itemIdValue = FindObjectMember(itemValue, "itemId");
						const JsonValue* quantityValue = FindObjectMember(itemValue, "quantity");
						ItemStack item{};
						if (itemIdValue == nullptr || quantityValue == nullptr
							|| !TryGetUint(*itemIdValue, item.itemId)
							|| !TryGetUint(*quantityValue, item.quantity)
							|| item.itemId == 0
							|| item.quantity == 0)
						{
							LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}'.rewards.items[{}] must define positive itemId and quantity",
								definition.questId,
								itemIndex);
							m_definitions.clear();
							return false;
						}

						definition.rewards.items.push_back(item);
					}
				}
			}

			LOG_INFO(Net, "[QuestRuntime] Loaded quest definition (quest_id={}, prereqs={}, steps={}, reward_items={})",
				definition.questId,
				definition.prerequisiteQuestIds.size(),
				definition.steps.size(),
				definition.rewards.items.size());
			m_definitions.push_back(std::move(definition));
		}

		if (m_definitions.empty())
		{
			LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: no quests found (path={})", m_questDefinitionsRelativePath);
			return false;
		}

		for (const QuestDefinition& definition : m_definitions)
		{
			for (const std::string& prerequisiteQuestId : definition.prerequisiteQuestIds)
			{
				if (!questIds.contains(prerequisiteQuestId))
				{
					LOG_ERROR(Net, "[QuestRuntime] Definition load FAILED: quest '{}' references missing prerequisite '{}'",
						definition.questId,
						prerequisiteQuestId);
					m_definitions.clear();
					return false;
				}
			}
		}

		LOG_INFO(Net, "[QuestRuntime] Definition load OK (path={}, quests={})", m_questDefinitionsRelativePath, m_definitions.size());
		return true;
	}
}
