// engine/core/Config.cpp
// Configuration system: JSON file + CLI overrides.
// Dot-notation keys walk the JSON tree: "a.b.c" -> root["a"]["b"]["c"].

#include "Config.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <cstdlib>
#include <cstring>

namespace engine::core {

// ─── Module-level state ───────────────────────────────────────────────────────

/// Parsed JSON document (may be empty if file absent).
static nlohmann::json g_json;

/// CLI overrides: "paths.content" -> "game/data" (string representation).
static std::unordered_map<std::string, std::string> g_overrides;

// ─── Helpers ─────────────────────────────────────────────────────────────────

/// Walk the JSON tree following dot-separated segments.
/// Returns nullptr if any segment is missing.
const nlohmann::json* Config::Resolve(std::string_view key) {
    const nlohmann::json* node = &g_json;
    std::string_view rem       = key;

    while (!rem.empty()) {
        auto dot = rem.find('.');
        std::string_view seg = (dot == std::string_view::npos) ? rem : rem.substr(0, dot);
        rem = (dot == std::string_view::npos) ? "" : rem.substr(dot + 1);

        std::string segStr(seg);
        if (!node->is_object() || !node->contains(segStr)) {
            return nullptr;
        }
        node = &(*node)[segStr];
    }
    return node;
}

void Config::ParseCLI(int argc, const char* const* argv) {
    // Accept arguments of the form --key=value.
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg || std::strncmp(arg, "--", 2) != 0) { continue; }
        const char* eq = std::strchr(arg + 2, '=');
        if (!eq) { continue; }

        std::string key(arg + 2, static_cast<std::size_t>(eq - (arg + 2)));
        std::string val(eq + 1);
        if (!key.empty()) {
            g_overrides[std::move(key)] = std::move(val);
        }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

void Config::Load(std::string_view jsonPath, int argc, const char* const* argv) {
    g_json = nlohmann::json::object();
    g_overrides.clear();

    // Parse CLI overrides first (highest priority).
    ParseCLI(argc, argv);

    // Load JSON file (best-effort; silently use empty object if absent).
    std::string path(jsonPath);
    std::ifstream file(path);
    if (file.is_open()) {
        try {
            g_json = nlohmann::json::parse(file, nullptr, /*exceptions=*/true, /*comments=*/true);
        } catch (const nlohmann::json::exception& e) {
            // File malformed; continue with empty config.
            // (Caller should LOG_WARN after logging is set up.)
            (void)e;
            g_json = nlohmann::json::object();
        }
    }
    // If file absent or failed: g_json stays as empty object; getters return defaults.
}

void Config::Shutdown() {
    g_json = nlohmann::json::object();
    g_overrides.clear();
}

bool Config::Has(std::string_view key) {
    std::string ks(key);
    if (g_overrides.count(ks)) { return true; }
    return Resolve(key) != nullptr;
}

std::string Config::GetString(std::string_view key, std::string_view defaultValue) {
    // CLI override takes priority.
    std::string ks(key);
    auto it = g_overrides.find(ks);
    if (it != g_overrides.end()) { return it->second; }

    const nlohmann::json* node = Resolve(key);
    if (node) {
        if (node->is_string()) { return node->get<std::string>(); }
        // Coerce non-string scalars to string.
        return node->dump();
    }
    return std::string(defaultValue);
}

int Config::GetInt(std::string_view key, int defaultValue) {
    std::string ks(key);
    auto it = g_overrides.find(ks);
    if (it != g_overrides.end()) {
        try { return std::stoi(it->second); }
        catch (...) { return defaultValue; }
    }

    const nlohmann::json* node = Resolve(key);
    if (node) {
        if (node->is_number_integer())  { return node->get<int>(); }
        if (node->is_number_float())    { return static_cast<int>(node->get<float>()); }
        if (node->is_string()) {
            try { return std::stoi(node->get<std::string>()); }
            catch (...) {}
        }
    }
    return defaultValue;
}

float Config::GetFloat(std::string_view key, float defaultValue) {
    std::string ks(key);
    auto it = g_overrides.find(ks);
    if (it != g_overrides.end()) {
        try { return std::stof(it->second); }
        catch (...) { return defaultValue; }
    }

    const nlohmann::json* node = Resolve(key);
    if (node) {
        if (node->is_number()) { return node->get<float>(); }
        if (node->is_string()) {
            try { return std::stof(node->get<std::string>()); }
            catch (...) {}
        }
    }
    return defaultValue;
}

bool Config::GetBool(std::string_view key, bool defaultValue) {
    std::string ks(key);
    auto it = g_overrides.find(ks);
    if (it != g_overrides.end()) {
        const std::string& v = it->second;
        if (v == "true"  || v == "1") { return true; }
        if (v == "false" || v == "0") { return false; }
        return defaultValue;
    }

    const nlohmann::json* node = Resolve(key);
    if (node) {
        if (node->is_boolean()) { return node->get<bool>(); }
        if (node->is_number())  { return node->get<int>() != 0; }
        if (node->is_string()) {
            const auto& s = node->get<std::string>();
            if (s == "true"  || s == "1") { return true; }
            if (s == "false" || s == "0") { return false; }
        }
    }
    return defaultValue;
}

std::string Config::Dump() {
    return g_json.dump(2);
}

} // namespace engine::core
