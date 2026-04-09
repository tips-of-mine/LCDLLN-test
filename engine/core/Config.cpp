#include "engine/core/Config.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

namespace engine::core
{
	namespace
	{
		static std::string Trim(std::string s)
		{
			auto notSpace = [](unsigned char c) { return !std::isspace(c); };
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
			s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
			return s;
		}

		static bool StartsWith(std::string_view s, std::string_view prefix)
		{
			return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
		}

		static bool EndsWithInsensitive(std::string_view s, std::string_view suffix)
		{
			if (s.size() < suffix.size())
			{
				return false;
			}
			const auto tail = s.substr(s.size() - suffix.size());
			for (size_t i = 0; i < suffix.size(); ++i)
			{
				const unsigned char a = static_cast<unsigned char>(tail[i]);
				const unsigned char b = static_cast<unsigned char>(suffix[i]);
				if (std::tolower(a) != std::tolower(b))
				{
					return false;
				}
			}
			return true;
		}

		struct JsonValue
		{
			enum class Type
			{
				Null,
				Bool,
				Number,
				String,
				Object,
				Array
			};

			Type type = Type::Null;
			bool b = false;
			double n = 0.0;
			std::string s;
			std::unordered_map<std::string, JsonValue> o;
			std::vector<JsonValue> a;
		};

		class JsonParser final
		{
		public:
			explicit JsonParser(std::string_view input)
				: m_input(input)
			{
			}

			bool Parse(JsonValue& out)
			{
				SkipWs();
				if (!ParseValue(out))
				{
					return false;
				}
				SkipWs();
				return m_pos == m_input.size();
			}

