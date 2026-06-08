# Refonte du menu Pause (variante C) — Design

**Date** : 2026-06-08
**Statut** : Validé (direction visuelle « variante C » choisie en brainstorming)
**Sous-projet** : 2 / 2 (le 1 = fondation theming runtime, PR #855)
**Dépend de** : `LnTheme::Active()` (sous-projet 1) → branche stackée sur `theming-runtime-spec`.

## Contexte et problème

Demande initiale de l'utilisateur (capture à l'appui) : le menu Pause in-game
« n'est pas beau » et « les boutons et les textes ne sont pas centrés ». Le
sous-projet 1 a posé l'infrastructure de thèmes ; ce sous-projet livre enfin la
correction visible.

Le menu Pause est rendu en ImGui dans
[`src/client/app/Engine.cpp`](../../../src/client/app/Engine.cpp), bloc
`if (m_inGamePauseMenuVisible)` (vers la ligne 10183). Deux défauts :

1. **Centrage cassé** : `const float btnW = menuW - 40.f;` puis `ImGui::Button(...)`
   posé au curseur par défaut → marge gauche ≈ 8 px (padding fenêtre), marge
   droite ≈ 32 px. Les boutons paraissent décalés vers la gauche. Le titre est
   centré sur `menuW` sans tenir compte du padding (≈ correct mais fragile).
2. **Aucun style** : `ImGui::Button` bruts avec les couleurs ImGui par défaut
   (le bleu/gris de la capture), incohérents avec le reste du jeu.

Direction retenue (maquette
[`docs/superpowers/mockups/pause-menu-mockup.html`](../mockups/pause-menu-mockup.html),
variante **C — médiéval accentué**) : cadre doré, titre lumineux, séparateur
doré, boutons à survol doré, *Quitter* en rouge danger. Le tout **piloté par le
thème actif** (`LnTheme::Active()`), donc recoloré automatiquement quand le
joueur change de thème (Or royal → accent doré ; Sylve émeraude → accent vert).

## Décisions

| Sujet | Décision |
|---|---|
| Portée | Menu Pause uniquement (le panneau Options reste tel quel pour l'instant) |
| Couleurs | Toutes issues de `LnTheme::Active()` (jamais codées en dur) |
| Sémantique boutons | Reprendre / Options / Se déconnecter = neutre ; Quitter = danger (rouge) |
| Centrage | Boutons à largeur fixe, centrés via la zone de contenu réelle |
| « Halo » survol | Bordure accent (ou rouge danger) dessinée sur survol via `ImDrawList` |
| Polices/accents | Libellés inchangés (ASCII, la police Windlass manque de glyphes accentués) |

## Architecture

Tout reste dans le bloc `if (m_inGamePauseMenuVisible)` de `Engine.cpp` — aucune
nouvelle classe ni fichier. Les couleurs `LnTheme::Rgba` sont converties en
`ImVec4` inline (`ImVec4{c.r, c.g, c.b, c.a}`) et en `ImU32` via
`ImGui::ColorConvertFloat4ToU32`.

### Chrome de la fenêtre
- Fond = `Active().panel` (alpha ≈ 0.96) via `PushStyleColor(ImGuiCol_WindowBg)`
  (on retire `SetNextWindowBgAlpha`, qui écraserait l'alpha).
- Bordure = `Active().accent`, `WindowBorderSize = 1.5`, `WindowRounding = 4`.
- Dimensions légèrement agrandies pour respirer : `menuW = 340`, `menuH = 250`.

### Titre
- `PAUSE` en couleur `Active().accent`, `SetWindowFontScale(1.3)`, **centré sur la
  zone de contenu réelle** : `SetCursorPosX(GetCursorPosX() + (avail - titleW)/2)`
  où `avail = GetContentRegionAvail().x`.

### Séparateur
- `PushStyleColor(ImGuiCol_Separator, accent @ 0.7)` autour de `ImGui::Separator()`.

### Boutons (centrés + thémés)
Une lambda locale `pauseButton(label, danger)` :
1. Centre : `SetCursorPosX(GetCursorPosX() + (avail - btnW)/2)`.
2. Couleurs depuis `Active()` : fond = `surface` ; survol/actif = teinte
   `accent` (ou `errorCol` si `danger`) en alpha faible ; bordure = `border` ;
   texte = `text` (ou rouge pour danger).
3. `FrameRounding = 3`, `FrameBorderSize = 1`, hauteur 34, largeur `btnW`.
4. Sur survol (`IsItemHovered`), dessine une bordure `accent`/`errorCol` pleine
   par-dessus le bouton (`GetWindowDrawList()->AddRect`) — le « halo » de la
   variante C, sans flou coûteux.
5. Renvoie `pressed`.

Ordre inchangé : **Reprendre** (ferme le menu), **Options** (ouvre le panneau
Options), **Se déconnecter** (`RequestLogoutToLoginScreen()`), **Quitter le jeu**
(`OnQuit()`, en `danger = true`). Les actions/`m_...` et appels restent
identiques — seul le rendu change.

## Tests

Pas de test unitaire automatisé (rendu ImGui immédiat, pas de logique pure
nouvelle). Validation **manuelle en jeu** :
- Boutons et titre visuellement centrés (marges gauche = droite).
- Cadre + titre + séparateur dorés en thème Or royal ; survol dore la bordure des
  boutons ; *Quitter* dore en rouge au survol.
- Bascule vers Sylve émeraude (via Options) : le menu Pause se recolore en vert
  (preuve que les couleurs viennent bien de `LnTheme::Active()`).

## Hors périmètre (YAGNI)

- Refonte du **panneau Options** in-game (même bug de centrage du titre, mais
  redesign de ses contrôles = autre passage). Optionnellement, un alignement
  minimal du titre Options peut être inclus s'il est trivial, sinon différé.
- Flou/glow gaussien réel (ImGui ne le fait pas à peu de frais) — approximé par
  une bordure accent sur survol.
- Animations d'ouverture/fermeture du menu.

## Déploiement

> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur — rendu UI
> pur, aucun opcode, handler ni migration. (Stacké sur PR #855 : merger #855
> **avant** cette PR.)
