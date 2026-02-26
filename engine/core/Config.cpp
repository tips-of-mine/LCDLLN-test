/**
 * @file Config.cpp
 * @brief Implementation of the runtime configuration subsystem.
 *
 * Storage model:
 *   - A flat std::unordered_map<string, string> (g_store) holds all resolved
 *     key-value pairs in dot-notation (e.g. "paths.content" → "game/data").
 *   - The JSON file is parsed with nlohmann/json and flattened into this map.
 *   - CLI overrides (--key=value) are merged on top, replacing any previous
 *     value for the same key.
 *
 * Typed getters convert the stored string representation on demand.
 */

#include "Config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::core {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
namespace {

using json = nlohmann::json;

/// Flat key→value store (all values as strings for uniform handling).
std::unordered_map<std::string, std::string> g_store;

// ---------------------------------------------------------------------------
// JSON flattening helpers
// ---------------------------------------------------------------------------

/**
 * @brief Recursively flattens a JSON object into dot-notation key-value pairs.
 *
 * Example:  { "paths": { "content": "game/data" } }
 *           → g_store["paths.content"] = "game/data"
 *
 * @param node    Current JSON node.
 * @param prefix  Current dot-notation prefix (empty for root).
 */
void FlattenJson(const json& node, const std::string& prefix) {
    if (node.is_object()) {
        for (auto& [k, v] : node.items()) {
            const std::string newKey = prefix.empty() ? k : (prefix + "." + k);
            FlattenJson(v, newKey);
        }
    } else {
        // Leaf node: convert to string representation.
        if (node.is_string())      { g_store[prefix] = node.get<std::string>(); }
        else if (node.is_number()) { g_store[prefix] = std::to_string(node.get<double>()); }
        else if (node.is_boolean()){ g_store[prefix] = node.get<bool>() ? "true" : "false"; }
        else                       { g_store[prefix] = node.dump(); }
    }
}

// ---------------------------------------------------------------------------
// CLI parsing helper
// ---------------------------------------------------------------------------

/**
 * @brief Parses a CLI argument of the form --key=value and stores it.
 *
 * Arguments that do not start with "--" or lack "=" are silently ignored.
 *
 * @param arg  Single argv entry (e.g. "--window.width=1280").
 */
void ParseCliArg(const char* arg) {
    if (!arg || std::strncmp(arg, "--", 2) != 0) { return; }
    const char* eq = std::strchr(arg + 2, '=');
    if (!eq) { return; }

    const std::string key(arg + 2, eq);
    const std::string value(eq + 1);
    g_store[key] = value;
}

// ---------------------------------------------------------------------------
// Normalise floating-point strings produced by std::to_string
// ---------------------------------------------------------------------------

/**
 * @brief Trims trailing zeros after the decimal point (e.g. "2.200000" → "2.2").
 *
 * Used only when storing numeric JSON values so that GetFloat() and GetInt()
 * round-trip cleanly.
 */
std::string TrimTrailingZeros(std::string s) {
    if (s.find('.') == std::string::npos) { return s; }
    const std::size_t last = s.find_last_not_of('0');
    if (last != std::string::npos) {
        s.erase(last + 1);
        if (s.back() == '.') { s.pop_back(); }
    }
    return s;
}

/**
 * @brief Converts a JSON leaf node to a clean string representation.
 */
std::string JsonLeafToString(const json& v) {
    if (v.is_string())      { return v.get<std::string>(); }
    if (v.is_number_float()){ return TrimTrailingZeros(std::to_string(v.get<double>())); }
    if (v.is_number())      { return std::to_string(v.get<long long>()); }
    if (v.is_boolean())     { return v.get<bool>() ? "true" : "false"; }
    return v.dump();
}

/**
 * @brief Overload of FlattenJson that uses the cleaner string conversion.
 */
void FlattenJsonClean(const json& node, const std::string& prefix) {
    if (node.is_object()) {
        for (auto& [k, v] : node.items()) {
            const std::string newKey = prefix.empty() ? k : (prefix + "." + k);
            FlattenJsonClean(v, newKey);
        }
    } else {
        g_store[prefix] = JsonLeafToString(node);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Config public API
// ---------------------------------------------------------------------------

void Config::Init(std::string_view jsonPath, int argc, const char* const* argv) {
    g_store.clear();

    // --- 1. Load JSON file (best-effort; missing file is not an error) -------
    if (!jsonPath.empty()) {
        std::string jsonPathStr{jsonPath};
        std::ifstream f{jsonPathStr};
        if (f.is_open()) {
            try {
                const json root = json::parse(f);
                FlattenJsonClean(root, {});
            } catch (const json::exception& e) {
                // Malformed JSON: log to stderr and continue with defaults.
                std::fprintf(stderr,
                             "[Config::Init] WARNING: JSON parse error in '%.*s': %s\n",
                             static_cast<int>(jsonPath.size()), jsonPath.data(),
                             e.what());
            }
        }
        // If the file does not exist we simply skip it — defaults will be used.
    }

    // --- 2. Apply CLI overrides (highest priority) ---------------------------
    for (int i = 1; i < argc; ++i) {
        ParseCliArg(argv[i]);
    }
}

void Config::Shutdown() {
    g_store.clear();
}

std::string Config::GetString(std::string_view key, std::string_view fallback) {
    const auto it = g_store.find(std::string(key));
    return (it != g_store.end()) ? it->second : std::string(fallback);
}

int Config::GetInt(std::string_view key, int fallback) {
    const auto it = g_store.find(std::string(key));
    if (it == g_store.end()) { return fallback; }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

float Config::GetFloat(std::string_view key, float fallback) {
    const auto it = g_store.find(std::string(key));
    if (it == g_store.end()) { return fallback; }
    try {
        return std::stof(it->second);
    } catch (...) {
        return fallback;
    }
}

bool Config::GetBool(std::string_view key, bool fallback) {
    const auto it = g_store.find(std::string(key));
    if (it == g_store.end()) { return fallback; }
    const std::string& v = it->second;
    if (v == "true"  || v == "1") { return true;  }
    if (v == "false" || v == "0") { return false; }
    return fallback;
}

bool Config::Has(std::string_view key) {
    return g_store.count(std::string(key)) > 0;
}

} // namespace engine::core
