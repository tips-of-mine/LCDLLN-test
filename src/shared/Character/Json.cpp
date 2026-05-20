#include "src/shared/Character/Json.h"

#include <cctype>
#include <cstddef>

namespace engine::json
{
    namespace
    {
        void SkipWs(const std::string& src, size_t& pos)
        {
            while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
                ++pos;
        }

        bool ParseValue(const std::string& src, size_t& pos, Value& out);

        bool ParseStringLit(const std::string& src, size_t& pos, std::string& out)
        {
            if (pos >= src.size() || src[pos] != '"') return false;
            ++pos;
            out.clear();
            while (pos < src.size())
            {
                const char c = src[pos++];
                if (c == '"') return true;
                if (c == '\\')
                {
                    if (pos >= src.size()) return false;
                    const char e = src[pos++];
                    switch (e)
                    {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                        if (pos + 4 > src.size()) return false;
                        pos += 4;
                        out.push_back('?');
                        break;
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

        bool ParseObject(const std::string& src, size_t& pos, Value& out)
        {
            if (pos >= src.size() || src[pos] != '{') return false;
            ++pos;
            out.type = Value::Type::Object;
            SkipWs(src, pos);
            if (pos < src.size() && src[pos] == '}') { ++pos; return true; }
            while (pos < src.size())
            {
                SkipWs(src, pos);
                std::string key;
                if (!ParseStringLit(src, pos, key)) return false;
                SkipWs(src, pos);
                if (pos >= src.size() || src[pos] != ':') return false;
                ++pos;
                SkipWs(src, pos);
                Value val;
                if (!ParseValue(src, pos, val)) return false;
                out.o.emplace(std::move(key), std::move(val));
                SkipWs(src, pos);
                if (pos < src.size() && src[pos] == ',') { ++pos; continue; }
                if (pos < src.size() && src[pos] == '}') { ++pos; return true; }
                return false;
            }
            return false;
        }

        bool ParseArray(const std::string& src, size_t& pos, Value& out)
        {
            if (pos >= src.size() || src[pos] != '[') return false;
            ++pos;
            out.type = Value::Type::Array;
            SkipWs(src, pos);
            if (pos < src.size() && src[pos] == ']') { ++pos; return true; }
            while (pos < src.size())
            {
                SkipWs(src, pos);
                Value val;
                if (!ParseValue(src, pos, val)) return false;
                out.a.push_back(std::move(val));
                SkipWs(src, pos);
                if (pos < src.size() && src[pos] == ',') { ++pos; continue; }
                if (pos < src.size() && src[pos] == ']') { ++pos; return true; }
                return false;
            }
            return false;
        }

        bool ParseNumber(const std::string& src, size_t& pos, Value& out)
        {
            const size_t start = pos;
            if (pos < src.size() && src[pos] == '-') ++pos;
            bool any = false;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) { any = true; ++pos; }
            if (pos < src.size() && src[pos] == '.')
            {
                ++pos;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) { any = true; ++pos; }
            }
            if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E'))
            {
                ++pos;
                if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
                while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) { any = true; ++pos; }
            }
            if (!any) return false;
            out.type = Value::Type::Number;
            try { out.n = std::stod(src.substr(start, pos - start)); }
            catch (...) { return false; }
            return true;
        }

        bool ParseValue(const std::string& src, size_t& pos, Value& out)
        {
            SkipWs(src, pos);
            if (pos >= src.size()) return false;
            const char c = src[pos];
            if (c == '{') return ParseObject(src, pos, out);
            if (c == '[') return ParseArray(src, pos, out);
            if (c == '"') { out.type = Value::Type::String; return ParseStringLit(src, pos, out.s); }
            if (c == 't' && src.compare(pos, 4, "true") == 0)  { pos += 4; out.type = Value::Type::Bool; out.b = true;  return true; }
            if (c == 'f' && src.compare(pos, 5, "false") == 0) { pos += 5; out.type = Value::Type::Bool; out.b = false; return true; }
            if (c == 'n' && src.compare(pos, 4, "null") == 0)  { pos += 4; out.type = Value::Type::Null; return true; }
            return ParseNumber(src, pos, out);
        }
    }

    bool Parse(const std::string& src, Value& out)
    {
        size_t pos = 0;
        if (!ParseValue(src, pos, out)) return false;
        SkipWs(src, pos);
        return pos == src.size();
    }
}
