# AUTH-UI.7 — Écran Choix de langue · LangSelectScreen (split + redesign visuel)

## Dépendances
- AUTH-UI.1 (socle commun)
- AUTH-UI.2 (Login — la langue mène vers Login une fois choisie)

## Objectif

1. **Split** : déplacer dans `AuthScreenLanguageSelect.cpp` les méthodes relatives à `Phase::LanguageSelectionFirstRun`.
2. **Split renderer** : implémenter `AuthImGuiLanguageSelect.cpp` aligné sur la maquette `LangSelectScreen` (écran 1 de `Screens1to4.jsx`).
3. **Redesign visuel** : hero 2 lignes, panel avec grille de cartes langue (drapeau SVG + nom), keycaps de navigation, badge étape "1 / 2".

---

## Périmètre fonctionnel (Phase::LanguageSelectionFirstRun)

### Méthodes presenter → `engine/client/auth/screens/AuthScreenLanguageSelect.cpp`

| Méthode |
|---|
| `ImGuiApplyFirstRunLanguageContinue()` |
| `ApplyLocaleSelection()` *(méthode privée, déplacée ici)* |
| `BuildModel_LanguageSelect()` **(nouvelle méthode privée)** |
| `Update_LanguageSelect()` **(nouvelle méthode privée — navigation clavier ←/→ + Enter)** |

---

## Cible visuelle (LangSelectScreen — Screens1to4.jsx)

### Structure globale

```
ln-stage
  ln-stage-col (max 720px)
    ln-hero
      "Les Chroniques"          ← ln-hero-line1
      "de la Lune Noire"        ← ln-hero-line2
    Panel
      header: "Choisissez votre langue"
      subtitle: {current.welcome}   ← ex. "Bienvenue, voyageur." / "Welcome, traveller."
      versionLabel: "1 / 2"
      icône "i" : "Vous pourrez modifier ce choix plus tard dans les Options > Langue."
      body:
        ln-lang-grid                ← grille cartes langue
          ln-lang-card [selected?]
            FlagSVG (fr ou en)
            ln-lang-name : "Français" / "English"
            ln-lang-native : "Français" / "English"
        ln-actions (justify flex-end)
          Button primary/lg  "Continuer"  keycap="↵"
      footer:
        KeycapHint "← →" → "naviguer"
        KeycapHint "↵"  → "valider"
```

### Cartes langue — implémentation ImGui

Chaque carte est un `ImGui::InvisibleButton()` + rendu custom via `ImDrawList` :

```
┌─────────────────────┐
│  [Drapeau SVG 54x38]│  ← DrawFlagFR() / DrawFlagEN()
│                     │
│  FRANÇAIS           │  ← font display, uppercase, ln-text
│  Français           │  ← font body, italic, ln-muted
└─────────────────────┘
```

État sélectionné : bordure `ln-accent` 2px, fond `rgba(232,197,110,.08)`.
État hover : bordure `ln-primary` 1px.

Largeur carte : `(availableWidth - gap * (n-1)) / n` où `n` = nombre de locales disponibles (2 actuellement, extensible).

### Drapeaux SVG → ImGui DrawList

Les drapeaux sont portés de `FlagSVG` React en primitives ImGui DrawList :

**Drapeau FR** (3 bandes verticales) :
```cpp
void DrawFlagFR(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    float w3 = size.x / 3.f;
    dl->AddRectFilled(pos, {pos.x + w3, pos.y + size.y}, IM_COL32(0,38,84,255));        // bleu
    dl->AddRectFilled({pos.x+w3, pos.y}, {pos.x+2*w3, pos.y+size.y}, IM_COL32(255,255,255,255)); // blanc
    dl->AddRectFilled({pos.x+2*w3, pos.y}, {pos.x+size.x, pos.y+size.y}, IM_COL32(237,41,57,255)); // rouge
}
```

**Drapeau EN** (Union Jack simplifié) :
Fond bleu marine `#012169`, croix blanche 10px, croix rouge 5px, diagonales blanches 7px / rouges 3px — implémenté via `AddRectFilled` + `AddLine` sur `ImDrawList`.

### Navigation clavier ←/→

Dans `Update_LanguageSelect()` :
- `Input::WasKeyPressed(Key::Left)` → `m_languageSelectionIndex = (m_languageSelectionIndex + n - 1) % n`
- `Input::WasKeyPressed(Key::Right)` → `m_languageSelectionIndex = (m_languageSelectionIndex + 1) % n`
- `Input::WasKeyPressed(Key::Enter)` → `ImGuiApplyFirstRunLanguageContinue()`

Le sous-titre du panel (RenderModel `titleLine2` ou `languagePanelSubtitle`) change dynamiquement en fonction de la carte sélectionnée :
- FR sélectionné → `"Bienvenue, voyageur."`
- EN sélectionné → `"Welcome, traveller."`

### Mapping RenderModel → ImGui

| RenderModel | ImGui |
|---|---|
| `languageFirstRunLayout = true` | Activer ce layout (vs. layout options standard) |
| `languagePanelSubtitle` | Sous-titre panel (message de bienvenue) |
| `languageVersionLabel` = `"1 / 2"` | Badge version panel header |
| `languageFirstRunCards` | Tableau de cartes (localeTag, nameAllCaps, nativeLine, selected, hovered) |
| `languageFooterLeft` | Texte keycap gauche footer |
| `languageFooterRight` | Texte keycap droite footer |
| `titleLine1` = `"Les Chroniques"` | Hero ligne 1 |
| `titleLine2` = `"de la Lune Noire"` | Hero ligne 2 |

---

## Note : Écran langue vs. LanguageOptions

`Phase::LanguageSelectionFirstRun` n'apparaît qu'au **premier lancement** (quand aucune locale n'est persistée). Par la suite, la langue se modifie depuis `Phase::LanguageOptions` (écran Options → onglet Langue, AUTH-UI.6).

`m_hasPersistedLocale` détermine si la phase est sautée au démarrage.

---

## Livrables

**Créés / Complétés :**
- `engine/client/auth/screens/AuthScreenLanguageSelect.cpp`
- `engine/render/auth/screens/AuthImGuiLanguageSelect.cpp`
- `engine/render/auth/AuthImGuiCommon.cpp` — ajout `DrawFlagFR()`, `DrawFlagEN()`

**Modifiés :**
- `engine/client/AuthUi.h` — `BuildModel_LanguageSelect()`, `Update_LanguageSelect()`
- `engine/client/auth/AuthUiPresenterCore.cpp` — dispatch

---

## Definition of Done

- [ ] Build Windows OK
- [ ] Hero 2 lignes visible au-dessus du panel
- [ ] 2 cartes langue avec drapeaux dessinés via DrawList
- [ ] Carte sélectionnée : bordure accent + fond doré léger
- [ ] Navigation ←/→ clavier cycle entre les cartes
- [ ] Sous-titre panel change dynamiquement selon la langue sélectionnée
- [ ] Badge "1 / 2" visible dans le header panel
- [ ] KeycapHints "← →" et "↵" dans le footer panel
- [ ] Bouton "Continuer" → `ImGuiApplyFirstRunLanguageContinue()` → transition vers Login
- [ ] Écran sauté si locale déjà persistée (`m_hasPersistedLocale = true`)
- [ ] Aucune régression Login / Options
- [ ] Rapport final
