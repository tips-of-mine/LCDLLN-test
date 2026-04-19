# Auth ImGui Renderer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrer le rendu de l'UI auth de `VkClearRect` manuels vers Dear ImGui, en créant `LnTheme.h` + `AuthImGuiRenderer`, branchés dans `Engine.cpp`.

**Architecture:** `WorldEditorImGui` gère tout le cycle de vie ImGui (init, NewFrame, RenderDrawData) — sa création devient inconditionnelle. `AuthImGuiRenderer` est une classe légère qui appelle uniquement les APIs `ImGui::Begin/End/Text/Button…` et stocke l'état transient UI (buffers de champs, onglet actif). Les deux coexistent dans le même contexte ImGui mais ne s'affichent jamais simultanément.

**Tech Stack:** C++17, Dear ImGui (déjà intégré), Vulkan, CMake, Windows + Linux.

> **Branche cible :** `claude/organize-ui-design-system-P9EAx`  
> **Spec :** `docs/superpowers/specs/2026-04-19-auth-imgui-renderer-design.md`  
> **Ne jamais modifier** les fichiers serveur ou gameplay.

---

## Cartographie des fichiers

| Fichier | Action | Responsabilité |
|---|---|---|
| `engine/render/LnTheme.h` | CRÉER | Tokens CSS → ImVec4/ImU32, seule source couleurs |
| `engine/render/AuthImGuiRenderer.h` | CRÉER | Déclaration classe + état transient |
| `engine/render/AuthImGuiRenderer.cpp` | CRÉER | Implémentation helpers + 10 méthodes écran |
| `engine/client/AuthUi.h` | MODIFIER (l.84-106) | +2 flags VisualState : `options`, `shardPick` |
| `engine/Engine.h` | MODIFIER (l.235-237) | +membre `m_authImGui` |
| `engine/Engine.cpp` | MODIFIER (l.2583-2597, 2770-2785, 2950, 2308-2317, 2419-2423) | Init inconditionnelle, NewFrame, BuildUi, RecordToBackbuffer, Shutdown |
| `CMakeLists.txt` | MODIFIER (après l.261) | +2 sources |
| `engine/render/AuthUiRenderer.h/.cpp` | CONSERVER | Ne pas toucher |

---

## Task 1 : LnTheme.h + CMakeLists

**Fichiers :**
- Créer : `engine/render/LnTheme.h`
- Modifier : `CMakeLists.txt` (après la ligne `engine/render/AuthUiRenderer.cpp`)

- [ ] **Étape 1 : Créer `engine/render/LnTheme.h`**

```cpp
#pragma once
#include "imgui.h"

namespace LnTheme {

// ── Palette de base (thème par défaut, dérivé de colors_and_type.css) ──
inline constexpr ImVec4 primary    {0.290f, 0.482f, 0.722f, 1.f}; // #4A7BB8
inline constexpr ImVec4 secondary  {0.361f, 0.420f, 0.549f, 1.f}; // #5C6B8C
inline constexpr ImVec4 accent     {0.910f, 0.773f, 0.431f, 1.f}; // #E8C56E
inline constexpr ImVec4 background {0.039f, 0.051f, 0.071f, 1.f}; // #0A0D12
inline constexpr ImVec4 surface    {0.071f, 0.094f, 0.133f, 1.f}; // #121822
inline constexpr ImVec4 panel      {0.078f, 0.110f, 0.157f, 1.f}; // #141C28
inline constexpr ImVec4 text       {0.949f, 0.957f, 0.973f, 1.f}; // #F2F4F8
inline constexpr ImVec4 muted      {0.608f, 0.659f, 0.722f, 1.f}; // #9BA8B8
inline constexpr ImVec4 border     {0.239f, 0.310f, 0.400f, 1.f}; // #3D4F66

// ── Status ──
inline constexpr ImVec4 success    {0.373f, 0.722f, 0.431f, 1.f}; // #5FB86E
inline constexpr ImVec4 warning    {0.910f, 0.647f, 0.361f, 1.f}; // #E8A55C
inline constexpr ImVec4 error_col  {0.769f, 0.251f, 0.251f, 1.f}; // #C44040

// ── Variantes alpha ──
inline ImVec4 PanelBg  (float a = 0.72f) { return {0.078f, 0.110f, 0.157f, a}; }
inline ImVec4 AccentDim(float a = 0.10f) { return {0.910f, 0.773f, 0.431f, a}; }
inline ImVec4 BorderActive()             { return accent; }

// ── Conversion ImU32 (pour DrawList) ──
inline ImU32 U32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

} // namespace LnTheme
```

- [ ] **Étape 2 : Ajouter les deux sources dans `CMakeLists.txt`**

Trouver la ligne `engine/render/AuthUiRenderer.cpp` (actuellement ligne 261) et ajouter juste après :

```cmake
        engine/render/LnTheme.h
        engine/render/AuthImGuiRenderer.cpp
```

- [ ] **Étape 3 : Vérifier que le header compile seul**

```bash
# Depuis la racine du repo, compiler uniquement engine_core pour attraper les erreurs de syntaxe
cmake --build build --target engine_core -- -j4
```

Résultat attendu : pas d'erreur liée à `LnTheme.h`. (`AuthImGuiRenderer.cpp` n'existe pas encore — CMake ignorera l'entrée manquante ou la signalera comme avertissement selon la config.)

- [ ] **Étape 4 : Commit**

```bash
git add engine/render/LnTheme.h CMakeLists.txt
git commit -m "feat: add LnTheme.h color tokens + CMakeLists entries"
```

---

## Task 2 : VisualState — ajout de `options` et `shardPick`

**Fichiers :**
- Modifier : `engine/client/AuthUi.h` (struct `VisualState`, lignes 84-106)

- [ ] **Étape 1 : Ajouter les deux flags à `VisualState`**

Dans `engine/client/AuthUi.h`, à la fin du bloc des flags (après `bool authStatusOk = false;`, ligne ~104), ajouter :

```cpp
    bool options   = false; ///< Écran Options (graphismes / son / contrôles…)
    bool shardPick = false; ///< Écran Choix du royaume (liste des serveurs)
```

Le struct complet après modification :

```cpp
struct VisualState
{
    bool active = false;
    bool login = false;
    bool registerMode = false;
    bool verifyEmail = false;
    bool forgotPassword = false;
    bool terms = false;
    bool characterCreate = false;
    bool languageSelection = false;
    bool languageOptions = false;
    bool submitting = false;
    bool error = false;
    bool minimalChrome = false;
    bool loginArtColumn = false;
    bool authLogoSpin = false;
    bool authStatusKnown = false;
    bool authStatusOk = false;
    bool options   = false; ///< Écran Options
    bool shardPick = false; ///< Écran Choix du royaume
};
```

- [ ] **Étape 2 : Compiler pour vérifier**

```bash
cmake --build build --target engine_core -- -j4
```

Résultat attendu : 0 erreur, 0 avertissement nouveau.

- [ ] **Étape 3 : Commit**

```bash
git add engine/client/AuthUi.h
git commit -m "feat: add options + shardPick flags to VisualState"
```

---

## Task 3 : AuthImGuiRenderer — header + stub + RenderLangScreen

**Fichiers :**
- Créer : `engine/render/AuthImGuiRenderer.h`
- Créer : `engine/render/AuthImGuiRenderer.cpp`

### Étape 1 — Créer le header

- [ ] **Créer `engine/render/AuthImGuiRenderer.h`**

