#pragma once

/**
 * @file Config.h
 * @brief Runtime configuration: JSON file + CLI overrides.
 *
 * Priority (highest → lowest):
 *   1. CLI arguments   --key=value  (dot-notation supported: paths.content)
 *   2. JSON config file (e.g. config.json at the working directory)
 *   3. Built-in defaults supplied by the caller
 *
 * Usage:
 *   Config::Init("config.json", argc, argv);
 *
 *   std::string root = Config::GetString("paths.content", "game/data");
 *   int width        = Config::GetInt   ("window.width",  1280);
 *   float gamma      = Config::GetFloat ("render.gamma",  2.2f);
 *   bool vsync       = Config::GetBool  ("render.vsync",  true);
 *
 * JSON example (config.json):
 * {
 *   "paths": { "content": "game/data" },
 *   "window": { "width": 1920, "height": 1080 },
 *   "render": { "gamma": 2.2, "vsync": true },
 *   "log":    { "level": "INFO", "file": "engine.log" }
 * }
 *
 * CLI override example:
 *   engine_app.exe --window.width=1280 --render.vsync=false
 */

#include <string>
#include <string_view>
#include <optional>

namespace engine::core {

/// Runtime configuration subsystem.
class Config {
public:
    Config() = delete;

    /**
     * @brief Loads configuration from a JSON file and CLI arguments.
     *
     * Missing file is not an error — built-in defaults are used instead.
     * CLI overrides are always applied on top.
     *
     * @param jsonPath  Path to the JSON config file (relative to CWD).
     * @param argc      Argument count from main().
     * @param argv      Argument vector from main().
     */
    static void Init(std::string_view jsonPath, int argc, const char* const* argv);

    /// Releases all internal state.
    static void Shutdown();

    // -----------------------------------------------------------------------
    // Typed getters with fallback defaults
    // -----------------------------------------------------------------------

    /**
     * @brief Returns a string value.
     * @param key      Dot-separated key (e.g. "paths.content").
     * @param fallback Default value if the key is absent.
     */
    static std::string GetString(std::string_view key,
                                 std::string_view fallback = "") ;

    /**
     * @brief Returns an integer value.
     * @param key      Dot-separated key.
     * @param fallback Default value if the key is absent or non-numeric.
     */
    static int GetInt(std::string_view key, int fallback = 0);

    /**
     * @brief Returns a float value.
     * @param key      Dot-separated key.
     * @param fallback Default value if the key is absent or non-numeric.
     */
    static float GetFloat(std::string_view key, float fallback = 0.0f);

    /**
     * @brief Returns a boolean value.
     *
     * JSON booleans, "true"/"false" strings and "1"/"0" are accepted.
     *
     * @param key      Dot-separated key.
     * @param fallback Default value if the key is absent.
     */
    static bool GetBool(std::string_view key, bool fallback = false);

    /**
     * @brief Returns true if the key exists in the resolved configuration.
     */
    static bool Has(std::string_view key);
};

} // namespace engine::core
