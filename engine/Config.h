#pragma once
// engine/core/Config.h
// Runtime configuration loader.
//  - Loads a JSON file (nlohmann/json).
//  - Provides typed getters with default-value fallback.
//  - CLI overrides take the form --key=value (highest priority).
//  - key uses dot notation: "paths.content", "window.width".
// Usage:
//   Config::Load("config.json", argc, argv);
//   int w = Config::GetInt("window.width", 1280);
//   std::string content = Config::GetString("paths.content", "game/data");

#include <string>
#include <string_view>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace engine::core {

class Config {
public:
    /// Load config from a JSON file and apply CLI overrides.
    /// @param jsonPath  Path to the JSON config file. If the file is absent or
    ///                  unparseable, defaults will still be returned by the
    ///                  getters.
    /// @param argc      argc from main() (pass 0 if not available).
    /// @param argv      argv from main() (pass nullptr if not available).
    static void Load(std::string_view jsonPath, int argc = 0, const char* const* argv = nullptr);

    /// Release all loaded data.
    static void Shutdown();

    // ── Typed getters ─────────────────────────────────────────────────────
    // All getters accept a dot-notation key (e.g. "window.width").
    // Returns defaultValue if the key is absent.

    /// Get a string value.
    static std::string GetString(std::string_view key, std::string_view defaultValue = "") ;

    /// Get an integer value (parsed from JSON number or CLI string).
    static int GetInt(std::string_view key, int defaultValue = 0);

    /// Get a floating-point value.
    static float GetFloat(std::string_view key, float defaultValue = 0.0f);

    /// Get a boolean value ("true"/"false"/"1"/"0" from CLI, bool from JSON).
    static bool GetBool(std::string_view key, bool defaultValue = false);

    /// Returns true if a key exists (in file or CLI overrides).
    static bool Has(std::string_view key);

    /// Returns the loaded JSON as a formatted string (for debug logging).
    static std::string Dump();

private:
    /// Resolve a dot-notation key from the JSON tree.
    /// Returns nullptr if absent.
    static const nlohmann::json* Resolve(std::string_view key);

    /// Apply CLI arguments of the form --key=value into overrides map.
    static void ParseCLI(int argc, const char* const* argv);
};

} // namespace engine::core