```cpp
#pragma once
#include <string_view>
#include "imgui.h"
#include "engine/client/AuthUi.h"
#include "engine/render/LnTheme.h"

namespace engine::render {

class AuthImGuiRenderer {
public:
    /// Rend l'UI auth si vs.active. Appelle ImGui::Begin/End en interne.
    /// Doit être appelé entre ImGui::NewFrame() et ImGui_ImplVulkan_RenderDrawData().
    void Render(const engine::client::AuthUiPresenter::VisualState& vs,
                const engine::client::AuthUiPresenter::RenderModel& rm,
                ImVec2 viewport);

    /// Remet à zéro tous les buffers transients (appelé sur changement d'écran si nécessaire).
    void Reset();

private:
    using VisualState = engine::client::AuthUiPresenter::VisualState;
    using RenderModel = engine::client::AuthUiPresenter::RenderModel;

    // ── État transient ──
    int  m_selectedLang  = 0;        // 0=fr, 1=en
    char m_loginId[128]  = {};
    char m_loginPw[128]  = {};
    bool m_rememberMe    = true;
    char m_regId[128]    = {};
    char m_regEmail[128] = {};
    char m_regPw[128]    = {};
    char m_regPw2[128]   = {};
    char m_verifyCode[7] = {};
    int  m_optionsTab    = 0;
    int  m_selectedShard = 0;

    // ── Méthodes par écran ──
    void RenderLangScreen      (const RenderModel&, ImVec2 vp);
    void RenderLoginScreen     (const RenderModel&, ImVec2 vp);
    void RenderRegisterScreen  (const RenderModel&, ImVec2 vp);
    void RenderErrorScreen     (const RenderModel&, ImVec2 vp);
    void RenderVerifyScreen    (const RenderModel&, ImVec2 vp);
    void RenderOptionsScreen   (const RenderModel&, ImVec2 vp);
    void RenderShardScreen     (const RenderModel&, ImVec2 vp);
    void RenderForgotScreen    (const RenderModel&, ImVec2 vp);
    void RenderTermsScreen     (const RenderModel&, ImVec2 vp);
    void RenderCharCreateScreen(const RenderModel&, ImVec2 vp);

    // ── Helpers partagés ──
    void BeginFullscreenOverlay(ImVec2 vp);
    /// Ouvre un child-window centré. Doit être suivi de EndPanel().
    bool BeginPanel(float width, ImVec2 vp,
                    std::string_view title, std::string_view subtitle,
                    std::string_view versionLabel = {});
    void EndPanel();
    /// Deux lang-cards côte à côte ; retourne l'index cliqué (-1 = aucun clic).
    int  DrawLangCards(int selected);
    void DrawField(std::string_view label, char* buf, int bufSz, bool password = false);
    void DrawBanner(std::string_view title, std::string_view msg, ImVec4 color);
    void DrawKeycapHints(std::initializer_list<std::pair<const char*, const char*>> hints);
    bool DrawPrimaryButton(std::string_view label, bool disabled = false);
    bool DrawGhostButton (std::string_view label, bool disabled = false);
    void DrawSeparator();
    void DrawBreadcrumb(std::initializer_list<const char*> steps, int current);
};

} // namespace engine::render
```

### Étape 2 — Créer le .cpp avec helpers + stub + RenderLangScreen

- [ ] **Créer `engine/render/AuthImGuiRenderer.cpp`**

