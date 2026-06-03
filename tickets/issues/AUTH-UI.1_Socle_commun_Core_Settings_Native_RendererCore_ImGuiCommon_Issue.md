# Issue: AUTH-UI.1

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/client/auth/AuthUiPresenterCore.cpp
- src/client/render/auth/AuthUiRendererCore.cpp

## Note
Socle commun Core/Settings/Native/Renderer

---

## Contenu du ticket (AUTH-UI.1)

# AUTH-UI.1 — Extraction du socle commun (Core + Settings + Native + RendererCore + ImGuiCommon)

## Dépendances
- AUTH-UI.0 (plan validé)
- STAB.13 (AuthUi.cpp existant)

## Objectif

Extraire de `AuthUi.cpp` (6 370 lignes) et `AuthUiRenderer.cpp` (1 057 lignes) les **fonctions partagées** dans des fichiers séparés, puis **supprimer les deux fichiers monolithiques**. À l'issue de ce ticket, le build doit passer sans `AuthUi.cpp` ni `AuthUiRenderer.cpp`.

Les méthodes spécifiques à chaque écran sont temporairement déplacées dans des stubs vides (corps `{ /* AUTH-UI.N */ }`) — ils seront remplis dans les tickets AUTH-UI.2 à AUTH-UI.11.

---

## Découpage — partie PRESENTER

### `engine/client/auth/AuthUiPresenterCore.cpp`

Méthodes à y placer (extraites depuis `AuthUi.cpp`) :

| Méthode | Rôle |
|---|---|
| `AuthUiPresenter::~AuthUiPresenter()` | Destructeur |
| `AuthUiPresenter::Init()` | Chargement config, init localization, démarrage sonde status |
| `AuthUiPresenter::Shutdown()` | Join worker, cleanup |
| `AuthUiPresenter::Update()` | Dispatch par phase (appelle Update_Login, Update_Register, etc.) |
| `AuthUiPresenter::BuildRenderModel()` | Switch/case sur m_phase → appelle BuildModel_XXX |
| `AuthUiPresenter::BuildPanelText()` | Texte HUD multi-lignes |
| `AuthUiPresenter::GetVisualState()` | Flags visuels agrégés |
| `AuthUiPresenter::PollAsyncResult()` | Lecture résultat worker + transitions de phase |
| `AuthUiPresenter::JoinWorker()` | Join du thread worker |
| `AuthUiPresenter::ResetMasterSession()` | Reset m_masterClient + m_masterSessionId |
| `AuthUiPresenter::OnEscape()` | Navigation retour / annulation |
| `AuthUiPresenter::BlocksWorldInput()` | true tant que flow auth actif |
| `AuthUiPresenter::SetViewportSize()` | Mise à jour m_viewportW/H |
| `AuthUiPresenter::BypassAuthGateForWorldEditor()` | Bypass flow pour l'éditeur |
| `AuthUiPresenter::Tr()` | Traduction localisée (délègue à m_localization) |
| `AuthUiPresenter::PhaseLogName()` | Nom de phase pour les logs |
| `AuthUiPresenter::AppendPasswordStars()` | Masquage mot de passe |
| `AuthUiPresenter::EnsurePasswordSalt()` | Génération/chargement sel Argon2 |
| `AuthUiPresenter::ComputeClientHash()` | Hash Argon2 du mot de passe client |
| `AuthUiPresenter::UpdateWindowTitle()` | SetTitle sur la fenêtre (Windows) |
| `AuthUiPresenter::ResolveActionButtonLabels()` | Résolution libellés i18n des boutons |
| `AuthUiPresenter::LoadRememberPreference()` | Lecture préférence "se souvenir" |
| `AuthUiPresenter::SaveRememberPreference()` | Écriture préférence "se souvenir" |
| `AuthUiPresenter::CurrentLocale()` | Tag locale courant |
| `AuthUiPresenter::LocalizedLanguageName()` | Nom de langue localisé |
| `AuthUiPresenter::StartStatusProbeWorker()` | Sonde périodique disponibilité serveur |
| `AuthUiPresenter::GetAuthLogoRotationRadians()` | Angle logo (inline, reste dans .h) |
| `AuthUiPresenter::ConsumePending*()` | Déjà dans Settings (voir ci-dessous) |

