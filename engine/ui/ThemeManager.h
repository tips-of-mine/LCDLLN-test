#pragma once

/**
 * @file ThemeManager.h
 * @brief Theme/skin: load theme.json + style.qss, apply to UI (logical classes hud/panel/button/tooltip). M16.5.
 */

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::ui {

/**
 * @brief Manages UI theme: variables (colors/fonts/icons) from theme.json and class styles from style.qss.
 * Paths are content-relative: fullPath = contentRoot + "/" + themePath (e.g. "ui/themes/default").
 * Fallback to built-in defaults if files are missing.
 */
class ThemeManager {
public:
    ThemeManager() = default;

    /**
     * @brief Loads theme from contentRoot + "/" + themePath (theme.json + style.qss).
     * @param contentRoot Content root path (e.g. from Config paths.content).
     * @param themePath    Relative theme path (e.g. "ui/themes/default" or "ui/themes/human").
     * @return true if at least one file loaded; false uses fallback (no crash).
     */
    bool LoadTheme(const std::string& contentRoot, const std::string& themePath);

    /**
     * @brief Applies current theme to ImGui style (colors for logical classes mapped to ImGuiCol_*).
     * Call once per frame before drawing HUD. Safe to call if no theme loaded (uses defaults).
     */
    void Apply() const;

    /**
     * @brief Returns the current theme path (last successful LoadTheme themePath).
     */
    const std::string& GetCurrentThemePath() const { return m_themePath; }

    /**
     * @brief Returns a color variable by name (e.g. "primary", "background"). RGBA 0..1. Returns default if missing.
     */
    void GetColorVariable(const std::string& name, float rgba[4]) const;

    /**
     * @brief Returns font path variable by name (e.g. "main"). Empty if missing.
     */
    std::string GetFontPath(const std::string& name) const;

    /**
     * @brief Optional: reload theme if theme files changed on disk (hot-reload). Call once per frame.
     * @param contentRoot Same as LoadTheme.
     * @param themePath   Same as LoadTheme.
     * @return true if a reload was performed.
     */
    bool TryHotReload(const std::string& contentRoot, const std::string& themePath);

private:
    void ApplyFallback() const;
    void ParseStyleQss(const std::string& qssContent);
    bool ParseHexColor(const std::string& s, float rgba[4]) const;

    std::string m_themePath;
    std::unordered_map<std::string, std::array<float, 4>> m_colorVariables;
    std::unordered_map<std::string, std::string> m_fontPaths;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_classStyles;
    std::string m_themeJsonPath;
    std::string m_styleQssPath;
    int64_t m_themeJsonMtime = 0;
    int64_t m_styleQssMtime = 0;
};

} // namespace engine::ui
