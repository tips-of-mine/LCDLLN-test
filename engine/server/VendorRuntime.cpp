#include "engine/server/VendorRuntime.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <cctype>
#include <cmath>
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
		// -------------------------------------------------------------------------
		// Minimal JSON parser (self-contained, no new dependencies).
		// -------------------------------------------------------------------------

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
				case '{': return ParseObject(outValue, outError);
				case '[': return ParseArray(outValue, outError);
				case '"':
					outValue = JsonValue{};
					outValue.type = JsonType::String;
					return ParseString(outValue.stringValue, outError);
				default: break;
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
					case '"':  outValue.push_back('"');  break;
					case '\\': outValue.push_back('\\'); break;
					case '/':  outValue.push_back('/');  break;
					case 'b':  outValue.push_back('\b'); break;
					case 'f':  outValue.push_back('\f'); break;
					case 'n':  outValue.push_back('\n'); break;
					case 'r':  outValue.push_back('\r'); break;
					case 't':  outValue.push_back('\t'); break;
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

		/// Return one object member or nullptr when the key is absent.
		const JsonValue* FindMember(const JsonValue& object, std::string_view key)
		{
			if (object.type != JsonType::Object)
			{
				return nullptr;
			}
			const auto it = object.objectValue.find(std::string(key));
			return (it == object.objectValue.end()) ? nullptr : &it->second;
		}

		/// Convert one JSON number to a validated non-negative 32-bit integer.
		bool TryGetUint32(const JsonValue& value, uint32_t& outValue)
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

		/// Convert one JSON number to a validated signed 32-bit integer (allows -1 for infinite stock).
		bool TryGetInt32(const JsonValue& value, int32_t& outValue)
		{
			if (value.type != JsonType::Number
				|| !std::isfinite(value.numberValue)
				|| value.numberValue < static_cast<double>(std::numeric_limits<int32_t>::min())
				|| value.numberValue > static_cast<double>(std::numeric_limits<int32_t>::max()))
			{
				return false;
			}
			const double truncated = std::floor(value.numberValue);
			if (std::abs(truncated - value.numberValue) > 0.000001)
			{
				return false;
			}
			outValue = static_cast<int32_t>(truncated);
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// VendorRuntime implementation
	// -------------------------------------------------------------------------

	VendorRuntime::VendorRuntime(const engine::core::Config& config)
		: m_config(config)
	{
		LOG_INFO(Net, "[VendorRuntime] Constructed");
	}

	VendorRuntime::~VendorRuntime()
	{
		Shutdown();
	}

	bool VendorRuntime::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[VendorRuntime] Init ignored: already initialized");
			return true;
		}

		if (!LoadDefinitions())
		{
			LOG_ERROR(Net, "[VendorRuntime] Init FAILED: definition load failed");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[VendorRuntime] Init OK (vendors={})", m_definitions.size());
		return true;
	}

	void VendorRuntime::Shutdown()
	{
		if (!m_initialized && m_definitions.empty())
		{
			return;
		}
		const size_t count = m_definitions.size();
		m_definitions.clear();
		m_initialized = false;
		LOG_INFO(Net, "[VendorRuntime] Destroyed (vendors={})", count);
	}

	const VendorDefinition* VendorRuntime::FindVendor(std::string_view vendorId) const
	{
		for (const VendorDefinition& def : m_definitions)
		{
			if (def.vendorId == vendorId)
			{
				return &def;
			}
		}
		return nullptr;
	}

	bool VendorRuntime::LoadDefinitions()
	{
		m_definitions.clear();

		constexpr std::string_view kRelativePath = "vendors/vendor_definitions.json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, std::string(kRelativePath));
		if (jsonText.empty())
		{
			LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: empty or missing file ({})", kRelativePath);
			return false;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, parseError))
		{
			LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: parse error '{}' ({})", parseError, kRelativePath);
			return false;
		}

		const JsonValue* vendorsValue = FindMember(root, "vendors");
		if (vendorsValue == nullptr || vendorsValue->type != JsonType::Array || vendorsValue->arrayValue.empty())
		{
			LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: root.vendors must be a non-empty array ({})", kRelativePath);
			return false;
		}

		std::unordered_set<std::string> seenIds;
		for (size_t vi = 0; vi < vendorsValue->arrayValue.size(); ++vi)
		{
			const JsonValue& vendorValue = vendorsValue->arrayValue[vi];
			if (vendorValue.type != JsonType::Object)
			{
				LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: vendors[{}] must be an object ({})", vi, kRelativePath);
				m_definitions.clear();
				return false;
			}

			const JsonValue* idValue       = FindMember(vendorValue, "vendorId");
			const JsonValue* typeValue     = FindMember(vendorValue, "vendorType");
			const JsonValue* itemsValue    = FindMember(vendorValue, "items");

			if (idValue == nullptr || idValue->type != JsonType::String || idValue->stringValue.empty())
			{
				LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: vendors[{}].vendorId must be a non-empty string ({})", vi, kRelativePath);
				m_definitions.clear();
				return false;
			}

			if (!seenIds.emplace(idValue->stringValue).second)
			{
				LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: duplicate vendorId '{}' ({})", idValue->stringValue, kRelativePath);
				m_definitions.clear();
				return false;
			}

			if (itemsValue == nullptr || itemsValue->type != JsonType::Array || itemsValue->arrayValue.empty())
			{
				LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: vendor '{}' items must be a non-empty array ({})",
					idValue->stringValue, kRelativePath);
				m_definitions.clear();
				return false;
			}

			VendorDefinition def{};
			def.vendorId   = idValue->stringValue;
			def.vendorType = (typeValue != nullptr && typeValue->type == JsonType::String)
				? typeValue->stringValue
				: "general";

			for (size_t ii = 0; ii < itemsValue->arrayValue.size(); ++ii)
			{
				const JsonValue& itemValue = itemsValue->arrayValue[ii];
				if (itemValue.type != JsonType::Object)
				{
					LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: vendor '{}' items[{}] must be an object ({})",
						def.vendorId, ii, kRelativePath);
					m_definitions.clear();
					return false;
				}

				const JsonValue* itemIdValue   = FindMember(itemValue, "itemId");
				const JsonValue* priceValue    = FindMember(itemValue, "buyPrice");
				const JsonValue* stockValue    = FindMember(itemValue, "stock");

				VendorItemDefinition item{};

				if (itemIdValue == nullptr || !TryGetUint32(*itemIdValue, item.itemId) || item.itemId == 0)
				{
					LOG_ERROR(Net,
						"[VendorRuntime] Definition load FAILED: vendor '{}' items[{}].itemId must be a positive integer ({})",
						def.vendorId, ii, kRelativePath);
					m_definitions.clear();
					return false;
				}

				if (priceValue == nullptr || !TryGetUint32(*priceValue, item.buyPrice))
				{
					LOG_ERROR(Net,
						"[VendorRuntime] Definition load FAILED: vendor '{}' items[{}].buyPrice must be a non-negative integer ({})",
						def.vendorId, ii, kRelativePath);
					m_definitions.clear();
					return false;
				}

				// stock is optional; omitting it (or setting -1) means infinite supply.
				if (stockValue != nullptr)
				{
					if (!TryGetInt32(*stockValue, item.stock) || item.stock < -1)
					{
						LOG_WARN(Net, "[VendorRuntime] vendor '{}' items[{}].stock invalid; defaulting to infinite",
							def.vendorId, ii);
						item.stock = -1;
					}
				}

				LOG_DEBUG(Net, "[VendorRuntime] Loaded vendor item (vendor={}, item_id={}, buy_price={}, stock={})",
					def.vendorId, item.itemId, item.buyPrice, item.stock);
				def.items.push_back(item);
			}

			LOG_INFO(Net, "[VendorRuntime] Loaded vendor (id={}, type={}, items={})",
				def.vendorId, def.vendorType, def.items.size());
			m_definitions.push_back(std::move(def));
		}

		if (m_definitions.empty())
		{
			LOG_ERROR(Net, "[VendorRuntime] Definition load FAILED: no vendors loaded ({})", kRelativePath);
			return false;
		}

		LOG_INFO(Net, "[VendorRuntime] Definition load OK (vendors={}, path={})", m_definitions.size(), kRelativePath);
		return true;
	}
}