```cpp
#include "engine/render/AuthImGuiRenderer.h"
#include <cstring>
#include <cstdio>

namespace engine::render {

// ════════════════════════════════════════════════════════════
//  PUBLIC
// ════════════════════════════════════════════════════════════

void AuthImGuiRenderer::Reset() {
    m_selectedLang  = 0;
    m_optionsTab    = 0;
    m_selectedShard = 0;
    m_rememberMe    = true;
    std::memset(m_loginId,    0, sizeof(m_loginId));
    std::memset(m_loginPw,    0, sizeof(m_loginPw));
    std::memset(m_regId,      0, sizeof(m_regId));
    std::memset(m_regEmail,   0, sizeof(m_regEmail));
    std::memset(m_regPw,      0, sizeof(m_regPw));
    std::memset(m_regPw2,     0, sizeof(m_regPw2));
    std::memset(m_verifyCode, 0, sizeof(m_verifyCode));
}

void AuthImGuiRenderer::Render(const VisualState& vs,
                                const RenderModel& rm,
                                ImVec2 vp)
{
    if (!vs.active) return;

    BeginFullscreenOverlay(vp);

    if      (vs.languageSelection) RenderLangScreen(rm, vp);
    else if (vs.login)             RenderLoginScreen(rm, vp);
    else if (vs.registerMode)      RenderRegisterScreen(rm, vp);
    else if (vs.error)             RenderErrorScreen(rm, vp);
    else if (vs.verifyEmail)       RenderVerifyScreen(rm, vp);
    else if (vs.options)           RenderOptionsScreen(rm, vp);
    else if (vs.shardPick)         RenderShardScreen(rm, vp);
    else if (vs.forgotPassword)    RenderForgotScreen(rm, vp);
    else if (vs.terms)             RenderTermsScreen(rm, vp);
    else if (vs.characterCreate)   RenderCharCreateScreen(rm, vp);

    ImGui::End(); // ferme l'overlay fullscreen
}

// ════════════════════════════════════════════════════════════
//  HELPERS PARTAGÉS
// ════════════════════════════════════════════════════════════

void AuthImGuiRenderer::BeginFullscreenOverlay(ImVec2 vp) {
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(vp);
    ImGui::SetNextWindowBgAlpha(1.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, LnTheme::background);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##ln_auth_overlay", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav        |
        ImGuiWindowFlags_NoScrollbar  |
        ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);
}

bool AuthImGuiRenderer::BeginPanel(float width, ImVec2 vp,
                                    std::string_view title,
                                    std::string_view subtitle,
                                    std::string_view versionLabel)
{
    const float panelX = (vp.x - width) * 0.5f;
    const float panelY = vp.y * 0.28f; // sous le hero (~28% du viewport)
    ImGui::SetCursorPos({panelX, panelY});

    ImGui::PushStyleColor(ImGuiCol_ChildBg, LnTheme::PanelBg());
    ImGui::PushStyleColor(ImGuiCol_Border,  LnTheme::border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  {20.f, 18.f});

    bool open = ImGui::BeginChild("##ln_panel", {width, 0.f}, true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    if (open) {
        // Header : titre + version label sur la même ligne
        if (!title.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
            ImGui::SetWindowFontScale(1.15f);
            ImGui::TextUnformatted(title.data(), title.data() + title.size());
            ImGui::SetWindowFontScale(1.f);
            ImGui::PopStyleColor();
        }
        if (!versionLabel.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x -
                            ImGui::CalcTextSize(versionLabel.data()).x);
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
            ImGui::TextUnformatted(versionLabel.data(), versionLabel.data() + versionLabel.size());
            ImGui::PopStyleColor();
        }
        if (!subtitle.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
            ImGui::TextUnformatted(subtitle.data(), subtitle.data() + subtitle.size());
            ImGui::PopStyleColor();
        }
        ImGui::PushStyleColor(ImGuiCol_Separator, LnTheme::border);
        ImGui::Separator();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    return open;
}

void AuthImGuiRenderer::EndPanel() {
    ImGui::EndChild();
}

int AuthImGuiRenderer::DrawLangCards(int selected) {
    int clicked = -1;
    const ImVec2 cardSize{160.f, 110.f};
    const float  spacing = 16.f;
    const float  totalW  = cardSize.x * 2.f + spacing;
    const float  startX  = (ImGui::GetContentRegionAvail().x - totalW) * 0.5f
                         + ImGui::GetCursorPosX();

    struct LangEntry { const char* id; const char* label; };
    static constexpr LangEntry langs[2] = {{"fr","Français"},{"en","English"}};

    for (int i = 0; i < 2; ++i) {
        // Card 0 : positionner le curseur X ; card 1 : simplement SameLine après EndChild de card 0
        if (i == 0) ImGui::SetCursorPosX(startX);

        const bool  isSelected = (selected == i);
        ImVec4      borderCol  = isSelected ? LnTheme::accent : LnTheme::border;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, LnTheme::surface);
        ImGui::PushStyleColor(ImGuiCol_Border,  borderCol);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   6.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);

        char childId[16]; std::snprintf(childId, sizeof(childId), "##lcard%d", i);
        ImGui::BeginChild(childId, cardSize, true);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        // Placeholder drapeau 54×38
        ImVec2 flagPos = {
            (cardSize.x - 54.f) * 0.5f,
            14.f
        };
        ImGui::SetCursorPos(flagPos);
        ImGui::GetWindowDrawList()->AddRectFilled(
            {ImGui::GetWindowPos().x + flagPos.x,
             ImGui::GetWindowPos().y + flagPos.y},
            {ImGui::GetWindowPos().x + flagPos.x + 54.f,
             ImGui::GetWindowPos().y + flagPos.y + 38.f},
            LnTheme::U32(LnTheme::surface));
        // Contour
        ImGui::GetWindowDrawList()->AddRect(
            {ImGui::GetWindowPos().x + flagPos.x,
             ImGui::GetWindowPos().y + flagPos.y},
            {ImGui::GetWindowPos().x + flagPos.x + 54.f,
             ImGui::GetWindowPos().y + flagPos.y + 38.f},
            LnTheme::U32(LnTheme::border));

        // Label langue
        ImGui::SetCursorPos({0.f, 60.f});
        float labelW = ImGui::CalcTextSize(langs[i].label).x;
        ImGui::SetCursorPosX((cardSize.x - labelW) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? LnTheme::accent : LnTheme::text);
        ImGui::TextUnformatted(langs[i].label);
        ImGui::PopStyleColor();

        // Clic sur la carte entière
        ImGui::SetCursorPos({0.f, 0.f});
        ImGui::InvisibleButton(childId, cardSize);
        if (ImGui::IsItemClicked()) clicked = i;

        ImGui::EndChild();
        // SameLine après card 0 → card 1 se placera immédiatement à droite avec le spacing
        if (i == 0) ImGui::SameLine(0.f, spacing);
    }
    return clicked;
}

void AuthImGuiRenderer::DrawField(std::string_view label, char* buf, int bufSz, bool password) {
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        LnTheme::surface);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, LnTheme::surface);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  LnTheme::surface);
    ImGui::PushStyleColor(ImGuiCol_Border,         LnTheme::border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);

    char inputId[64]; std::snprintf(inputId, sizeof(inputId), "##f_%s", label.data());
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
    if (password) flags |= ImGuiInputTextFlags_Password;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText(inputId, buf, static_cast<size_t>(bufSz), flags);

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::Spacing();
}

void AuthImGuiRenderer::DrawBanner(std::string_view title, std::string_view msg, ImVec4 color) {
    ImVec4 bgColor = color; bgColor.w = 0.12f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bgColor);
    ImGui::PushStyleColor(ImGuiCol_Border,  color);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::BeginChild("##banner", {-FLT_MIN, 0.f}, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::PushStyleColor(ImGuiCol_Text, color);
    if (!title.empty()) ImGui::TextUnformatted(title.data(), title.data() + title.size());
    ImGui::PopStyleColor();
    if (!msg.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
        ImGui::TextWrapped("%.*s", (int)msg.size(), msg.data());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::Spacing();
}

void AuthImGuiRenderer::DrawKeycapHints(
    std::initializer_list<std::pair<const char*, const char*>> hints)
{
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    bool first = true;
    for (auto& [key, label] : hints) {
        if (!first) { ImGui::SameLine(0.f, 14.f); }
        ImGui::Text("[%s] %s", key, label);
        first = false;
    }
    ImGui::PopStyleColor();
}

bool AuthImGuiRenderer::DrawPrimaryButton(std::string_view label, bool disabled) {
    if (disabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        LnTheme::primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.39f,0.58f,0.82f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.19f,0.38f,0.62f,1.f});
    ImGui::PushStyleColor(ImGuiCol_Text,          LnTheme::text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    char id[128]; std::snprintf(id, sizeof(id), "%.*s##primary", (int)label.size(), label.data());
    bool clicked = ImGui::Button(id, {-FLT_MIN, 32.f});
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(4);
    if (disabled) ImGui::EndDisabled();
    return clicked;
}

bool AuthImGuiRenderer::DrawGhostButton(std::string_view label, bool disabled) {
    if (disabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, LnTheme::AccentDim(0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  LnTheme::AccentDim(0.15f));
    ImGui::PushStyleColor(ImGuiCol_Border,        LnTheme::border);
    ImGui::PushStyleColor(ImGuiCol_Text,          LnTheme::text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    char id[128]; std::snprintf(id, sizeof(id), "%.*s##ghost", (int)label.size(), label.data());
    bool clicked = ImGui::Button(id, {-FLT_MIN, 32.f});
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    if (disabled) ImGui::EndDisabled();
    return clicked;
}

void AuthImGuiRenderer::DrawSeparator() {
    ImGui::PushStyleColor(ImGuiCol_Separator, LnTheme::border);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void AuthImGuiRenderer::DrawBreadcrumb(
    std::initializer_list<const char*> steps, int current)
{
    int i = 0;
    for (const char* s : steps) {
        bool done   = i < current;
        bool active = i == current;
        ImVec4 col = done   ? LnTheme::success
                   : active ? LnTheme::accent
                             : LnTheme::muted;
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("%02d %s", i + 1, s);
        ImGui::PopStyleColor();
        ++i;
        if (i < (int)steps.size()) { ImGui::SameLine(0.f, 12.f); ImGui::Text("›"); ImGui::SameLine(0.f, 12.f); }
    }
    ImGui::Spacing();
}

// ════════════════════════════════════════════════════════════
//  ÉCRAN 1 — SÉLECTION DE LANGUE
// ════════════════════════════════════════════════════════════

void AuthImGuiRenderer::RenderLangScreen(const RenderModel& rm, ImVec2 vp) {
    // Hero centré
    const char* hero1 = "LES CHRONIQUES";
    const char* hero2 = "DE LA LUNE NOIRE";
    ImGui::SetWindowFontScale(1.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
    float w1 = ImGui::CalcTextSize(hero1).x;
    float w2 = ImGui::CalcTextSize(hero2).x;
    ImGui::SetCursorPos({(vp.x - w1) * 0.5f, vp.y * 0.08f});
    ImGui::TextUnformatted(hero1);
    ImGui::SetCursorPos({(vp.x - w2) * 0.5f, ImGui::GetCursorPosY()});
    ImGui::TextUnformatted(hero2);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.f);

    const std::string& subtitle = rm.sectionTitle.empty() ? std::string("Bienvenue, voyageur.") : rm.sectionTitle;
    if (!BeginPanel(720.f, vp, "Choisissez votre langue", subtitle, "1 / 2")) {
        EndPanel(); return;
    }

    ImGui::Spacing();
    int clicked = DrawLangCards(m_selectedLang);
    if (clicked >= 0) m_selectedLang = clicked;
    ImGui::Spacing();

    // Bouton Continuer aligné à droite
    float btnW = 160.f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btnW + ImGui::GetCursorPosX());
    ImGui::PushStyleColor(ImGuiCol_Button,        LnTheme::primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.39f,0.58f,0.82f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4{0.19f,0.38f,0.62f,1.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::Button("Continuer  ->##lang_continue", {btnW, 32.f});
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(3);

    DrawSeparator();
    DrawKeycapHints({{"<-  ->", "naviguer"}, {"Entree", "valider"}});

    EndPanel();
}

// ════════════════════════════════════════════════════════════
//  STUBS — les autres écrans (implémentés dans les tâches suivantes)
// ════════════════════════════════════════════════════════════

void AuthImGuiRenderer::RenderLoginScreen     (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderRegisterScreen  (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderErrorScreen     (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderVerifyScreen    (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderOptionsScreen   (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderShardScreen     (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderForgotScreen    (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderTermsScreen     (const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
void AuthImGuiRenderer::RenderCharCreateScreen(const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }

} // namespace engine::render
```

