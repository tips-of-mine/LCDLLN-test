/**
 * @file ThemeManager.cpp
 * @brief Theme manager: theme.json variables + style.qss application, fallback, hot-reload (M16.5).
 */

#include "engine/ui/ThemeManager.h"
#include "engine/core/Log.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>

#include <filesystem>

namespace engine::ui {

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

int64_t GetFileMtime(const std::string& path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return t.time_since_epoch().count();
}

void DefaultColor(float rgba[4]) {
    rgba[0] = 0.4f; rgba[1] = 0.4f; rgba[2] = 0.45f; rgba[3] = 1.f;
}

} // namespace

bool ThemeManager::LoadTheme(const std::string& contentRoot, const std::string& themePath) {
    m_themePath = themePath;
    m_colorVariables.clear();
    m_fontPaths.clear();
    m_classStyles.clear();

    const std::string base = contentRoot.empty() ? themePath : (contentRoot + "/" + themePath);
    m_themeJsonPath = base + "/theme.json";
    m_styleQssPath = base + "/style.qss";

    bool ok = false;

    std::ifstream fj(m_themeJsonPath);
    if (fj.is_open()) {
        try {
            nlohmann::json j = nlohmann::json::parse(fj);
            if (j.contains("colors") && j["colors"].is_object()) {
                for (auto it = j["colors"].begin(); it != j["colors"].end(); ++it) {
                    std::string val = it.value().get<std::string>();
                    std::array<float, 4> rgba = {0.4f, 0.4f, 0.45f, 1.f};
                    if (ParseHexColor(val, rgba.data()))
                        m_colorVariables[it.key()] = rgba;
                }
            }
            if (j.contains("fonts") && j["fonts"].is_object()) {
                for (auto it = j["fonts"].begin(); it != j["fonts"].end(); ++it)
                    m_fontPaths[it.key()] = it.value().get<std::string>();
            }
            ok = true;
        } catch (const std::exception& e) {
            LOG_ERROR(Render, "ThemeManager: theme.json parse error: {}", e.what());
        }
    } else {
        LOG_INFO(Render, "ThemeManager: theme.json not found at {}, using fallback", m_themeJsonPath);
    }

    const std::string qssContent = ReadFile(m_styleQssPath);
    if (!qssContent.empty()) {
        ParseStyleQss(qssContent);
        ok = true;
    } else {
        LOG_INFO(Render, "ThemeManager: style.qss not found at {}, using fallback", m_styleQssPath);
    }

    m_themeJsonMtime = GetFileMtime(m_themeJsonPath);
    m_styleQssMtime = GetFileMtime(m_styleQssPath);

    return ok;
}

void ThemeManager::ParseStyleQss(const std::string& qssContent) {
    std::string selector;
    std::string prop;
    std::string value;
    enum { None, InSelector, InProp, InValue } state = None;

    for (size_t i = 0; i < qssContent.size(); ++i) {
        char c = qssContent[i];
        if (state == None) {
            if (c == '.') {
                state = InSelector;
                selector.clear();
            }
            continue;
        }
        if (state == InSelector) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                selector += c;
            else if (c == '{') {
                state = InProp;
                prop.clear();
                value.clear();
            } else if (!std::isspace(static_cast<unsigned char>(c)))
                state = None;
            continue;
        }
        if (state == InProp) {
            if (c == ':') {
                state = InValue;
                value.clear();
            } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '-')
                prop += c;
            else if (c == '}')
                state = None;
            continue;
        }
        if (state == InValue) {
            if (c == ';' || c == '}') {
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
                if (!selector.empty() && !prop.empty())
                    m_classStyles[selector][prop] = value;
                prop.clear();
                value.clear();
                if (c == '}')
                    state = None;
                else
                    state = InProp;
            } else if (!std::isspace(static_cast<unsigned char>(c)) || !value.empty())
                value += c;
            continue;
        }
    }
}

bool ThemeManager::ParseHexColor(const std::string& s, float rgba[4]) const {
    if (s.empty() || s[0] != '#') return false;
    size_t len = s.size() - 1;
    if (len != 6 && len != 8) return false;
    unsigned int r = 0, g = 0, b = 0, a = 255;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < len; ++i) {
        int v = hex(s[1 + i]);
        if (v < 0) return false;
        if (i < 2) r = r * 16 + v;
        else if (i < 4) g = g * 16 + v;
        else if (i < 6) b = b * 16 + v;
        else a = a * 16 + v;
    }
    rgba[0] = r / 255.f; rgba[1] = g / 255.f; rgba[2] = b / 255.f;
    rgba[3] = (len == 8) ? (a / 255.f) : 1.f;
    return true;
}

