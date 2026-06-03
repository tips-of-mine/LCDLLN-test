# Conception — Éditeur monde : boucle d'édition d'une zone (complète et utilisable)

- **Date** : 2026-06-03
- **Sous-projet** : 1 / 3 (fondation — précède « éditer → jouer » puis « monde entier »)
- **Statut** : conception validée, prête pour le plan d'implémentation
- **Déploiement** : ✅ client/éditeur uniquement — **aucun redéploiement serveur**

## 1. Contexte et problème

Le moteur client/serveur de LCDLLN est avancé. L'**éditeur monde**
(`lcdlln_world_editor.exe`, voir `src/world_editor/`, ~34 000 lignes, 256 fichiers)
contient déjà beaucoup d'outils matures (sculpt, stamps, splat paint, chaînes de
montagnes, vallées, érosion hydraulique/thermique, rivières/lacs/réseau hydro/côtes,
grottes/arches/surplombs, portails de donjon, placement d'arbres). Mais du point de
vue de l'utilisateur, **il n'est pas utilisable pour construire la carte** : « la
seule chose qui marche, c'est créer une zone, mais on ne peut rien en faire ».

### Diagnostic (analyse du code)

L'architecture des **données** est saine : les 10 outils de terrain et les commandes
undo/redo travaillent **tous** sur le même modèle, `TerrainDocument` (chunks
`terrain.bin`, magic `TRRN`), qui est aussi le format que le **jeu streame**
(`StreamCache::LoadTerrainChunk`, `src/client/world/StreamCache.cpp`). Le
`height.r16h` n'est qu'un **cache GPU** reconstruit depuis les chunks et affiché par
`TerrainRenderer`. Il n'y a donc **pas** de mauvais choix d'architecture à corriger.

Ce qui est cassé, c'est la **plomberie de la boucle de travail** :

1. **Créer-zone ne remplit pas les chunks.** `WorldEditorSession::ActionNewMap`
   (`src/world_editor/ui/WorldEditorSession.cpp`) écrit `height.r16h` + splat + grass
   + le JSON méta, mais **n'initialise aucun chunk** dans `TerrainDocument`. Le modèle
   source de vérité est donc vide après création.