- [ ] **Étape 3 : Compiler**

```bash
cmake --build build --target engine_core -- -j4
```

Résultat attendu : 0 erreur. `AuthImGuiRenderer.cpp` compile, les stubs ne génèrent pas d'avertissements (les casts `(void)` les neutralisent).

- [ ] **Étape 4 : Commit**

```bash
git add engine/render/AuthImGuiRenderer.h engine/render/AuthImGuiRenderer.cpp
git commit -m "feat: add AuthImGuiRenderer stub + RenderLangScreen"
```

---

## Task 4 : Engine.cpp + Engine.h — branchement ImGui

**Fichiers :**
- Modifier : `engine/Engine.h` (section membres privés, vers ligne 235)
- Modifier : `engine/Engine.cpp` (lignes 2583-2597, 2770-2785, 2950, 2308-2317, 2419-2423)

### Étape 1 — Ajouter le membre dans Engine.h

- [ ] **Dans `engine/Engine.h`**, après la déclaration de `m_worldEditorImGui` (ligne ~235), ajouter :

```cpp
    std::unique_ptr<engine::render::AuthImGuiRenderer> m_authImGui;
```

Ajouter également l'include en haut du header (ou dans Engine.cpp, selon la convention du projet) :

```cpp
#include "engine/render/AuthImGuiRenderer.h"
```

### Étape 2 — Init inconditionnelle (ligne ~2583)

- [ ] **Modifier le bloc d'init `WorldEditorImGui` dans `Engine.cpp`**

Remplacer :
```cpp
if (m_worldEditorExe && !m_worldEditorImGui && m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid()
    && m_window.GetNativeHandle() != nullptr && m_vkDeviceContext.SupportsDynamicRendering())
```

Par :
```cpp
if (!m_worldEditorImGui && m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid()
    && m_window.GetNativeHandle() != nullptr && m_vkDeviceContext.SupportsDynamicRendering())
```

Et dans le corps du bloc, après le `if (m_worldEditorImGui->Init(...))` réussi, ajouter la condition éditeur autour de `SetEditorContext`/`AttachPlatformWindow` et la création de l'auth renderer :

```cpp
if (m_worldEditorImGui->Init(
        m_vkInstance.GetHandle(),
        m_vkDeviceContext,
        m_vkSwapchain.GetImageFormat(),
        m_vkSwapchain.GetImageCount(),
        VK_API_VERSION_1_1,
        m_window.GetNativeHandle()))
{
    if (m_worldEditorExe) {
        m_worldEditorImGui->SetEditorContext(m_worldEditorSession.get(), &m_cfg);
        m_worldEditorImGui->AttachPlatformWindow(m_window.GetNativeHandle(), m_window);
    }
    m_authImGui = std::make_unique<engine::render::AuthImGuiRenderer>();
}
else
{
    m_worldEditorImGui.reset();
}
```

### Étape 3 — NewFrame conditionnel (ligne ~2770)

- [ ] **Modifier la condition du bloc NewFrame**

Remplacer :
```cpp
if (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady())
{
    // ...calcul imguiDw/imguiDh...
    m_worldEditorImGui->NewFrame(static_cast<float>(dt), imguiDw, imguiDh);
}
```

Par :
```cpp
const bool authUiActive = m_authImGui && authVisualState.active;
if (m_worldEditorImGui && m_worldEditorImGui->IsReady()
    && ((m_worldEditorExe) || authUiActive))
{
    float imguiDw = static_cast<float>(std::max(1, m_width));
    float imguiDh = static_cast<float>(std::max(1, m_height));
    if (m_vkSwapchain.IsValid())
    {
        const VkExtent2D extImg = m_vkSwapchain.GetExtent();
        if (extImg.width > 0 && extImg.height > 0)
        {
            imguiDw = static_cast<float>(extImg.width);
            imguiDh = static_cast<float>(extImg.height);
        }
    }
    m_worldEditorImGui->NewFrame(static_cast<float>(dt), imguiDw, imguiDh);
}
```

> **Note :** La variable `authVisualState` est une locale de la même fonction. Vérifiez qu'elle est déclarée avant ce bloc (elle l'est, vers ligne ~2001).

### Étape 4 — BuildUi + Render auth (ligne ~2950)

- [ ] **Modifier la section BuildUi**

Le code existant est :
```cpp
m_worldEditorImGui->BuildUi(&overlay);
```

Modifier pour :
```cpp
if (m_worldEditorExe && m_worldEditorImGui)
    m_worldEditorImGui->BuildUi(&overlay);

if (m_authImGui && authVisualState.active)
    m_authImGui->Render(authVisualState, authRenderModel,
                        {static_cast<float>(ext.width), static_cast<float>(ext.height)});
```

> **Note :** `ext` et `authRenderModel` sont des locales disponibles dans ce scope. Adapter les noms si nécessaire en consultant les déclarations réelles.

### Étape 5 — RecordToBackbuffer (ligne ~2308)

- [ ] **Modifier la condition RecordToBackbuffer**

Remplacer :
```cpp
if (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady()
    && m_vkDeviceContext.SupportsDynamicRendering() && backbufferView != VK_NULL_HANDLE
    && !presentSolidColorDebug)
```

Par :
```cpp
if (m_worldEditorImGui && m_worldEditorImGui->IsReady()
    && (m_worldEditorExe || (m_authImGui && authVisualState.active))
    && m_vkDeviceContext.SupportsDynamicRendering() && backbufferView != VK_NULL_HANDLE
    && !presentSolidColorDebug)
```

> **Note :** Ce bloc est dans `Engine::Render()`, pas `Engine::Update()`. `authVisualState` n'est peut-être pas accessible ici. Si ce n'est pas le cas, promouvoir `authVisualState` en membre `m_authVisualState` ou passer un bool `m_authUiActive` mis à jour dans Update().

### Étape 6 — Shutdown (ligne ~2419)

- [ ] **Ajouter le reset de m_authImGui dans le bloc shutdown**

Après `m_worldEditorImGui.reset();`, ajouter :
```cpp
m_authImGui.reset();
```

Le bloc complet devient :
```cpp
if (m_worldEditorImGui)
{
    m_worldEditorImGui->DetachPlatformWindow(m_window);
    m_worldEditorImGui->Shutdown(m_vkDeviceContext.GetDevice());
    m_worldEditorImGui.reset();
}
m_authImGui.reset();
```

- [ ] **Étape 7 : Compiler**

```bash
cmake --build build --target engine_core -- -j4
```

Résultat attendu : 0 erreur de compilation. Avertissements possibles sur variables non utilisées (normaux dans les stubs).

- [ ] **Étape 8 : Commit + push**

```bash
git add engine/Engine.h engine/Engine.cpp
git commit -m "feat: wire AuthImGuiRenderer into Engine — unconditional ImGui init"
git push
```

---

## Task 5 : Écrans 2 à 5 — Login, Inscription, Erreurs, Courriel

