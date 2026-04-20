# Auth ImGui Renderer — Design Spec
**Date :** 2026-04-19  
**Branche :** `claude/organize-ui-design-system-P9EAx`  
**Repo :** `tips-of-mine/lcdlln-test`

---

## Contexte

L'UI d'authentification (sélection de langue, login, inscription, etc.) est actuellement rendue via des `VkClearRect` calculés à la main dans `engine/render/AuthUiRenderer.cpp` (764 lignes). Le résultat est visuellement incorrect et impossible à maintenir. Dear ImGui est déjà intégré et compilé dans le projet (utilisé par `WorldEditorImGui`).

**Objectif :** migrer l'UI auth vers ImGui en créant `AuthImGuiRenderer` qui reproduit fidèlement le design system `lune-noire-design-system/project/ui_kits/auth_flow/`.

---

## Décisions de conception

| Question | Décision |
|---|---|
| Init ImGui | Inconditionnelle (client + éditeur) |
| Périmètre | Tous les écrans en une PR |
| Layouts | Détaillés dans la spec, validés avant code |
| VisualState manquants | Ajouter `options` et `shardPick` à `VisualState` |
| Structure renderer | Classe `AuthImGuiRenderer` avec état transient explicite (pattern `WorldEditorImGui`) |

---

## Fichiers créés / modifiés

| Fichier | Action | Rôle |
|---|---|---|
| `engine/render/LnTheme.h` | CRÉÉ | Tokens CSS → `ImVec4` + `ImU32` |
| `engine/render/AuthImGuiRenderer.h` | CRÉÉ | Déclaration classe `AuthImGuiRenderer` |
| `engine/render/AuthImGuiRenderer.cpp` | CRÉÉ | Implémentation — 10 méthodes d'écran |
| `engine/client/AuthUi.h` | MODIFIÉ | +2 flags `VisualState`: `options`, `shardPick` |
| `engine/Engine.cpp` | MODIFIÉ | Init ImGui inconditionnelle + appel `Render()` |
| `CMakeLists.txt` | MODIFIÉ | +`LnTheme.h`, +`AuthImGuiRenderer.cpp` |
| `engine/render/AuthUiRenderer.h/.cpp` | CONSERVÉ | Temporairement — ne pas supprimer |

---

## Section 1 — `LnTheme.h`

Header-only, `namespace LnTheme`. Seule source de vérité pour toutes les couleurs de l'UI. Dérivé intégralement de `design/lune-noire-design-system/project/colors_and_type.css`.

### Tokens couleur (thème par défaut)

```cpp
namespace LnTheme {
  inline constexpr ImVec4 primary    {0.290f, 0.482f, 0.722f, 1.f}; // #4A7BB8
  inline constexpr ImVec4 secondary  {0.361f, 0.420f, 0.549f, 1.f}; // #5C6B8C
  inline constexpr ImVec4 accent     {0.910f, 0.773f, 0.431f, 1.f}; // #E8C56E
  inline constexpr ImVec4 background {0.039f, 0.051f, 0.071f, 1.f}; // #0A0D12
  inline constexpr ImVec4 surface    {0.071f, 0.094f, 0.133f, 1.f}; // #121822
  inline constexpr ImVec4 panel      {0.078f, 0.110f, 0.157f, 1.f}; // #141C28
  inline constexpr ImVec4 text       {0.949f, 0.957f, 0.973f, 1.f}; // #F2F4F8
  inline constexpr ImVec4 muted      {0.608f, 0.659f, 0.722f, 1.f}; // #9BA8B8
  inline constexpr ImVec4 border     {0.239f, 0.310f, 0.400f, 1.f}; // #3D4F66

  inline constexpr ImVec4 success    {0.373f, 0.722f, 0.431f, 1.f}; // #5FB86E
  inline constexpr ImVec4 warning    {0.910f, 0.647f, 0.361f, 1.f}; // #E8A55C
  inline constexpr ImVec4 error      {0.769f, 0.251f, 0.251f, 1.f}; // #C44040

  inline ImVec4 PanelBg(float a = 0.72f)  { return {0.078f, 0.110f, 0.157f, a}; }
  inline ImVec4 AccentDim(float a = 0.10f) { return {0.910f, 0.773f, 0.431f, a}; }
  inline ImVec4 BorderActive()             { return accent; }

  inline ImU32 U32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }
}
```

Pas de race theming dans cette PR — `LnTheme` couvre uniquement le thème par défaut.

---

## Section 2 — `AuthImGuiRenderer`

### Classe