		private:
			void SkipWs()
			{
				while (m_pos < m_input.size())
				{
					const unsigned char c = static_cast<unsigned char>(m_input[m_pos]);
					if (!std::isspace(c))
					{
						break;
					}
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

			bool ParseValue(JsonValue& out)
			{
				if (m_pos >= m_input.size())
				{
					return false;
				}

				switch (m_input[m_pos])
				{
				case '{': return ParseObject(out);
				case '[': return ParseArray(out);
				case '"': return ParseStringValue(out);
				default:
					break;
				}

				if (StartsWith(m_input.substr(m_pos), "true"))
				{
					m_pos += 4;
					out.type = JsonValue::Type::Bool;
					out.b = true;
					return true;
				}
				if (StartsWith(m_input.substr(m_pos), "false"))
				{
					m_pos += 5;
					out.type = JsonValue::Type::Bool;
					out.b = false;
					return true;
				}
				if (StartsWith(m_input.substr(m_pos), "null"))
				{
					m_pos += 4;
					out.type = JsonValue::Type::Null;
					return true;
				}

				return ParseNumber(out);
			}

			bool ParseObject(JsonValue& out)
			{
				if (!Consume('{'))
				{
					return false;
				}
				out = JsonValue{};
				out.type = JsonValue::Type::Object;

				SkipWs();
				if (Consume('}'))
				{
					return true;
				}

				while (true)
				{
					SkipWs();
					std::string key;
					if (!ParseString(key))
					{
						return false;
					}
					SkipWs();
					if (!Consume(':'))
					{
						return false;
					}
					SkipWs();
					JsonValue value;
					if (!ParseValue(value))
					{
						return false;
					}
					out.o.emplace(std::move(key), std::move(value));
					SkipWs();
					if (Consume('}'))
					{
						return true;
					}
					if (!Consume(','))
					{
						return false;
					}
				}
			}

			bool ParseArray(JsonValue& out)
			{
				if (!Consume('['))
				{
					return false;
				}
				out = JsonValue{};
				out.type = JsonValue::Type::Array;

				SkipWs();
				if (Consume(']'))
				{
					return true;
				}

				while (true)
				{
					SkipWs();
					JsonValue value;
					if (!ParseValue(value))
					{
						return false;
					}
					out.a.emplace_back(std::move(value));
					SkipWs();
					if (Consume(']'))
					{
						return true;
					}
					if (!Consume(','))
					{
						return false;
					}
				}
			}

			bool ParseStringValue(JsonValue& out)
			{
				out = JsonValue{};
				out.type = JsonValue::Type::String;
				return ParseString(out.s);
			}

			bool ParseString(std::string& out)
			{
				if (!Consume('"'))
				{
					return false;
				}
				out.clear();
				while (m_pos < m_input.size())
				{
					const char c = m_input[m_pos++];
					if (c == '"')
					{
						return true;
					}
					if (c == '\\')
					{
						if (m_pos >= m_input.size())
						{
							return false;
						}
						const char e = m_input[m_pos++];
						switch (e)
						{
						case '"': out.push_back('"'); break;
						case '\\': out.push_back('\\'); break;
						case '/': out.push_back('/'); break;
						case 'b': out.push_back('\b'); break;
						case 'f': out.push_back('\f'); break;
						case 'n': out.push_back('\n'); break;
						case 'r': out.push_back('\r'); break;
						case 't': out.push_back('\t'); break;
						default: return false;
						}
					}
					else
					{
						out.push_back(c);
					}
				}
				return false;
			}

			bool ParseNumber(JsonValue& out)
			{
				const size_t start = m_pos;
				if (m_input[m_pos] == '-')
				{
					++m_pos;
				}
				bool any = false;
				while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
				{
					any = true;
					++m_pos;
				}
				if (m_pos < m_input.size() && m_input[m_pos] == '.')
				{
					++m_pos;
					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
					{
						any = true;
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
					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
					{
						any = true;
						++m_pos;
					}
				}

				if (!any)
				{
					return false;
				}

				const auto token = m_input.substr(start, m_pos - start);
				char* endPtr = nullptr;
				const double d = std::strtod(std::string(token).c_str(), &endPtr);
				if (endPtr == nullptr)
				{
					return false;
				}

				out = JsonValue{};
				out.type = JsonValue::Type::Number;
				out.n = d;
				return true;
			}

			std::string_view m_input;
			size_t m_pos = 0;
		};

		static void MergeJsonFlatten(const JsonValue& v, const std::string& prefix, Config& cfg)
		{
			switch (v.type)
			{
			case JsonValue::Type::Null:
				return;
			case JsonValue::Type::Bool:
				cfg.SetDefault(prefix, v.b);
				return;
			case JsonValue::Type::Number:
			{
				const double d = v.n;
				const double intPart = std::trunc(d);
				if (intPart == d && d >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
					d <= static_cast<double>(std::numeric_limits<int64_t>::max()))
				{
					cfg.SetDefault(prefix, static_cast<int64_t>(d));
				}
				else
				{
					cfg.SetDefault(prefix, d);
				}
				return;
			}
			case JsonValue::Type::String:
				cfg.SetDefault(prefix, v.s);
				return;
			case JsonValue::Type::Object:
				for (const auto& [k, child] : v.o)
				{
					const std::string next = prefix.empty() ? k : (prefix + "." + k);
					MergeJsonFlatten(child, next, cfg);
				}
				return;
			case JsonValue::Type::Array:
				for (size_t i = 0; i < v.a.size(); ++i)
				{
					const std::string next = std::format("{}[{}]", prefix, i);
					MergeJsonFlatten(v.a[i], next, cfg);
				}
				return;
			default:
				return;
			}
		}

		static bool LoadIni(Config& cfg, std::istream& in)
		{
			std::string section;
			std::string line;
			while (std::getline(in, line))
			{
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#')
				{
					continue;
				}
				if (line.front() == '[' && line.back() == ']')
				{
					section = Trim(line.substr(1, line.size() - 2));
					continue;
				}
				const auto eq = line.find('=');
				if (eq == std::string::npos)
				{
					continue;
				}
				const std::string key = Trim(line.substr(0, eq));
				const std::string val = Trim(line.substr(eq + 1));
				const std::string fullKey = section.empty() ? key : (section + "." + key);
				if (auto scalar = Config::ParseScalar(val))
				{
					cfg.SetValue(fullKey, *scalar);
				}
				else
				{
					cfg.SetValue(fullKey, val);
				}
			}
			return true;
		}
	}

	Config Config::Load(std::string_view filePath, int argc, char** argv)
	{
		Config cfg;

		// Baseline defaults (ticket requirement: defaults when file is missing).
		cfg.SetDefault("paths.content", std::string("game/data"));
		cfg.SetDefault("log.level", std::string("Info"));
		cfg.SetDefault("log.file", std::string("engine.log"));
		cfg.SetDefault("log.console", true);

		cfg.LoadFromFile(filePath);
		(void)cfg.MergeDefaultsFromJsonFile("external/external_links.json");
		cfg.ApplyCli(argc, argv);
		return cfg;
	}

	void Config::SetDefault(std::string_view key, Value value)
	{
		const std::string owned = ToOwnedKey(key);
		if (m_values.find(owned) == m_values.end())
		{
			m_values.emplace(owned, std::move(value));
		}
	}

	bool Config::LoadFromFile(std::string_view filePath)
	{
		std::ifstream in(std::string(filePath), std::ios::in | std::ios::binary);
		if (!in.is_open())
		{
			return false;
		}

		if (EndsWithInsensitive(filePath, ".ini"))
		{
			return LoadIni(*this, in);
		}

		// Default to JSON when extension is unknown.
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string text = ss.str();

		JsonValue root;
		JsonParser parser(text);
		if (!parser.Parse(root))
		{
			return false;
		}
		if (root.type != JsonValue::Type::Object)
		{
			return false;
		}

		// Merge JSON into config by overriding defaults (file has higher priority than defaults).
		// We flatten nested objects into dotted keys.
		Config fileCfg;
		MergeJsonFlatten(root, "", fileCfg);
		for (const auto& [k, v] : fileCfg.m_values)
		{
			SetValue(k, v);
		}

		return true;
	}

	bool Config::MergeDefaultsFromJsonFile(std::string_view filePath)
	{
		std::ifstream in(std::string(filePath), std::ios::in | std::ios::binary);
		if (!in.is_open())
		{
			return false;
		}
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string text = ss.str();

		JsonValue root;
		JsonParser parser(text);
		if (!parser.Parse(root))
		{
			return false;
		}
		if (root.type != JsonValue::Type::Object)
		{
			return false;
		}

		MergeJsonFlatten(root, "", *this);
		return true;
	}

	void Config::ApplyCli(int argc, char** argv)
	{
		for (int i = 1; i < argc; ++i)
		{
			const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
			if (!StartsWith(arg, "--"))
			{
				continue;
			}

			const auto eq = arg.find('=');
			if (eq == std::string_view::npos || eq <= 2)
			{
				continue;
			}

			const std::string_view key = arg.substr(2, eq - 2);
			const std::string_view value = arg.substr(eq + 1);

			if (auto scalar = ParseScalar(value))
			{
				SetValue(key, *scalar);
			}
			else
			{
				SetValue(key, std::string(value));
			}
		}
	}

	bool Config::Has(std::string_view key) const
	{
		return m_values.find(ToOwnedKey(key)) != m_values.end();
	}

	std::string Config::GetString(std::string_view key, std::string_view fallback) const
	{
		const auto it = m_values.find(ToOwnedKey(key));
		if (it == m_values.end())
		{
			return std::string(fallback);
		}

		if (const auto* p = std::get_if<std::string>(&it->second))
		{
			return *p;
		}
		if (const auto* p = std::get_if<bool>(&it->second))
		{
			return *p ? "true" : "false";
		}
		if (const auto* p = std::get_if<int64_t>(&it->second))
		{
			return std::to_string(*p);
		}
		if (const auto* p = std::get_if<double>(&it->second))
		{
			return std::to_string(*p);
		}
		return std::string(fallback);
	}

	std::string Config::GetEffectiveMasterHost(std::string_view fallback) const
	{
		const std::string explicitHost = GetString("client.master_host", "");
		if (!explicitHost.empty())
		{
			return explicitHost;
		}
		const std::string tcpDefault = GetString("client.master_tcp_host", "");
		if (!tcpDefault.empty())
		{
			return tcpDefault;
		}
		return std::string(fallback);
	}

	int64_t Config::GetInt(std::string_view key, int64_t fallback) const
	{
		const auto it = m_values.find(ToOwnedKey(key));
		if (it == m_values.end())
		{
			return fallback;
		}

		if (const auto* p = std::get_if<int64_t>(&it->second))
		{
			return *p;
		}
		if (const auto* p = std::get_if<double>(&it->second))
		{
			return static_cast<int64_t>(*p);
		}
		if (const auto* p = std::get_if<bool>(&it->second))
		{
			return *p ? 1 : 0;
		}
		if (const auto* p = std::get_if<std::string>(&it->second))
		{
			int64_t v = 0;
			const auto sv = std::string_view(*p);
			const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
			if (ec == std::errc{} && ptr == sv.data() + sv.size())
			{
				return v;
			}
		}
		return fallback;
	}

	double Config::GetDouble(std::string_view key, double fallback) const
	{
		const auto it = m_values.find(ToOwnedKey(key));
		if (it == m_values.end())
		{
			return fallback;
		}

		if (const auto* p = std::get_if<double>(&it->second))
		{
			return *p;
		}
		if (const auto* p = std::get_if<int64_t>(&it->second))
		{
			return static_cast<double>(*p);
		}
		if (const auto* p = std::get_if<bool>(&it->second))
		{
			return *p ? 1.0 : 0.0;
		}
		if (const auto* p = std::get_if<std::string>(&it->second))
		{
			char* endPtr = nullptr;
			const double d = std::strtod(p->c_str(), &endPtr);
			if (endPtr != nullptr && endPtr != p->c_str())
			{
				return d;
			}
		}
		return fallback;
	}

	bool Config::GetBool(std::string_view key, bool fallback) const
	{
		const auto it = m_values.find(ToOwnedKey(key));
		if (it == m_values.end())
		{
			return fallback;
		}

		if (const auto* p = std::get_if<bool>(&it->second))
		{
			return *p;
		}
		if (const auto* p = std::get_if<int64_t>(&it->second))
		{
			return *p != 0;
		}
		if (const auto* p = std::get_if<double>(&it->second))
		{
			return *p != 0.0;
		}
		if (const auto* p = std::get_if<std::string>(&it->second))
		{
			const std::string lower = [&]()
			{
				std::string t = *p;
				std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return t;
			}();
			if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
			{
				return true;
			}
			if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
			{
				return false;
			}
		}

		return fallback;
	}

	void Config::SetValue(std::string_view key, Value value)
	{
		m_values[ToOwnedKey(key)] = std::move(value);
	}

	std::string Config::ToOwnedKey(std::string_view key)
	{
		return std::string(key);
	}

	std::optional<Config::Value> Config::ParseScalar(std::string_view text)
	{
		std::string v(text);
		v = Trim(std::move(v));
		if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
		{
			return std::string(v.substr(1, v.size() - 2));
		}

		std::string lower = v;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lower == "true")
		{
			return true;
		}
		if (lower == "false")
		{
			return false;
		}

		int64_t i = 0;
		{
			const auto sv = std::string_view(v);
			const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), i);
			if (ec == std::errc{} && ptr == sv.data() + sv.size())
			{
				return i;
			}
		}

		char* endPtr = nullptr;
		const double d = std::strtod(v.c_str(), &endPtr);
		if (endPtr != nullptr && endPtr != v.c_str() && *endPtr == '\0')
		{
			return d;
		}

		return std::nullopt;
	}
}

