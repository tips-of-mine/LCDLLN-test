#include "engine/server/EventRuntime.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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

		/// Minimal JSON parser kept local to the event runtime to avoid new dependencies.
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

		/// Convert one JSON number into a validated finite float.
		bool TryGetFloat(const JsonValue& value, float& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue))
			{
				return false;
			}

			outValue = static_cast<float>(value.numberValue);
			return true;
		}

		/// Parse one JSON string into a dynamic event trigger type.
		bool TryParseDynamicEventTriggerType(std::string_view text, DynamicEventTriggerType& outType)
		{
			if (text == "time")
			{
				outType = DynamicEventTriggerType::Time;
				return true;
			}
			if (text == "random")
			{
				outType = DynamicEventTriggerType::Random;
				return true;
			}

			return false;
		}
	}

	const char* GetDynamicEventTriggerTypeName(DynamicEventTriggerType type)
	{
		switch (type)
		{
		case DynamicEventTriggerType::Time:
			return "time";
		case DynamicEventTriggerType::Random:
			return "random";
		default:
			return "time";
		}
	}

	EventRuntime::EventRuntime(const engine::core::Config& config)
		: m_config(config)
	{
		LOG_INFO(Net, "[EventRuntime] Constructed");
	}

	EventRuntime::~EventRuntime()
	{
		Shutdown();
	}

	bool EventRuntime::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[EventRuntime] Init ignored: already initialized");
			return true;
		}

		if (!LoadDefinitions())
		{
			LOG_ERROR(Net, "[EventRuntime] Init FAILED: definition load failed");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[EventRuntime] Init OK (events={})", m_definitions.size());
		return true;
	}

	void EventRuntime::Shutdown()
	{
		if (!m_initialized && m_definitions.empty())
		{
			return;
		}

		const size_t definitionCount = m_definitions.size();
		m_definitions.clear();
		m_initialized = false;
		LOG_INFO(Net, "[EventRuntime] Destroyed (events={})", definitionCount);
	}

	bool EventRuntime::LoadDefinitions()
	{
		m_definitions.clear();

		const std::filesystem::path zonesDirectory = engine::platform::FileSystem::ResolveContentPath(m_config, "zones");
		const std::vector<std::filesystem::path> zoneEntries = engine::platform::FileSystem::ListDirectory(zonesDirectory);
		if (zoneEntries.empty())
		{
			LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: no zone directories found ({})", zonesDirectory.string());
			return false;
		}

		std::unordered_set<std::string> eventIds;
		for (const std::filesystem::path& zoneEntry : zoneEntries)
		{
			if (!std::filesystem::is_directory(zoneEntry))
			{
				continue;
			}

			const std::string relativePath = "zones/" + zoneEntry.filename().string() + "/events.json";
			const std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(m_config, relativePath);
			if (!engine::platform::FileSystem::Exists(fullPath))
			{
				LOG_DEBUG(Net, "[EventRuntime] Zone event file skipped: missing {}", relativePath);
				continue;
			}

			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
			if (jsonText.empty())
			{
				LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: empty event file ({})", relativePath);
				m_definitions.clear();
				return false;
			}

			JsonValue root;
			std::string parseError;
			JsonParser parser(jsonText);
			if (!parser.Parse(root, parseError))
			{
				LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: parse error '{}' ({})", parseError, relativePath);
				m_definitions.clear();
				return false;
			}

			const JsonValue* zoneIdValue = FindObjectMember(root, "zoneId");
			const JsonValue* eventsValue = FindObjectMember(root, "events");
			uint32_t zoneId = 0;
			if (zoneIdValue == nullptr || !TryGetUint(*zoneIdValue, zoneId))
			{
				LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: {} must define a valid root.zoneId", relativePath);
				m_definitions.clear();
				return false;
			}
			if (eventsValue == nullptr || eventsValue->type != JsonType::Array || eventsValue->arrayValue.empty())
			{
				LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: {} must define a non-empty root.events array", relativePath);
				m_definitions.clear();
				return false;
			}

			for (size_t eventIndex = 0; eventIndex < eventsValue->arrayValue.size(); ++eventIndex)
			{
				const JsonValue& eventValue = eventsValue->arrayValue[eventIndex];
				if (eventValue.type != JsonType::Object)
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: {} events[{}] must be an object",
						relativePath,
						eventIndex);
					m_definitions.clear();
					return false;
				}

				const JsonValue* idValue = FindObjectMember(eventValue, "id");
				const JsonValue* triggerValue = FindObjectMember(eventValue, "trigger");
				const JsonValue* cooldownValue = FindObjectMember(eventValue, "cooldownSec");
				const JsonValue* phasesValue = FindObjectMember(eventValue, "phases");
				if (idValue == nullptr || idValue->type != JsonType::String || idValue->stringValue.empty())
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: {} events[{}].id must be a non-empty string",
						relativePath,
						eventIndex);
					m_definitions.clear();
					return false;
				}

				DynamicEventDefinition definition{};
				definition.eventId = idValue->stringValue;
				definition.zoneId = zoneId;
				if (!eventIds.emplace(definition.eventId).second)
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: duplicate event id '{}'", definition.eventId);
					m_definitions.clear();
					return false;
				}

				if (triggerValue == nullptr || triggerValue->type != JsonType::Object)
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' must define a trigger object", definition.eventId);
					m_definitions.clear();
					return false;
				}

				const JsonValue* triggerTypeValue = FindObjectMember(*triggerValue, "type");
				const JsonValue* triggerSecondsValue = FindObjectMember(*triggerValue, "seconds");
				if (triggerTypeValue == nullptr || triggerTypeValue->type != JsonType::String
					|| !TryParseDynamicEventTriggerType(triggerTypeValue->stringValue, definition.triggerType))
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' trigger.type is invalid", definition.eventId);
					m_definitions.clear();
					return false;
				}
				if (triggerSecondsValue == nullptr
					|| !TryGetUint(*triggerSecondsValue, definition.triggerSeconds)
					|| definition.triggerSeconds == 0)
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' trigger.seconds must be positive", definition.eventId);
					m_definitions.clear();
					return false;
				}

				if (cooldownValue == nullptr
					|| !TryGetUint(*cooldownValue, definition.cooldownSeconds)
					|| definition.cooldownSeconds == 0)
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' cooldownSec must be positive", definition.eventId);
					m_definitions.clear();
					return false;
				}

				definition.startNotificationText = idValue->stringValue;
				if (const JsonValue* startTextValue = FindObjectMember(eventValue, "startText");
					startTextValue != nullptr && startTextValue->type == JsonType::String)
				{
					definition.startNotificationText = startTextValue->stringValue;
				}

				definition.completionNotificationText = idValue->stringValue;
				if (const JsonValue* completionTextValue = FindObjectMember(eventValue, "completionText");
					completionTextValue != nullptr && completionTextValue->type == JsonType::String)
				{
					definition.completionNotificationText = completionTextValue->stringValue;
				}

				if (phasesValue == nullptr || phasesValue->type != JsonType::Array || phasesValue->arrayValue.empty())
				{
					LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' must define a non-empty phases array",
						definition.eventId);
					m_definitions.clear();
					return false;
				}

				for (size_t phaseIndex = 0; phaseIndex < phasesValue->arrayValue.size(); ++phaseIndex)
				{
					const JsonValue& phaseValue = phasesValue->arrayValue[phaseIndex];
					if (phaseValue.type != JsonType::Object)
					{
						LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase[{}] must be an object",
							definition.eventId,
							phaseIndex);
						m_definitions.clear();
						return false;
					}

					const JsonValue* phaseIdValue = FindObjectMember(phaseValue, "id");
					const JsonValue* phaseTextValue = FindObjectMember(phaseValue, "text");
					const JsonValue* phaseProgressValue = FindObjectMember(phaseValue, "progressRequired");
					const JsonValue* phaseSpawnsValue = FindObjectMember(phaseValue, "spawns");
					if (phaseIdValue == nullptr || phaseIdValue->type != JsonType::String || phaseIdValue->stringValue.empty())
					{
						LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase[{}].id must be a non-empty string",
							definition.eventId,
							phaseIndex);
						m_definitions.clear();
						return false;
					}
					if (phaseTextValue == nullptr || phaseTextValue->type != JsonType::String)
					{
						LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' must define text",
							definition.eventId,
							phaseIdValue->stringValue);
						m_definitions.clear();
						return false;
					}
					uint32_t progressRequired = 0;
					if (phaseProgressValue == nullptr
						|| !TryGetUint(*phaseProgressValue, progressRequired)
						|| progressRequired == 0)
					{
						LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' progressRequired must be positive",
							definition.eventId,
							phaseIdValue->stringValue);
						m_definitions.clear();
						return false;
					}
					definition.phases.emplace_back();
					DynamicEventPhaseDefinition& phase = definition.phases.back();
					phase.phaseId = phaseIdValue->stringValue;
					phase.notificationText = phaseTextValue->stringValue;
					phase.progressRequired = progressRequired;

					if (phaseSpawnsValue == nullptr || phaseSpawnsValue->type != JsonType::Array || phaseSpawnsValue->arrayValue.empty())
					{
						LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' must define a non-empty spawns array",
							definition.eventId,
							phase.phaseId);
						m_definitions.clear();
						return false;
					}

					for (size_t spawnIndex = 0; spawnIndex < phaseSpawnsValue->arrayValue.size(); ++spawnIndex)
					{
						const JsonValue& spawnValue = phaseSpawnsValue->arrayValue[spawnIndex];
						if (spawnValue.type != JsonType::Object)
						{
							LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' spawn[{}] must be an object",
								definition.eventId,
								phase.phaseId,
								spawnIndex);
							m_definitions.clear();
							return false;
						}

						const JsonValue* archetypeIdValue = FindObjectMember(spawnValue, "archetypeId");
						const JsonValue* positionValue = FindObjectMember(spawnValue, "position");
						const JsonValue* countValue = FindObjectMember(spawnValue, "count");
						const JsonValue* leashValue = FindObjectMember(spawnValue, "leashDistanceMeters");
						DynamicEventSpawnDefinition spawn{};
						if (archetypeIdValue == nullptr
							|| !TryGetUint(*archetypeIdValue, spawn.archetypeId)
							|| spawn.archetypeId == 0)
						{
							LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' spawn[{}] archetypeId must be positive",
								definition.eventId,
								phase.phaseId,
								spawnIndex);
							m_definitions.clear();
							return false;
						}
						if (countValue == nullptr
							|| !TryGetUint(*countValue, spawn.count)
							|| spawn.count == 0)
						{
							LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' spawn[{}] count must be positive",
								definition.eventId,
								phase.phaseId,
								spawnIndex);
							m_definitions.clear();
							return false;
						}
						if (positionValue == nullptr
							|| positionValue->type != JsonType::Array
							|| positionValue->arrayValue.size() != 3
							|| !TryGetFloat(positionValue->arrayValue[0], spawn.positionMetersX)
							|| !TryGetFloat(positionValue->arrayValue[1], spawn.positionMetersY)
							|| !TryGetFloat(positionValue->arrayValue[2], spawn.positionMetersZ))
						{
							LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' spawn[{}] position must be [x, y, z]",
								definition.eventId,
								phase.phaseId,
								spawnIndex);
							m_definitions.clear();
							return false;
						}

						if (leashValue != nullptr)
						{
							if (!TryGetFloat(*leashValue, spawn.leashDistanceMeters) || spawn.leashDistanceMeters <= 0.0f)
							{
								LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' phase '{}' spawn[{}] leashDistanceMeters must be positive",
									definition.eventId,
									phase.phaseId,
									spawnIndex);
								m_definitions.clear();
								return false;
							}
						}

						phase.spawns.push_back(std::move(spawn));
					}
				}

				if (const JsonValue* rewardsValue = FindObjectMember(eventValue, "rewards");
					rewardsValue != nullptr)
				{
					if (rewardsValue->type != JsonType::Object)
					{
						LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' rewards must be an object", definition.eventId);
						m_definitions.clear();
						return false;
					}

					if (const JsonValue* xpValue = FindObjectMember(*rewardsValue, "xp"); xpValue != nullptr)
					{
						if (!TryGetUint(*xpValue, definition.rewards.experience))
						{
							LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' rewards.xp must be a non-negative integer", definition.eventId);
							m_definitions.clear();
							return false;
						}
					}

					if (const JsonValue* goldValue = FindObjectMember(*rewardsValue, "gold"); goldValue != nullptr)
					{
						if (!TryGetUint(*goldValue, definition.rewards.gold))
						{
							LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' rewards.gold must be a non-negative integer", definition.eventId);
							m_definitions.clear();
							return false;
						}
					}

					if (const JsonValue* itemsValue = FindObjectMember(*rewardsValue, "items"); itemsValue != nullptr)
					{
						if (itemsValue->type != JsonType::Array)
						{
							LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' rewards.items must be an array", definition.eventId);
							m_definitions.clear();
							return false;
						}

						for (size_t itemIndex = 0; itemIndex < itemsValue->arrayValue.size(); ++itemIndex)
						{
							const JsonValue& itemValue = itemsValue->arrayValue[itemIndex];
							if (itemValue.type != JsonType::Object)
							{
								LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' rewards.items[{}] must be an object",
									definition.eventId,
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
								LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: event '{}' rewards.items[{}] must define positive itemId and quantity",
									definition.eventId,
									itemIndex);
								m_definitions.clear();
								return false;
							}

							definition.rewards.items.push_back(item);
						}
					}
				}

				LOG_INFO(Net, "[EventRuntime] Loaded event definition (id={}, zone_id={}, trigger={}, phases={})",
					definition.eventId,
					definition.zoneId,
					GetDynamicEventTriggerTypeName(definition.triggerType),
					definition.phases.size());
				m_definitions.push_back(std::move(definition));
			}

			LOG_INFO(Net, "[EventRuntime] Zone event file loaded (path={}, zone_id={}, events={})",
				relativePath,
				zoneId,
				eventsValue->arrayValue.size());
		}

		if (m_definitions.empty())
		{
			LOG_ERROR(Net, "[EventRuntime] Definition load FAILED: no events loaded");
			return false;
		}

		LOG_INFO(Net, "[EventRuntime] Definition load OK (events={})", m_definitions.size());
		return true;
	}
}