```cpp
namespace engine::render {

class AuthImGuiRenderer {
public:
    void Render(const engine::client::AuthUiPresenter::VisualState&,
                const engine::client::AuthUiPresenter::RenderModel&,
                ImVec2 viewport);
    void Reset();

private:
    // État transient par écran
    int  m_selectedLang  = 0;
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

    // Écrans
    void RenderLangScreen      (const RenderModel&, ImVec2);
    void RenderLoginScreen     (const RenderModel&, ImVec2);
    void RenderRegisterScreen  (const RenderModel&, ImVec2);
    void RenderErrorScreen     (const RenderModel&, ImVec2);
    void RenderVerifyScreen    (const RenderModel&, ImVec2);
    void RenderOptionsScreen   (const RenderModel&, ImVec2);
    void RenderShardScreen     (const RenderModel&, ImVec2);
    void RenderForgotScreen    (const RenderModel&, ImVec2);
    void RenderTermsScreen     (const RenderModel&, ImVec2);
    void RenderCharCreateScreen(const RenderModel&, ImVec2);

    // Helpers partagés
    void BeginFullscreenOverlay(ImVec2 viewport);
    void DrawPanel(float width, float posX, float posY,
                   std::string_view title, std::string_view subtitle);
    void EndPanel();
    void DrawLangCard(int idx, std::string_view label, bool selected, ImVec2 cardSize);
    void DrawField(std::string_view label, char* buf, int bufSz, bool password = false);
    void DrawBanner(std::string_view title, std::string_view msg, ImVec4 color);
    void DrawKeycapHint(std::string_view key, std::string_view label);
    void DrawPrimaryButton(std::string_view label, bool disabled = false);
    void DrawGhostButton(std::string_view label);
};

} // namespace engine::render
```

### Dispatch

```cpp
void AuthImGuiRenderer::Render(const VisualState& vs, const RenderModel& rm, ImVec2 vp) {
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
    ImGui::End();
}
```

---

## Section 3 — Layouts des écrans

Tous les layouts sont dérivés de `Screens1to4.jsx`, `Screens5to7.jsx` et `primitives.jsx`.  
La barre de navigation en bas de `index.html` est un artefact de la présentation du design kit — elle n'est pas rendue en jeu.

