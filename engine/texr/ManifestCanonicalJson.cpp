#include "engine/texr/ManifestCanonicalJson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

namespace lcdlln::manifest {
namespace {

void AppendEscapedUtf8(std::string& out, const std::string& s)
{
	out.push_back('"');
	for (unsigned char uc : s)
	{
		const char c = static_cast<char>(uc);
		switch (c)
		{
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (uc < 0x20u)
			{
				char buf[7];
				std::snprintf(buf, sizeof(buf), "\\u%04x", uc);
				out += buf;
			}
			else
			{
				out.push_back(c);
			}
		}
	}
	out.push_back('"');
}

std::string FormatNumber(double d)
{
	if (std::isnan(d) || std::isinf(d))
	{
		return "null";
	}
	if (d == 0.0)
	{
		return "0";
	}
	// Entiers représentés comme tels (évite 1.0).
	double ip = 0;
	if (std::modf(d, &ip) == 0.0 && d >= -9007199254740992.0 && d <= 9007199254740992.0)
	{
		std::ostringstream oss;
		oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
		oss << static_cast<int64_t>(d);
		return oss.str();
	}
	std::ostringstream oss;
	oss << d;
	return oss.str();
}

}  // namespace

std::string CanonicalStringify(const nlohmann::json& j)
{
	if (j.is_null())
	{
		return "null";
	}
	if (j.is_boolean())
	{
		return j.get<bool>() ? "true" : "false";
	}
	if (j.is_number_unsigned())
	{
		return std::to_string(j.get<uint64_t>());
	}
	if (j.is_number_integer())
	{
		return std::to_string(j.get<int64_t>());
	}
	if (j.is_number_float())
	{
		return FormatNumber(j.get<double>());
	}
	if (j.is_string())
	{
		std::string out;
		AppendEscapedUtf8(out, j.get_ref<const std::string&>());
		return out;
	}
	if (j.is_array())
	{
		std::string out;
		out.push_back('[');
		for (std::size_t i = 0; i < j.size(); ++i)
		{
			if (i > 0)
			{
				out.push_back(',');
			}
			out += CanonicalStringify(j[static_cast<nlohmann::json::size_type>(i)]);
		}
		out.push_back(']');
		return out;
	}
	if (j.is_object())
	{
		std::vector<std::string> keys;
		keys.reserve(j.size());
		for (auto it = j.begin(); it != j.end(); ++it)
		{
			keys.push_back(it.key());
		}
		std::sort(keys.begin(), keys.end());
		std::string out;
		out.push_back('{');
		bool first = true;
		for (const std::string& k : keys)
		{
			if (!first)
			{
				out.push_back(',');
			}
			first = false;
			AppendEscapedUtf8(out, k);
			out.push_back(':');
			out += CanonicalStringify(j.at(k));
		}
		out.push_back('}');
		return out;
	}
	return "null";
}

}  // namespace lcdlln::manifest
