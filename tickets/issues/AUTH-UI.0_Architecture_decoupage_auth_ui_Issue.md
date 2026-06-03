# Issue: AUTH-UI.0

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- tickets/UI/AUTH-UI.0_Architecture_decoupage_auth_ui.md

## Note
Architecture decoupage (doc)

---

## Contenu du ticket (AUTH-UI.0)

# AUTH-UI.0 — Découpage architectural : AuthUi.cpp → un fichier par écran

## Contexte

Suite à STAB.13, `AuthUiPresenter` est entièrement implémenté dans deux fichiers monolithiques :

| Fichier | Lignes |
|---|---|
| `engine/client/AuthUi.cpp` | 6 370 |
| `engine/render/AuthUiRenderer.cpp` | 1 057 |

Les 12 phases (Login, Register, VerifyEmail, EmailConfirmationPending, ForgotPassword, Terms, CharacterCreate, ShardPick, LanguageSelectionFirstRun, LanguageOptions, Submitting, Error) y cohabitent sans séparation structurelle.

Ce ticket définit la **cible architecturale** à atteindre par les tickets AUTH-UI.1 à AUTH-UI.8. Il ne modifie pas de code : il produit uniquement le plan et les règles de découpage.

---

## Objectif

Passer d'une architecture monolithique à une architecture **un fichier par écran**, sans modifier le header `AuthUi.h` ni le comportement observable du jeu.

---

## Structure cible

### Presenter (`engine/client/`)

```
engine/client/
├── AuthUi.h                                    ← INCHANGÉ (déclaration de classe complète)
├── auth/
│   ├── AuthUiPresenterCore.cpp                 ← Init, Shutdown, ~AuthUiPresenter, Update (dispatch),
│   │                                              BuildRenderModel (dispatch), BuildPanelText,
│   │                                              GetVisualState, PollAsyncResult, JoinWorker,
│   │                                              ResetMasterSession, BlocksWorldInput,
│   │                                              OnEscape, SetViewportSize,
│   │                                              BypassAuthGateForWorldEditor,
│   │                                              Tr, PhaseLogName, AppendPasswordStars,
│   │                                              ComputeClientHash, EnsurePasswordSalt,
│   │                                              UpdateWindowTitle, ResolveActionButtonLabels,
│   │                                              LoadRememberPreference, SaveRememberPreference,
│   │                                              CurrentLocale, LocalizedLanguageName,
│   │                                              StartStatusProbeWorker
│   │
│   ├── AuthUiPresenterSettings.cpp             ← ConsumePendingVideoSettings,
│   │                                              ConsumePendingAudioSettings,
│   │                                              ConsumePendingControlSettings,
│   │                                              ConsumePendingGameSettings,
│   │                                              BuildLanguageOptionsImGuiMirror,
│   │                                              ImGuiApplyLanguageOptionsMenu,
│   │                                              ImGuiCloseLanguageOptionsWithoutApply,
│   │                                              CommitLanguageOptionsMenuApply,
│   │                                              OpenLanguageOptions,
│   │                                              OptionsSubmenuLineCount,
│   │                                              EnterOptionsSubmenuFromRoot
│   │
│   ├── AuthUiPresenterNative.cpp               ← HandleNativeAuthScreen (Win32 form natif)
│   │
│   └── screens/
│       ├── AuthScreenLanguageSelect.cpp        ← Phase::LanguageSelectionFirstRun
│       │                                          (BuildModel + Update + ImGuiApplyFirstRunLanguageContinue)
│       ├── AuthScreenLogin.cpp                 ← Phase::Login
│       │                                          (BuildModel + Update + StartLoginWorker + ImGuiLogin*)
│       ├── AuthScreenRegister.cpp              ← Phase::Register
│       │                                          (BuildModel + Update + StartRegisterWorker
│       │                                          + StartUsernameCheckWorker + ImGuiRegister*)
│       ├── AuthScreenVerifyEmail.cpp           ← Phase::VerifyEmail + Phase::EmailConfirmationPending
│       │                                          (BuildModel + Update + StartVerifyEmailWorker
│       │                                          + ImGuiVerifyEmail*)
│       ├── AuthScreenForgotPassword.cpp        ← Phase::ForgotPassword
│       │                                          (BuildModel + Update + StartForgotPasswordWorker
│       │                                          + ImGuiNavigateToForgotFromLogin
│       │                                          + ImGuiSubmitForgotPassword + ImGuiBackFromForgotToLogin)
│       ├── AuthScreenError.cpp                 ← Phase::Error
│       │                                          (BuildModel + ImGuiAcknowledgeErrorScreen)
│       ├── AuthScreenTerms.cpp                 ← Phase::Terms
│       │                                          (BuildModel + Update + StartTermsStatusWorker
│       │                                          + StartTermsAcceptWorker + ImGuiTerms*)
│       ├── AuthScreenCharacterCreate.cpp       ← Phase::CharacterCreate
│       │                                          (BuildModel + Update + StartCharacterCreateWorker
│       │                                          + ImGuiCharacterCreate*)
│       ├── AuthScreenShardPick.cpp             ← Phase::ShardPick
│       │                                          (BuildModel + Update + ImGuiShardPick*)
│       └── AuthScreenOptions.cpp              ← Phase::LanguageOptions
│                                                 (BuildModel + Update + ImGuiOpenLanguageOptionsMenu)
```

