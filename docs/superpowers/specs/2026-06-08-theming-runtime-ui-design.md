# Fondation theming runtime de l'UI in-game — Design

**Date** : 2026-06-08
**Statut** : Validé (design)
**Sous-projet** : 1 / 2 (le 2 = refonte du menu Pause variante C, qui consommera ce socle)

## Contexte et problème

Le menu Pause in-game ([`src/client/app/Engine.cpp`](../../../src/client/app/Engine.cpp)
vers la ligne 10172) est jugé peu soigné : boutons mal centrés et style ImGui par
défaut, incohérent avec les écrans auth/options qui suivent eux une charte
(`LnTheme`). En cadrant l'amélioration, le besoin a grandi : l'utilisateur veut
**plusieurs thèmes de couleur sélectionnables** qui recolorent **toute l'UI
in-game**, pas seulement le menu Pause.

Aujourd'hui `LnTheme` ([`src/client/render/LnTheme.h`](../../../src/client/render/LnTheme.h))
expose des constantes `inline constexpr Rgba` (figées à la compilation). Une
dizaine d'écrans ImGui les lisent directement (`LnTheme::kAccent`, etc. — ~28
fichiers sous `src/client/render/`). Pour permettre des thèmes commutables à
l'exécution, il faut convertir ce jeu de constantes en **palette runtime**, puis
migrer les consommateurs.

Ce spec couvre **uniquement la fondation theming**. La refonte visuelle du menu
Pause (cadre doré, halos, centrage corrigé — « variante C » de la maquette
[`docs/superpowers/mockups/pause-menu-mockup.html`](../mockups/pause-menu-mockup.html))
fait l'objet du sous-projet 2, à spécifier ensuite.

## Décisions cadrées

| Sujet | Décision |
|---|---|
| Portée des thèmes | Toute l'UI in-game (palette `LnTheme` globale) |
| Persistance | Locale client, clé `ui.theme` dans `config.json` (pas de serveur) |
| Set de thèmes initial | `or_royal` (défaut, = palette actuelle) + `sylve_emeraude` |
| Extensibilité | Data-driven : ajouter un thème = ajouter une entrée au registre |
| Séquencement | Fondation d'abord, refonte menu Pause ensuite |

## Architecture

### Modèle de données

```
struct Palette {
    Rgba primary, secondary, accent, background, surface, panel,
         text, muted, border, success, warning, errorCol;
};
```

Les 12 champs reprennent exactement les couleurs actuelles de `LnTheme`. Les
helpers dérivés existants (`PanelBg`, `AccentDim`, `BorderActive`) sont conservés
mais opèrent désormais sur la palette **active** au lieu des constantes.

### Registre runtime

Un registre interne (table statique de `{nom, Palette}`) contient les thèmes
disponibles. API publique de `LnTheme` :

- `const Palette& Active()` — palette du thème courant (jamais nulle ; défaut
  `or_royal`).
- `bool SetActive(std::string_view name)` — bascule le thème ; renvoie `false`
  et conserve le thème courant si `name` est inconnu.
- `std::vector<std::string_view> Names()` — liste des thèmes pour alimenter l'UI.
- `std::string_view ActiveName()` — nom du thème courant (pour persistance/UI).

`or_royal` est construit à partir des constantes actuelles (aucune valeur
perdue, juste réorganisée). `sylve_emeraude` est une nouvelle entrée : accent
vert/or pâle, surfaces vert-gris sombre, en gardant `text`/`muted` lisibles et
`success`/`warning`/`errorCol` sémantiquement distincts (le danger reste
identifiable, ne pas le confondre avec l'accent).

### Migration des consommateurs (~28 fichiers)

Remplacement mécanique dans tous les écrans ImGui sous `src/client/render/` :

- `LnTheme::kAccent` → `LnTheme::Active().accent`
- idem pour les 12 champs (`kPrimary`, `kSurface`, `kPanel`, `kText`, `kMuted`,
  `kBorder`, `kSuccess`, `kWarning`, `kErrorCol`, `kSecondary`, `kBackground`).

Aucune logique de rendu ne change : seule la **source** des couleurs devient
runtime. Les helpers `AuthImGuiCommon` (boutons primaire/fantôme/danger, toggle,
bannière) deviennent automatiquement thémés puisqu'ils lisent la palette active.

**Compatibilité** : on retire les constantes `kXxx` au profit des accesseurs
(`Active().xxx`) plutôt que de garder des alias, pour qu'aucun call site ne lise
par erreur une couleur figée. Si le diff s'avère trop large à relire, des alias
`inline const Rgba& kAccent = ...` ne sont **pas** viables (référence vers un
état mutable global, fragile à l'init) — on assume donc la migration explicite.

### Sélecteur de thème (UI)

Un combo « Thème de l'interface » :

- **Panneau Options in-game** (`Engine.cpp` ~ligne 10218, bloc
  `m_inGameOptionsPanelVisible`) — emplacement principal.
- **Options auth** (`AuthImGuiOptions.cpp`) — même contrôle, pour pouvoir changer
  de thème avant d'entrer en jeu.

Comportement : sélection → `LnTheme::SetActive(name)` immédiat (aperçu live, tout
l'UI se recolore à la frame suivante) **et** écriture de la clé `ui.theme` dans
`config.json`.

### Persistance

- Clé : `ui.theme` (string) dans `config.json`.
- Au démarrage : lire `ui.theme`, appeler `SetActive`. Si absente ou invalide →
  défaut `or_royal` (et ne pas crasher).
- Écriture : à chaque changement via le sélecteur.
- 100 % local client (aucune table DB, aucun handler serveur).

## Tests

Tests unitaires côté partagé/client compilables sous Linux (ctest), **sans
dépendance ImGui** :

1. `Names()` contient `or_royal` et `sylve_emeraude`.
2. `SetActive("or_royal")` puis `Active()` renvoie la palette dorée attendue
   (vérifier `accent` ≈ #E8C56E).
3. `SetActive("sylve_emeraude")` change bien `accent`/`primary`.
4. `SetActive("inconnu")` renvoie `false` et **ne change pas** le thème courant.
5. Invariant palette : chaque thème a ses 12 champs ; les couleurs opaques ont
   `a == 1` ; `errorCol` reste distinct de `accent` dans chaque thème.
6. `ActiveName()` reflète le dernier `SetActive` réussi.

Note CI : `LnTheme` est aujourd'hui header-only ; s'il gagne un `.cpp` (registre,
état actif), penser à l'ajouter aux cibles concernées dans
`src/CMakeLists.txt`, et **uniquement client** (pas `server_app`).

## Hors périmètre (YAGNI)

- Synchronisation du thème par compte (serveur) — explicitement écarté, local
  d'abord.
- Thèmes Azur arcane / Sang & pourpre — l'architecture les permet en une entrée,
  mais ils ne sont pas dans le set initial.
- Refonte visuelle du menu Pause (cadre doré, halos, centrage) — sous-projet 2.
- Édition de thèmes personnalisés par l'utilisateur (color pickers) — non demandé.

## Déploiement

> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur — le thème
> est une préférence locale (`config.json`), aucun opcode, handler ni migration DB.

## Suite

Sous-projet 2 : refonte du menu Pause (variante C de la maquette) consommant
`LnTheme::Active()`, avec correction du centrage (boutons à largeur fixe posés à
`X = (largeur_contenu − largeur_bouton) / 2`, titre centré sur la zone de contenu
réelle padding inclus).
