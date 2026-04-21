# AUTH-UI.5 — Écran Confirmation courriel · ConfirmEmailScreen (split + redesign visuel)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.3 (Register — c'est Register qui envoie vers VerifyEmail)

## Objectif

1. **Split** : déplacer dans `AuthScreenVerifyEmail.cpp` les méthodes relatives à `Phase::VerifyEmail` et `Phase::EmailConfirmationPending`.
2. **Split renderer** : implémenter `AuthImGuiVerifyEmail.cpp` aligné sur la maquette `ConfirmEmailScreen` (écran 5 de `Screens5to7.jsx`).
3. **Redesign visuel** : fil d'Ariane, 6 cases de saisie chiffres individuelles, liens "Renvoyer" / "Modifier le courriel", badge popup info.

---

## Périmètre fonctionnel

### Phase::VerifyEmail + Phase::EmailConfirmationPending

Ces deux phases sont regroupées dans un seul fichier car `EmailConfirmationPending` est un sous-état post-inscription ("Vérifiez vos emails") avant la saisie du code.

### Méthodes presenter → `engine/client/auth/screens/AuthScreenVerifyEmail.cpp`

| Méthode |
|---|
| `StartVerifyEmailWorker()` |
| `ImGuiSubmitVerifyEmailCode()` |
| `ImGuiBackFromVerifyToLogin()` |
| `ImGuiVerifyEmailClearDigits()` |
| `ImGuiVerifyEmailBackToEditRegisterEmail()` |
| `ImGuiSetVerifyEmailPartialCode()` |
| `ImGuiEmailConfirmationBackToLogin()` |
| `BuildModel_VerifyEmail()` **(nouvelle méthode privée)** |
| `BuildModel_EmailConfirmationPending()` **(nouvelle méthode privée)** |
| `Update_VerifyEmail()` **(nouvelle méthode privée)** |

---

## Cible visuelle — Phase::VerifyEmail (ConfirmEmailScreen — Screens5to7.jsx)

### Structure globale

```
ln-stage
  ln-stage-col (max 560px)
    Breadcrumb  [01 Langue] [02 Compte] [03 Courriel ← actif] [04 Monde]
    Panel
      header: "Vérifiez votre courriel"  |  badge "3 / 4"  |  icône "i"
      subtitle: "Nous avons envoyé un code à {email}."
      body:
        [Banner error si code incorrect]
        Section "Code de vérification"
          6 cases chiffres individuelles (style monospace)
        Row liens :
          Button text/sm "Renvoyer le code"
          Button text/sm "Modifier le courriel"
        ln-actions:
          Button ghost/md "Retour"   keycap="Échap"
          Button primary/md "Valider le code"  keycap="↵"  (désactivé si < 6 chiffres)
    footer italic muted : astuce mode dev ("123456" = succès, "000000" = erreur)
```

### 6 cases chiffres — implémentation ImGui

Chaque case est un `ImGui::InputText` de largeur fixe (`clamp(40px, 6vw, 56px)`) avec :
- `ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_NoHorizontalScroll`
- Longueur max : 1 caractère
- Saisie d'un chiffre → focus auto sur la case suivante via `ImGui::SetKeyboardFocusHere()`
- Bordure active : `ln-primary` ; bordure neutre : `ln-border`
- Police : `font-mono`, taille `clamp(20px, 2.2vw, 28px)`
- L'état est stocké dans `m_verifyCode` (string 6 chars, complétée avec `'\0'` pour les cases vides)
- `ImGuiSetVerifyEmailPartialCode()` est appelée à chaque frappe

### Mapping RenderModel → ImGui

| RenderModel | ImGui |
|---|---|
| `authRegisterCrumbLabels` + `authRegisterCrumbCurrent=2` | `DrawAuthBreadcrumb()` |
| `authVerifyPanelTitle` | Titre panel |
| `authVerifyPanelSubtitle` | Sous-titre (inclut l'adresse courriel) |
| `authVerifyPanelBadge` = `"3 / 4"` | Badge |
| `authVerifyInfoPopupText` | Texte popup icône "i" |
| `authVerifyDigitLabel` | Label section au-dessus des 6 cases |
| `authVerifyResendLabel` | Libellé "Renvoyer le code" |
| `authVerifyChangeEmailLabel` | Libellé "Modifier le courriel" |
| `authVerifySubmitLabel` | Libellé "Valider le code" |
| `authVerifyBackLabel` | Libellé "Retour" |
| `authVerifyBackKeycap` | Badge keycap retour |
| `authVerifySubmitKeycap` | Badge keycap valider |
| `authVerifyDevHint` | Texte italic footer (mode dev uniquement) |
| `errorText` | Banner error si non vide |

---

## Cible visuelle — Phase::EmailConfirmationPending

Cette phase est une **page intermédiaire** affichée juste après l'inscription réussie, avant que l'utilisateur ne reçoive et saisisse le code. Elle ne figure pas dans les maquettes HTML comme écran séparé, mais doit être cohérente visuellement.

### Structure proposée (dérivée de ConfirmEmailScreen)

```
Panel
  header: "Inscription réussie"  |  badge "3 / 4"
  subtitle: "Un lien de vérification a été envoyé à {email}."
  body:
    Banner kind=ok "Compte créé"
      "Vérifiez votre courriel et cliquez sur le lien de confirmation,
       ou saisissez le code reçu ci-dessous."
    [même 6 cases chiffres que VerifyEmail]
  actions:
    Button ghost/md "Retour à la connexion"
```

Si `emailConfirmationPending=true` (depuis `GetVisualState()`), afficher ce panneau simplifié. `ImGuiEmailConfirmationBackToLogin()` ramène à `Phase::Login`.

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenVerifyEmail.cpp`
- `engine/render/auth/screens/AuthImGuiVerifyEmail.cpp`

**Modifiés :**
- `engine/client/AuthUi.h` — méthodes privées `BuildModel_VerifyEmail()`, `BuildModel_EmailConfirmationPending()`, `Update_VerifyEmail()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Fil d'Ariane 4 étapes, étape 3 active
- [ ] 6 cases chiffres : navigation automatique case→case à chaque frappe
- [ ] Bouton "Valider le code" désactivé tant que < 6 chiffres saisis
- [ ] Code incorrect → Banner error
- [ ] "Renvoyer le code" → `ImGuiVerifyEmailClearDigits()`
- [ ] "Modifier le courriel" → `ImGuiVerifyEmailBackToEditRegisterEmail()`
- [ ] "Retour" → `ImGuiBackFromVerifyToLogin()`
- [ ] Phase::EmailConfirmationPending : panneau intermédiaire distinct affiché correctement
- [ ] Aucune régression Register / Error
- [ ] Rapport final