### Renderer (`engine/render/`)

```
engine/render/
├── AuthUiRenderer.h                            ← INCHANGÉ
├── auth/
│   ├── AuthUiRendererCore.cpp                  ← Dispatch, layout metrics, common draw primitives,
│   │                                              DrawBackground, AuthGlyphPass, toutes méthodes
│   │                                              actuelles sauf écrans ImGui
│   ├── AuthImGuiCommon.cpp                     ← Primitives React→ImGui portées depuis le design system :
│   │                                              Panel, Field, Button, Banner, KeycapHint,
│   │                                              Breadcrumb, Toggle, Slider, Dropdown, Keybind
│   └── screens/
│       ├── AuthImGuiLanguageSelect.cpp         ← ImGui LangSelectScreen (écran 1)
│       ├── AuthImGuiLogin.cpp                  ← ImGui LoginScreen (écran 2)
│       ├── AuthImGuiRegister.cpp               ← ImGui RegisterScreen (écran 3)
│       ├── AuthImGuiError.cpp                  ← ImGui RegisterErrorScreen (écran 4)
│       ├── AuthImGuiVerifyEmail.cpp            ← ImGui ConfirmEmailScreen (écran 5)
│       ├── AuthImGuiOptions.cpp                ← ImGui OptionsScreen (écran 6)
│       └── AuthImGuiShardPick.cpp              ← ImGui ShardPickScreen (écran 7)
```

---

## Règles de découpage (obligatoires pour tous les tickets AUTH-UI.*)

### R1 — Pas de duplication de la déclaration de classe
`AuthUi.h` reste la source unique de vérité. Chaque `.cpp` de screen inclut `"engine/client/AuthUi.h"` (ou chemin relatif équivalent via le include path CMake).

### R2 — Pas de nouveaux champs ni méthodes publiques
Le découpage est purement organisationnel. Les signatures publiques dans `AuthUi.h` ne changent pas.

### R3 — Méthodes privées partagées → Core uniquement
`Tr()`, `PhaseLogName()`, `ResolveActionButtonLabels()`, `SetPhase()`, `EnterAuthErrorPhase()`, `JoinWorker()` restent dans `AuthUiPresenterCore.cpp`. Les `.cpp` écrans les appellent normalement via `this->`.

### R4 — CMakeLists.txt mis à jour
Chaque nouveau `.cpp` est ajouté à la target `engine_core` (presenter) ou `engine_core` / section renderer. L'ancien `engine/client/AuthUi.cpp` et `engine/render/AuthUiRenderer.cpp` sont **supprimés** de la target et du disque à la fin du ticket AUTH-UI.0 (ou au terme du dernier ticket AUTH-UI.*).

### R5 — Pas de modification de comportement
Chaque ticket doit conserver un comportement observable identique à l'état STAB.13 pour les écrans non encore redessinés. La régression est mesurée par : build OK + smoke test lancement binaire.

### R6 — Un ticket = une branche = une PR
Chaque AUTH-UI.* s'exécute sur sa propre branche `feature/auth-ui-N`.

---

## Ordre d'exécution recommandé

```
AUTH-UI.0  (ce ticket — architecture plan, aucun code)
AUTH-UI.1  AuthUiPresenterCore.cpp + AuthUiPresenterSettings.cpp + AuthUiPresenterNative.cpp
           + AuthUiRendererCore.cpp + AuthImGuiCommon.cpp
           → suppression de l'ancien AuthUi.cpp et AuthUiRenderer.cpp
AUTH-UI.2  AuthScreenLogin + AuthImGuiLogin      (+ redesign visuel écran 2)
AUTH-UI.3  AuthScreenRegister + AuthImGuiRegister (+ redesign visuel écran 3)
AUTH-UI.4  AuthScreenError + AuthImGuiError       (+ redesign visuel écran 4)
AUTH-UI.5  AuthScreenVerifyEmail + AuthImGuiVerifyEmail (+ redesign visuel écran 5)
AUTH-UI.6  AuthScreenOptions + AuthImGuiOptions   (+ redesign visuel écran 6)
AUTH-UI.7  AuthScreenLanguageSelect + AuthImGuiLanguageSelect (+ redesign visuel écran 1)
AUTH-UI.8  AuthScreenShardPick + AuthImGuiShardPick (+ redesign visuel écran 7)
AUTH-UI.9  AuthScreenForgotPassword (split only — pas de maquette HTML)
AUTH-UI.10 AuthScreenTerms (split only)
AUTH-UI.11 AuthScreenCharacterCreate (split only)
```

AUTH-UI.1 est le ticket critique : il extrait le socle commun, supprime les fichiers monolithiques, et valide que le build passe sans les anciens fichiers.

---

## Livrables de AUTH-UI.0

- Ce fichier ticket (référence archivée)
- Aucun fichier source modifié

## Definition of Done

- [ ] Ce document est committé dans `tickets/AUTH-UI/`
- [ ] L'équipe (ou l'agent IA) a lu et validé la structure cible avant AUTH-UI.1
