# Réorganisation des menus, barre d'actions et palette d'outils de l'éditeur monde

Date : 2026-07-17
Statut : validé (design approuvé en séance)
Périmètre : 100 % client/éditeur — aucun redéploiement serveur.

## 1. Contexte et objectif

L'utilisateur a comparé notre éditeur monde (`lcdlln_world_editor.exe`) à
Unreal Engine 5.8 et souhaite en rapprocher l'**organisation des menus, des
barres d'outils et des raccourcis** (pas les fonctionnalités : notre éditeur
reste spécialisé pour LCDLLN).

Problèmes constatés dans l'existant :

- **Menu « Vue » fourre-tout** (`src/world_editor/ui/WorldEditorImGui.cpp`,
  `BuildUi`, ~ligne 849) : mélange toggles de panneaux (rôle d'un menu
  Fenêtre), options viewport (grille, aide caméra), un slider de vitesse
  caméra (rôle de Préférences), des lignes d'astuce et la gestion de
  disposition.
- **Menu « Edition » quasi vide** : Annuler/Rétablir seulement, alors que
  `DeleteCommand`, `HistoryPanel` et `EditorModeRegistry` existent.
- **« Sauvegarder » et « Charger une carte » comme items de premier niveau**
  de la barre de menu (boutons déguisés en menus) + **texte de statut dans la
  barre de menu** (disparaît, bouge).
- **« Quitter » grisé** (stub no-op) dans Fichier.
- **Barre anglaise fantôme** : `WorldEditorShell::RenderMenuBar` (M100.1,
  File/Edit/View/Tools/Window/Help + layouts Sculpting/Painting/Placement
  aliasés sur Default) est supprimée au runtime par `SetMenuBarSuppressed(true)`
  → code mort qui diverge du menu français.
- **Barre d'outils = 15 boutons d'outils terrain sans aucune action**
  (`src/world_editor/ui/EditorToolbar.{h,cpp}`) : pas de Sauvegarder, pas
  d'Annuler/Rétablir, pas de Valider/Exporter. UE fait l'inverse : actions en
  haut, outils dans une palette latérale.
- **Raccourcis** dispersés (F1..F12, Ctrl+Shift+lettre, Ctrl+Z/Y) dans
  `WorldEditorShell::HandleShortcut`, affichés inégalement, aucune vue
  d'ensemble.

## 2. Décisions validées avec l'utilisateur