2. **La synchro chunks→GPU est sous gardes fragiles.**
   `Engine::SyncWorldEditorHeightmapFromDocument` (`src/client/app/Engine.cpp`) ne
   s'exécute que si `m_worldEditorExe && m_worldEditorShell->IsInitialized() &&
   m_worldEditorTerrainTools.IsValid() && m_terrain.IsValid()`. Si
   `RebuildWorldEditorTerrainGpu` n'a pas tourné (retour anticipé quand le heightmap
   est absent/vide), les édits ne s'affichent jamais.
3. **La sauvegarde est un stub** en mode éditeur **embarqué** (`--editor`) :
   `m_terrainSaveHook` y retourne `true` sans rien écrire (`Engine.cpp`). Elle ne
   fonctionne réellement que dans le `.exe` **standalone** (`--world-editor`).
4. **Code mort** : l'ancien chemin de pinceau `TerrainEditingTools::ApplyBrush`
   (`Engine.cpp`, garde `m_worldEditorSession` legacy) n'est plus utilisé par le
   `WorldEditorShell` moderne et brouille la lecture.
5. **Panneaux placeholders** : Inspector, Outliner, Console, Asset Browser affichent
   un texte « placeholder M100.1 » et ne font rien. Pas de **système de sélection**
   ni de **hiérarchie de scène**.
6. **Viewport** : `ScenePanel` affiche un placeholder ; la 3D est rendue en **plein
   écran** derrière les panneaux ImGui flottants. Le render-to-texture du dock
   (`EditorViewportRenderTarget`, « PR 2 ») n'est pas branché.

## 2. Objectif

Rendre l'éditeur **réellement utilisable pour créer et éditer une zone de bout en
bout** : créer/ouvrir une zone, voir le terrain, l'éditer avec les outils existants,
inspecter/sélectionner les entités, **sauvegarder, fermer, rouvrir, et retrouver un
résultat identique**. Livrer une expérience *finie* (vrais panneaux, sélection,
viewport intégré, un seul pipeline propre), pas seulement un correctif minimal.

### Critère d'acceptation principal

> Dans `lcdlln_world_editor.exe` : créer une zone → sculpter un relief → peindre une
> texture splat → poser un prop → **Enregistrer** → fermer l'éditeur → rouvrir →
> **charger la zone** → le relief, la texture et le prop sont **identiques** ; la
> sélection d'une entité affiche ses propriétés dans l'Inspector ; l'Outliner liste
> les entités ; la Console montre les logs.

## 3. Principe directeur

**Les chunks (`TerrainDocument`) sont l'unique source de vérité.** Tout en dérive :

- **Rendu** : `r16h`/`HeightmapData` est un **cache GPU** reconstruit depuis les
  chunks (on ne réécrit **pas** `TerrainRenderer` — option ② hors périmètre).
- **Sauvegarde** : on persiste les chunks (`SaveDirtyToDisk`) comme artefact
  autoritaire ; `r16h` est reconstruit au chargement.
- **Panneaux / sélection** : le modèle de scène et l'Inspector lisent les
  documents (`TerrainDocument`, `WaterDocument`, `MeshInsertDocument`,
  `DungeonPortalDocument`, instances de layout), jamais une copie parallèle.
- **Un seul pipeline d'outils** : le moderne (`WorldEditorShell` / `ActiveTool`).
  L'ancien chemin pinceau est retiré.

**Cible** : le binaire **standalone** `lcdlln_world_editor.exe` (`--world-editor`).
Le mode embarqué `--editor` reste hors périmètre (son hook de sauvegarde stub sera
soit aligné, soit explicitement marqué non supporté).

## 4. Conception détaillée (blocs)

Les blocs sont conçus pour être **livrables et révisables indépendamment**, dans
l'ordre ci-dessous. A est la fondation ; F (le plus risqué) est en dernier.

### Bloc A — Cycle de vie du terrain fiable (cœur de la boucle)

**Rôle** : garantir que créer → éditer → voir → sauver → recharger fonctionne et est
cohérent, avec les chunks comme source de vérité.

- **A1. Créer-zone initialise les chunks.** `ActionNewMap` peuple `TerrainDocument`
  avec un jeu complet de chunks **plats** couvrant l'empreinte d'édition de la zone
  (résolution d'édition configurée, cf. `editor.world.*`), les marque *dirty*, et
  les écrit sur disque (`chunks/chunk_i_j/terrain.bin`). Plus de `TerrainDocument`
  vide. Le `r16h` initial est dérivé de ces chunks plats.
- **A2. Reconstruction GPU garantie.** Après création **et** après chargement,
  `RebuildWorldEditorTerrainGpu` s'exécute toujours (plus de retour anticipé silencieux
  sur heightmap absent : on le reconstruit depuis les chunks). La caméra se recentre
  sur la zone.
- **A3. Synchro chunks→GPU robuste.** Définir **un seul propriétaire** de l'état
  « terrain sale → ré-upload » : tout changement de chunk (callback
  `TerrainDocument::SetOnChunkChanged`) déclenche la synchro au prochain frame, sans
  gardes qui peuvent l'empêcher silencieusement. Si une précondition manque
  (ex. GPU pas prêt), on **diffère** la synchro plutôt que de l'abandonner.
- **A4. Sauvegarde autoritaire = chunks.** `ActionSaveCurrentMap` appelle
  `TerrainDocument::SaveDirtyToDisk` (chunks) **avant** d'écrire le JSON méta + splat
  + grass. Le `r16h` n'est plus la vérité stockée (il peut rester en cache disque
  optionnel pour accélérer le 1er affichage, mais reconstructible).
- **A5. Rechargement vérifié.** `ActionLoadMapByZoneId` relit les chunks dans
  `TerrainDocument`, reconstruit le GPU. Aller-retour testé identique (cf. Bloc I).

**Effets de bord** : écritures disque dans `world_editor/maps/<zoneId>/chunks/`.
**Contraintes** : exécution main thread pour les uploads GPU.

### Bloc B — Modèle de scène + sélection

**Rôle** : exposer les entités éditables d'une zone sous une forme unifiée, et un état
de sélection partagé, pour alimenter Outliner/Inspector et le picking viewport.

- **B1. Modèle de scène (`EditorSceneModel`, nouveau, PascalCase).** Vue agrégée,
  en lecture, des entités de la zone courante construite à partir des documents
  existants :
  - Terrain (entité implicite unique : métadonnées de zone).
  - Plans d'eau (`WaterDocument` : lacs, rivières, océan).
  - Mesh inserts (`MeshInsertDocument` : grottes, arches, surplombs).
  - Portails de donjon (`DungeonPortalDocument`).
  - Instances de layout (props/arbres, `WorldMapEditLayoutInstance`).

  Chaque entité expose : `id` stable, `type`, libellé, `transform` (si applicable),
  et un accès à ses propriétés typées. Le modèle **ne possède pas** les données ; il
  référence les documents (source de vérité unique).
- **B2. État de sélection (`EditorSelection`, nouveau).** Possédé par
  `WorldEditorShell`, partagé par référence aux panneaux. Contient la/les entités
  sélectionnées et notifie les observateurs au changement.
- **B3. Picking viewport.** Clic dans la vue 3D (hors outil actif d'édition) →
  raycast (réutilise `TerrainRaycast` et les bornes des entités) → sélectionne
  l'entité la plus proche, ou le terrain. Met à jour `EditorSelection`.

**Contraintes** : le modèle de scène se reconstruit/rafraîchit quand un document
change ; pas de copie persistante divergente.

### Bloc C — Panneau Outliner (réel)

**Rôle** : remplacer le placeholder par une liste navigable des entités de la zone.

- Liste groupée par type (Terrain / Eau / Volumes / Donjons / Props), depuis
  `EditorSceneModel`.
- Clic = sélectionne l'entité (`EditorSelection`) ; surbrillance de la sélection
  courante (synchro bidirectionnelle avec le picking viewport).
- Bascule de **visibilité** par entité/groupe (drapeau d'affichage éditeur ; n'altère
  pas les données).

### Bloc D — Panneau Inspector (réel)

**Rôle** : afficher et éditer les propriétés de la sélection courante.

- Affiche les propriétés de l'entité sélectionnée depuis `EditorSceneModel`.
- Champs éditables (transform : position/rotation/échelle ; params spécifiques au
  type) qui émettent des **commandes** via `CommandStack` (donc undo/redo). Aucune
  écriture directe hors commande.
- Sélection « Terrain » : affiche les métadonnées de zone (id, résolution, taille
  monde, niveau d'eau, atmosphère) en lecture, éditables là où c'est déjà supporté.

### Bloc E — Panneau Console (réel)

**Rôle** : afficher la sortie `Log` du moteur dans l'éditeur.

- Branche un *sink* sur le système `Log` existant (`src/shared/core/`) vers un ring
  buffer borné (taille configurable, ex. `editor.console.capacity`).
- Affichage filtrable par niveau (info/warn/error), auto-scroll optionnel, bouton
  « clear ». Thread-safe (le Log peut venir d'autres threads → buffer protégé).

### Bloc F — Viewport 3D dans le panneau (le plus risqué — en dernier)

**Rôle** : faire vivre la vue 3D **dans** le dock `ScenePanel` au lieu du plein écran.

- Compléter `EditorViewportRenderTarget` : passe FrameGraph copiant
  `SceneColor_LDR` → image du render target → texture ImGui affichée par
  `ScenePanel`.
- Redimensionnement du dock → redimensionne le render target ; le picking (B3) et la
  caméra utilisent les coordonnées **locales au panneau**.
- **Repli de sécurité** : si non terminé/instable, on conserve le rendu plein écran
  actuel. Ce bloc est placé en **fin de plan** pour qu'un échec ici ne bloque aucun
  autre bloc. ⚠️ Ne pas toucher au `frontFace`/`cullMode` du terrain (cf. CLAUDE.md,
  garde anti-régression « terrain invisible »).

### Bloc G — Nettoyage legacy

**Rôle** : ne laisser qu'un seul pipeline d'outils.

- Supprimer la branche pinceau morte `TerrainEditingTools::ApplyBrush` et l'objet
  `m_worldEditorSession` legacy dans `Engine.cpp`, ainsi que les gardes associées.
- **Précision** : `TerrainEditingTools` **reste** utilisé comme *uploader* du cache
  GPU (`FlushHeightmap`) par la synchro du Bloc A — on ne retire que la branche
  d'édition obsolète, pas la classe.

### Bloc H — Menu Fichier + persistance config

**Rôle** : un menu Fichier réellement câblé et une config persistée.

- Nouveau / Ouvrir / Enregistrer / Enregistrer sous branchés sur les actions des
  blocs A (et le modèle de scène). « Ouvrir » liste les zones existantes.
- Ajouter `engine::core::Config::SaveToFile` (le TODO M100.4) pour persister les
  préférences éditeur + le **layout des docks** ImGui entre sessions.

### Bloc I — Vérification

**Rôle** : prouver la boucle et prévenir les régressions.

- **Tests unitaires portables** (compilés et exécutés en CI `build-linux`/ctest,
  comme les 21 suites existantes de `src/world_editor/tests/`) :
  - Aller-retour terrain : créer chunks plats → éditer → `SaveDirtyToDisk` → relire →
    égalité octet-à-octet des hauteurs.
  - `EditorSceneModel` : reflète correctement le contenu des documents.
  - `EditorSelection` : sélection/désélection, notifications.
  - Commandes Inspector : édition d'un transform → undo → redo cohérents.
- **Limites** : l'UI ImGui/Vulkan est **Windows-only** (no-op Linux) → le rendu et le
  picking ne sont pas testables en CI ; vérification manuelle via le scénario
  d'acceptation (§2). La **logique** ciblée par les tests est portable.

## 5. Découpage en livraisons (PR)

Pressenti (le plan détaillé suivra) :

1. **PR A** — Cycle de vie terrain fiable (Bloc A) + tests aller-retour (Bloc I
   partiel). *Valeur immédiate : la boucle marche enfin.*
2. **PR B** — Modèle de scène + sélection + picking (Bloc B) + tests.
3. **PR C/D/E** — Panneaux Outliner, Inspector, Console (peuvent être 1 à 3 PR).
4. **PR G/H** — Nettoyage legacy + menu Fichier + `Config::SaveToFile`.
5. **PR F** — Viewport-dans-panneau (en dernier, avec repli plein écran).

Chaque PR : CI verte avant la suivante. **Déploiement serveur : non requis** pour
toutes (client/éditeur uniquement).

## 6. Risques et mitigations

- **Viewport render-to-texture (F)** : risque de rouvrir le bug « terrain invisible »
  si on touche au pipeline. *Mitigation* : bloc isolé en fin de plan, repli plein
  écran, interdiction de modifier `frontFace`/`cullMode` (CLAUDE.md).
- **Pas de toolchain locale** (cmake/MSVC absents) : compilation **uniquement** via
  CI / VS. *Mitigation* : itérations via CI ; tests pensés pour ctest `build-linux`.
- **Coût mémoire des chunks** (264 Ko/chunk) : initialiser tous les chunks d'une zone
  d'un coup peut être lourd. *Mitigation* : initialiser à la résolution d'édition
  configurée ; chargement paresseux conservé là où pertinent.
- **Mode embarqué `--editor`** : hors périmètre ; risque de confusion utilisateur.
  *Mitigation* : documenter que le binaire supporté est `lcdlln_world_editor.exe`.

## 7. Hors périmètre

- Sous-projet 2 : export → charger/jouer la zone dans le jeu.
- Sous-projet 3 : grille multi-zones, carte d'ensemble, navigation inter-zones.
- Option ② : suppression du `r16h` / rendu éditeur directement depuis les chunks.
- Panneau Asset Browser complet (peut être abordé plus tard).

## 8. Conventions

- Commentaires en **français**, identifiants/commandes en **anglais**.
- Nouveau code/classes/fichiers en **PascalCase** (`EditorSceneModel`,
  `EditorSelection`) ; conventions existantes `m_camelCase`/`kPascalCase` conservées ;
  docs en kebab-case.
- **Règle stricte éditeur monde** : toute fonction ajoutée/modifiée dans
  `src/world_editor/` (et les portions éditeur de `Engine.cpp`) est **documentée**
  (`///` Doxygen : rôle, params non-évidents, effets de bord, contraintes thread).
- Pas de terme interdit dans le code/commits/PR.
