#include "src/shardd/gameplay/spell/SpellKitLibrary.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
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
		bool TryGetFloat(const JsonValue& value, float& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue))
			{
				return false;
			}

			outValue = static_cast<float>(value.numberValue);
			return true;
		}

		/// Mappe la chaîne `targetType` du JSON vers l'enum (false si inconnue).
		bool TryParseTargetKind(std::string_view text, SpellTargetKind& outKind)
		{
			if (text == "SingleEnemy") { outKind = SpellTargetKind::SingleEnemy; return true; }
			if (text == "SingleAlly") { outKind = SpellTargetKind::SingleAlly; return true; }
			if (text == "SelfOnly") { outKind = SpellTargetKind::SelfOnly; return true; }
			if (text == "AreaAroundSelf") { outKind = SpellTargetKind::AreaAroundSelf; return true; }
			return false;
		}

		/// Mappe la chaîne `type` d'un effet vers l'enum (false si inconnue).
		bool TryParseEffectType(std::string_view text, SpellEffectType& outType)
		{
			if (text == "DirectDamage") { outType = SpellEffectType::DirectDamage; return true; }
			if (text == "DamageOverTime") { outType = SpellEffectType::DamageOverTime; return true; }
			if (text == "DirectHeal") { outType = SpellEffectType::DirectHeal; return true; }
			if (text == "HealOverTime") { outType = SpellEffectType::HealOverTime; return true; }
			if (text == "BuffDamagePercent") { outType = SpellEffectType::BuffDamagePercent; return true; }
			if (text == "DebuffDamageTakenPercent") { outType = SpellEffectType::DebuffDamageTakenPercent; return true; }
			if (text == "TauntThreatMult") { outType = SpellEffectType::TauntThreatMult; return true; }
			if (text == "SlowMobPercent") { outType = SpellEffectType::SlowMobPercent; return true; }
			if (text == "ThreatReducePercent") { outType = SpellEffectType::ThreatReducePercent; return true; }
			return false;
		}

		/// Valide la cohérence champ/type d'un effet parsé (DoT sans durée, etc.).
		bool ValidateEffect(const SpellEffectDef& effect, std::string& outError, const std::string& label)
		{
			switch (effect.type)
			{
			case SpellEffectType::DirectDamage:
			case SpellEffectType::DirectHeal:
			case SpellEffectType::TauntThreatMult:
				if (effect.mult <= 0.0f)
				{
					outError = label + ": mult must be positive for this effect type";
					return false;
				}
				return true;
			case SpellEffectType::DamageOverTime:
				if (effect.mult <= 0.0f || effect.tickPeriodMs == 0 || effect.durationMs == 0)
				{
					outError = label + ": DamageOverTime requires mult, tickPeriodMs and durationMs";
					return false;
				}
				return true;
			case SpellEffectType::HealOverTime:
				if ((effect.mult <= 0.0f && effect.percentMaxHpPerTick <= 0.0f)
					|| effect.tickPeriodMs == 0 || effect.durationMs == 0)
				{
					outError = label + ": HealOverTime requires (mult or percentMaxHpPerTick), tickPeriodMs and durationMs";
					return false;
				}
				return true;
			case SpellEffectType::BuffDamagePercent:
			case SpellEffectType::DebuffDamageTakenPercent:
			case SpellEffectType::SlowMobPercent:
				if (effect.percent <= 0.0f || effect.durationMs == 0)
				{
					outError = label + ": percent and durationMs required for this effect type";
					return false;
				}
				return true;
			case SpellEffectType::ThreatReducePercent:
				if (effect.percent <= 0.0f || effect.percent > 100.0f)
				{
					outError = label + ": percent must be in (0,100] for ThreatReducePercent";
					return false;
				}
				return true;
			}

			outError = label + ": unhandled effect type";
			return false;
		}
	}

	SpellKitLibrary::SpellKitLibrary(const engine::core::Config& config)
		: m_config(config)
	{
		LOG_INFO(Net, "[SpellKitLibrary] Constructed");
	}

	bool SpellKitLibrary::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[SpellKitLibrary] Init ignored: already initialized");
			return true;
		}

		const std::filesystem::path spellsDirectory =
			engine::platform::FileSystem::ResolveContentPath(m_config, "gameplay/spells");
		const std::vector<std::filesystem::path> entries =
			engine::platform::FileSystem::ListDirectory(spellsDirectory);
		if (entries.empty())
		{
			LOG_ERROR(Net, "[SpellKitLibrary] Init FAILED: no spell kit files found ({})", spellsDirectory.string());
			return false;
		}

		for (const std::filesystem::path& entry : entries)
		{
			if (entry.extension() != ".json")
			{
				continue;
			}

			const std::string relativePath = "gameplay/spells/" + entry.filename().string();
			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
			if (jsonText.empty())
			{
				LOG_ERROR(Net, "[SpellKitLibrary] Init FAILED: empty or unreadable kit ({})", relativePath);
				m_kits.clear();
				return false;
			}

			std::string loadError;
			if (!LoadKitFromText(jsonText, loadError))
			{
				LOG_ERROR(Net, "[SpellKitLibrary] Init FAILED: {} ({})", loadError, relativePath);
				m_kits.clear();
				return false;
			}
		}

		if (m_kits.empty())
		{
			LOG_ERROR(Net, "[SpellKitLibrary] Init FAILED: no kit loaded");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[SpellKitLibrary] Init OK (kits={})", m_kits.size());
		return true;
	}

	const std::vector<SpellDef>* SpellKitLibrary::FindKit(std::string_view profile) const
	{
		const auto it = m_kits.find(std::string(profile));
		if (it == m_kits.end())
		{
			return nullptr;
		}

		return &it->second;
	}

	const SpellDef* SpellKitLibrary::FindSpell(std::string_view profile, std::string_view spellId) const
	{
		const std::vector<SpellDef>* kit = FindKit(profile);
		if (kit == nullptr)
		{
			return nullptr;
		}

		for (const SpellDef& spell : *kit)
		{
			if (spell.spellId == spellId)
			{
				return &spell;
			}
		}

		return nullptr;
	}

	bool SpellKitLibrary::LoadKitFromText(std::string_view jsonText, std::string& outError)
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
		const std::string profile = profileValue->stringValue;

		if (m_kits.find(profile) != m_kits.end())
		{
			outError = "duplicate profile kit '" + profile + "'";
			return false;
		}

		const JsonValue* spellsValue = FindObjectMember(root, "spells");
		if (spellsValue == nullptr || spellsValue->type != JsonType::Array || spellsValue->arrayValue.empty())
		{
			outError = "root.spells must be a non-empty array";
			return false;
		}

		std::vector<SpellDef> kit;
		std::unordered_set<uint32_t> usedSlots;
		std::unordered_set<std::string> usedIds;
		for (size_t index = 0; index < spellsValue->arrayValue.size(); ++index)
		{
			const JsonValue& entry = spellsValue->arrayValue[index];
			const std::string entryLabel = profile + ".spells[" + std::to_string(index) + "]";
			if (entry.type != JsonType::Object)
			{
				outError = entryLabel + " must be an object";
				return false;
			}

			SpellDef spell{};

			const JsonValue* idValue = FindObjectMember(entry, "id");
			if (idValue == nullptr || idValue->type != JsonType::String || idValue->stringValue.empty())
			{
				outError = entryLabel + ".id must be a non-empty string";
				return false;
			}
			spell.spellId = idValue->stringValue;
			if (!usedIds.emplace(spell.spellId).second)
			{
				outError = "duplicate spell id '" + spell.spellId + "'";
				return false;
			}

			const JsonValue* nameValue = FindObjectMember(entry, "name");
			if (nameValue == nullptr || nameValue->type != JsonType::String || nameValue->stringValue.empty())
			{
				outError = spell.spellId + ": name must be a non-empty string";
				return false;
			}
			spell.name = nameValue->stringValue;

			const JsonValue* slotValue = FindObjectMember(entry, "slot");
			if (slotValue == nullptr || !TryGetUint(*slotValue, spell.slot)
				|| spell.slot < 1 || spell.slot > 4)
			{
				outError = spell.spellId + ": slot must be an integer in [1,4]";
				return false;
			}
			if (!usedSlots.emplace(spell.slot).second)
			{
				outError = spell.spellId + ": duplicate slot " + std::to_string(spell.slot);
				return false;
			}

			const JsonValue* castValue = FindObjectMember(entry, "castTimeMs");
			if (castValue == nullptr || !TryGetUint(*castValue, spell.castTimeMs))
			{
				outError = spell.spellId + ": castTimeMs must be an unsigned integer";
				return false;
			}

			// cooldownMs == 0 autorisé (sort spammable, ex. healer Soin rapide).
			const JsonValue* cooldownValue = FindObjectMember(entry, "cooldownMs");
			if (cooldownValue == nullptr || !TryGetUint(*cooldownValue, spell.cooldownMs))
			{
				outError = spell.spellId + ": cooldownMs must be an unsigned integer";
				return false;
			}

			const JsonValue* costValue = FindObjectMember(entry, "resourceCostPercent");
			if (costValue == nullptr || !TryGetUint(*costValue, spell.resourceCostPercent)
				|| spell.resourceCostPercent > 100)
			{
				outError = spell.spellId + ": resourceCostPercent must be in [0,100]";
				return false;
			}

			const JsonValue* rangeValue = FindObjectMember(entry, "rangeMeters");
			if (rangeValue == nullptr || !TryGetFloat(*rangeValue, spell.rangeMeters) || spell.rangeMeters < 0.0f)
			{
				outError = spell.spellId + ": rangeMeters must be a non-negative number";
				return false;
			}

			const JsonValue* targetValue = FindObjectMember(entry, "targetType");
			if (targetValue == nullptr || targetValue->type != JsonType::String
				|| !TryParseTargetKind(targetValue->stringValue, spell.targetType))
			{
				outError = spell.spellId + ": targetType must be one of SingleEnemy|SingleAlly|SelfOnly|AreaAroundSelf";
				return false;
			}

			const JsonValue* areaValue = FindObjectMember(entry, "areaRadiusMeters");
			if (areaValue == nullptr || !TryGetFloat(*areaValue, spell.areaRadiusMeters) || spell.areaRadiusMeters < 0.0f)
			{
				outError = spell.spellId + ": areaRadiusMeters must be a non-negative number";
				return false;
			}
			if (spell.targetType == SpellTargetKind::AreaAroundSelf && spell.areaRadiusMeters <= 0.0f)
			{
				outError = spell.spellId + ": AreaAroundSelf requires a positive areaRadiusMeters";
				return false;
			}

			const JsonValue* effectsValue = FindObjectMember(entry, "effects");
			if (effectsValue == nullptr || effectsValue->type != JsonType::Array || effectsValue->arrayValue.empty())
			{
				outError = spell.spellId + ": effects must be a non-empty array";
				return false;
			}

			for (size_t effectIndex = 0; effectIndex < effectsValue->arrayValue.size(); ++effectIndex)
			{
				const JsonValue& effectEntry = effectsValue->arrayValue[effectIndex];
				const std::string effectLabel = spell.spellId + ".effects[" + std::to_string(effectIndex) + "]";
				if (effectEntry.type != JsonType::Object)
				{
					outError = effectLabel + " must be an object";
					return false;
				}

				SpellEffectDef effect{};
				const JsonValue* typeValue = FindObjectMember(effectEntry, "type");
				if (typeValue == nullptr || typeValue->type != JsonType::String
					|| !TryParseEffectType(typeValue->stringValue, effect.type))
				{
					outError = effectLabel + ": unknown effect type";
					return false;
				}

				// Champs optionnels selon le type — validés ensuite par ValidateEffect.
				if (const JsonValue* multValue = FindObjectMember(effectEntry, "mult"))
				{
					if (!TryGetFloat(*multValue, effect.mult) || effect.mult < 0.0f)
					{
						outError = effectLabel + ": mult must be a non-negative number";
						return false;
					}
				}
				if (const JsonValue* percentValue = FindObjectMember(effectEntry, "percent"))
				{
					if (!TryGetFloat(*percentValue, effect.percent) || effect.percent < 0.0f)
					{
						outError = effectLabel + ": percent must be a non-negative number";
						return false;
					}
				}
				if (const JsonValue* tickValue = FindObjectMember(effectEntry, "tickPeriodMs"))
				{
					if (!TryGetUint(*tickValue, effect.tickPeriodMs))
					{
						outError = effectLabel + ": tickPeriodMs must be an unsigned integer";
						return false;
					}
				}
				if (const JsonValue* durationValue = FindObjectMember(effectEntry, "durationMs"))
				{
					if (!TryGetUint(*durationValue, effect.durationMs))
					{
						outError = effectLabel + ": durationMs must be an unsigned integer";
						return false;
					}
				}
				if (const JsonValue* hpTickValue = FindObjectMember(effectEntry, "percentMaxHpPerTick"))
				{
					if (!TryGetFloat(*hpTickValue, effect.percentMaxHpPerTick) || effect.percentMaxHpPerTick < 0.0f)
					{
						outError = effectLabel + ": percentMaxHpPerTick must be a non-negative number";
						return false;
					}
				}

				if (!ValidateEffect(effect, outError, effectLabel))
				{
					return false;
				}

				spell.effects.push_back(effect);
			}

			kit.push_back(std::move(spell));
		}

		// Tri par slot : la barre d'action client consomme l'ordre tel quel.
		std::sort(kit.begin(), kit.end(),
			[](const SpellDef& a, const SpellDef& b) { return a.slot < b.slot; });

		LOG_INFO(Net, "[SpellKitLibrary] Kit loaded (profile={}, spells={})", profile, kit.size());
		m_kits.emplace(profile, std::move(kit));
		return true;
	}
}
