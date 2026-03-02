/**
 * @file Config.cpp
 * @brief Config implementation: JSON/INI load, CLI overrides, typed getters.
 */

#include "engine/core/Config.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <cctype>

namespace engine::core {

namespace {

std::unordered_map<std::string, std::string>& GetStore() {
    static std::unordered_map<std::string, std::string> store;
    return store;
}

void Set(std::string key, std::string value) {
    GetStore()[std::move(key)] = std::move(value);
}

std::string Get(std::string_view key) {
    auto it = GetStore().find(std::string(key));
    if (it != GetStore().end()) return it->second;
    return {};
}

void LoadJsonRecursive(const nlohmann::json& j, const std::string& prefix) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::string key = prefix.empty() ? std::string(it.key()) : (prefix + "." + std::string(it.key()));
            if (it.value().is_object()) {
                LoadJsonRecursive(it.value(), key);
            } else if (it.value().is_string()) {
                Set(key, it.value().get<std::string>());
            } else if (it.value().is_number_integer()) {
                Set(key, std::to_string(it.value().get<int64_t>()));
            } else if (it.value().is_number_float()) {
                Set(key, std::to_string(it.value().get<double>()));
            } else if (it.value().is_boolean()) {
                Set(key, it.value().get<bool>() ? "true" : "false");
            }
        }
    }
}

void LoadIni(std::istream& in) {
    std::string line, section;
    while (std::getline(in, line)) {
        size_t pos = 0;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        if (pos >= line.size() || line[pos] == ';' || line[pos] == '#') continue;
        if (line[pos] == '[') {
            size_t end = line.find(']', pos);
            if (end != std::string::npos) {
                section = line.substr(pos + 1, end - pos - 1);
                continue;
            }
        }
        size_t eq = line.find('=', pos);
        if (eq != std::string::npos) {
            std::string k = line.substr(pos, eq - pos);
            while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
            std::string v = line.substr(eq + 1);
            pos = 0;
            while (pos < v.size() && (v[pos] == ' ' || v[pos] == '\t')) ++pos;
            v = v.substr(pos);
            if (!section.empty()) k = section + "." + k;
            Set(k, v);
        }
    }
}

void LoadJson(std::istream& in) {
    try {
        nlohmann::json j = nlohmann::json::parse(in);
        LoadJsonRecursive(j, "");
    } catch (...) {}
}

} // namespace

void Config::Load(std::string_view configPath) {
    GetStore().clear();
    Set("paths.content", "game/data");
    Set("version", "0.1");
    if (configPath.empty()) return;
    std::string path(configPath);
    std::ifstream f(path);
    if (!f.is_open()) return;
    if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".json") == 0) {
        LoadJson(f);
    } else {
        LoadIni(f);
    }
}

void Config::ApplyArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
            size_t eq = arg.find('=', 2);
            if (eq != std::string::npos) {
                std::string key = arg.substr(2, eq - 2);
                std::string value = arg.substr(eq + 1);
                Set(key, value);
            }
        }
    }
}

std::string Config::GetString(std::string_view key, const std::string& defaultVal) {
    std::string v = Get(key);
    return v.empty() ? defaultVal : v;
}

int Config::GetInt(std::string_view key, int defaultVal) {
    std::string v = Get(key);
    if (v.empty()) return defaultVal;
    try {
        return std::stoi(v);
    } catch (...) {
        return defaultVal;
    }
}

double Config::GetDouble(std::string_view key, double defaultVal) {
    std::string v = Get(key);
    if (v.empty()) return defaultVal;
    try {
        return std::stod(v);
    } catch (...) {
        return defaultVal;
    }
}

bool Config::GetBool(std::string_view key, bool defaultVal) {
    std::string v = Get(key);
    if (v.empty()) return defaultVal;
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "1" || v == "true" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "no") return false;
    return defaultVal;
}

bool Config::Has(std::string_view key) {
    return GetStore().count(std::string(key)) != 0;
}

} // namespace engine::core