### Écran 1 — Sélection de langue (`languageSelection`)
- **Fond :** `LnTheme::background` couvrant tout le viewport (`BeginFullscreenOverlay`)
- **Hero :** deux lignes centrées en haut — "Les Chroniques" / "de la Lune Noire" — police ImGui par défaut (Windlass n'est pas chargée dans ImGui dans cette PR), couleur `LnTheme::text`, taille via `ImGui::SetWindowFontScale`
- **Panel :** centré, ~720px large, fond `PanelBg(0.72)`, border `LnTheme::border`
  - Header : titre "Choisissez votre langue" + sous-titre dynamique (`rm.sectionTitle` ou "Bienvenue, voyageur.")
  - Version label "1 / 2" (droite du header)
  - Deux lang-cards côte à côte (Français / English) :
    - Placeholder drapeau : rectangle 54×38px, couleur `LnTheme::surface`
    - Label langue en dessous
    - Border `LnTheme::border`; si sélectionnée → border `LnTheme::accent`
  - Bouton "Continuer →" `kind=primary`, aligné à droite
  - Footer : keycap hints "← → naviguer" + "↵ valider"

### Écran 2 — Connexion (`login`)
- **Panel :** ~460px
- Hero identique à écran 1
- Champs : Identifiant (text), Mot de passe (password)
- Toggle "Se souvenir de moi" (`m_rememberMe`)
- Bannière erreur si `rm.errorText` non vide
- Bannière info si `vs.submitting`
- Actions : bouton text "Mot de passe oublié ?", bouton ghost "Créer un compte", bouton primary "Se connecter"
- Liens secondaires sous le panel : "⚙ Options" + "Quitter"
- Footer keycaps : "Tab champ suivant" + "↵ se connecter" + "Échap quitter"

### Écran 3 — Inscription (`registerMode`)
- **Panel :** ~680px
- Breadcrumb 4 étapes (Langue / Compte / Courriel / Monde), étape active = 1
- Grille 2 colonnes : Identifiant (span 2), Courriel (span 2), Mot de passe + barre de force (col 1), Confirmation (col 2), Dropdowns Jour/Mois/Année
- Barre de force pw : 4 segments colorés (faible = `error`, moyen = `warning`, fort = `success`)
- Actions : bouton ghost "Retour", bouton primary "Créer le compte" (disabled si validation échoue)
- Footer keycaps : "Tab / ↵ / Échap"

### Écran 4 — Erreurs (`error`)
- **Panel :** ~640px
- Bannière erreur/warning (`rm.errorText` → titre ; `rm.infoBanner` → détail)
- Bloc "Champ à corriger" : fond `surface`, label `muted`, valeur `accent`
- Bloc "Comment corriger" : border-left `accent`, fond `AccentDim(0.04)`, texte italic `text`
- Bouton ghost "Retour au formulaire"
- Si erreur réseau → bouton primary "Réessayer" supplémentaire

### Écran 5 — Vérification courriel (`verifyEmail`)
- **Panel :** ~560px
- Breadcrumb étape 2
- Sous-titre avec adresse mail (`rm.sectionTitle`)
- 6 `InputText` d'un caractère chacun, centrés, police mono, 56×64px, focus auto-avance
- Liens text "Renvoyer le code" + "Modifier le courriel"
- Actions : bouton ghost "Retour", bouton primary "Valider le code" (disabled si < 6 chiffres)

### Écran 6 — Options (`options`)
- **Layout fullscreen split** : sidebar 220px + panneau principal
- Sidebar : titre "Options", 7 tabs (Graphismes / Son / Contrôles / Langue / Interface / Réseau / Compte)
- Tab actif = `m_optionsTab`
- Panneau principal : header catégorie + body scrollable + footer (Annuler / Appliquer)
- Contenu par tab :
  - **Graphismes :** Dropdown résolution, Dropdown qualité, Slider FOV, Toggle plein écran, Toggle vsync
  - **Son :** 4 Sliders (Maître, Musique, Effets, Voix)
  - **Contrôles :** Slider sensibilité, 2 Toggles, liste raccourcis clavier (9 entrées)
  - **Langue :** Dropdown interface+textes
  - **Interface :** Slider taille, Slider opacité, Toggle infobulles
  - **Réseau :** Dropdown serveur préféré, latence courante (mono), Toggle UDP
  - **Compte :** bloc identifiant+TAG-ID, boutons Changer pw / Changer courriel / Se déconnecter

### Écran 7 — Choix du royaume (`shardPick`)
- **Panel :** ~820px
- Breadcrumb 4 étapes (Compte / Royaume / Personnage / Entrée), étape 1
- Liste de shards hard-codée (4 entrées fixes : Morneplaine, Korvath, Cendrebois, Sous-Ombre) dans cette PR — le Presenter n'expose pas encore ces données via `RenderModel` :
  - Chaque rangée : initiale du shard (lettre), nom + description, barre de charge colorée, nb joueurs, ping (mono), statut
  - Shard sélectionné → border `LnTheme::accent`
  - Shard hors ligne → grisé, non sélectionnable
- Actions : bouton ghost "Retour", keycap "↑↓ naviguer", bouton primary "Entrer dans le monde"

### Écrans stub (layout minimal, piloté par `RenderModel`)
- **Mot de passe oublié** (`forgotPassword`) : panel 460px, 1 field email, bouton primary "Envoyer", bouton ghost "Retour"
- **CGU** (`terms`) : panel 560px, zone scrollable `rm.bodyLines`, boutons Accepter / Refuser
- **Création de personnage** (`characterCreate`) : panel 680px, champs générés depuis `rm.fields`, actions depuis `rm.actions`

---

## Section 4 — Intégration `Engine.cpp`

### Nouveau membre

```cpp
std::unique_ptr<engine::render::AuthImGuiRenderer> m_authImGui;
```

### Init (dans `Engine::Update()`)

```cpp
if (!m_imguiInitialized
    && m_vkDeviceContext.IsValid() && m_vkSwapchain.IsValid()
    && m_window.GetNativeHandle() != nullptr
    && m_vkDeviceContext.SupportsDynamicRendering()) {

    if (m_worldEditorExe) {
        m_worldEditorImGui = std::make_unique<engine::editor::WorldEditorImGui>();
        // ... init existante inchangée
    }
    m_authImGui = std::make_unique<engine::render::AuthImGuiRenderer>();
    m_imguiInitialized = true;
}
```

### NewFrame

```cpp
bool needImguiFrame = (m_worldEditorExe && m_worldEditorImGui && m_worldEditorImGui->IsReady())
                   || (m_authImGui && authVisualState.active);
if (needImguiFrame) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}
```

### BuildUi / Render

```cpp
if (m_worldEditorExe && m_worldEditorImGui)
    m_worldEditorImGui->BuildUi(&overlay);

if (m_authImGui && authVisualState.active)
    m_authImGui->Render(authVisualState, authRenderModel,
                        {(float)ext.width, (float)ext.height});
```

`RecordToBackbuffer` de `WorldEditorImGui` est inchangé — il consomme toutes les draw data ImGui (éditeur + auth).

---

## Section 5 — `VisualState` — ajouts

Dans `engine/client/AuthUi.h`, struct `VisualState` :

```cpp
bool options   = false;   // écran Options
bool shardPick = false;   // écran Choix du royaume
```

---

## Section 6 — `CMakeLists.txt`

Deux lignes ajoutées dans la liste des sources cibles `engine` :

```cmake
engine/render/LnTheme.h
engine/render/AuthImGuiRenderer.cpp
```

---

## Contraintes de build

- `AuthUiRenderer.h/.cpp` conservés (ne pas supprimer)
- APIs ImGui standard uniquement (pas de libs tierces)
- Build doit passer sur Linux et Windows
- Commit + push après chaque étape terminée sur `claude/organize-ui-design-system-P9EAx`
- Ne jamais modifier les fichiers serveur ou gameplay

---

## Ordre d'implémentation suggéré

1. `LnTheme.h` + CMakeLists → commit
2. `VisualState` +2 flags → commit
3. `AuthImGuiRenderer.h/.cpp` — stub + `RenderLangScreen` → commit
4. `Engine.cpp` — init + branchement → commit + push
5. Écrans 2 à 5 (login, register, error, verify) → commit
6. Écrans 6 à 7 (options, shard) → commit
7. Écrans stubs (forgot, terms, charCreate) → commit + push final
