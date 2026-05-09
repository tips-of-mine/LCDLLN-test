#include "engine/server/VendorCatalog.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
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

		bool TryGetInt32(const JsonValue& value, int32_t& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue))
			{
				return false;
			}
			const double truncated = value.numberValue < 0.0 ? std::ceil(value.numberValue) : std::floor(value.numberValue);
			if (std::abs(truncated - value.numberValue) > 0.000001)
			{
				return false;
			}
			if (truncated < static_cast<double>(std::numeric_limits<int32_t>::min())
				|| truncated > static_cast<double>(std::numeric_limits<int32_t>::max()))
			{
				return false;
			}
			outValue = static_cast<int32_t>(truncated);
			return true;
		}

		inline uint64_t MakeStockKey(uint32_t vendorId, uint32_t itemId)
		{
			return (static_cast<uint64_t>(vendorId) << 32) | static_cast<uint64_t>(itemId);
		}
	}

	void VendorStockBook::ResetFromCatalog(const std::vector<VendorDefinition>& vendors)
	{
		m_remaining.clear();
		for (const VendorDefinition& v : vendors)
		{
			for (const VendorItemDefinition& it : v.items)
			{
				if (it.stock < 0)
				{
					continue;
				}
				m_remaining[MakeStockKey(v.vendorId, it.itemId)] = static_cast<uint32_t>(it.stock);
			}
		}
		LOG_INFO(Net, "[VendorStockBook] Reset OK (finite_entries={})", m_remaining.size());
	}

	std::optional<uint32_t> VendorStockBook::GetRemaining(uint32_t vendorId, uint32_t itemId) const
	{
		const uint64_t key = MakeStockKey(vendorId, itemId);
		const auto it = m_remaining.find(key);
		if (it == m_remaining.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	bool VendorStockBook::TryConsume(uint32_t vendorId, uint32_t itemId, uint32_t quantity, std::string& outError)
	{
		const uint64_t key = MakeStockKey(vendorId, itemId);
		const auto it = m_remaining.find(key);
		if (it == m_remaining.end())
		{
			return true;
		}
		if (it->second < quantity)
		{
			outError = "out_of_stock";
			LOG_WARN(Net, "[VendorStockBook] Consume blocked: stock={} need={}", it->second, quantity);
			return false;
		}
		it->second -= quantity;
		LOG_INFO(Net, "[VendorStockBook] Consume OK (vendor_id={}, item_id={}, remaining={})", vendorId, itemId, it->second);
		return true;
	}

	uint32_t VendorCatalog::ComputeSellPrice(uint32_t buyPrice)
	{
		if (buyPrice == 0u)
		{
			return 0u;
		}
		const uint64_t sp = (static_cast<uint64_t>(buyPrice) * 25ull) / 100ull;
		const uint32_t out = static_cast<uint32_t>(std::min<uint64_t>(sp, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
		return out > 0u ? out : 1u;
	}

	void VendorCatalog::ApplyDefaultVendor()
	{
		m_vendors.clear();
		VendorDefinition v{};
		v.vendorId = 1;
		v.displayName = "General Goods";
		v.kind = "general";
		v.items.push_back(VendorItemDefinition{ 1u, 100u, 50 });
		v.items.push_back(VendorItemDefinition{ 2u, 500u, -1 });
		m_vendors.push_back(std::move(v));
	}

	bool VendorCatalog::Load(const engine::core::Config& config)
	{
		const std::string text = engine::platform::FileSystem::ReadAllTextContent(config, "config/vendors.json");
		if (text.empty())
		{
			LOG_WARN(Net, "[VendorCatalog] Load: missing config/vendors.json — using default vendor");
			ApplyDefaultVendor();
			LOG_INFO(Net, "[VendorCatalog] Init OK (default vendors={})", m_vendors.size());
			return true;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(text);
		if (!parser.Parse(root, parseError))
		{
			LOG_WARN(Net, "[VendorCatalog] Parse FAILED: {} — default vendor", parseError);
			ApplyDefaultVendor();
			LOG_INFO(Net, "[VendorCatalog] Init OK (default vendors={})", m_vendors.size());
			return true;
		}

		if (root.type != JsonType::Object)
		{
			LOG_WARN(Net, "[VendorCatalog] Root must be object — default vendor");
			ApplyDefaultVendor();
			LOG_INFO(Net, "[VendorCatalog] Init OK (default vendors={})", m_vendors.size());
			return true;
		}

		const JsonValue* arr = FindObjectMember(root, "vendors");
		if (arr == nullptr || arr->type != JsonType::Array)
		{
			LOG_WARN(Net, "[VendorCatalog] Missing vendors array — default vendor");
			ApplyDefaultVendor();
			LOG_INFO(Net, "[VendorCatalog] Init OK (default vendors={})", m_vendors.size());
			return true;
		}

		std::vector<VendorDefinition> loaded;
		for (const JsonValue& ven : arr->arrayValue)
		{
			if (ven.type != JsonType::Object)
			{
				continue;
			}
			VendorDefinition def{};
			const JsonValue* idV = FindObjectMember(ven, "vendor_id");
			const JsonValue* nameV = FindObjectMember(ven, "display_name");
			const JsonValue* kindV = FindObjectMember(ven, "kind");
			const JsonValue* itemsV = FindObjectMember(ven, "items");
			uint64_t vid = 0;
			if (idV == nullptr || nameV == nullptr || itemsV == nullptr || itemsV->type != JsonType::Array
				|| !TryGetUint(*idV, vid) || vid == 0u || vid > 0xFFFFFFFFull || nameV->type != JsonType::String)
			{
				LOG_WARN(Net, "[VendorCatalog] Skipping invalid vendor entry");
				continue;
			}
			def.vendorId = static_cast<uint32_t>(vid);
			def.displayName = nameV->stringValue;
			if (kindV != nullptr && kindV->type == JsonType::String)
			{
				def.kind = kindV->stringValue;
			}
			for (const JsonValue& entry : itemsV->arrayValue)
			{
				if (entry.type != JsonType::Object)
				{
					continue;
				}
				const JsonValue* iid = FindObjectMember(entry, "item_id");
				const JsonValue* bp = FindObjectMember(entry, "buy_price");
				const JsonValue* st = FindObjectMember(entry, "stock");
				VendorItemDefinition row{};
				uint64_t iid64 = 0;
				uint64_t bp64 = 0;
				if (iid == nullptr || bp == nullptr || !TryGetUint(*iid, iid64) || !TryGetUint(*bp, bp64))
				{
					continue;
				}
				row.itemId = static_cast<uint32_t>(iid64);
				row.buyPrice = static_cast<uint32_t>(std::min<uint64_t>(bp64, std::numeric_limits<uint32_t>::max()));
				if (st != nullptr)
				{
					int32_t st32 = 0;
					if (TryGetInt32(*st, st32))
					{
						row.stock = st32;
					}
				}
				else
				{
					row.stock = -1;
				}
				def.items.push_back(row);
			}
			if (!def.items.empty())
			{
				loaded.push_back(std::move(def));
			}
		}

		if (loaded.empty())
		{
			LOG_WARN(Net, "[VendorCatalog] No valid vendors — default vendor");
			ApplyDefaultVendor();
			LOG_INFO(Net, "[VendorCatalog] Init OK (default vendors={})", m_vendors.size());
			return true;
		}

		m_vendors = std::move(loaded);
		LOG_INFO(Net, "[VendorCatalog] Init OK (vendors={})", m_vendors.size());
		return true;
	}

	const VendorDefinition* VendorCatalog::FindVendor(uint32_t vendorId) const
	{
		for (const VendorDefinition& v : m_vendors)
		{
			if (v.vendorId == vendorId)
			{
				return &v;
			}
		}
		return nullptr;
	}

	const VendorItemDefinition* VendorCatalog::FindVendorItem(uint32_t vendorId, uint32_t itemId) const
	{
		const VendorDefinition* v = FindVendor(vendorId);
		if (v == nullptr)
		{
			return nullptr;
		}
		for (const VendorItemDefinition& it : v->items)
		{
			if (it.itemId == itemId)
			{
				return &it;
			}
		}
		return nullptr;
	}
}