**Fichiers :**
- Modifier : `engine/render/AuthImGuiRenderer.cpp` (remplacer les 4 stubs)

- [ ] **Étape 1 : Implémenter `RenderLoginScreen`**

Remplacer le stub :
```cpp
void AuthImGuiRenderer::RenderLoginScreen(const RenderModel& rm, ImVec2 vp) { (void)rm; (void)vp; }
```

Par :
```cpp
void AuthImGuiRenderer::RenderLoginScreen(const RenderModel& rm, ImVec2 vp) {
    // Hero
    const char* hero1 = "LES CHRONIQUES";
    const char* hero2 = "DE LA LUNE NOIRE";
    ImGui::SetWindowFontScale(1.4f);
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
    ImGui::SetCursorPos({(vp.x - ImGui::CalcTextSize(hero1).x) * 0.5f, vp.y * 0.07f});
    ImGui::TextUnformatted(hero1);
    ImGui::SetCursorPos({(vp.x - ImGui::CalcTextSize(hero2).x) * 0.5f, ImGui::GetCursorPosY()});
    ImGui::TextUnformatted(hero2);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.f);

    if (!BeginPanel(460.f, vp, "Connexion", {}, "v0.8.4")) { EndPanel(); return; }

    // Bannières
    if (!rm.errorText.empty())
        DrawBanner("Echec de la connexion", rm.errorText, LnTheme::error_col);
    // (submitting est porté par VisualState mais pas accessible ici — le Presenter
    //  peut remplir rm.infoBanner avec "Contact du serveur maitre..." si submitting)
    if (!rm.infoBanner.empty())
        DrawBanner("Verification en cours", rm.infoBanner, LnTheme::primary);

    DrawField("Identifiant",    m_loginId, sizeof(m_loginId), false);
    DrawField("Mot de passe",   m_loginPw, sizeof(m_loginPw), true);

    // Toggle "Se souvenir de moi"
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::Checkbox("##remember", &m_rememberMe);
    ImGui::SameLine();
    ImGui::TextUnformatted("Se souvenir de moi");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Actions
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::SmallButton("Mot de passe oublie ?##forgot_link");
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0,0,0,0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, LnTheme::AccentDim());
    ImGui::PushStyleColor(ImGuiCol_Border,        LnTheme::border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::Button("Creer un compte##register_link", {0.f, 28.f});
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.f, 8.f);
    DrawPrimaryButton("Se connecter");

    DrawSeparator();
    DrawKeycapHints({{"Tab","champ suivant"},{"Entree","se connecter"},{"Echap","quitter"}});

    EndPanel();

    // Liens sous le panel
    float linksY = ImGui::GetCursorPosY() + 10.f;
    float linksX = (vp.x - 160.f) * 0.5f;
    ImGui::SetCursorPos({linksX, linksY});
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::SmallButton("Options##opts_link");
    ImGui::SameLine(0.f, 20.f);
    ImGui::SmallButton("Quitter##quit_link");
    ImGui::PopStyleColor();
}
```

- [ ] **Étape 2 : Implémenter `RenderRegisterScreen`**

Remplacer le stub par :
```cpp
void AuthImGuiRenderer::RenderRegisterScreen(const RenderModel& rm, ImVec2 vp) {
    (void)rm;
    DrawBreadcrumb({"Langue","Compte","Courriel","Monde"}, 1);
    if (!BeginPanel(680.f, vp, "Creer un compte",
                    "Forger votre identite dans les terres de la Lune Noire.", "2 / 4"))
    { EndPanel(); return; }

    // Grille 2 colonnes via SameLine
    DrawField("Identifiant",       m_regId,    sizeof(m_regId));    // span 2 (pleine largeur)
    DrawField("Adresse courriel",  m_regEmail, sizeof(m_regEmail)); // span 2

    // Col 1 : Mot de passe + barre de force
    const float halfW = (ImGui::GetContentRegionAvail().x - 12.f) * 0.5f;
    ImGui::SetNextItemWidth(halfW);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, LnTheme::surface);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::InputText("##pw_reg", m_regPw, sizeof(m_regPw), ImGuiInputTextFlags_Password);
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(1);

    // Barre de force (4 segments)
    int strength = 0;
    size_t pwLen = std::strlen(m_regPw);
    if (pwLen >= 8) strength++;
    bool hasUpper = false, hasDigit = false, hasSym = false;
    for (char c : std::string_view{m_regPw, pwLen}) {
        if (c >= 'A' && c <= 'Z') hasUpper = true;
        if (c >= '0' && c <= '9') hasDigit = true;
        if (!(std::isalnum((unsigned char)c))) hasSym = true;
    }
    if (hasUpper) strength++; if (hasDigit) strength++; if (hasSym) strength++;

    float segW = (halfW - 9.f) / 4.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 segPos = {ImGui::GetWindowPos().x + ImGui::GetCursorPosX(),
                     ImGui::GetWindowPos().y + ImGui::GetCursorPosY() + 2.f};
    for (int s = 0; s < 4; ++s) {
        ImVec4 col = (s < strength)
            ? (strength <= 1 ? LnTheme::error_col : strength == 2 ? LnTheme::warning : LnTheme::success)
            : LnTheme::border;
        dl->AddRectFilled(segPos, {segPos.x + segW, segPos.y + 5.f}, LnTheme::U32(col), 2.f);
        segPos.x += segW + 3.f;
    }
    ImGui::Dummy({halfW, 8.f});

    // Col 2 : Confirmation
    ImGui::SameLine(0.f, 12.f);
    ImGui::SetNextItemWidth(halfW);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, LnTheme::surface);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::InputText("##pw2_reg", m_regPw2, sizeof(m_regPw2), ImGuiInputTextFlags_Password);
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(1);
    ImGui::Spacing();

    // Dropdowns Jour / Mois / Année (ImGui::Combo simplifiés)
    static int sDay = 0, sMon = 0, sYr = 20; // indices par défaut
    static const char* months[] = {"Janv.","Fevr.","Mars","Avr.","Mai","Juin",
                                    "Juil.","Aout","Sept.","Oct.","Nov.","Dec."};
    float thirdW = (ImGui::GetContentRegionAvail().x - 16.f) / 3.f;
    ImGui::SetNextItemWidth(thirdW);
    ImGui::Combo("##day", &sDay, [](void*, int idx, const char** out) {
        static char buf[4]; std::snprintf(buf, sizeof(buf), "%02d", idx + 1);
        *out = buf; return true;
    }, nullptr, 31);
    ImGui::SameLine(0.f, 8.f);
    ImGui::SetNextItemWidth(thirdW);
    ImGui::Combo("##month", &sMon, months, 12);
    ImGui::SameLine(0.f, 8.f);
    static const char* years[90] = {};
    static bool yearsInited = false;
    if (!yearsInited) {
        static char yrBufs[90][8];
        for (int i = 0; i < 90; ++i) {
            std::snprintf(yrBufs[i], sizeof(yrBufs[i]), "%d", 2010 - i);
            years[i] = yrBufs[i];
        }
        yearsInited = true;
    }
    ImGui::SetNextItemWidth(thirdW);
    ImGui::Combo("##year", &sYr, years, 90);
    ImGui::Spacing();

    bool canSubmit = (strength >= 3) && std::strlen(m_regPw) > 0 &&
                     std::strcmp(m_regPw, m_regPw2) == 0;
    DrawGhostButton("Retour");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
    DrawPrimaryButton("Creer le compte", !canSubmit);

    DrawSeparator();
    DrawKeycapHints({{"Tab","champ suivant"},{"Entree","valider"},{"Echap","retour"}});
    EndPanel();
}
```

- [ ] **Étape 3 : Implémenter `RenderErrorScreen`**

