# AUTH-UI — Index des tickets

Refactoring de `AuthUi.cpp` (6 370 lignes) + `AuthUiRenderer.cpp` (1 057 lignes) :
un fichier par écran + redesign visuel aligné sur `design/lune-noire-design-system/project/ui_kits/auth_flow/`.

---

## Vue d'ensemble

| Ticket | Écran | Phase C++ | Maquette HTML | Type |
|---|---|---|---|---|
| AUTH-UI.0 | Architecture plan | — | — | Plan |
| AUTH-UI.1 | Socle commun | Toutes (dispatch) | `primitives.jsx` → `AuthImGuiCommon` | Split + primitives |
| AUTH-UI.2 | Connexion | `Login` | `LoginScreen` (écran 2) | Split + redesign |
| AUTH-UI.3 | Inscription | `Register` | `RegisterScreen` (écran 3) | Split + redesign |
| AUTH-UI.4 | Erreurs | `Error` | `RegisterErrorScreen` (écran 4) | Split + redesign |
| AUTH-UI.5 | Confirmation courriel | `VerifyEmail` + `EmailConfirmationPending` | `ConfirmEmailScreen` (écran 5) | Split + redesign |
| AUTH-UI.6 | Options | `LanguageOptions` | `OptionsScreen` (écran 6) | Split + redesign |
| AUTH-UI.7 | Choix de langue | `LanguageSelectionFirstRun` | `LangSelectScreen` (écran 1) | Split + redesign |
| AUTH-UI.8 | Choix du serveur | `ShardPick` | `ShardPickScreen` (écran 7) | Split + redesign |
| AUTH-UI.9 | Mot de passe oublié | `ForgotPassword` | *(pas de maquette)* | Split only |
| AUTH-UI.10 | Conditions générales | `Terms` | *(pas de maquette)* | Split only |
| AUTH-UI.11 | Création de personnage | `CharacterCreate` | *(pas de maquette)* | Split only |

---

## Ordre d'exécution recommandé

```
AUTH-UI.0  → AUTH-UI.1  → AUTH-UI.2  → AUTH-UI.3
                                              ↓
                                         AUTH-UI.4
                                              ↓
AUTH-UI.7  ← AUTH-UI.6  ← AUTH-UI.5  ←────────
     ↓
AUTH-UI.8 → AUTH-UI.9 → AUTH-UI.10 → AUTH-UI.11
```

AUTH-UI.1 est le ticket critique (build doit passer sans les anciens fichiers monolithiques).
AUTH-UI.11 est le ticket de clôture (zéro stub vide, flux complet validé).

---

## Fichiers source produits (cible)

### Presenter
```
engine/client/AuthUi.h                                       (inchangé)
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
```

### Renderer
```
engine/render/AuthUiRenderer.h                               (inchangé)
engine/render/auth/AuthUiRendererCore.cpp
engine/render/auth/AuthImGuiCommon.cpp
engine/render/auth/screens/AuthImGuiLanguageSelect.cpp
engine/render/auth/screens/AuthImGuiLogin.cpp
engine/render/auth/screens/AuthImGuiRegister.cpp
engine/render/auth/screens/AuthImGuiError.cpp
engine/render/auth/screens/AuthImGuiVerifyEmail.cpp
engine/render/auth/screens/AuthImGuiOptions.cpp
engine/render/auth/screens/AuthImGuiShardPick.cpp
engine/render/auth/screens/AuthImGuiForgotPassword.cpp
engine/render/auth/screens/AuthImGuiTerms.cpp
engine/render/auth/screens/AuthImGuiCharacterCreate.cpp
```

### Supprimés
```
engine/client/AuthUi.cpp
engine/render/AuthUiRenderer.cpp
```
