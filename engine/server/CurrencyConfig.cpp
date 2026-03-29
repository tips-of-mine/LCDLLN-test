#include "engine/server/CurrencyConfig.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
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

		/// Minimal JSON parser (subset) — same approach as EventRuntime JSON loader.
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
				if (m_pos + 4 <= m_input.size() && m_input.substr(m_pos, 4) == "true")
				{
					m_pos += 4;
					outValue = JsonValue{};
					outValue.type = JsonType::Bool;
					outValue.boolValue = true;
					return true;
				}
				if (m_pos + 5 <= m_input.size() && m_input.substr(m_pos, 5) == "false")
				{
					m_pos += 5;
					outValue = JsonValue{};
					outValue.type = JsonType::Bool;
					outValue.boolValue = false;
					return true;
				}
				if (m_pos + 4 <= m_input.size() && m_input.substr(m_pos, 4) == "null")
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
				if (m_pos < m_input.size() && m_input[m_pos] == '-')
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

		bool TryGetUint(const JsonValue& value, uint64_t& outValue)
		{
			if (value.type != JsonType::Number
				|| !std::isfinite(value.numberValue)
				|| value.numberValue < 0.0
				|| value.numberValue > static_cast<double>(std::numeric_limits<uint64_t>::max()))
			{
				return false;
			}
			const double truncated = std::floor(value.numberValue);
			if (std::abs(truncated - value.numberValue) > 0.000001)
			{
				return false;
			}
			outValue = static_cast<uint64_t>(truncated);
			return true;
		}

		bool TryGetUint8(const JsonValue& value, uint8_t& outValue)
		{
			uint64_t wide = 0;
			if (!TryGetUint(value, wide) || wide > 255u)
			{
				return false;
			}
			outValue = static_cast<uint8_t>(wide);
			return true;
		}
	}

	void CurrencyConfig::ApplyDefaults()
	{
		m_definitions.clear();
		m_definitions.push_back(
			CurrencyDefinition{ 1u, "gold", "Gold", 999999999ull, "textures/ui/currency_gold.texr" });
		m_definitions.push_back(
			CurrencyDefinition{ 2u, "honor", "Honor", 100000ull, "textures/ui/currency_honor.texr" });
		m_definitions.push_back(
			CurrencyDefinition{ 3u, "badges", "Badges", 999999ull, "textures/ui/currency_badges.texr" });
		m_definitions.push_back(
			CurrencyDefinition{ 4u, "premium_currency", "Premium", 999999999ull, "textures/ui/currency_premium.texr" });
		m_conversion = CurrencyConversion{ 100, 100 };
	}

	bool CurrencyConfig::Load(const engine::core::Config& config)
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, "config/currencies.json");
		if (text.empty())
		{
			LOG_WARN(Net, "[CurrencyConfig] Load: missing or empty config/currencies.json — using defaults");
			ApplyDefaults();
			LOG_INFO(Net, "[CurrencyConfig] Init OK (defaults, currencies={})", m_definitions.size());
			return true;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(text);
		if (!parser.Parse(root, parseError))
		{
			LOG_WARN(Net, "[CurrencyConfig] Parse FAILED: {} — using defaults", parseError);
			ApplyDefaults();
			LOG_INFO(Net, "[CurrencyConfig] Init OK (fallback defaults, currencies={})", m_definitions.size());
			return true;
		}

		if (root.type != JsonType::Object)
		{
			LOG_WARN(Net, "[CurrencyConfig] Root must be object — using defaults");
			ApplyDefaults();
			LOG_INFO(Net, "[CurrencyConfig] Init OK (fallback defaults, currencies={})", m_definitions.size());
			return true;
		}

		const JsonValue* currenciesValue = FindObjectMember(root, "currencies");
		if (currenciesValue == nullptr || currenciesValue->type != JsonType::Array)
		{
			LOG_WARN(Net, "[CurrencyConfig] Missing currencies array — using defaults");
			ApplyDefaults();
			LOG_INFO(Net, "[CurrencyConfig] Init OK (fallback defaults, currencies={})", m_definitions.size());
			return true;
		}

		std::vector<CurrencyDefinition> loaded;
		for (const JsonValue& entry : currenciesValue->arrayValue)
		{
			if (entry.type != JsonType::Object)
			{
				LOG_WARN(Net, "[CurrencyConfig] Skipping non-object currency entry");
				continue;
			}
			CurrencyDefinition def{};
			const JsonValue* idV = FindObjectMember(entry, "id");
			const JsonValue* keyV = FindObjectMember(entry, "key");
			const JsonValue* nameV = FindObjectMember(entry, "name");
			const JsonValue* maxV = FindObjectMember(entry, "max_amount");
			const JsonValue* iconV = FindObjectMember(entry, "icon");
			if (idV == nullptr || keyV == nullptr || nameV == nullptr || maxV == nullptr
				|| !TryGetUint8(*idV, def.id) || keyV->type != JsonType::String || nameV->type != JsonType::String
				|| !TryGetUint(*maxV, def.maxAmount))
			{
				LOG_WARN(Net, "[CurrencyConfig] Invalid currency entry skipped");
				continue;
			}
			def.key = keyV->stringValue;
			def.displayName = nameV->stringValue;
			if (iconV != nullptr && iconV->type == JsonType::String)
			{
				def.iconRelativePath = iconV->stringValue;
			}
			loaded.push_back(std::move(def));
		}

		if (loaded.empty())
		{
			LOG_WARN(Net, "[CurrencyConfig] No valid currency entries — using defaults");
			ApplyDefaults();
			LOG_INFO(Net, "[CurrencyConfig] Init OK (fallback defaults, currencies={})", m_definitions.size());
			return true;
		}

		m_definitions = std::move(loaded);

		const JsonValue* conv = FindObjectMember(root, "conversion");
		if (conv != nullptr && conv->type == JsonType::Object)
		{
			const JsonValue* cps = FindObjectMember(*conv, "copper_per_silver");
			const JsonValue* spg = FindObjectMember(*conv, "silver_per_gold");
			uint64_t v = 0;
			if (cps != nullptr && TryGetUint(*cps, v) && v > 0u)
			{
				m_conversion.copperPerSilver = v;
			}
			v = 0;
			if (spg != nullptr && TryGetUint(*spg, v) && v > 0u)
			{
				m_conversion.silverPerGold = v;
			}
		}

		LOG_INFO(Net, "[CurrencyConfig] Init OK (currencies={}, conversion c/s={} s/g={})",
			m_definitions.size(),
			m_conversion.copperPerSilver,
			m_conversion.silverPerGold);
		return true;
	}

	uint64_t CurrencyConfig::GetMaxAmount(uint8_t currencyId) const
	{
		const CurrencyDefinition* def = FindById(currencyId);
		if (def == nullptr)
		{
			return 0;
		}
		return def->maxAmount;
	}

	const CurrencyDefinition* CurrencyConfig::FindById(uint8_t currencyId) const
	{
		for (const CurrencyDefinition& def : m_definitions)
		{
			if (def.id == currencyId)
			{
				return &def;
			}
		}
		return nullptr;
	}
}
