# AUTH-UI.2 — Écran Connexion · LoginScreen (split + redesign visuel)

## Dépendances
- AUTH-UI.1 (socle commun opérationnel, stubs en place)

## Objectif

1. **Split** : déplacer dans `AuthScreenLogin.cpp` toutes les méthodes `AuthUiPresenter` relatives à `Phase::Login`.
2. **Split renderer** : implémenter `AuthImGuiLogin.cpp` avec la logique ImGui de l'écran connexion.
3. **Redesign visuel** : aligner le rendu ImGui sur la maquette `LoginScreen` (écran 2 de `Screens1to4.jsx`).

---

## Périmètre fonctionnel (Phase::Login)

### Méthodes presenter → `engine/client/auth/screens/AuthScreenLogin.cpp`

| Méthode | Origine AuthUi.cpp |
|---|---|
| `StartLoginWorker()` | Worker TCP MasterShardClientFlow |
| `ImGuiSubmitLogin()` | Validation + lancement worker |
| `ImGuiNavigateToRegisterFromLogin()` | Transition vers Phase::Register |
| `ImGuiBackFromRegisterToLogin()` | Retour Phase::Login depuis Register |
| `ImGuiOpenForgotPasswordPortal()` | Ouverture portail web ou transition ForgotPassword |
| `ImGuiRequestClose()` | Demande de fermeture fenêtre |
| `BuildModel_Login()` **(nouvelle méthode privée)** | Contenu RenderModel pour Phase::Login |
| `Update_Login()` **(nouvelle méthode privée)** | Gestion input Login (Tab/Enter/Escape) |

> **Note** : `BuildModel_Login()` et `Update_Login()` sont des **méthodes privées** à ajouter dans la section `private:` de `AuthUi.h` (ou via un fichier include privé si le projet adopte cette convention). Elles sont appelées depuis le dispatch dans `AuthUiPresenterCore.cpp`.

---

## Cible visuelle (LoginScreen — Screens1to4.jsx)

### Structure globale

```
ln-stage                           ← centrage viewport
  ln-stage-col (max 460px)
    ln-hero                        ← Logo 2 lignes
      "Les Chroniques"
      "de la Lune Noire"
    Panel
      header: titre "Connexion"  |  badge "v0.8.4"  |  icône "i"
      body:
        [Banner error?]            ← si m_userErrorText non vide
        [Banner info?]             ← si Phase::Submitting (spinner texte)
        Field "Identifiant"        ← inputPlaceholder="votre nom d'aventurier"
        Field "Mot de passe"       ← type password, inputPlaceholder="••••••••"
        Toggle "Se souvenir de moi"  + hint "Conserve l'identifiant..."
        ln-actions
          Button text/sm "Mot de passe oublié ?"   (ancre gauche)
          ln-actions-right
            Button ghost/md "Créer un compte"  keycap="Ctrl+R"
            Button primary/md "Se connecter"   keycap="↵"
    hors panel:
      Button text/sm "⚙ Options"
      Button text/sm "Quitter"
```

### Mapping RenderModel → ImGui

| RenderModel | ImGui |
|---|---|
| `titleLine1` = `"Les Chroniques"` | `ln-hero-line1` (grande police display, centré) |
| `titleLine2` = `"de la Lune Noire"` | `ln-hero-line2` |
| `sectionTitle` = `"Connexion"` | Header panel |
| `authLoginVersionBadge` | Badge `"v0.8.4"` en haut à droite du panel header |
| `fields[0]` login | `DrawAuthField()` identifiant |
| `fields[1]` password | `DrawAuthField()` mot de passe (secret=true) |
| `bodyLines[0]` rememberMe checkbox | `DrawAuthToggle()` + `authRememberDetailLine` comme hint |
| `authLoginFooterChips` | `DrawAuthKeycapHint()` dans footer panel |
| `actions[0]` forgotPassword | `DrawAuthButton(kind=text)` gauche |
| `actions[1]` register | `DrawAuthButton(kind=ghost)` + badge `"Ctrl+R"` |
| `actions[2]` submit | `DrawAuthButton(kind=primary)` + badge `"↵"` |
| `actions[3]` options | `DrawAuthButton(kind=text)` hors panel |
| `actions[4]` quit | `DrawAuthButton(kind=text)` hors panel |
| `errorText` | `DrawAuthBanner(kind=error)` si non vide |
| `infoBanner` | `DrawAuthBanner(kind=info)` si submitting |

### Points d'attention implémentation ImGui

- Le **hero** ("Les Chroniques / de la Lune Noire") est dessiné **hors du panel**, au-dessus, centré sur la largeur de la colonne.
- Le **badge version** (`authLoginVersionBadge`) s'affiche à droite du titre dans le header panel — utiliser `ImGui::SameLine()` + alignement à droite.
- L'icône **"i"** (popup info) doit utiliser `infoIconX/Y/W/H` du RenderModel pour le hit-testing souris.
- Le **Toggle** "Se souvenir de moi" est implémenté via `DrawAuthToggle()` depuis `AuthImGuiCommon.cpp`.
- En état `Phase::Submitting`, le bouton "Se connecter" est désactivé (`ImGui::BeginDisabled(true)`).
- Les boutons **"⚙ Options"** et **"Quitter"** sont affichés sous le panel, hors du groupe panel, avec `Button kind=text`.

### Couleurs / style (design tokens depuis `colors_and_type.css`)

```cpp
// À lire depuis la palette du design system :
// --ln-accent        #E8C56E    (or chaud)
// --ln-primary       #4A7BB8    (bleu cobalt)
// --ln-bg-panel      rgba(12,16,22,0.88)
// --ln-border        rgba(61,79,102,0.55)
// --ln-text          #D6C9B0
// --ln-muted         #6B7E95
// --ln-error         #B33A3A
// --ln-warning       #C07A2A
// --ln-success       #3A8A5C
// Font display       Windlass (ou fallback serif)
// Font UI            Morpheus / uppercase tracking 0.2em
// Font body          système, italic pour hints
// Font mono          monospace pour codes/badges
```

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenLogin.cpp` (stub AUTH-UI.1 → implémenté)
- `engine/render/auth/screens/AuthImGuiLogin.cpp` (stub AUTH-UI.1 → implémenté)

**Modifiés :**
- `engine/client/AuthUi.h` — ajout déclarations privées `BuildModel_Login()`, `Update_Login()` si non encore présentes
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch Update et BuildRenderModel appelle les nouvelles fonctions

---

## Definition of Done

- [ ] Build Windows OK (configure + build)
- [ ] L'écran connexion s'affiche visuellement conforme à la maquette `LoginScreen` (Screens1to4.jsx)
- [ ] Hero 2 lignes visible au-dessus du panel
- [ ] Badge version visible dans le header panel
- [ ] Icône "i" ouvre le popup info
- [ ] Toggle "Se souvenir de moi" fonctionne
- [ ] Submit → passage en Submitting → spinner banner
- [ ] Champs Tab/Enter navigables
- [ ] Boutons "Options" et "Quitter" visibles sous le panel
- [ ] Aucune régression sur les autres écrans (Register, Options…)
- [ ] Rapport final : fichiers + commandes + résultats