void ThemeManager::GetColorVariable(const std::string& name, float rgba[4]) const {
    auto it = m_colorVariables.find(name);
    if (it != m_colorVariables.end()) {
        rgba[0] = it->second[0]; rgba[1] = it->second[1];
        rgba[2] = it->second[2]; rgba[3] = it->second[3];
        return;
    }
    DefaultColor(rgba);
}

std::string ThemeManager::GetFontPath(const std::string& name) const {
    auto it = m_fontPaths.find(name);
    return it != m_fontPaths.end() ? it->second : "";
}

void ThemeManager::Apply() const {
    ImGuiStyle& style = ImGui::GetStyle();
    float windowBg[4], button[4], buttonHovered[4], buttonActive[4], text[4], popupBg[4];
    DefaultColor(windowBg); DefaultColor(button); DefaultColor(buttonHovered);
    DefaultColor(buttonActive); DefaultColor(text); DefaultColor(popupBg);

    auto resolve = [this](const std::string& val, float out[4]) {
        if (val.size() > 4 && val.compare(0, 4, "var(") == 0) {
            size_t end = val.find(')');
            if (end != std::string::npos) {
                std::string varName = val.substr(4, end - 4);
                while (!varName.empty() && (varName.front() == ' ' || varName.front() == '-')) varName.erase(0, 1);
                if (varName.size() > 2 && varName.compare(0, 2, "--") == 0)
                    varName = varName.substr(2);
                GetColorVariable(varName, out);
                return;
            }
        }
        if (val.size() >= 7 && val[0] == '#')
            ParseHexColor(val, out);
    };

    auto getClassColor = [this, &resolve](const std::string& className, const std::string& prop, float out[4]) {
        auto cit = m_classStyles.find(className);
        if (cit == m_classStyles.end()) return;
        auto pit = cit->second.find(prop);
        if (pit == cit->second.end()) return;
        resolve(pit->second, out);
    };

    getClassColor("panel", "background-color", windowBg);
    getClassColor("panel", "color", text);
    getClassColor("button", "background-color", button);
    getClassColor("tooltip", "background-color", popupBg);
    getClassColor("hud", "background-color", windowBg);

    float bh[4] = { std::min(1.f, button[0] * 1.2f), std::min(1.f, button[1] * 1.2f), std::min(1.f, button[2] * 1.2f), button[3] };
    float ba[4] = { button[0] * 0.8f, button[1] * 0.8f, button[2] * 0.8f, button[3] };
    getClassColor("button", "hover", bh);
    getClassColor("button", "active", ba);

    style.Colors[ImGuiCol_WindowBg] = ImVec4(windowBg[0], windowBg[1], windowBg[2], windowBg[3]);
    style.Colors[ImGuiCol_Button] = ImVec4(button[0], button[1], button[2], button[3]);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(bh[0], bh[1], bh[2], bh[3]);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(ba[0], ba[1], ba[2], ba[3]);
    style.Colors[ImGuiCol_Text] = ImVec4(text[0], text[1], text[2], text[3]);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(popupBg[0], popupBg[1], popupBg[2], popupBg[3]);
}

void ThemeManager::ApplyFallback() const {
    (void)this;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.12f, 0.94f);
    ImGui::GetStyle().Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.22f, 1.f);
    ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.28f, 0.32f, 1.f);
    ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.16f, 0.18f, 1.f);
    ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.f);
    ImGui::GetStyle().Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.1f, 0.94f);
}

bool ThemeManager::TryHotReload(const std::string& contentRoot, const std::string& themePath) {
    const std::string base = contentRoot.empty() ? themePath : (contentRoot + "/" + themePath);
    const std::string jsonPath = base + "/theme.json";
    const std::string qssPath = base + "/style.qss";
    int64_t jm = GetFileMtime(jsonPath);
    int64_t qm = GetFileMtime(qssPath);
    if (jm > m_themeJsonMtime || qm > m_styleQssMtime) {
        return LoadTheme(contentRoot, themePath);
    }
    return false;
}

} // namespace engine::ui