| Question | Décision |
|---|---|
| Organisation toolbar | **Actions en haut + palette d'outils verticale à gauche** (style UE Modes) |
| Palette de commandes (recherche d'actions) | **Incluse dans ce chantier** (PR 3) |
| Raccourcis clavier | **Fenêtre récapitulative en lecture seule** (pas de rebinding) |
| Approche technique | **Registre d'actions central** (option B), pas de duplication par surface |

## 3. Architecture : registre d'actions central

Nouveau module `src/world_editor/actions/` (PascalCase, doc `///` en français
— règle stricte de l'éditeur monde) :

- `EditorAction.h` — struct pure data :
  - `Id` : identifiant stable kebab-case (ex. `file.save`, `tool.terrain-sculpt`,
    `panel.outliner`) ;
  - `Label` : libellé FR affiché ;
  - `Category` : enum (Fichier, Edition, Vue, Fenetre, Outils, Aide) — sert au
    groupement dans la palette Ctrl+P et la fenêtre raccourcis ;
  - `Section` : sous-groupe optionnel (ex. « Import », « Export ») pour les
    en-têtes `SeparatorText` des menus ;
  - `ShortcutText` : texte affiché (« Ctrl+S ») — l'exécution des raccourcis
    reste dans `HandleShortcut`, le registre est la source du *texte* ;
  - `EnabledPredicate` : `std::function<bool()>` (ex. Annuler grisé si pile vide) ;
  - `CheckedPredicate` : optionnel, pour les toggles (panneaux, grille) ;
  - `Execute` : `std::function<void()>`.
- `EditorActionRegistry.{h,cpp}` — enregistre les actions au boot
  (construction dans `WorldEditorImGui`/`WorldEditorShell` avec capture des
  dépendances existantes : `WorldEditorSession`, `CommandStack`, panneaux),
  itération par catégorie/section, lookup par id. Ids uniques garantis
  (assert + test).

**Consommateurs** : menus (PR 1), barre d'actions (PR 2), palette d'outils
(PR 2), palette de commandes (PR 3), fenêtre raccourcis (PR 3). Une action se
déclare une fois et apparaît partout.

Les 15 outils (`ActiveTool`) sont enregistrés comme actions de catégorie
Outils avec section = famille (voir §6) : le menu Outils, la palette de
gauche et la palette Ctrl+P en dérivent.

## 4. Barre de menu cible

`Fichier | Édition | Vue | Fenêtre | Outils | Aide` — en-têtes de section
via `ImGui::SeparatorText`, raccourcis affichés sur chaque item concerné.

- **Fichier** :
  - Nouvelle zone (assistant)… ; Appliquer un preset de zone…
  - Charger une carte ▸ (liste + Rafraîchir) ; **Cartes récentes ▸** (nouveau,
    max 8 entrées, persistées via `UserPrefsStore` / `user_prefs.json`,
    mises à jour à chaque load/save réussi)
  - Sauvegarder la carte courante `Ctrl+S`
  - — IMPORT — Importer une texture… ; Importer un son…
  - — EXPORT — Valider la zone ; Exporter en runtime (garde le blocage
    validation existant + tooltip)
  - — **Quitter** (réel) : demande de fermeture par le même chemin que la
    croix fenêtre ; si état non sauvegardé, modale de confirmation
    « Quitter sans sauvegarder ? » (Sauvegarder et quitter / Quitter /
    Annuler). L'état « non sauvegardé » = révision du `CommandStack`
    différente de la révision au dernier `ActionSaveCurrentMap` réussi
    (compteur posé par le shell).
- **Édition** :
  - Annuler `Ctrl+Z` ; Rétablir `Ctrl+Y` ; Historique des annulations
    (ouvre/affiche `HistoryPanel`)
  - — Supprimer la sélection `Suppr` (grisé si sélection vide ; branché sur
    `DeleteCommand` existant). Pas d'item « Dupliquer » : aucune commande de
    duplication n'existe aujourd'hui (hors périmètre, cf. §11)
  - — Préférences… : fenêtre regroupant l'actuel menu Options
    (AZERTY/QWERTY, mode Simple/Avancé) + vitesse caméra (sortie du menu Vue)
  - Raccourcis clavier… (fenêtre récap, cf. §8)
- **Vue** (viewport uniquement) : Grille (afficher/masquer) ; Aide caméra
  (WASD) ; Atmosphère (jour/nuit).
- **Fenêtre** : toggles de tous les panneaux du shell (+ Bibliothèque de
  textures, Validation de zone) ; — Réinitialiser la disposition des fenêtres
  (comportement actuel conservé).
- **Outils** : sous-menus par famille (cf. §6), items = actions outils du
  registre, checkmark sur l'outil actif.
- **Aide** : Diagnostic (« pourquoi ça ne marche pas ? ») ; Raccourcis
  clavier… ; À propos (version + hash de commit si disponible au build,
  sinon « dev »).

Disparaissent de la barre de menu : items « Sauvegarder » / « Charger une
carte » de premier niveau (remplacés par Fichier + barre d'actions), texte de
statut (remplacé par la barre de statut §7), menu Options (fusionné dans
Édition > Préférences), astuces texte du menu Vue (déplacées en tooltips),
slider vitesse caméra (déplacé dans Préférences).

**Suppression de la barre anglaise fantôme** : `RenderMenuBar`,
`SetMenuBarSuppressed` et les layouts nommés morts (Sculpting/Painting/
Placement, aliases de Default) sont retirés de `WorldEditorShell` ; le menu
français devient l'unique barre. Les fonctions encore utiles (toggles de
panneaux, reset layout) passent par le registre.

## 5. Barre d'actions (en haut, sous le menu — remplace la rangée d'outils)

`EditorToolbar` est reconverti en **barre d'actions** (hauteur 48 px
conservée) : `Sauvegarder | Annuler | Rétablir || Valider la zone |
Exporter en runtime`. Icônes via `ToolbarIconAtlas` (nouvelles entrées
d'icônes ; fallback lettre centrale comme aujourd'hui si atlas indisponible),
tooltip « Libellé (raccourci) », grisé selon `EnabledPredicate`. Les
invariants géométriques existants (`BuildLayout` pur, `HitTest` pur, jamais
un pixel sur le viewport 3D — cf. `EditorToolbarTests.cpp`) sont conservés
et les tests adaptés au nouveau contenu.

## 6. Palette d'outils (nouveau panneau dockable à gauche)

Nouveau panneau `ToolPalettePanel` (implémente `IPanel`, enregistré dans le
shell, présent dans la disposition par défaut, ancré à gauche) :

- Bouton « Aucun outil » (désélection) en tête.
- Groupes repliables : **Terrain** (Sculpture, Tampon, Peinture de texture) ·
  **Eau** (Lac, Rivière, Réseau fluvial, Littoral) · **Macro** (Chaîne de
  montagnes, Chaîne de vallées, Érosion hydraulique, Érosion thermique/vent) ·
  **Structures** (Grotte, Surplomb, Arche, Portail de donjon).
- Bouton = icône + libellé, outil actif surligné (même ambre que l'actuelle
  toolbar), tooltip avec raccourci, clic = `Execute` de l'action outil
  (wrap `SetActiveTool`).
- Toggleable depuis Fenêtre ; la disposition par défaut du DockBuilder est
  mise à jour pour réserver la colonne gauche.

## 7. Barre de statut (en bas, ~24 px)

Fenêtre ImGui fixe pleine largeur en bas (même technique que la toolbar,
jamais sur le viewport 3D) : à gauche le message de session
(`WorldEditorSession::Status()`), puis carte courante (zone id), outil actif ;
à droite l'indicateur **« Tout enregistré » / « ● Non sauvegardé »** (même
source dirty que la confirmation de Quitter). La zone réservée au dockspace
est réduite d'autant.

