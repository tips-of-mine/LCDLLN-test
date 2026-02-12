#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::core {

class Config {
public:
    void SetDefault(const std::string& key, const std::string& value);

    bool LoadFromFile(const std::string& path);
    void ApplyCommandLine(int argc, char** argv);

    std::string GetString(const std::string& key, const std::string& fallback = "") const;
    std::int32_t GetInt(const std::string& key, std::int32_t fallback = 0) const;
    double GetFloat(const std::string& key, double fallback = 0.0) const;
    bool GetBool(const std::string& key, bool fallback = false) const;

private:
    bool LoadIni(const std::string& text);
    bool LoadJson(const std::string& text);

    static std::string Trim(const std::string& value);

    std::unordered_map<std::string, std::string> m_values;
};

} // namespace engine::core