### `engine/client/auth/AuthUiPresenterSettings.cpp`

| Méthode |
|---|
| `ConsumePendingVideoSettings()` |
| `ConsumePendingAudioSettings()` |
| `ConsumePendingControlSettings()` |
| `ConsumePendingGameSettings()` |
| `BuildLanguageOptionsImGuiMirror()` |
| `ImGuiApplyLanguageOptionsMenu()` |
| `ImGuiCloseLanguageOptionsWithoutApply()` |
| `CommitLanguageOptionsMenuApply()` |
| `OpenLanguageOptions()` |
| `OptionsSubmenuLineCount()` |
| `EnterOptionsSubmenuFromRoot()` |

### `engine/client/auth/AuthUiPresenterNative.cpp`

| Méthode |
|---|
| `HandleNativeAuthScreen()` (Win32 form natif, bloc `#if LCDLLN_PLATFORM_WINDOWS`) |

### Stubs vides (un par écran, à compléter par AUTH-UI.2 à AUTH-UI.11)

Créer `engine/client/auth/screens/AuthScreen{Login,Register,VerifyEmail,ForgotPassword,Error,Terms,CharacterCreate,ShardPick,LanguageSelect,Options}.cpp`, chacun contenant une fonction stub vide :

```cpp
// AuthScreenLogin.cpp
#include "engine/client/AuthUi.h"
// Méthodes Login — implémentées dans AUTH-UI.2
// void AuthUiPresenter::ImGuiSubmitLogin(...) { /* AUTH-UI.2 */ }
// etc.
```

Aucune méthode publique ne doit être absente après le split : toutes les méthodes de `AuthUi.h` doivent avoir exactement une définition dans l'un des `.cpp`.

---

## Découpage — partie RENDERER

### `engine/render/auth/AuthUiRendererCore.cpp`

Extraire depuis `AuthUiRenderer.cpp` :

- Constructeur/Destructeur de `AuthUiRenderer`
- `Init()`, `Shutdown()`
- `BuildAuthUiLayoutMetrics()`
- `DrawBackground()` / `DrawBackdrop()`
- `AuthGlyphPass()` (Vulkan render pass auth)
- Dispatch par écran (appelle `DrawImGui_XXX()`)
- Toutes les fonctions utilitaires de dessin bas niveau

### `engine/render/auth/AuthImGuiCommon.cpp`

Primitives ImGui partagées entre tous les écrans, portées depuis `primitives.jsx` du design system :

| Primitive design | Équivalent ImGui C++ |
|---|---|
| `<Panel>` | `DrawAuthPanel()` — cadre semi-transparent, titre 2 lignes, sous-titre, badge version, icône "i", footer |
| `<Field>` | `DrawAuthField()` — label + InputText + placeholder + indicateur status (ok/err/pending) + statusMsg + tooltip |
| `<Button kind="primary\|ghost\|text\|danger">` | `DrawAuthButton()` — style + keycap badge |
| `<Banner kind="error\|warning\|info">` | `DrawAuthBanner()` — fond coloré + icône + titre + message |
| `<KeycapHint>` | `DrawAuthKeycapHint()` — puce clavier + libellé |
| `<Breadcrumb>` | `DrawAuthBreadcrumb()` — fil d'étapes avec état current/done/pending |
| `<Toggle>` | `DrawAuthToggle()` — checkbox stylée + label + hint |
| `<Slider>` | `DrawAuthSlider()` — curseur + label + valeur + unité |
| `<Dropdown>` | `DrawAuthDropdown()` — combo stylé + label + tooltip |
| `FlagSVG` | `DrawFlagFR()` / `DrawFlagEN()` — drapeaux SVG portés en primitives ImGui DrawList |

