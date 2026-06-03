# Issue: AUTH-UI.3

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/client/auth/screens/AuthScreenRegister.cpp
- src/client/render/auth/screens/AuthImGuiRegister.cpp

## Note
RegisterScreen

---

## Contenu du ticket (AUTH-UI.3)

# AUTH-UI.3 — Écran Inscription · RegisterScreen (split + redesign visuel)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.2 (Login opérationnel — les transitions Register↔Login sont testées ensemble)

## Objectif

1. **Split** : déplacer dans `AuthScreenRegister.cpp` toutes les méthodes relatives à `Phase::Register`.
2. **Split renderer** : implémenter `AuthImGuiRegister.cpp` aligné sur la maquette `RegisterScreen` (écran 3 de `Screens1to4.jsx`).
3. **Redesign visuel** : grille 2 colonnes, fil d'Ariane, barre de force de mot de passe, indicateur disponibilité identifiant, dropdowns date de naissance.

---

## Périmètre fonctionnel (Phase::Register)

### Méthodes presenter → `engine/client/auth/screens/AuthScreenRegister.cpp`

| Méthode |
|---|
| `StartRegisterWorker()` |
| `StartUsernameCheckWorker()` |
| `ImGuiSubmitRegister()` |
| `ImGuiNavigateToRegisterFromLogin()` *(si non déjà en Auth-UI.2)* |
| `ImGuiBackFromRegisterToLogin()` *(si non déjà en Auth-UI.2)* |
| `BuildRegisterFieldsMirrorForImGui()` |
| `BuildModel_Register()` **(nouvelle méthode privée)** |
| `Update_Register()` **(nouvelle méthode privée)** |

---

## Cible visuelle (RegisterScreen — Screens1to4.jsx)

### Structure globale

```
ln-stage (padding clamp 12px→24px)
  ln-stage-col (max 680px)
    Breadcrumb  [01 Langue] [02 Compte ← actif] [03 Courriel] [04 Monde]
    Panel
      header: "Créer un compte"  |  badge "2 / 4"  |  icône "i"
      subtitle: "Forger votre identité dans les terres de la Lune Noire."
      body (grille 2 colonnes ln-form-grid cols-2):
        [span-2] Field "Identifiant"      ← disponibilité username temps-réel
        [span-2] Field "Adresse courriel" ← validation format email
        [col-1]  Field "Mot de passe"     ← + barre force 4 segments
        [col-2]  Field "Confirmation"     ← indicateur correspondance
        [col-1]  Dropdown "Jour"          ← 1–31
        [col-2]  Dropdown "Mois"          ← Janv.–Déc.
        [col-3 sur 3]  Dropdown "Année"   ← 1920–2010 (défilant)
        ln-actions:
          Button ghost/md "Retour"  keycap="Échap"   (gauche)
          ln-actions-right:
            Button text/sm  "Voir les erreurs"        (lien démo → Phase::Error)
            Button primary/md "Créer le compte" keycap="↵"  (désactivé si !canSubmit)
    footer keycaps (hors panel, centré):
      KeycapHint "Tab → champ suivant"
      KeycapHint "↵ → valider"
      KeycapHint "Échap → retour"
```

### Mapping RenderModel → ImGui

| RenderModel | ImGui |
|---|---|
| `authRegisterCrumbLabels` + `authRegisterCrumbCurrent` | `DrawAuthBreadcrumb()` — 4 étapes |
| `sectionTitle` = `"Créer un compte"` | Titre panel |
| `authRegisterPanelSubtitle` | Sous-titre panel |
| `authRegisterPanelBadge` = `"2 / 4"` | Badge version panel header |
| `fields[0]` identifiant | `DrawAuthField()` span-2, `usernameCheckState` → indicateur |
| `fields[1]` courriel | `DrawAuthField()` span-2, status ok/err + `fieldError` |
| `fields[2]` mot de passe | `DrawAuthField()` col-1 + barre de force dessous |
| `fields[3]` confirmation | `DrawAuthField()` col-2, `passwordMatchState` → ok/err |
| `dropdowns[0]` jour | `DrawAuthDropdown()` |
| `dropdowns[1]` mois | `DrawAuthDropdown()` |
| `dropdowns[2]` année | `DrawAuthDropdown()` |
| `authRegisterFooterChips` | `DrawAuthKeycapHint()` footer hors panel |
| `authRegisterShowErrorsLabel` | Bouton text "Voir les erreurs" |
| `actions[N]` | Retour + Créer le compte |
| `authRegisterEmailHint` | Tooltip champ courriel |

### Indicateur disponibilité identifiant (UsernameCheckState)

Basé sur `fields[0].usernameCheckState` (reflète `m_usernameCheckState`) :

| Valeur | Affichage ImGui |
|---|---|
| `0` Idle | Pas d'indicateur |
| `1` Pending | Icône `…` orange / `DrawAuthField(status=pending)` |
| `2` Available | Icône `✓` verte + msg `"Identifiant disponible"` |
| `3` Taken | Icône `✕` rouge + msg `"Identifiant déjà pris"` |

### Barre de force du mot de passe

4 segments horizontaux sous le champ mot de passe :

```cpp
// Score : 0..4 (longueur>=8, majuscule, chiffre, symbole)
// 0-1 : rouge  (ln-error)
// 2   : orange (ln-warning)
// 3-4 : vert   (ln-success)
// Segments actifs = score ; inactifs = ln-border
```

Implémenté via `ImDrawList::AddRectFilled()` sur 4 rectangles de 4px de hauteur.

### Grille 2 colonnes (ln-form-grid)

ImGui ne supporte pas nativement les grilles CSS. Implémentation via colonnes `ImGui::Columns(2)` ou `ImGui::BeginTable("reg_grid", 2)` avec colonne span-2 simulée par `SetNextItemWidth(availWidth)`.

Recommandé : `ImGui::BeginTable` avec flags `ImGuiTableFlags_NoBordersInBody`.

### Dropdowns date de naissance

Utiliser `DrawAuthDropdown()` de `AuthImGuiCommon.cpp`. L'état ouvert/fermé est géré par `m_openDropdownIndex` (-1=aucun, 0=jour, 1=mois, 2=année) — **un seul dropdown ouvert à la fois**.

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenRegister.cpp`
- `engine/render/auth/screens/AuthImGuiRegister.cpp`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_Register()`, `Update_Register()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Fil d'Ariane 4 étapes visible, étape 2 active
- [ ] Grille 2 colonnes : identifiant + courriel span-2, mdp + confirm côte à côte, 3 dropdowns date
- [ ] Indicateur disponibilité identifiant : Idle / Pending / Available / Taken
- [ ] Barre de force mot de passe : 4 segments colorés
- [ ] Indicateur correspondance mot de passe : vert si match, rouge sinon
- [ ] Bouton "Créer le compte" désactivé tant que formulaire invalide
- [ ] Navigation Tab/Enter/Escape fonctionnelle
- [ ] Transition Register → Error (bouton "Voir les erreurs")
- [ ] Transition Register → Login (Retour / Escape)
- [ ] Aucune régression Login
- [ ] Rapport final