## 8. Palette de commandes `Ctrl+P` et fenêtre Raccourcis

- **Palette** : fenêtre modale centrée ouverte par `Ctrl+P` (ajout dans
  `HandleShortcut`), champ texte avec focus automatique, filtrage incrémental
  sur libellé + catégorie des actions du registre. Le filtrage/classement est
  une **fonction pure** (`FilterActions(query, actions) → indices ordonnés`,
  insensible aux accents/casse, préfixe > sous-chaîne) testée unitairement.
  Navigation ↑/↓, `Entrée` exécute (si `EnabledPredicate` vrai — sinon item
  grisé non exécutable), `Échap` ferme.
- **Fenêtre Raccourcis clavier** (lecture seule) : tableau généré du registre,
  groupé par catégorie, colonnes Libellé | Raccourci. Les raccourcis
  « hors action » (déplacement caméra WASD/ZQSD, numpad modes caméra) sont
  ajoutés via une petite table statique documentée dans le même fichier.
  Accessible par Édition et Aide.

## 9. Tests (ctest, exécutés par build-linux)

- `EditorActionRegistryTests` : unicité des ids, présence des actions
  attendues par catégorie, prédicats enabled (pile undo vide → `file.save`
  actif mais `edit.undo` grisé), textes de raccourcis sans doublon
  contradictoire.
- `EditorToolbarTests` adaptés : mêmes invariants géométriques sur le nouveau
  contenu actions.
- `ToolPalettePanel` : construction des groupes (pure data) — familles
  complètes, 15 outils couverts, aucun oublié/dupliqué (test croisé avec
  l'enum `ActiveTool`).
- `CommandPaletteTests` : filtrage pur (accents, casse, préfixe vs
  sous-chaîne, requête vide → tout, ordre stable).
- Cartes récentes : sérialisation/désérialisation `UserPrefs`, plafond à 8,
  déduplication, plus récent en tête.

## 10. Découpage en PRs (empilées, merge dans l'ordre)

1. **PR 1 — Registre d'actions + menus + statut** : module `actions/`,
   réorganisation complète de la barre de menu, barre de statut, Quitter réel
   + modale, cartes récentes, fenêtre Préférences, suppression de la barre
   anglaise fantôme. Tests registre + récents.
2. **PR 2 — Barre d'actions + palette d'outils** : reconversion
   `EditorToolbar`, `ToolPalettePanel`, disposition par défaut, icônes.
   Tests toolbar + palette.
3. **PR 3 — Palette de commandes + fenêtre Raccourcis** : `Ctrl+P`, filtrage
   pur, fenêtre récap. Tests filtrage.

**Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur
(aucun opcode, aucune migration, aucune config serveur).

## 11. Hors périmètre (explicitement)

- Rebinding des raccourcis (rejeté : lecture seule).
- Commande « Dupliquer la sélection » : n'existe pas dans le CommandStack
  actuel ; à créer dans un chantier édition dédié si besoin.
- Dispositions nommées type UE (Charger/Enregistrer la disposition) — on garde
  uniquement Réinitialiser ; extension future possible.
- Recherche intégrée en tête de chaque menu (la palette Ctrl+P couvre le
  besoin en un seul endroit).
- Toute évolution du rendu (ciel, viewport) — discutée séparément.
- Le menu in-game de l'éditeur intégré au client (`m_editorEnabled` sans
  `--world-editor`) : le chantier cible le binaire éditeur monde ; les
  chemins partagés doivent rester compilables et le comportement in-game
  inchangé.