### Stubs renderer (un par écran)

Créer `engine/render/auth/screens/AuthImGui{LanguageSelect,Login,Register,Error,VerifyEmail,Options,ShardPick}.cpp` avec des stubs vides — à compléter par AUTH-UI.2 à AUTH-UI.8.

---

## CMakeLists.txt

### Retirer
```cmake
engine/client/AuthUi.cpp
engine/render/AuthUiRenderer.cpp
```

### Ajouter dans `engine_core`
```cmake
engine/client/auth/AuthUiPresenterCore.cpp
engine/client/auth/AuthUiPresenterSettings.cpp
engine/client/auth/AuthUiPresenterNative.cpp
engine/client/auth/screens/AuthScreenLogin.cpp
engine/client/auth/screens/AuthScreenRegister.cpp
engine/client/auth/screens/AuthScreenVerifyEmail.cpp
engine/client/auth/screens/AuthScreenForgotPassword.cpp
engine/client/auth/screens/AuthScreenError.cpp
engine/client/auth/screens/AuthScreenTerms.cpp
engine/client/auth/screens/AuthScreenCharacterCreate.cpp
engine/client/auth/screens/AuthScreenShardPick.cpp
engine/client/auth/screens/AuthScreenLanguageSelect.cpp
engine/client/auth/screens/AuthScreenOptions.cpp
engine/render/auth/AuthUiRendererCore.cpp
engine/render/auth/AuthImGuiCommon.cpp
engine/render/auth/screens/AuthImGuiLanguageSelect.cpp
engine/render/auth/screens/AuthImGuiLogin.cpp
engine/render/auth/screens/AuthImGuiRegister.cpp
engine/render/auth/screens/AuthImGuiError.cpp
engine/render/auth/screens/AuthImGuiVerifyEmail.cpp
engine/render/auth/screens/AuthImGuiOptions.cpp
engine/render/auth/screens/AuthImGuiShardPick.cpp
```

### Supprimer physiquement du repo
```
engine/client/AuthUi.cpp
engine/render/AuthUiRenderer.cpp
```

---

## Livrables

**Créés :**
- `engine/client/auth/AuthUiPresenterCore.cpp`
- `engine/client/auth/AuthUiPresenterSettings.cpp`
- `engine/client/auth/AuthUiPresenterNative.cpp`
- `engine/client/auth/screens/AuthScreen{Login,Register,VerifyEmail,ForgotPassword,Error,Terms,CharacterCreate,ShardPick,LanguageSelect,Options}.cpp` (stubs)
- `engine/render/auth/AuthUiRendererCore.cpp`
- `engine/render/auth/AuthImGuiCommon.cpp`
- `engine/render/auth/screens/AuthImGui{LanguageSelect,Login,Register,Error,VerifyEmail,Options,ShardPick}.cpp` (stubs)

**Supprimés :**
- `engine/client/AuthUi.cpp`
- `engine/render/AuthUiRenderer.cpp`

**Modifiés :**
- `CMakeLists.txt`
- `engine/client/AuthUi.h` (si ajout d'un include guard ou forward déclaration nécessaire, minimal)

---

## Definition of Done

- [ ] Build Windows (configure + build) — **aucune erreur de compilation**
- [ ] Toutes les méthodes déclarées dans `AuthUi.h` ont exactement une définition (aucun ODR, aucun symbole manquant au link)
- [ ] `engine/client/AuthUi.cpp` et `engine/render/AuthUiRenderer.cpp` **n'existent plus** dans le repo
- [ ] Le binaire `lcdlln.exe` se lance et affiche l'écran auth (comportement identique à STAB.13)
- [ ] Aucun chemin absolu introduit
- [ ] Rapport final : fichiers créés/supprimés/modifiés + commandes + résultats
