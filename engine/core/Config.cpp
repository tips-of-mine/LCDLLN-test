#include "engine/core/Config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

namespace engine::core {

namespace {


std::string TrimCopy(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

class JsonParser {
public:
    JsonParser(const std::string& text, std::unordered_map<std::string, std::string>& out)
        : m_text(text), m_out(out) {
    }

    bool Parse() {
        SkipWs();
        return ParseObject("");
    }

private:
    bool ParseObject(const std::string& prefix) {
        if (!Consume('{')) {
            return false;
        }

        SkipWs();
        if (Consume('}')) {
            return true;
        }

        while (m_pos < m_text.size()) {
            std::string key;
            if (!ParseString(key)) {
                return false;
            }

            SkipWs();
            if (!Consume(':')) {
                return false;
            }

            SkipWs();
            const std::string fullKey = prefix.empty() ? key : prefix + "." + key;
            if (!ParseValue(fullKey)) {
                return false;
            }

            SkipWs();
            if (Consume('}')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
            SkipWs();
        }

        return false;
    }

    bool ParseValue(const std::string& key) {
        SkipWs();
        if (Peek() == '{') {
            return ParseObject(key);
        }

        std::string value;
        if (Peek() == '"') {
            if (!ParseString(value)) {
                return false;
            }
            m_out[key] = value;
            return true;
        }

        const std::size_t start = m_pos;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ',' || c == '}' || std::isspace(static_cast<unsigned char>(c))) {
                break;
            }
            ++m_pos;
        }

        value = m_text.substr(start, m_pos - start);
        value = TrimCopy(value);
        if (value.empty()) {
            return false;
        }

        m_out[key] = value;
        return true;
    }

    bool ParseString(std::string& out) {
        if (!Consume('"')) {
            return false;
        }

        std::ostringstream result;
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos++];
            if (c == '"') {
                out = result.str();
                return true;
            }
            if (c == '\\' && m_pos < m_text.size()) {
                const char escaped = m_text[m_pos++];
                result << escaped;
                continue;
            }
            result << c;
        }

        return false;
    }

    char Peek() const {
        if (m_pos >= m_text.size()) {
            return '\0';
        }
        return m_text[m_pos];
    }

    void SkipWs() {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
        }
    }

    bool Consume(char expected) {
        if (Peek() != expected) {
            return false;
        }
        ++m_pos;
        return true;
    }

    const std::string& m_text;
    std::unordered_map<std::string, std::string>& m_out;
    std::size_t m_pos = 0;
};

} // namespace

void Config::SetDefault(const std::string& key, const std::string& value) {
    m_values.insert_or_assign(key, value);
}

bool Config::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") {
        return LoadJson(text);
    }

    return LoadIni(text);
}

void Config::ApplyCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (!arg.starts_with("--")) {
            continue;
        }

        const std::size_t split = arg.find('=');
        if (split == std::string::npos || split <= 2) {
            continue;
        }

        const std::string key = arg.substr(2, split - 2);
        const std::string value = arg.substr(split + 1);
        m_values[key] = value;
    }
}

std::string Config::GetString(const std::string& key, const std::string& fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end()) {
        return fallback;
    }
    return it->second;
}

std::int32_t Config::GetInt(const std::string& key, std::int32_t fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end()) {
        return fallback;
    }

    std::int32_t value = fallback;
    const auto* first = it->second.data();
    const auto* last = first + it->second.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc() || ptr != last) {
        return fallback;
    }
    return value;
}

double Config::GetFloat(const std::string& key, double fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end()) {
        return fallback;
    }

    try {
        return std::stod(it->second);
    } catch (...) {
        return fallback;
    }
}

bool Config::GetBool(const std::string& key, bool fallback) const {
    const auto it = m_values.find(key);
    if (it == m_values.end()) {
        return fallback;
    }

    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }

    return fallback;
}

bool Config::LoadIni(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    std::string section;

    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const std::size_t split = line.find('=');
        if (split == std::string::npos) {
            continue;
        }

        const std::string key = Trim(line.substr(0, split));
        const std::string value = Trim(line.substr(split + 1));
        if (key.empty()) {
            continue;
        }

        const std::string fullKey = section.empty() ? key : section + "." + key;
        m_values[fullKey] = value;
    }

    return true;
}

bool Config::LoadJson(const std::string& text) {
    JsonParser parser(text, m_values);
    return parser.Parse();
}

std::string Config::Trim(const std::string& value) {
    return TrimCopy(value);
}

} // namespace engine::core
