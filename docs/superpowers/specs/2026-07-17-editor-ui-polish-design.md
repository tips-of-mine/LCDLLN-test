# Polish visuel de l'éditeur monde — design

Date : 2026-07-17 (suite immédiate de la réorganisation UI #976/#977/#979)
Statut : validé en séance (retour utilisateur sur le build final : « très loin
de l'interface d'UE » — capture montrant fenêtres flottantes superposées,
thème ImGui par défaut, lettres placeholder).
Périmètre : 100 % binaire éditeur — aucun redéploiement serveur.
Boussole : **simple au premier regard, complet via les menus**.

## Problèmes constatés sur le build mergé

1. L'ancien `world_editor_imgui.ini` de l'utilisateur a été rechargé : les
   nouvelles fenêtres (Palette d'outils) flottent au centre au lieu d'être
   dockées — la disposition par défaut n'est posée qu'en l'absence d'ini.
2. Trop de panneaux ouverts par défaut (Quest Editor, Routines…) qui se
   chevauchent en flottant.
3. Deux systèmes de disposition superposés : le dockspace du shell
   (`editor_world_layout.ini`, panneaux M100.x) ET le dockspace V2 de
   WorldEditorImGui (`world_editor_imgui.ini`, fenêtres session M43.x).
4. Thème ImGui par défaut (gris-bleu) et lettres S/Z/Y/V/E en guise
   d'icônes.
5. État « aucune carte » : viewport gris vide sans guidage.

## Décisions

1. **Disposition versionnée et unifiée** : clé `editorLayoutVersion` dans
   `user_prefs.json` ; si différente de `kEditorLayoutVersion` (constante
   code, bump à chaque évolution de disposition), la disposition est
   reconstruite automatiquement (suppression des DEUX ini + re-pose). La
   disposition par défaut V2 docke AUSSI les panneaux du shell (par nom de
   fenêtre) : gauche = Palette d'outils (+ Outils), droite = Carte /
   Affichage / Atmosphère / Import / Objets / Outliner / Inspector / Tool
   Properties en onglets, bas = Statut. Les panneaux avancés (Asset Browser,
   Console, History, Surface Table, Collision Editor, Building Editor,
   Quest Editor, Routines) sont **masqués par défaut** (ré-ouvrables via le
   menu Fenêtre) et pré-dockés pour apparaître au bon endroit.
2. **Thème sombre type UE** (`EditorTheme.cpp`, appliqué uniquement quand
   `isWorldEditorExe`) : palette anthracite, accent doré (identité LCDLLN),
   arrondis 4-6 px, paddings généreux, onglets/en-têtes contrastés.
3. **Icônes vectorielles** pour la barre d'actions (disquette, flèches
   undo/redo, coche, export) dessinées via ImDrawList — pas d'assets PNG à
   pipeliner ; les lettres restent le fallback des ids inconnus.
4. **Écran d'accueil** : tant qu'aucune carte n'est chargée (heightmap du
   document vide), fenêtre centrée « Bienvenue » : bouton « Nouvelle zone
   (assistant)… », liste des cartes récentes cliquables, renvoi vers le
   panneau Carte ; fermable, disparaît dès qu'une carte est chargée.

## Hors périmètre

- Icônes des 15 outils de la palette (pastilles couleur conservées).
- Refonte des panneaux de contenu (Carte, Tool Properties…).
- Toute évolution du rendu 3D.

## Tests

- `editor_modes_tests` : round-trip `editorLayoutVersion` (défaut 0).
- Le reste est du rendu ImGui (non testable unitairement dans ce repo) —
  validation visuelle utilisateur.

## Livraison

1 PR unique (`claude/editor-ui-polish` → main) — les 4 volets sont cohérents
et co-dépendants visuellement. **Déploiement** : ✅ client/éditeur uniquement.