```cpp
void AuthImGuiRenderer::RenderErrorScreen(const RenderModel& rm, ImVec2 vp) {
    if (!BeginPanel(640.f, vp, "Inscription impossible", {}, "Erreur")) { EndPanel(); return; }

    // Bannière erreur ou warning (réseau = warning)
    const bool isNetwork = !rm.infoBanner.empty();
    ImVec4 bannerColor = isNetwork ? LnTheme::warning : LnTheme::error_col;
    DrawBanner(rm.titleLine1.empty() ? "Erreur" : rm.titleLine1,
               rm.errorText, bannerColor);

    // Bloc "Champ a corriger" (rm.sectionTitle porte le nom du champ)
    if (!rm.sectionTitle.empty()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, LnTheme::surface);
        ImGui::PushStyleColor(ImGuiCol_Border,  LnTheme::border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   6.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
        ImGui::BeginChild("##err_field", {-FLT_MIN, 52.f}, true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("CHAMP A CORRIGER");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::accent);
        ImGui::TextUnformatted(rm.sectionTitle.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::Spacing();
    }

    // Bloc "Comment corriger" (rm.infoBanner porte le conseil)
    if (!rm.infoBanner.empty() || isNetwork) {
        const std::string& conseil = rm.infoBanner.empty() ? rm.errorText : rm.infoBanner;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, LnTheme::AccentDim(0.04f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.f);
        // border-left accent simulé via DrawList
        ImGui::BeginChild("##err_fix", {-FLT_MIN, 0.f}, false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(1);
        // Trait vertical accent 3px
        ImVec2 wpos = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(wpos, {wpos.x + 3.f, wpos.y + 60.f},
                                                  LnTheme::U32(LnTheme::accent));
        ImGui::SetCursorPosX(8.f);
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::accent);
        ImGui::TextUnformatted("COMMENT CORRIGER");
        ImGui::PopStyleColor();
        ImGui::SetCursorPosX(8.f);
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
        ImGui::TextWrapped("%s", conseil.c_str());
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::Spacing();
    }

    DrawGhostButton("Retour au formulaire");
    if (isNetwork) {
        ImGui::SameLine(0.f, 8.f);
        DrawPrimaryButton("Reessayer");
    }
    EndPanel();
}
```

- [ ] **Étape 4 : Implémenter `RenderVerifyScreen`**

```cpp
void AuthImGuiRenderer::RenderVerifyScreen(const RenderModel& rm, ImVec2 vp) {
    DrawBreadcrumb({"Langue","Compte","Courriel","Monde"}, 2);
    const std::string& sub = rm.sectionTitle.empty()
        ? std::string("Nous avons envoye un code a 6 chiffres.")
        : rm.sectionTitle;
    if (!BeginPanel(560.f, vp, "Verifiez votre courriel", sub, "3 / 4")) { EndPanel(); return; }

    if (!rm.errorText.empty())
        DrawBanner("Code incorrect", rm.errorText, LnTheme::error_col);

    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::TextUnformatted("CODE DE VERIFICATION");
    ImGui::PopStyleColor();

    // 6 cellules chiffre individuelles
    const float cellW = 52.f, cellH = 60.f, cellGap = 8.f;
    const float totalW = cellW * 6.f + cellGap * 5.f;
    float startX = ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - totalW) * 0.5f;

    for (int i = 0; i < 6; ++i) {
        ImGui::SetCursorPosX(startX + i * (cellW + cellGap));
        char cellId[8]; std::snprintf(cellId, sizeof(cellId), "##cd%d", i);
        char cellBuf[2] = {m_verifyCode[i], '\0'};
        ImGui::PushStyleColor(ImGuiCol_FrameBg, LnTheme::surface);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
        ImGui::SetNextItemWidth(cellW);
        if (ImGui::InputText(cellId, cellBuf, 2,
                ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AlwaysOverwrite)) {
            m_verifyCode[i] = cellBuf[0];
            if (cellBuf[0] && i < 5) ImGui::SetKeyboardFocusHere(1); // auto-avance
        }
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(1);
        if (i < 5) ImGui::SameLine(0.f, cellGap);
    }
    ImGui::Spacing();

    // Liens secondaires centrés
    float linksW = 220.f;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - linksW) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::SmallButton("Renvoyer le code##resend");
    ImGui::SameLine(0.f, 12.f);
    ImGui::SmallButton("Modifier le courriel##changemail");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    bool codeComplete = (std::strlen(m_verifyCode) == 6);
    DrawGhostButton("Retour");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
    DrawPrimaryButton("Valider le code", !codeComplete);

    EndPanel();
}
```

- [ ] **Étape 5 : Compiler**

```bash
cmake --build build --target engine_core -- -j4
```

Résultat attendu : 0 erreur.

- [ ] **Étape 6 : Commit**

```bash
git add engine/render/AuthImGuiRenderer.cpp
git commit -m "feat: implement login, register, error, verify screens"
```

---

## Task 6 : Écrans 6 et 7 — Options, Choix du royaume

**Fichiers :**
- Modifier : `engine/render/AuthImGuiRenderer.cpp` (remplacer 2 stubs)

- [ ] **Étape 1 : Implémenter `RenderOptionsScreen`**

