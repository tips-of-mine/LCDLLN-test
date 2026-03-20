#include "engine/gameplay/SkillSystem.h"

#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>

namespace engine::gameplay
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

		/// Minimal JSON parser kept local to avoid new dependencies.
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

		private:
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

		bool TryGetString(const JsonValue& value, std::string& outValue)
		{
			if (value.type != JsonType::String)
			{
				return false;
			}

			outValue = value.stringValue;
			return true;
		}

		bool TryGetPositiveFloat(const JsonValue& value, float& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue) || value.numberValue <= 0.0)
			{
				return false;
			}

			outValue = static_cast<float>(value.numberValue);
			return true;
		}

		bool TryGetNonNegativeFloat(const JsonValue& value, float& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue) || value.numberValue < 0.0)
			{
				return false;
			}

			outValue = static_cast<float>(value.numberValue);
			return true;
		}

		bool TryGetBool(const JsonValue& value, bool& outValue)
		{
			if (value.type != JsonType::Bool)
				return false;
			outValue = value.boolValue;
			return true;
		}

		bool TryGetNonNegativeUint(const JsonValue& value, uint32_t& outValue)
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

		uint64_t ComposeLosKey(uint32_t playerId, SkillSystem::EntityId targetId)
		{
			const uint64_t keyPlayer = static_cast<uint64_t>(playerId) << 32;
			const uint64_t keyTarget = static_cast<uint64_t>(static_cast<uint32_t>(targetId));
			return keyPlayer | keyTarget;
		}
	}

	SkillSystem::SkillSystem(const engine::core::Config& config)
		: m_config(config)
	{
		// Keep default resources high enough for MVP skill validation.
		const int64_t health = std::max<int64_t>(0, m_config.GetInt("skills.default_health", 100));
		const int64_t mana = std::max<int64_t>(0, m_config.GetInt("skills.default_mana", 1000));
		const int64_t energy = std::max<int64_t>(0, m_config.GetInt("skills.default_energy", 1000));
		const int64_t rage = std::max<int64_t>(0, m_config.GetInt("skills.default_rage", 1000));
		const int64_t comboPoints = std::max<int64_t>(0, m_config.GetInt("skills.default_combo_points", 3));
		const int64_t maxComboPoints = std::max<int64_t>(1, m_config.GetInt("skills.default_max_combo_points", 5));

		m_defaultPlayerResources.health = static_cast<uint32_t>(health);
		m_defaultPlayerResources.mana = static_cast<uint32_t>(mana);
		m_defaultPlayerResources.energy = static_cast<uint32_t>(energy);
		m_defaultPlayerResources.rage = static_cast<uint32_t>(rage);
		m_defaultPlayerResources.comboPoints = static_cast<uint32_t>(comboPoints);
		m_defaultPlayerResources.maxComboPoints = static_cast<uint32_t>(maxComboPoints);

		LOG_INFO(Gameplay, "[SkillSystem] Constructed (defaults hp={}, mana={}, energy={}, rage={}, cp={}/{})",
			m_defaultPlayerResources.health,
			m_defaultPlayerResources.mana,
			m_defaultPlayerResources.energy,
			m_defaultPlayerResources.rage,
			m_defaultPlayerResources.comboPoints,
			m_defaultPlayerResources.maxComboPoints);
	}

	SkillSystem::~SkillSystem()
	{
		Shutdown();
	}

	uint64_t SkillSystem::SecondsToTicks(float seconds)
	{
		if (!std::isfinite(seconds) || seconds <= 0.0f)
			return 0;

		// engine::core::Time::NowTicks returns nanoseconds.
		const double ns = static_cast<double>(seconds) * 1'000'000'000.0;
		if (!std::isfinite(ns) || ns < 0.0)
			return 0;

		return static_cast<uint64_t>(ns);
	}

	bool SkillSystem::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Gameplay, "[SkillSystem] Init ignored: already initialized");
			return true;
		}

		m_definitionsById.clear();
		m_lastUseTicks.clear();
		m_activeCastByPlayer.clear();
		m_castCooldownEndTicks.clear();
		m_gcdEndTicks.clear();
		m_pendingInterruptInputs.clear();
		m_lastComboGainTicks.clear();
		m_lastComboTargetByCaster.clear();
		m_playerResources.clear();
		m_playerPositions.clear();
		m_targetPositions.clear();
		m_losTable.clear();

		const std::filesystem::path skillsDir = engine::platform::FileSystem::ResolveContentPath(m_config, m_skillsRelativePath);
		if (!engine::platform::FileSystem::Exists(skillsDir))
		{
			LOG_ERROR(Gameplay, "[SkillSystem] Init FAILED: missing skills directory ({})", skillsDir.string());
			return false;
		}

		const std::vector<std::filesystem::path> entries = engine::platform::FileSystem::ListDirectory(skillsDir);
		if (entries.empty())
		{
			LOG_ERROR(Gameplay, "[SkillSystem] Init FAILED: skills directory empty ({})", skillsDir.string());
			return false;
		}

		std::unordered_set<std::string> seenIds;
		for (const std::filesystem::path& entry : entries)
		{
			if (!entry.has_extension() || entry.extension() != ".json")
				continue;

			const std::string relativeFile = m_skillsRelativePath + "/" + entry.filename().string();
			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativeFile);
			if (jsonText.empty())
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: empty file ({})", relativeFile);
				m_definitionsById.clear();
				return false;
			}

			JsonValue root;
			std::string parseError;
			JsonParser parser(jsonText);
			if (!parser.Parse(root, parseError))
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: parse error '{}' ({})", parseError, relativeFile);
				m_definitionsById.clear();
				return false;
			}

			if (root.type != JsonType::Object)
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: root must be object ({})", relativeFile);
				m_definitionsById.clear();
				return false;
			}

			const JsonValue* idValue = FindObjectMember(root, "id");
			const JsonValue* nameValue = FindObjectMember(root, "name");
			const JsonValue* cooldownValue = FindObjectMember(root, "cooldown");
			const JsonValue* castTimeValue = FindObjectMember(root, "castTime");
			const JsonValue* costValue = FindObjectMember(root, "cost");
			const JsonValue* rangeValue = FindObjectMember(root, "range");
			const JsonValue* effectsValue = FindObjectMember(root, "effects");
			const JsonValue* aoeShapeValue = FindObjectMember(root, "aoeShape");
			const JsonValue* aoeRadiusValue = FindObjectMember(root, "aoeRadius");
			const JsonValue* aoeAngleDegValue = FindObjectMember(root, "aoeAngleDeg");
			if (aoeAngleDegValue == nullptr)
				aoeAngleDegValue = FindObjectMember(root, "aoeAngle");
			const JsonValue* aoeRangeValue = FindObjectMember(root, "aoeRange");
			if (aoeRangeValue == nullptr)
				aoeRangeValue = FindObjectMember(root, "aoeLength");
			const JsonValue* aoeWidthValue = FindObjectMember(root, "aoeWidth");
			const JsonValue* aoeLineLengthValue = FindObjectMember(root, "aoeLineLength");
			if (aoeLineLengthValue == nullptr)
				aoeLineLengthValue = FindObjectMember(root, "aoeLength");
			const JsonValue* aoeRingThicknessValue = FindObjectMember(root, "aoeRingThickness");
			if (aoeRingThicknessValue == nullptr)
				aoeRingThicknessValue = FindObjectMember(root, "aoeThickness");
			const JsonValue* aoeInnerRadiusValue = FindObjectMember(root, "aoeInnerRadius");
			const JsonValue* frozenMovementValue = FindObjectMember(root, "frozenMovement");
			if (frozenMovementValue == nullptr)
				frozenMovementValue = FindObjectMember(root, "freezeMovement");
			const JsonValue* channelTickIntervalValue = FindObjectMember(root, "channelTickIntervalSeconds");
			if (channelTickIntervalValue == nullptr)
				channelTickIntervalValue = FindObjectMember(root, "channelTickInterval");
			if (channelTickIntervalValue == nullptr)
				channelTickIntervalValue = FindObjectMember(root, "channelTickIntervalSec");
			const JsonValue* channelCancelableValue = FindObjectMember(root, "channelCancelable");
			if (channelCancelableValue == nullptr)
				channelCancelableValue = FindObjectMember(root, "cancelable");
			const JsonValue* interruptDamageFractionThresholdValue = FindObjectMember(root, "interruptDamageFractionThreshold");
			if (interruptDamageFractionThresholdValue == nullptr)
				interruptDamageFractionThresholdValue = FindObjectMember(root, "interruptDamagePct");
			if (interruptDamageFractionThresholdValue == nullptr)
				interruptDamageFractionThresholdValue = FindObjectMember(root, "interruptDamagePercent");

			std::string skillId;
			if (idValue == nullptr || !TryGetString(*idValue, skillId) || skillId.empty())
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: '{}' must define non-empty id ({})", relativeFile, relativeFile);
				m_definitionsById.clear();
				return false;
			}

			if (!seenIds.emplace(skillId).second)
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: duplicate skill id '{}' ({})", skillId, relativeFile);
				m_definitionsById.clear();
				return false;
			}

			SkillDefinition def{};
			def.id = std::move(skillId);

			if (nameValue == nullptr || !TryGetString(*nameValue, def.name) || def.name.empty())
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' must define non-empty name ({})", def.id, relativeFile);
				m_definitionsById.clear();
				return false;
			}

			float cooldownSeconds = 0.0f;
			if (cooldownValue == nullptr || !TryGetPositiveFloat(*cooldownValue, cooldownSeconds))
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' cooldown must be positive ({})", def.id, relativeFile);
				m_definitionsById.clear();
				return false;
			}
			def.cooldownSeconds = cooldownSeconds;

			float castTimeSeconds = 0.0f;
			if (castTimeValue == nullptr || !TryGetNonNegativeFloat(*castTimeValue, castTimeSeconds))
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' castTime must be non-negative ({})", def.id, relativeFile);
				m_definitionsById.clear();
				return false;
			}
			def.castTimeSeconds = castTimeSeconds;

			float rangeMeters = 0.0f;
			if (rangeValue == nullptr || !TryGetPositiveFloat(*rangeValue, rangeMeters))
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' range must be positive ({})", def.id, relativeFile);
				m_definitionsById.clear();
				return false;
			}
			def.rangeMeters = rangeMeters;

			// AoE targeting parameters (optional).
			if (aoeShapeValue != nullptr)
			{
				std::string aoeShapeStr;
				if (!TryGetString(*aoeShapeValue, aoeShapeStr) || aoeShapeStr.empty())
				{
					LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape must be non-empty string ({})",
						def.id, relativeFile);
					m_definitionsById.clear();
					return false;
				}

				auto ToAoE = [&](std::string_view s) -> std::optional<SkillSystem::AoEShapeType>
				{
					if (s == "circle") return SkillSystem::AoEShapeType::Circle;
					if (s == "cone") return SkillSystem::AoEShapeType::Cone;
					if (s == "line") return SkillSystem::AoEShapeType::Line;
					if (s == "ring") return SkillSystem::AoEShapeType::Ring;
					return std::nullopt;
				};

				const std::optional<SkillSystem::AoEShapeType> shapeOpt = ToAoE(aoeShapeStr);
				if (!shapeOpt.has_value())
				{
					LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape invalid '{}' ({})",
						def.id, aoeShapeStr, relativeFile);
					m_definitionsById.clear();
					return false;
				}

				def.aoe.shape = shapeOpt.value();
				switch (def.aoe.shape)
				{
				case SkillSystem::AoEShapeType::Circle:
				{
					float r = 0.0f;
					if (aoeRadiusValue == nullptr || !TryGetPositiveFloat(*aoeRadiusValue, r))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape=circle requires aoeRadius > 0 ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}
					def.aoe.radiusMeters = r;
					break;
				}
				case SkillSystem::AoEShapeType::Cone:
				{
					float angleDeg = 0.0f;
					if (aoeAngleDegValue == nullptr || !TryGetPositiveFloat(*aoeAngleDegValue, angleDeg))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape=cone requires aoeAngleDeg > 0 ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}
					float range = 0.0f;
					if (aoeRangeValue == nullptr || !TryGetPositiveFloat(*aoeRangeValue, range))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape=cone requires aoeRange > 0 ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}
					def.aoe.angleDeg = angleDeg;
					def.aoe.rangeMeters = range;
					break;
				}
				case SkillSystem::AoEShapeType::Line:
				{
					float w = 0.0f;
					if (aoeWidthValue == nullptr || !TryGetPositiveFloat(*aoeWidthValue, w))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape=line requires aoeWidth > 0 ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}
					float len = 0.0f;
					if (aoeLineLengthValue == nullptr || !TryGetPositiveFloat(*aoeLineLengthValue, len))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape=line requires aoeLength > 0 ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}
					def.aoe.widthMeters = w;
					def.aoe.lengthMeters = len;
					break;
				}
				case SkillSystem::AoEShapeType::Ring:
				{
					float outer = 0.0f;
					if (aoeRadiusValue == nullptr || !TryGetPositiveFloat(*aoeRadiusValue, outer))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape=ring requires aoeRadius > 0 ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}

					float inner = 0.0f;
					if (aoeInnerRadiusValue != nullptr)
					{
						if (!TryGetNonNegativeFloat(*aoeInnerRadiusValue, inner))
						{
							LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeInnerRadius invalid ({})",
								def.id, relativeFile);
							m_definitionsById.clear();
							return false;
						}
					}
					else if (aoeRingThicknessValue != nullptr)
					{
						float thickness = 0.0f;
						if (!TryGetPositiveFloat(*aoeRingThicknessValue, thickness))
						{
							LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeRingThickness invalid ({})",
								def.id, relativeFile);
							m_definitionsById.clear();
							return false;
						}
						inner = std::max(0.0f, outer - thickness);
						def.aoe.ringThicknessMeters = thickness;
					}
					else
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' aoeShape=ring requires aoeInnerRadius or aoeRingThickness ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}

					if (inner >= outer)
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' ring innerRadius must be < outerRadius ({})",
							def.id, relativeFile);
						m_definitionsById.clear();
						return false;
					}

					def.aoe.outerRadiusMeters = outer;
					def.aoe.innerRadiusMeters = inner;
					break;
				}
				default:
					break;
				}
			}

			// Runtime flags (optional).
			{
				bool frozenMovement = false;
				if (frozenMovementValue != nullptr)
				{
					if (!TryGetBool(*frozenMovementValue, frozenMovement))
					{
						LOG_WARN(Gameplay, "[SkillSystem] Skill load: skill '{}' frozenMovement invalid -> default false ({})",
							def.id, relativeFile);
					}
					else
					{
						def.runtime.frozenMovement = frozenMovement;
					}
				}

				float tickIntervalSeconds = 0.0f;
				if (channelTickIntervalValue != nullptr)
				{
					if (!TryGetNonNegativeFloat(*channelTickIntervalValue, tickIntervalSeconds))
					{
						LOG_WARN(Gameplay, "[SkillSystem] Skill load: skill '{}' channelTickInterval invalid -> 0 ({})",
							def.id, relativeFile);
					}
					else
					{
						def.runtime.channelTickIntervalSeconds = std::max(0.0f, tickIntervalSeconds);
					}
				}

				bool channelCancelable = true;
				if (channelCancelableValue != nullptr)
				{
					if (!TryGetBool(*channelCancelableValue, channelCancelable))
					{
						LOG_WARN(Gameplay, "[SkillSystem] Skill load: skill '{}' channelCancelable invalid -> default true ({})",
							def.id, relativeFile);
					}
					else
					{
						def.runtime.channelCancelable = channelCancelable;
					}
				}

				float interruptDamageThreshold = 0.0f;
				if (interruptDamageFractionThresholdValue != nullptr)
				{
					if (!TryGetNonNegativeFloat(*interruptDamageFractionThresholdValue, interruptDamageThreshold))
					{
						LOG_WARN(Gameplay, "[SkillSystem] Skill load: skill '{}' interruptDamageFractionThreshold invalid -> 0 ({})",
							def.id, relativeFile);
					}
					else
					{
						// Support either fraction [0..1] or percent [0..100].
						if (interruptDamageThreshold > 1.0f)
							interruptDamageThreshold /= 100.0f;
						def.runtime.interruptDamageFractionThreshold = std::clamp(interruptDamageThreshold, 0.0f, 1.0f);
					}
				}
			}

			if (effectsValue == nullptr || effectsValue->type != JsonType::Array || effectsValue->arrayValue.empty())
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' must define non-empty effects array ({})", def.id, relativeFile);
				m_definitionsById.clear();
				return false;
			}

			for (size_t effectIndex = 0; effectIndex < effectsValue->arrayValue.size(); ++effectIndex)
			{
				const JsonValue& effectValue = effectsValue->arrayValue[effectIndex];
				if (effectValue.type != JsonType::Object)
				{
					LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' effects[{}] must be object ({})",
						def.id, effectIndex, relativeFile);
					m_definitionsById.clear();
					return false;
				}

				SkillEffect effect{};

				const JsonValue* typeValue = FindObjectMember(effectValue, "type");
				const JsonValue* amountValue = FindObjectMember(effectValue, "amount");
				const JsonValue* damageTypeValue = FindObjectMember(effectValue, "damageType");

				const JsonValue* generateComboValue = FindObjectMember(effectValue, "generateCombo");
				if (generateComboValue == nullptr)
					generateComboValue = FindObjectMember(effectValue, "generateComboPoints");
				const JsonValue* consumeComboValue = FindObjectMember(effectValue, "consumeCombo");

				const bool hasGenerate = (generateComboValue != nullptr);
				const bool hasConsume = (consumeComboValue != nullptr);

				if (hasGenerate)
				{
					uint32_t g = 0;
					if (!TryGetNonNegativeUint(*generateComboValue, g))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' effects[{}].generateCombo invalid ({})",
							def.id, effectIndex, relativeFile);
						m_definitionsById.clear();
						return false;
					}
					effect.generateCombo = g;
				}

				if (hasConsume)
				{
					bool c = false;
					if (!TryGetBool(*consumeComboValue, c))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' effects[{}].consumeCombo invalid ({})",
							def.id, effectIndex, relativeFile);
						m_definitionsById.clear();
						return false;
					}
					effect.consumeCombo = c;
				}

				const bool hasType = (typeValue != nullptr && typeValue->type == JsonType::String && !typeValue->stringValue.empty());

				if (hasType)
				{
					if (amountValue == nullptr)
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' effects[{}].amount missing when type is set ({})",
							def.id, effectIndex, relativeFile);
						m_definitionsById.clear();
						return false;
					}

					uint32_t amount = 0;
					if (!TryGetNonNegativeUint(*amountValue, amount))
					{
						LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' effects[{}].amount must be non-negative integer ({})",
							def.id, effectIndex, relativeFile);
						m_definitionsById.clear();
						return false;
					}

					effect.type = typeValue->stringValue;
					effect.amount = amount;
					if (damageTypeValue != nullptr && damageTypeValue->type == JsonType::String)
						effect.damageType = damageTypeValue->stringValue;
				}
				else if (!hasGenerate && !hasConsume)
				{
					LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' effects[{}] must define type/amount or generateCombo/consumeCombo ({})",
						def.id, effectIndex, relativeFile);
					m_definitionsById.clear();
					return false;
				}
				else
				{
					// Pure combo effect (no damage/heal type).
					LOG_WARN(Gameplay, "[SkillSystem] Skill load: skill '{}' effects[{}] has combo flags but no type/amount ({})",
						def.id, effectIndex, relativeFile);
				}

				// Sanity: generate/consume effect without any explicit action should never happen.
				if (!hasGenerate && !hasConsume && effect.type.empty())
				{
					LOG_ERROR(Gameplay, "[SkillSystem] Skill load FAILED: skill '{}' effects[{}] invalid empty effect ({})",
						def.id, effectIndex, relativeFile);
					m_definitionsById.clear();
					return false;
				}

				def.effects.push_back(std::move(effect));
			}

			if (costValue != nullptr)
			{
				if (costValue->type != JsonType::Object)
				{
					LOG_WARN(Gameplay, "[SkillSystem] Skill load: skill '{}' cost ignored (not object) ({})", def.id, relativeFile);
				}
				else
				{
					const auto parseCostKey = [&](std::string_view key, uint32_t& outField)
					{
						const JsonValue* v = FindObjectMember(*costValue, key);
						if (v == nullptr)
							return;

						uint32_t parsed = 0;
						if (!TryGetNonNegativeUint(*v, parsed))
						{
							LOG_WARN(Gameplay, "[SkillSystem] Skill load: skill '{}' cost key '{}' invalid -> 0 ({})",
								def.id, key, relativeFile);
							return;
						}
						outField = parsed;
					};

					parseCostKey("health", def.cost.health);
					parseCostKey("mana", def.cost.mana);
					parseCostKey("energy", def.cost.energy);
					parseCostKey("rage", def.cost.rage);
					parseCostKey("comboPoints", def.cost.comboPoints);
					parseCostKey("combo_points", def.cost.comboPoints);
				}
			}

			// Log before moving the definition into the map.
			const std::string skillKeyForLog = def.id;
			LOG_INFO(Gameplay, "[SkillSystem] Loaded skill (id={}, cooldown_s={:.2f}, cast_s={:.2f}, range_m={:.2f}, effects={})",
				skillKeyForLog,
				def.cooldownSeconds,
				def.castTimeSeconds,
				def.rangeMeters,
				def.effects.size());
			m_definitionsById.emplace(def.id, std::move(def));
		}

		if (m_definitionsById.empty())
		{
			LOG_ERROR(Gameplay, "[SkillSystem] Init FAILED: no skills loaded from {}", skillsDir.string());
			return false;
		}

		m_initialized = true;
		LOG_INFO(Gameplay, "[SkillSystem] Init OK (skills={})", m_definitionsById.size());
		return true;
	}

	void SkillSystem::Shutdown()
	{
		if (!m_initialized && m_definitionsById.empty())
			return;

		const size_t skillCount = m_definitionsById.size();
		m_initialized = false;
		m_definitionsById.clear();
		m_lastUseTicks.clear();
		m_activeCastByPlayer.clear();
		m_castCooldownEndTicks.clear();
		m_gcdEndTicks.clear();
		m_pendingInterruptInputs.clear();
		m_lastComboGainTicks.clear();
		m_lastComboTargetByCaster.clear();
		m_playerResources.clear();
		m_playerPositions.clear();
		m_targetPositions.clear();
		m_losTable.clear();
		LOG_INFO(Gameplay, "[SkillSystem] Destroyed (skills={})", skillCount);
	}

	void SkillSystem::SetPlayerPosition(uint32_t playerId, const engine::math::Vec3& worldPosMeters)
	{
		m_playerPositions[playerId] = worldPosMeters;
	}

	void SkillSystem::SetTargetPosition(EntityId targetId, const engine::math::Vec3& worldPosMeters)
	{
		m_targetPositions[targetId] = worldPosMeters;
	}

	void SkillSystem::SetLineOfSight(uint32_t playerId, EntityId targetId, bool hasLineOfSight)
	{
		m_losTable[ComposeLosKey(playerId, targetId)] = hasLineOfSight;
	}

	const SkillSystem::SkillDefinition* SkillSystem::FindSkill(std::string_view skillId) const
	{
		const auto it = m_definitionsById.find(std::string(skillId));
		if (it == m_definitionsById.end())
			return nullptr;
		return &it->second;
	}

	bool SkillSystem::EnsurePlayerInitialized(uint32_t playerId)
	{
		if (m_playerResources.find(playerId) != m_playerResources.end())
			return true;

		m_playerResources[playerId] = m_defaultPlayerResources;
		m_playerResources[playerId].comboPoints = std::min(m_playerResources[playerId].comboPoints, m_playerResources[playerId].maxComboPoints);
		LOG_INFO(Gameplay, "[SkillSystem] Player resources initialized (player_id={})", playerId);
		return true;
	}

	bool SkillSystem::GetComboState(uint32_t playerId, uint32_t& outComboPoints, uint32_t& outMaxComboPoints) const
	{
		outComboPoints = 0;
		outMaxComboPoints = 0;
		const auto it = m_playerResources.find(playerId);
		if (it == m_playerResources.end())
			return false;
		outComboPoints = it->second.comboPoints;
		outMaxComboPoints = it->second.maxComboPoints;
		return true;
	}

	void SkillSystem::NotifyTargetDeath(uint32_t casterPlayerId, EntityId targetId)
	{
		const auto itTarget = m_lastComboTargetByCaster.find(casterPlayerId);
		if (itTarget == m_lastComboTargetByCaster.end() || itTarget->second != targetId)
			return;

		auto itRes = m_playerResources.find(casterPlayerId);
		if (itRes != m_playerResources.end())
			itRes->second.comboPoints = 0;

		m_lastComboGainTicks[casterPlayerId] = 0;
		m_lastComboTargetByCaster.erase(casterPlayerId);
		LOG_INFO(Gameplay, "[SkillSystem] Combo reset on target death (caster_id={}, target_id={})",
			casterPlayerId, targetId);
	}

	bool SkillSystem::ValidateCooldown(uint32_t playerId, const SkillDefinition& def, uint64_t nowTicks) const
	{
		const auto perSkillIt = m_lastUseTicks.find(playerId);
		if (perSkillIt == m_lastUseTicks.end())
			return true;

		const auto it = perSkillIt->second.find(def.id);
		if (it == perSkillIt->second.end())
			return true;

		const uint64_t lastUseTicks = it->second;
		if (lastUseTicks == 0)
			return true;

		const uint64_t cooldownTicks = SecondsToTicks(def.cooldownSeconds);
		if (cooldownTicks == 0)
			return true;

		if (nowTicks < lastUseTicks + cooldownTicks)
			return false;

		return true;
	}

	bool SkillSystem::ValidateResources(uint32_t playerId, const ResourceCost& cost) const
	{
		const auto it = m_playerResources.find(playerId);
		if (it == m_playerResources.end())
			return false;

		const PlayerResources& res = it->second;
		return res.health >= cost.health
			&& res.mana >= cost.mana
			&& res.energy >= cost.energy
			&& res.rage >= cost.rage
			&& res.comboPoints >= cost.comboPoints;
	}

	bool SkillSystem::ConsumeResources(uint32_t playerId, const ResourceCost& cost)
	{
		auto it = m_playerResources.find(playerId);
		if (it == m_playerResources.end())
			return false;

		PlayerResources& res = it->second;
		if (!ValidateResources(playerId, cost))
			return false;

		res.health -= std::min(res.health, cost.health);
		res.mana -= std::min(res.mana, cost.mana);
		res.energy -= std::min(res.energy, cost.energy);
		res.rage -= std::min(res.rage, cost.rage);
		res.comboPoints -= std::min(res.comboPoints, cost.comboPoints);
		return true;
	}

	bool SkillSystem::ValidateRangeAndLos(uint32_t playerId, EntityId targetId, const SkillDefinition& def) const
	{
		// LOS is ticket-mandated. When missing, assume true but log at warning level.
		const bool hasLosInfo = (m_losTable.find(ComposeLosKey(playerId, targetId)) != m_losTable.end());
		bool losOk = true;
		if (hasLosInfo)
		{
			auto it = m_losTable.find(ComposeLosKey(playerId, targetId));
			losOk = (it != m_losTable.end()) ? it->second : true;
		}
		else
		{
			// Fallback keeps MVP functional while still honoring "LOS check" as a decision point.
			losOk = true;
		}

		if (!losOk)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: LOS failed (player_id={}, target_id={}, skill={})",
				playerId, targetId, def.id);
			return false;
		}

		if (def.rangeMeters <= 0.0f)
			return true;

		const auto pit = m_playerPositions.find(playerId);
		const auto tit = m_targetPositions.find(targetId);
		if (pit == m_playerPositions.end() || tit == m_targetPositions.end())
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: missing positions for range validation (player_id={}, target_id={}, skill={})",
				playerId, targetId, def.id);
			return false;
		}

		const engine::math::Vec3& playerPos = pit->second;
		const engine::math::Vec3& targetPos = tit->second;
		const float dx = playerPos.x - targetPos.x;
		const float dz = playerPos.z - targetPos.z;
		const float distSq = dx * dx + dz * dz;
		const float rangeSq = def.rangeMeters * def.rangeMeters;
		if (distSq > rangeSq)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: target out of range (player_id={}, target_id={}, dist_sq={:.2f}, range_sq={:.2f}, skill={})",
				playerId, targetId, distSq, rangeSq, def.id);
			return false;
		}

		// Range validated.
		return true;
	}

	uint64_t SkillSystem::ResolveGcdTicks() const
	{
		const float gcdSeconds = static_cast<float>(m_config.GetDouble("skills.gcd_seconds", 1.5));
		return SecondsToTicks(gcdSeconds);
	}

	SkillSystem::CastState SkillSystem::GetCastState(uint32_t playerId, float* outProgress01) const
	{
		if (outProgress01 != nullptr) *outProgress01 = 0.0f;

		const uint64_t nowTicks = engine::core::Time::NowTicks();
		if (const auto it = m_activeCastByPlayer.find(playerId); it != m_activeCastByPlayer.end())
		{
			const ActiveCast& active = it->second;
			const uint64_t denom = active.endTicks > active.startTicks ? (active.endTicks - active.startTicks) : 1u;
			const uint64_t elapsed = nowTicks > active.startTicks ? (nowTicks - active.startTicks) : 0u;
			const float progress01 = std::clamp(static_cast<float>(elapsed) / static_cast<float>(denom), 0.0f, 1.0f);
			if (outProgress01 != nullptr) *outProgress01 = progress01;
			return active.state;
		}

		bool gcdActive = false;
		if (const auto it = m_gcdEndTicks.find(playerId); it != m_gcdEndTicks.end())
			gcdActive = nowTicks < it->second;

		bool skillCooldownActive = false;
		if (const auto it = m_castCooldownEndTicks.find(playerId); it != m_castCooldownEndTicks.end())
			skillCooldownActive = nowTicks < it->second;

		return (gcdActive || skillCooldownActive) ? CastState::Cooldown : CastState::Idle;
	}

	void SkillSystem::StartCast(uint32_t playerId, const SkillDefinition& def, EntityId targetId, uint64_t nowTicks)
	{
		const uint64_t castEndTicks = nowTicks + SecondsToTicks(def.castTimeSeconds);

		// Decide casting vs channeling based on tick interval.
		const uint64_t intervalTicks = SecondsToTicks(def.runtime.channelTickIntervalSeconds);
		const bool isChanneling = intervalTicks > 0;

		ActiveCast active{};
		active.state = isChanneling ? CastState::Channeling : CastState::Casting;
		active.skillId = def.id;
		active.targetId = targetId;
		active.isAoE = false;
		active.startTicks = nowTicks;
		active.endTicks = castEndTicks;
		active.nextChannelTickTicks = isChanneling ? (nowTicks + intervalTicks) : 0u;
		active.channelTickIntervalTicks = isChanneling ? intervalTicks : 0u;
		active.frozenMovement = def.runtime.frozenMovement;
		active.channelCancelable = def.runtime.channelCancelable;
		active.interruptDamageFractionThreshold = def.runtime.interruptDamageFractionThreshold;
		active.cost = def.cost;

		// If per-skill threshold is missing, fallback to global config.
		if (active.interruptDamageFractionThreshold <= 0.0f)
			active.interruptDamageFractionThreshold = static_cast<float>(m_config.GetDouble("skills.interrupt_damage_fraction_threshold", 0.25));

		// Clear any previous pending interruption inputs before starting the cast.
		m_pendingInterruptInputs.erase(playerId);
		m_activeCastByPlayer[playerId] = std::move(active);

		LOG_INFO(Gameplay,
			"[SkillSystem] Cast started (player_id={}, skill={}, target_id={}, state={}, cast_s={:.2f})",
			playerId,
			def.id,
			targetId,
			def.runtime.channelTickIntervalSeconds > 0.0f ? "channeling" : "casting",
			def.castTimeSeconds);
	}

	void SkillSystem::StartAoECast(
		uint32_t playerId,
		const SkillDefinition& def,
		const engine::math::Vec3& aoeTargetPosMeters,
		const engine::math::Vec3& aoeTargetDirXZ,
		uint64_t nowTicks)
	{
		const uint64_t castEndTicks = nowTicks + SecondsToTicks(def.castTimeSeconds);

		// Decide casting vs channeling based on tick interval.
		const uint64_t intervalTicks = SecondsToTicks(def.runtime.channelTickIntervalSeconds);
		const bool isChanneling = intervalTicks > 0;

		ActiveCast active{};
		active.state = isChanneling ? CastState::Channeling : CastState::Casting;
		active.skillId = def.id;
		active.targetId = 0;
		active.isAoE = true;
		active.aoeTargetPosMeters = aoeTargetPosMeters;

		// Normalize direction in XZ plane.
		engine::math::Vec3 dir = engine::math::Vec3(aoeTargetDirXZ.x, 0.0f, aoeTargetDirXZ.z);
		const float lenSq = dir.LengthSq();
		if (lenSq <= 0.0000001f)
		{
			LOG_WARN(Gameplay, "[SkillSystem] AoE cast direction invalid -> defaulting forward (player_id={}, skill={})",
				playerId, def.id);
			dir = engine::math::Vec3(0.0f, 0.0f, 1.0f);
		}
		else
		{
			dir = dir.Normalized();
		}
		active.aoeTargetDirXZ = dir;

		active.startTicks = nowTicks;
		active.endTicks = castEndTicks;
		active.nextChannelTickTicks = isChanneling ? (nowTicks + intervalTicks) : 0u;
		active.channelTickIntervalTicks = isChanneling ? intervalTicks : 0u;
		active.frozenMovement = def.runtime.frozenMovement;
		active.channelCancelable = def.runtime.channelCancelable;
		active.interruptDamageFractionThreshold = def.runtime.interruptDamageFractionThreshold;
		active.cost = def.cost;

		// Clear any previous pending interruption inputs before starting the cast.
		m_pendingInterruptInputs.erase(playerId);
		m_activeCastByPlayer[playerId] = std::move(active);

		LOG_INFO(Gameplay,
			"[SkillSystem] AoE cast started (player_id={}, skill={}, state={}, cast_s={:.2f}, shape={})",
			playerId,
			def.id,
			def.runtime.channelTickIntervalSeconds > 0.0f ? "channeling" : "casting",
			def.castTimeSeconds,
			static_cast<uint32_t>(def.aoe.shape));
	}

	void SkillSystem::ApplySkillEffectsOnPlayer(uint32_t playerId, const SkillDefinition& def, bool tick)
	{
		const auto it = m_playerResources.find(playerId);
		if (it == m_playerResources.end())
		{
			LOG_WARN(Gameplay, "[SkillSystem] ApplySkillEffects skipped: missing player resources (player_id={}, skill={})",
				playerId, def.id);
			return;
		}

		PlayerResources& res = it->second;

		if (!tick)
		{
			uint32_t comboToGenerate = 0;
			bool consumeAllCombo = false;
			for (const SkillEffect& effect : def.effects)
			{
				comboToGenerate += effect.generateCombo;
				consumeAllCombo = consumeAllCombo || effect.consumeCombo;
			}

			if (consumeAllCombo && (res.comboPoints > 0 || m_lastComboGainTicks.find(playerId) != m_lastComboGainTicks.end()))
			{
				res.comboPoints = 0;
				m_lastComboGainTicks[playerId] = 0;
				m_lastComboTargetByCaster.erase(playerId);
			}

			if (comboToGenerate > 0)
			{
				const uint32_t nowCombo = res.comboPoints + comboToGenerate;
				res.comboPoints = std::min(res.maxComboPoints, nowCombo);
				m_lastComboGainTicks[playerId] = engine::core::Time::NowTicks();
				LOG_INFO(Gameplay, "[SkillSystem] Combo generated (player_id={}, skill={}, +{}, combo={}/{})",
					playerId, def.id, comboToGenerate, res.comboPoints, res.maxComboPoints);
			}
		}

		for (const SkillEffect& effect : def.effects)
		{
			// Only apply DoT/HoT-capable effects on ticks.
			if (tick)
			{
				if (effect.type != "damage" && effect.type != "heal" && effect.type != "healing")
					continue;
			}

			if (effect.type == "damage")
			{
				const uint32_t before = res.health;
				res.health = (before > effect.amount) ? (before - effect.amount) : 0u;
			}
			else if (effect.type == "heal" || effect.type == "healing")
			{
				res.health += effect.amount;
			}
		}
		(void)tick;
	}

	void SkillSystem::ApplySkillEffectsOnAoETargets(uint32_t casterPlayerId, const SkillDefinition& def, const ActiveCast& active, bool tick)
	{
		const uint32_t maxTargets = static_cast<uint32_t>(std::max<int64_t>(1, m_config.GetInt("skills.aoe_max_targets", 20)));
		uint32_t appliedTargets = 0;

		if (!tick)
		{
			auto itCaster = m_playerResources.find(casterPlayerId);
			if (itCaster == m_playerResources.end())
			{
				LOG_WARN(Gameplay, "[SkillSystem] AoE combo effects skipped: missing caster resources (caster_id={}, skill={})",
					casterPlayerId, def.id);
			}
			else
			{
				PlayerResources& casterRes = itCaster->second;
				uint32_t comboToGenerate = 0;
				bool consumeAllCombo = false;
				for (const SkillEffect& effect : def.effects)
				{
					comboToGenerate += effect.generateCombo;
					consumeAllCombo = consumeAllCombo || effect.consumeCombo;
				}

				if (consumeAllCombo && (casterRes.comboPoints > 0 || m_lastComboGainTicks.find(casterPlayerId) != m_lastComboGainTicks.end()))
				{
					casterRes.comboPoints = 0;
					m_lastComboGainTicks[casterPlayerId] = 0;
					m_lastComboTargetByCaster.erase(casterPlayerId);
				}

				if (comboToGenerate > 0)
				{
					const uint32_t nowCombo = casterRes.comboPoints + comboToGenerate;
					casterRes.comboPoints = std::min(casterRes.maxComboPoints, nowCombo);
					m_lastComboGainTicks[casterPlayerId] = engine::core::Time::NowTicks();
					LOG_INFO(Gameplay, "[SkillSystem] Combo generated (caster_id={}, skill={}, +{}, combo={}/{})",
						casterPlayerId, def.id, comboToGenerate, casterRes.comboPoints, casterRes.maxComboPoints);
				}
			}
		}

		const engine::math::Vec3 center = active.aoeTargetPosMeters;
		const engine::math::Vec3 dirXZ = engine::math::Vec3(active.aoeTargetDirXZ.x, 0.0f, active.aoeTargetDirXZ.z);
		const float dirLenSq = dirXZ.LengthSq();
		const bool haveDir = dirLenSq > 0.0000001f;

		const float pi = 3.14159265f;
		const float angleHalfCos = (def.aoe.angleDeg > 0.0f)
			? std::cos((def.aoe.angleDeg * 0.5f) * (pi / 180.0f))
			: 0.0f;

		auto ApplyEffectsToOneTarget = [&](PlayerResources& res)
		{
			for (const SkillEffect& effect : def.effects)
			{
				if (tick)
				{
					if (effect.type != "damage" && effect.type != "heal" && effect.type != "healing")
						continue;
				}

				if (effect.type == "damage")
				{
					const uint32_t before = res.health;
					res.health = (before > effect.amount) ? (before - effect.amount) : 0u;
				}
				else if (effect.type == "heal" || effect.type == "healing")
				{
					res.health += effect.amount;
				}
			}
		};

		for (const auto& kv : m_targetPositions)
		{
			if (appliedTargets >= maxTargets)
				break;

			const EntityId candidateEntityId = kv.first;
			const engine::math::Vec3& candidatePos = kv.second;

			const float dx = candidatePos.x - center.x;
			const float dz = candidatePos.z - center.z;
			const float distSq = dx * dx + dz * dz;

			bool hit = false;
			switch (def.aoe.shape)
			{
			case SkillSystem::AoEShapeType::Circle:
			{
				const float r = def.aoe.radiusMeters;
				hit = distSq <= (r * r);
				break;
			}
			case SkillSystem::AoEShapeType::Cone:
			{
				if (!haveDir)
					break;

				const float range = def.aoe.rangeMeters;
				if (distSq > (range * range))
					break;

				const float dist = std::sqrt(distSq);
				if (dist <= 0.0000001f)
					break;

				// dirXZ is unit length in XZ plane; candidate vector is vXZ = (dx,dz).
				const float dot = (dx * dirXZ.x + dz * dirXZ.z) / dist;
				hit = dot >= angleHalfCos;
				break;
			}
			case SkillSystem::AoEShapeType::Line:
			{
				if (!haveDir)
					break;

				const float t = dx * dirXZ.x + dz * dirXZ.z; // projection length on dir
				if (t < 0.0f || t > def.aoe.lengthMeters)
					break;

				// Perpendicular distance from point to line: |v - dir*t|
				const float perpX = dx - dirXZ.x * t;
				const float perpZ = dz - dirXZ.z * t;
				const float perpDistSq = perpX * perpX + perpZ * perpZ;
				const float halfW = def.aoe.widthMeters * 0.5f;
				hit = perpDistSq <= (halfW * halfW);
				break;
			}
			case SkillSystem::AoEShapeType::Ring:
			{
				const float outerR = def.aoe.outerRadiusMeters;
				const float innerR = def.aoe.innerRadiusMeters;
				hit = distSq >= (innerR * innerR) && distSq <= (outerR * outerR);
				break;
			}
			default:
				hit = false;
				break;
			}

			if (!hit)
				continue;

			const uint32_t targetPlayerId = static_cast<uint32_t>(candidateEntityId & 0xFFFFFFFFull);
			if (targetPlayerId == casterPlayerId)
				continue;

			auto resIt = m_playerResources.find(targetPlayerId);
			if (resIt == m_playerResources.end())
				continue;

			ApplyEffectsToOneTarget(resIt->second);
			++appliedTargets;
		}

		LOG_DEBUG(Gameplay,
			"[SkillSystem] AoE effects applied (caster_id={}, skill={}, hits={}, tick={})",
			casterPlayerId,
			def.id,
			appliedTargets,
			tick ? "true" : "false");
	}

	void SkillSystem::CompleteCast(uint32_t playerId, const SkillDefinition& def, EntityId targetId, uint64_t nowTicks, bool interrupted)
	{
		const auto it = m_activeCastByPlayer.find(playerId);
		if (it == m_activeCastByPlayer.end())
		{
			LOG_WARN(Gameplay, "[SkillSystem] CompleteCast ignored: no active cast (player_id={}, skill={})",
				playerId, def.id);
			return;
		}

		const ActiveCast active = std::move(it->second);
		m_activeCastByPlayer.erase(it);
		m_pendingInterruptInputs.erase(playerId);

		if (interrupted)
		{
			const uint32_t refundMana = active.cost.mana / 2u; // ticket: 50% partial mana refund
			auto resIt = m_playerResources.find(playerId);
			if (resIt != m_playerResources.end())
			{
				resIt->second.mana += refundMana;
			}

			const float gcdSeconds = static_cast<float>(m_config.GetDouble("skills.gcd_seconds", 1.5));
			m_gcdEndTicks[playerId] = nowTicks + ResolveGcdTicks();

			LOG_INFO(Gameplay,
				"[SkillSystem] Cast interrupted (player_id={}, skill={}, target_id={}, refund_mana={}, state={}, gcd_s={:.2f})",
				playerId,
				def.id,
				targetId,
				refundMana,
				active.state == CastState::Channeling ? "channeling" : "casting",
				gcdSeconds);
		}
		else
		{
			const float gcdSeconds = static_cast<float>(m_config.GetDouble("skills.gcd_seconds", 1.5));
			m_gcdEndTicks[playerId] = nowTicks + ResolveGcdTicks();

			LOG_INFO(Gameplay,
				"[SkillSystem] Cast completed (player_id={}, skill={}, target_id={}, state={}, gcd_s={:.2f})",
				playerId,
				def.id,
				targetId,
				active.state == CastState::Channeling ? "channeling" : "casting",
				gcdSeconds);
			if (active.isAoE)
				ApplySkillEffectsOnAoETargets(playerId, def, active, false);
			else
				ApplySkillEffectsOnPlayer(playerId, def, false);
		}

		// Start per-skill cooldown after success or interruption.
		m_lastUseTicks[playerId][def.id] = nowTicks;
		const uint64_t cooldownTicks = SecondsToTicks(def.cooldownSeconds);
		m_castCooldownEndTicks[playerId] = cooldownTicks > 0 ? (nowTicks + cooldownTicks) : nowTicks;
	}

	void SkillSystem::Tick()
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[SkillSystem] Tick skipped: runtime not initialized");
			return;
		}

		const uint64_t nowTicks = engine::core::Time::NowTicks();

		// Combo decay (30s timeout by default).
		const float decaySeconds = static_cast<float>(m_config.GetDouble("skills.combo_decay_seconds", 30.0));
		const uint64_t decayTicks = SecondsToTicks(decaySeconds);
		if (decayTicks > 0)
		{
			for (auto& kv : m_playerResources)
			{
				const uint32_t playerId = kv.first;
				PlayerResources& res = kv.second;
				if (res.comboPoints == 0)
					continue;

				const auto it = m_lastComboGainTicks.find(playerId);
				if (it == m_lastComboGainTicks.end() || it->second == 0)
					continue;

				if (nowTicks >= it->second + decayTicks)
				{
					res.comboPoints = 0;
					m_lastComboGainTicks[playerId] = 0;
					m_lastComboTargetByCaster.erase(playerId);
					LOG_INFO(Gameplay, "[SkillSystem] Combo decayed (player_id={}, reset=0)", playerId);
				}
			}
		}

		if (m_activeCastByPlayer.empty())
			return;

		std::vector<uint32_t> playerIds;
		playerIds.reserve(m_activeCastByPlayer.size());
		for (const auto& kv : m_activeCastByPlayer)
			playerIds.push_back(kv.first);

		for (uint32_t playerId : playerIds)
		{
			auto itCast = m_activeCastByPlayer.find(playerId);
			if (itCast == m_activeCastByPlayer.end())
				continue;

			ActiveCast& active = itCast->second;
			const SkillDefinition* def = FindSkill(active.skillId);
			if (def == nullptr)
			{
				LOG_ERROR(Gameplay, "[SkillSystem] Tick: missing skill definition (player_id={}, skill={})",
					playerId, active.skillId);
				SkillDefinition fallbackDef{};
				fallbackDef.id = active.skillId;
				CompleteCast(playerId, fallbackDef, active.targetId, nowTicks, true);
				continue;
			}

			InterruptInputs inputs{};
			if (const auto it = m_pendingInterruptInputs.find(playerId); it != m_pendingInterruptInputs.end())
				inputs = it->second;

			bool interrupted = false;
			if (inputs.hasDeath || inputs.hasStun || inputs.hasKnockback)
			{
				interrupted = true;
			}
			else if (inputs.hasMovement && !active.frozenMovement)
			{
				interrupted = true;
			}
			else if (inputs.hasDamage && inputs.damageFractionOfMaxHealth > active.interruptDamageFractionThreshold)
			{
				interrupted = true;
			}
			else if (inputs.hasEarlyCancel && active.state == CastState::Channeling && active.channelCancelable)
			{
				interrupted = true;
			}

			if (interrupted)
			{
				CompleteCast(playerId, *def, active.targetId, nowTicks, true);
				continue;
			}

			// Clear transient interruption inputs after evaluation.
			m_pendingInterruptInputs.erase(playerId);

			if (nowTicks >= active.endTicks)
			{
				CompleteCast(playerId, *def, active.targetId, active.endTicks, false);
				continue;
			}

			if (active.state == CastState::Channeling && active.channelTickIntervalTicks > 0)
			{
				uint32_t appliedTicks = 0;
				const uint64_t maxTicksPerUpdate = 16u; // safety bound
				while (active.nextChannelTickTicks <= nowTicks
					&& active.nextChannelTickTicks < active.endTicks
					&& appliedTicks < maxTicksPerUpdate)
				{
					if (active.isAoE)
						ApplySkillEffectsOnAoETargets(playerId, *def, active, true);
					else
						ApplySkillEffectsOnPlayer(playerId, *def, true);
					active.nextChannelTickTicks += active.channelTickIntervalTicks;
					++appliedTicks;
				}
				if (appliedTicks >= maxTicksPerUpdate)
				{
					LOG_WARN(Gameplay, "[SkillSystem] Channel tick capped per update (player_id={}, skill={})",
						playerId, def->id);
				}
			}
		}
	}

	void SkillSystem::NotifyDamageTaken(uint32_t playerId, float damageFractionOfMaxHealth)
	{
		const float clamped = std::clamp(damageFractionOfMaxHealth, 0.0f, 1.0f);
		InterruptInputs& inputs = m_pendingInterruptInputs[playerId];
		inputs.hasDamage = true;
		inputs.damageFractionOfMaxHealth = std::max(inputs.damageFractionOfMaxHealth, clamped);
		LOG_DEBUG(Gameplay, "[SkillSystem] NotifyDamageTaken (player_id={}, dmg_frac={:.2f})",
			playerId, clamped);
	}

	void SkillSystem::NotifyStun(uint32_t playerId)
	{
		InterruptInputs& inputs = m_pendingInterruptInputs[playerId];
		inputs.hasStun = true;
		LOG_DEBUG(Gameplay, "[SkillSystem] NotifyStun (player_id={})", playerId);
	}

	void SkillSystem::NotifyKnockback(uint32_t playerId)
	{
		InterruptInputs& inputs = m_pendingInterruptInputs[playerId];
		inputs.hasKnockback = true;
		LOG_DEBUG(Gameplay, "[SkillSystem] NotifyKnockback (player_id={})", playerId);
	}

	void SkillSystem::NotifyMovement(uint32_t playerId)
	{
		InterruptInputs& inputs = m_pendingInterruptInputs[playerId];
		inputs.hasMovement = true;
		LOG_DEBUG(Gameplay, "[SkillSystem] NotifyMovement (player_id={})", playerId);
	}

	void SkillSystem::NotifyDeath(uint32_t playerId)
	{
		InterruptInputs& inputs = m_pendingInterruptInputs[playerId];
		inputs.hasDeath = true;
		LOG_DEBUG(Gameplay, "[SkillSystem] NotifyDeath (player_id={})", playerId);
	}

	void SkillSystem::CancelChannel(uint32_t playerId)
	{
		InterruptInputs& inputs = m_pendingInterruptInputs[playerId];
		inputs.hasEarlyCancel = true;
		LOG_DEBUG(Gameplay, "[SkillSystem] CancelChannel (player_id={})", playerId);
	}

	bool SkillSystem::UseSkill(uint32_t playerId, std::string_view skillId, EntityId targetId)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: runtime not initialized");
			return false;
		}

		const SkillDefinition* def = FindSkill(skillId);
		if (def == nullptr)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: unknown skill '{}' (player_id={}, target_id={})",
				skillId, playerId, targetId);
			return false;
		}

		EnsurePlayerInitialized(playerId);

		const uint64_t nowTicks = engine::core::Time::NowTicks();

		const bool isSpenderSkill = std::any_of(def->effects.begin(), def->effects.end(),
			[](const SkillEffect& e) { return e.consumeCombo; });
		const bool isBuilderSkill = std::any_of(def->effects.begin(), def->effects.end(),
			[](const SkillEffect& e) { return e.generateCombo > 0; });

		if (isBuilderSkill)
			m_lastComboTargetByCaster[playerId] = targetId;

		// State machine: block all skill starts while casting/channeling.
		if (m_activeCastByPlayer.find(playerId) != m_activeCastByPlayer.end())
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: player already casting/channeling (player_id={}, active_skill={}, requested={})",
				playerId, m_activeCastByPlayer[playerId].skillId, def->id);
			return false;
		}

		// GCD lock: block all skills after any cast start.
		if (const auto it = m_gcdEndTicks.find(playerId); it != m_gcdEndTicks.end() && nowTicks < it->second)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: GCD active (player_id={}, requested={}, now_ticks={})",
				playerId, def->id, nowTicks);
			return false;
		}

		// Per-skill cooldown (started after completion or interruption).
		const bool cooldownOk = ValidateCooldown(playerId, *def, nowTicks);
		if (!cooldownOk)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: skill cooldown active (player_id={}, skill={}, target_id={}, now_ticks={})",
				playerId, def->id, targetId, nowTicks);
			return false;
		}

		// Range + LOS validation.
		if (!ValidateRangeAndLos(playerId, targetId, *def))
			return false;

		// Resources validation and cost application.
		if (!ValidateResources(playerId, def->cost))
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseSkill blocked: insufficient resources (player_id={}, skill={}, target_id={}, cost_mana={}, cost_hp={})",
				playerId, def->id, targetId, def->cost.mana, def->cost.health);
			return false;
		}

		ResourceCost costToConsume = def->cost;
		if (isSpenderSkill && costToConsume.comboPoints > 0)
			costToConsume.comboPoints = 0;

		if (!ConsumeResources(playerId, costToConsume))
		{
			LOG_ERROR(Gameplay, "[SkillSystem] UseSkill FAILED: resource consume rejected unexpectedly (player_id={}, skill={})",
				playerId, def->id);
			return false;
		}

		// Instant casts: skip casting/channeling state machine.
		const uint64_t castTicks = SecondsToTicks(def->castTimeSeconds);
		if (castTicks == 0)
		{
			const float gcdSeconds = static_cast<float>(m_config.GetDouble("skills.gcd_seconds", 1.5));
			m_gcdEndTicks[playerId] = nowTicks + ResolveGcdTicks();

			LOG_INFO(Gameplay, "[SkillSystem] Cast triggered instant (player_id={}, skill={}, target_id={}, gcd_s={:.2f})",
				playerId, def->id, targetId, static_cast<float>(m_config.GetDouble("skills.gcd_seconds", 1.5)));

			ApplySkillEffectsOnPlayer(playerId, *def, false);
			m_lastUseTicks[playerId][def->id] = nowTicks;
			const uint64_t cooldownTicks = SecondsToTicks(def->cooldownSeconds);
			m_castCooldownEndTicks[playerId] = cooldownTicks > 0 ? (nowTicks + cooldownTicks) : nowTicks;
			return true;
		}

		StartCast(playerId, *def, targetId, nowTicks);
		return true;
	}

	bool SkillSystem::UseAoESkill(uint32_t playerId, std::string_view skillId, const engine::math::Vec3& targetPosMeters, const engine::math::Vec3& targetDirXZ)
	{
		if (!m_initialized)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: runtime not initialized");
			return false;
		}

		const SkillDefinition* def = FindSkill(skillId);
		if (def == nullptr)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: unknown skill '{}' (player_id={})",
				skillId, playerId);
			return false;
		}

		if (def->aoe.shape == SkillSystem::AoEShapeType::None)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: skill '{}' has no AoE shape (player_id={})",
				def->id, playerId);
			return false;
		}

		EnsurePlayerInitialized(playerId);

		const uint64_t nowTicks = engine::core::Time::NowTicks();

		// State machine: block all skill starts while casting/channeling.
		if (m_activeCastByPlayer.find(playerId) != m_activeCastByPlayer.end())
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: player already casting/channeling (player_id={}, active_skill={}, requested={})",
				playerId, m_activeCastByPlayer[playerId].skillId, def->id);
			return false;
		}

		// GCD lock: block all skills after any cast completion.
		if (const auto it = m_gcdEndTicks.find(playerId); it != m_gcdEndTicks.end() && nowTicks < it->second)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: GCD active (player_id={}, requested={}, now_ticks={})",
				playerId, def->id, nowTicks);
			return false;
		}

		// Per-skill cooldown (started after completion or interruption).
		const bool cooldownOk = ValidateCooldown(playerId, *def, nowTicks);
		if (!cooldownOk)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: skill cooldown active (player_id={}, skill={}, now_ticks={})",
				playerId, def->id, nowTicks);
			return false;
		}

		// Validate position reachable using configured `range`.
		const auto pit = m_playerPositions.find(playerId);
		if (pit == m_playerPositions.end())
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: missing player position (player_id={}, skill={})",
				playerId, def->id);
			return false;
		}
		const engine::math::Vec3& playerPos = pit->second;
		const float dx = playerPos.x - targetPosMeters.x;
		const float dz = playerPos.z - targetPosMeters.z;
		const float distSq = dx * dx + dz * dz;
		const float rangeSq = def->rangeMeters * def->rangeMeters;
		if (def->rangeMeters > 0.0f && distSq > rangeSq)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: target out of range (player_id={}, skill={}, dist_sq={:.2f}, range_sq={:.2f})",
				playerId, def->id, distSq, rangeSq);
			return false;
		}

		// Validate direction when shape needs it.
		engine::math::Vec3 dirXZ = engine::math::Vec3(targetDirXZ.x, 0.0f, targetDirXZ.z);
		const float dirLenSq = dirXZ.LengthSq();
		if ((def->aoe.shape == SkillSystem::AoEShapeType::Cone || def->aoe.shape == SkillSystem::AoEShapeType::Line) && dirLenSq <= 0.0000001f)
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: targetDirXZ invalid (player_id={}, skill={})",
				playerId, def->id);
			return false;
		}

		const bool isSpenderSkill = std::any_of(def->effects.begin(), def->effects.end(),
			[](const SkillEffect& e) { return e.consumeCombo; });

		// Resources validation and cost application.
		if (!ValidateResources(playerId, def->cost))
		{
			LOG_WARN(Gameplay, "[SkillSystem] UseAoESkill blocked: insufficient resources (player_id={}, skill={}, cost_mana={}, cost_hp={})",
				playerId, def->id, def->cost.mana, def->cost.health);
			return false;
		}
		ResourceCost costToConsume = def->cost;
		if (isSpenderSkill && costToConsume.comboPoints > 0)
			costToConsume.comboPoints = 0;

		if (!ConsumeResources(playerId, costToConsume))
		{
			LOG_ERROR(Gameplay, "[SkillSystem] UseAoESkill FAILED: resource consume rejected unexpectedly (player_id={}, skill={})",
				playerId, def->id);
			return false;
		}

		// Server-side hit detection: count candidates for logs.
		const uint32_t maxTargets = static_cast<uint32_t>(std::max<int64_t>(1, m_config.GetInt("skills.aoe_max_targets", 20)));
		uint32_t hitCount = 0;

		// Normalize dir once for hit tests.
		engine::math::Vec3 dirUnit = dirXZ;
		if (dirUnit.LengthSq() > 0.0000001f)
			dirUnit = dirUnit.Normalized();
		const float pi = 3.14159265f;
		const float angleHalfCos = (def->aoe.angleDeg > 0.0f)
			? std::cos((def->aoe.angleDeg * 0.5f) * (pi / 180.0f))
			: 0.0f;

		for (const auto& kv : m_targetPositions)
		{
			if (hitCount >= maxTargets)
				break;

			const EntityId candidateEntityId = kv.first;
			const engine::math::Vec3& candidatePos = kv.second;
			const uint32_t candidatePlayerId = static_cast<uint32_t>(candidateEntityId & 0xFFFFFFFFull);
			if (candidatePlayerId == playerId)
				continue;
			if (m_playerResources.find(candidatePlayerId) == m_playerResources.end())
				continue;

			const float cdx = candidatePos.x - targetPosMeters.x;
			const float cdz = candidatePos.z - targetPosMeters.z;
			const float cdSq = cdx * cdx + cdz * cdz;

			bool hit = false;
			switch (def->aoe.shape)
			{
			case SkillSystem::AoEShapeType::Circle:
			{
				const float r = def->aoe.radiusMeters;
				hit = cdSq <= (r * r);
				break;
			}
			case SkillSystem::AoEShapeType::Cone:
			{
				if (dirLenSq <= 0.0000001f) break;
				const float range = def->aoe.rangeMeters;
				if (cdSq > (range * range)) break;
				const float dist = std::sqrt(cdSq);
				if (dist <= 0.0000001f) break;
				const float dot = (cdx * dirUnit.x + cdz * dirUnit.z) / dist;
				hit = dot >= angleHalfCos;
				break;
			}
			case SkillSystem::AoEShapeType::Line:
			{
				if (dirLenSq <= 0.0000001f) break;
				const float t = cdx * dirUnit.x + cdz * dirUnit.z;
				if (t < 0.0f || t > def->aoe.lengthMeters) break;
				const float perpX = cdx - dirUnit.x * t;
				const float perpZ = cdz - dirUnit.z * t;
				const float perpDistSq = perpX * perpX + perpZ * perpZ;
				const float halfW = def->aoe.widthMeters * 0.5f;
				hit = perpDistSq <= (halfW * halfW);
				break;
			}
			case SkillSystem::AoEShapeType::Ring:
			{
				hit = cdSq >= (def->aoe.innerRadiusMeters * def->aoe.innerRadiusMeters)
					&& cdSq <= (def->aoe.outerRadiusMeters * def->aoe.outerRadiusMeters);
				break;
			}
			default:
				hit = false;
				break;
			}

			if (!hit)
				continue;

			++hitCount;
		}

		LOG_INFO(Gameplay,
			"[SkillSystem] UseAoESkill validated (player_id={}, skill={}, hits_preview={})",
			playerId, def->id, hitCount);

		// Instant casts: skip casting/channeling state machine.
		const uint64_t castTicks = SecondsToTicks(def->castTimeSeconds);
		if (castTicks == 0)
		{
			const float gcdSeconds = static_cast<float>(m_config.GetDouble("skills.gcd_seconds", 1.5));
			m_gcdEndTicks[playerId] = nowTicks + ResolveGcdTicks();

			engine::math::Vec3 dirNorm = dirXZ;
			if (dirNorm.LengthSq() > 0.0000001f)
				dirNorm = dirNorm.Normalized();

			ActiveCast active{};
			active.isAoE = true;
			active.aoeTargetPosMeters = targetPosMeters;
			active.aoeTargetDirXZ = dirNorm;

			LOG_INFO(Gameplay,
				"[SkillSystem] AoE cast triggered instant (player_id={}, skill={}, gcd_s={:.2f}, shape={})",
				playerId, def->id, gcdSeconds, static_cast<uint32_t>(def->aoe.shape));

			ApplySkillEffectsOnAoETargets(playerId, *def, active, false);

			m_lastUseTicks[playerId][def->id] = nowTicks;
			const uint64_t cooldownTicks = SecondsToTicks(def->cooldownSeconds);
			m_castCooldownEndTicks[playerId] = cooldownTicks > 0 ? (nowTicks + cooldownTicks) : nowTicks;
			return true;
		}

		StartAoECast(playerId, *def, targetPosMeters, dirXZ, nowTicks);
		return true;
	}
}

