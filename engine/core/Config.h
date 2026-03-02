/**
 * @file Config.h
 * @brief Runtime config: load JSON/INI, typed getters, CLI overrides (--key=value). Defaults if file absent; CLI priority.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace engine::core {

/**
 * Config singleton: merge defaults + file (json/ini) + CLI overrides.
 * CLI overrides (--key=value) take priority over file, file over defaults.
 */
class Config {
public:
    /** Load from file path (optional). If empty or missing, use defaults only. */
    static void Load(std::string_view configPath);

    /** Apply CLI overrides from argv (--key=value). Call after Load. */
    static void ApplyArgs(int argc, char* argv[]);

    /** Get string; key supports "section.key" for nested. */
    static std::string GetString(std::string_view key, const std::string& defaultVal = "");

    /** Get int. */
    static int GetInt(std::string_view key, int defaultVal = 0);

    /** Get double. */
    static double GetDouble(std::string_view key, double defaultVal = 0.0);

    /** Get bool (true: "1", "true", "yes"; false otherwise). */
    static bool GetBool(std::string_view key, bool defaultVal = false);

    /** Check if key exists (from file or CLI). */
    static bool Has(std::string_view key);
};

} // namespace engine::core