```cpp
void AuthImGuiRenderer::RenderOptionsScreen(const RenderModel& /*rm*/, ImVec2 vp) {
    // Layout fullscreen split : sidebar 220px + panneau principal
    const float sideW  = 220.f;
    const float mainW  = vp.x - sideW;
    const float height = vp.y;

    static const struct Tab { const char* icon; const char* label; } tabs[] = {
        {"[G]","Graphismes"},{"[S]","Son"},{"[K]","Controles"},
        {"[L]","Langue"},{"[U]","Interface"},{"[N]","Reseau"},{"[A]","Compte"}
    };
    static const int tabCount = 7;

    // Sidebar
    ImGui::SetCursorPos({0.f, 0.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, LnTheme::panel);
    ImGui::PushStyleColor(ImGuiCol_Border,  LnTheme::border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::BeginChild("##opts_sidebar", {sideW, height}, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(2);

    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::SetWindowFontScale(1.1f);
    ImGui::TextUnformatted("OPTIONS");
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    DrawSeparator();

    for (int i = 0; i < tabCount; ++i) {
        bool active = (m_optionsTab == i);
        ImGui::PushStyleColor(ImGuiCol_Button,
            active ? LnTheme::AccentDim(0.15f) : ImVec4{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, LnTheme::AccentDim(0.08f));
        ImGui::PushStyleColor(ImGuiCol_Text, active ? LnTheme::accent : LnTheme::text);
        char btnId[32]; std::snprintf(btnId, sizeof(btnId), "%s %s##tab%d",
                                      tabs[i].icon, tabs[i].label, i);
        if (ImGui::Button(btnId, {-FLT_MIN, 32.f})) m_optionsTab = i;
        ImGui::PopStyleColor(3);
    }
    ImGui::EndChild();

    // Panneau principal
    ImGui::SetCursorPos({sideW, 0.f});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, LnTheme::background);
    ImGui::BeginChild("##opts_main", {mainW, height}, false, ImGuiWindowFlags_None);
    ImGui::PopStyleColor(1);

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
    ImGui::TextUnformatted("CATEGORIE");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextUnformatted(tabs[m_optionsTab].label);
    ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleColor();
    DrawSeparator();

    // Corps scrollable
    ImGui::BeginChild("##opts_body", {-FLT_MIN, height - 100.f}, false);

    static bool dirty = false;
    static float fov = 70.f; // NOLINT: variables statiques d'état UI intentionnelles
    static bool fullscreen = true, vsync = true;
    static float masterVol = 80.f, musicVol = 60.f, sfxVol = 75.f, voiceVol = 90.f;
    static float uiScale = 100.f, uiOpacity = 90.f;
    static bool showTips = true;

    auto TouchedSlider = [](const char* label, float* val, float mn, float mx, const char* fmt) {
        if (ImGui::SliderFloat(label, val, mn, mx, fmt)) dirty = true;
    };
    auto TouchedCheckbox = [](const char* label, bool* val) {
        if (ImGui::Checkbox(label, val)) dirty = true;
    };

    if (m_optionsTab == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("AFFICHAGE");
        ImGui::PopStyleColor();
        static int resIdx = 2;
        static const char* resolutions[] = {"1280x720","1600x900","1920x1080","2560x1440","3840x2160"};
        if (ImGui::Combo("Resolution", &resIdx, resolutions, 5)) dirty = true;
        static int qualIdx = 2;
        static const char* qualities[] = {"Basse","Moyenne","Haute","Ultra"};
        if (ImGui::Combo("Qualite graphique", &qualIdx, qualities, 4)) dirty = true;
        TouchedSlider("Champ de vision", &fov, 60.f, 120.f, "%.0f deg");
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("MODES");
        ImGui::PopStyleColor();
        TouchedCheckbox("Plein ecran",              &fullscreen);
        TouchedCheckbox("Synchronisation verticale",&vsync);
    }
    else if (m_optionsTab == 1) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("VOLUMES");
        ImGui::PopStyleColor();
        TouchedSlider("Volume maitre", &masterVol, 0.f, 100.f, "%.0f%%");
        TouchedSlider("Musique",       &musicVol,  0.f, 100.f, "%.0f%%");
        TouchedSlider("Effets",        &sfxVol,    0.f, 100.f, "%.0f%%");
        TouchedSlider("Voix",          &voiceVol,  0.f, 100.f, "%.0f%%");
    }
    else if (m_optionsTab == 2) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("SOURIS");
        ImGui::PopStyleColor();
        static float sens = 40.f;
        TouchedSlider("Sensibilite", &sens, 10.f, 100.f, "%.0f%%");
        static bool invertY = false, azerty = true;
        TouchedCheckbox("Inverser axe Y",          &invertY);
        TouchedCheckbox("Disposition ZQSD (AZERTY)",&azerty);
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("RACCOURCIS CLAVIER");
        ImGui::PopStyleColor();
        static const struct { const char* name; const char* key; }
        binds[] = {{"Avancer","Z"},{"Reculer","S"},{"Gauche","Q"},{"Droite","D"},
                   {"Interagir","E"},{"Sort 1","1"},{"Sort 2","2"},{"Inventaire","I"},{"Carte","M"}};
        for (auto& b : binds) {
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
            ImGui::Text("%-16s", b.name);
            ImGui::SameLine(180.f);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::accent);
            ImGui::TextUnformatted(b.key);
            ImGui::PopStyleColor();
        }
    }
    else if (m_optionsTab == 3) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("LANGUE D'AFFICHAGE");
        ImGui::PopStyleColor();
        static int langIdx = 0;
        static const char* langs[] = {"Francais","English"};
        if (ImGui::Combo("Interface et textes", &langIdx, langs, 2)) dirty = true;
    }
    else if (m_optionsTab == 4) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("AFFICHAGE DE L'INTERFACE");
        ImGui::PopStyleColor();
        TouchedSlider("Taille de l'interface",  &uiScale,   80.f, 140.f, "%.0f%%");
        TouchedSlider("Opacite des panneaux",   &uiOpacity, 40.f, 100.f, "%.0f%%");
        TouchedCheckbox("Afficher les infobulles", &showTips);
    }
    else if (m_optionsTab == 5) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("CONNEXION");
        ImGui::PopStyleColor();
        static int srvIdx = 0;
        static const char* servers[] = {"Europe (Morneplaine)","Europe (Korvath)","Automatique"};
        if (ImGui::Combo("Serveur prefere", &srvIdx, servers, 3)) dirty = true;
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::success);
        ImGui::Text("Latence actuelle : 34 ms");
        ImGui::PopStyleColor();
        static bool udpMode = false;
        TouchedCheckbox("Mode gameplay UDP (experimental)", &udpMode);
    }
    else if (m_optionsTab == 6) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("COMPTE CONNECTE");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, LnTheme::surface);
        ImGui::PushStyleColor(ImGuiCol_Border,  LnTheme::border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
        ImGui::BeginChild("##acct_info", {-FLT_MIN, 60.f}, true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::accent);
        ImGui::TextUnformatted("morwenna");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("TAG-ID : MRWN-4F2A-81C7");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted("ACTIONS");
        ImGui::PopStyleColor();
        DrawGhostButton("Changer le mot de passe");
        DrawGhostButton("Changer le courriel");
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4{0.5f,0.1f,0.1f,1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.7f,0.1f,0.1f,1.f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
        ImGui::Button("Se deconnecter##logout", {-FLT_MIN, 32.f});
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);
    }

    ImGui::EndChild(); // opts_body

    // Footer
    DrawSeparator();
    DrawGhostButton("Retour");
    ImGui::SameLine(mainW - 220.f);
    DrawGhostButton("Annuler", !dirty);
    ImGui::SameLine(0.f, 8.f);
    if (DrawPrimaryButton("Appliquer", !dirty)) dirty = false;

    ImGui::EndChild(); // opts_main
}
```

- [ ] **Étape 2 : Implémenter `RenderShardScreen`**

```cpp
void AuthImGuiRenderer::RenderShardScreen(const RenderModel& /*rm*/, ImVec2 vp) {
    DrawBreadcrumb({"Compte","Royaume","Personnage","Entree"}, 1);
    if (!BeginPanel(820.f, vp,
                    "Choisissez votre royaume",
                    "Chaque monde possede sa population, ses regles et ses evenements.",
                    "3 / 4 en ligne"))
    { EndPanel(); return; }

    struct Shard {
        const char* id; const char* name; const char* desc;
        int players; int cap; int ping; // ping=-1 = offline
        bool warn; bool offline;
        const char* event;
    };
    static constexpr Shard shards[] = {
        {"M","Morneplaine","Terres brumeuses, PvE cooperatif.", 1842,3000,28,false,false,"Chasse de la lune noire"},
        {"K","Korvath",    "Forteresse orc, PvP ouvert.",       2734,3000,42,true, false,nullptr},
        {"C","Cendrebois", "Foret maudite, RP semi-hardcore.",   612, 2000,35,false,false,"Festival des ames"},
        {"S","Sous-Ombre", "Maintenance en cours - retour a 21h.", 0,2000,-1,false,true, nullptr},
    };

    for (int i = 0; i < 4; ++i) {
        const Shard& s = shards[i];
        bool isSelected = (m_selectedShard == i);

        ImVec4 rowBorder = isSelected ? LnTheme::accent
                         : s.offline  ? LnTheme::surface
                                      : LnTheme::border;

        if (s.offline) ImGui::BeginDisabled();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, isSelected ? LnTheme::AccentDim(0.06f)
                                                           : LnTheme::surface);
        ImGui::PushStyleColor(ImGuiCol_Border, rowBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   6.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);
        char rowId[16]; std::snprintf(rowId, sizeof(rowId), "##shard%d", i);
        ImGui::BeginChild(rowId, {-FLT_MIN, 64.f}, true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        // Initiale
        ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? LnTheme::accent : LnTheme::muted);
        ImGui::SetWindowFontScale(1.4f);
        ImGui::SetCursorPosY(14.f);
        ImGui::Text("  %s", s.id);
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();

        // Nom + description
        ImGui::SameLine(60.f);
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
        ImGui::TextUnformatted(s.name);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextUnformatted(s.desc);
        ImGui::PopStyleColor();
        if (s.event) {
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::accent);
            ImGui::Text("★ %s", s.event);
            ImGui::PopStyleColor();
        }
        ImGui::EndGroup();

        // Barre de charge (droite)
        float loadFrac = s.cap > 0 ? (float)s.players / (float)s.cap : 0.f;
        ImVec4 barCol = loadFrac > 0.85f ? LnTheme::warning : LnTheme::success;
        ImGui::SameLine(ImGui::GetWindowWidth() - 220.f);
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::Text("Charge  %.0f%%", loadFrac * 100.f);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barCol);
        char loadId[16]; std::snprintf(loadId, sizeof(loadId), "##load%d", i);
        ImGui::ProgressBar(loadFrac, {120.f, 6.f}, "");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::Text("%d / %d", s.players, s.cap);
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        // Ping
        ImGui::SameLine(ImGui::GetWindowWidth() - 90.f);
        if (s.ping >= 0) {
            ImVec4 pingCol = s.ping < 40 ? LnTheme::success
                           : s.ping < 80 ? LnTheme::warning : LnTheme::error_col;
            ImGui::PushStyleColor(ImGuiCol_Text, pingCol);
            ImGui::Text("%d ms", s.ping);
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
            ImGui::TextUnformatted("--");
            ImGui::PopStyleColor();
        }

        // Statut
        ImGui::SameLine(ImGui::GetWindowWidth() - 45.f);
        ImVec4 statusCol = s.offline ? LnTheme::error_col
                         : s.warn    ? LnTheme::warning : LnTheme::success;
        ImGui::PushStyleColor(ImGuiCol_Text, statusCol);
        ImGui::TextUnformatted(s.offline ? "Hors ligne" : s.warn ? "Sature" : "En ligne");
        ImGui::PopStyleColor();

        // Clic
        ImGui::SetCursorPos({0.f, 0.f});
        char invId[16]; std::snprintf(invId, sizeof(invId), "##sinv%d", i);
        ImGui::InvisibleButton(invId, {ImGui::GetWindowWidth(), 64.f});
        if (ImGui::IsItemClicked() && !s.offline) m_selectedShard = i;

        ImGui::EndChild();
        if (s.offline) ImGui::EndDisabled();
        ImGui::Spacing();
    }

    DrawGhostButton("Retour");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.f);
    DrawKeycapHints({{"↑↓","naviguer"}});
    ImGui::SameLine(0.f, 12.f);
    DrawPrimaryButton("Entrer dans le monde");

    EndPanel();
}
```

- [ ] **Étape 3 : Compiler**

```bash
cmake --build build --target engine_core -- -j4
```

Résultat attendu : 0 erreur.

- [ ] **Étape 4 : Commit**

```bash
git add engine/render/AuthImGuiRenderer.cpp
git commit -m "feat: implement options + shard pick screens"
```

---

## Task 7 : Écrans stubs — ForgotPassword, Terms, CharCreate

**Fichiers :**
- Modifier : `engine/render/AuthImGuiRenderer.cpp` (remplacer 3 derniers stubs)

- [ ] **Étape 1 : Implémenter `RenderForgotScreen`**

```cpp
void AuthImGuiRenderer::RenderForgotScreen(const RenderModel& rm, ImVec2 vp) {
    (void)rm;
    if (!BeginPanel(460.f, vp, "Mot de passe oublie", {}, {})) { EndPanel(); return; }
    static char forgotEmail[128] = {};
    DrawField("Adresse courriel", forgotEmail, sizeof(forgotEmail));
    DrawPrimaryButton("Envoyer le lien");
    ImGui::SameLine(0.f, 8.f);
    DrawGhostButton("Retour");
    EndPanel();
}
```

- [ ] **Étape 2 : Implémenter `RenderTermsScreen`**

```cpp
void AuthImGuiRenderer::RenderTermsScreen(const RenderModel& rm, ImVec2 vp) {
    if (!BeginPanel(560.f, vp, "Conditions d'utilisation", {}, {})) { EndPanel(); return; }
    ImGui::BeginChild("##terms_body", {-FLT_MIN, 300.f}, false);
    if (rm.bodyLines.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::muted);
        ImGui::TextWrapped("(Le texte des conditions sera fourni par le Presenter.)");
        ImGui::PopStyleColor();
    } else {
        for (const auto& line : rm.bodyLines) {
            ImGui::PushStyleColor(ImGuiCol_Text, LnTheme::text);
            ImGui::TextWrapped("%s", line.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    DrawSeparator();
    DrawGhostButton("Refuser");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.6f);
    DrawPrimaryButton("Accepter");
    EndPanel();
}
```

- [ ] **Étape 3 : Implémenter `RenderCharCreateScreen`**

```cpp
void AuthImGuiRenderer::RenderCharCreateScreen(const RenderModel& rm, ImVec2 vp) {
    const std::string& title = rm.titleLine1.empty() ? std::string("Creation de personnage") : rm.titleLine1;
    if (!BeginPanel(680.f, vp, title, rm.titleLine2, {})) { EndPanel(); return; }

    // Champs générés depuis rm.fields
    static std::vector<std::vector<char>> fieldBufs; // non-membre car rm.fields peut varier
    if (fieldBufs.size() != rm.fields.size()) {
        fieldBufs.resize(rm.fields.size());
        for (auto& b : fieldBufs) b.assign(128, '\0');
    }
    for (size_t i = 0; i < rm.fields.size(); ++i)
        DrawField(rm.fields[i].label, fieldBufs[i].data(), 128);

    // Actions depuis rm.actions
    for (size_t i = 0; i < rm.actions.size(); ++i) {
        const auto& a = rm.actions[i];
        if (a.kind == engine::client::AuthUiPresenter::RenderAction::Kind::Primary)
            DrawPrimaryButton(a.label);
        else
            DrawGhostButton(a.label);
        if (i + 1 < rm.actions.size()) ImGui::SameLine(0.f, 8.f);
    }
    EndPanel();
}
```

> **Note :** `RenderAction::Kind` et `RenderField::label` — vérifier les noms exacts dans `engine/client/AuthUi.h` et adapter si nécessaire.

- [ ] **Étape 4 : Compiler**

```bash
cmake --build build --target engine_core -- -j4
```

Résultat attendu : 0 erreur.

- [ ] **Étape 5 : Commit + push final**

```bash
git add engine/render/AuthImGuiRenderer.cpp
git commit -m "feat: implement forgot-password, terms, char-create screens"
git push
```

---

## Vérification manuelle (après push)

Une fois l'intégration complète, lancer le jeu en mode client (sans `--editor`) et vérifier :

1. **Écran langue** : fond sombre, hero centré, 2 cartes avec placeholder, carte sélectionnée en or, bouton Continuer actif.
2. **Écran login** : panel 460px, 2 champs, toggle, 3 boutons.
3. **Build Linux** : `cmake --build build_linux --target engine_core -- -j4` sans erreur.
4. **Pas de régression éditeur** : lancer avec `--editor`, vérifier que le world editor s'affiche normalement.

---

## Notes importantes

- `AuthUiRenderer.h/.cpp` ne doivent **pas** être supprimés dans cette PR.
- Ne jamais modifier les fichiers serveur ou gameplay.
- `LnTheme::error_col` est nommé `error_col` (pas `error`) pour éviter le conflit avec `std::error`.
- Les noms `RenderAction::Kind::Primary`, `RenderField::label`, `RenderBodyLine::text` doivent être vérifiés dans `engine/client/AuthUi.h` avant Task 7 — adapter si les noms réels diffèrent.
