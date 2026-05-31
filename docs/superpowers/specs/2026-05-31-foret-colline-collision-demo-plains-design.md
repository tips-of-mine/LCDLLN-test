# Spec — Forêt riveraine, colline et collision props sur `demo_plains`

Date : 2026-05-31
Statut : **implémenté** (branche `feat/demo-plains-decor-collision`).

> **Note d'implémentation — support couleur de sommet (COLOR_0).** Découvert en cours :
> les props « nature » (arbres, herbe, fleurs…) sont colorés par couleur de sommet
> (`COLOR_0`), sans texture, alors que les props *trim* (94 existants) sont texturés.
> Le pipeline props (texturé-PBR) ignorait la couleur de sommet. Ajout réalisé (choix
> utilisateur « fidèle ») : `StaticVertex` gagne `color[4]` (stride GPU 48 ; format `.mesh`
> disque reste 32, étendu à l'upload) ; `GeometryPass` + `ShadowMapPass` passent à stride
> 48 ; un flag matériau `VertexColorAlbedo` fait que `gbuffer_geometry.frag` prend l'albedo
> dans `COLOR_0` pour les props sans texture (props texturés et avatar inchangés).
> Conversion Blender = méthode retenue (les 94 existants venaient de Blender), pas FBX2glTF.

## 1. Objectif

Faire évoluer la carte par défaut `demo_plains` :

1. Retirer les objets décoratifs placés autour de l'eau (sauf le coffre et le villageois).
2. Convertir **tous** les props FBX non encore convertis en glTF (244 fichiers), pour
   disposer du catalogue complet.
3. Dessiner une **forêt riveraine** d'environ 80 arbres (mélange de 4 essences) autour
   du bassin d'eau.
4. Ajouter une **colline prononcée** (~15 m de haut, ~40 m de rayon).
5. Rendre **tous les objets solides** : les personnages ne peuvent plus traverser les
   arbres, le coffre ni le villageois (collision props ↔ personnage, aujourd'hui
   inexistante).

Le tout livré dans **une seule PR**.

## 2. État actuel (contexte vérifié)

- **Zone par défaut** : `demo_plains` (`game/data/zones/demo_plains/`).
  - Heightmap : `terrain_height.r16h`, format `HAMP` (magic `0x504D4148`), grille
    **1025×1025** uint16 little-endian, en-tête 12 octets (magic + width + height),
    données row-major `index = z*width + x`. Normalisé `[0,1] = h/65535`.
  - Eau de test procédurale : `config.json > world.test_water` = centre **(55, 0)**,
    `half_size_m = 40`, `depth_m = 2`, `enabled = true`. Le bassin couvre
    ~`x∈[15,95]`, `z∈[-40,40]`. Bol déjà creusé dans le heightmap.
- **Objets placés** : `config.json > world.interactables`, indices 0 à 9.
  - `0` = **Villageois** (NPC, mesh `models/characters/humains/Male_Ranger/Male_Ranger.glb`,
    scale 95, rot_x -90, yaw 270) — **à garder**.
  - `1` = **Coffre** (`meshes/props/Chest_Wood.gltf`) — **à garder**.
  - `2` à `9` = décors riverains (Stall_Empty, Stall_Cart_Empty, Barrel, Crate_Wooden ×2,
    Barrel_Apples, Workbench, Cauldron) — **à retirer**.
- **Rendu des props** : `Engine::LoadInteractableProps()`
  (`src/client/app/Engine.cpp:9811`) charge chaque mesh statique via
  `StaticMeshLoader::LoadCpuOnlyForTests`, **cuit la transformation monde dans les
  sommets** (contournement d'un bug d'instance buffer partagé), pose le point le plus
  bas au sol (`GroundHeightAt`), résout les textures **par URI relatif au dossier du
  mesh** (`meshDir + baseColorUri/normalUri/ormUri`). Rendu par
  `Engine::RecordPropsGeometry()` (`Engine.cpp:9962`), un draw call par partie de matériau.
- **Props convertis** : `game/data/meshes/props/` contient **94** `.gltf` (+ `.bin`),
  générés par **Blender** (`generator: "Khronos glTF Blender I/O v4.0.44"`), matériaux
  `MI_Trim_Furniture/Metal/Cloth`, textures *trim* partagées
  (`T_Trim_Furniture/Metal/Cloth_{BaseColor,Normal,ORM}.png`, `T_Page_Noise.png`) dans
  le même dossier.
- **FBX sources** : `tools/asset_pipeline/inbox/meshes/props/` contient **338** `.fbx`
  → **244 non convertis**. Ces FBX référencent déjà les noms de textures `T_Trim_*`
  (28 occurrences dans `Barrel.fbx`).
- **Collision** : `IWorldCollider` (`src/client/gameplay/CharacterController.h:12`).
  Seule implémentation : `TerrainCollider` (sol via heightmap + `QueryWater`).
  **Aucune collision contre les props** (commenté explicitement, ligne 28 de
  `TerrainCollider.h`). Le `CharacterController` reçoit aujourd'hui le `TerrainCollider`
  nu (`Engine.cpp` ~ligne 5018).
- **Environnement** : pas de toolchain de build C++ locale (compilation via CI).
  **Blender 5.1 est installé** (`C:/Program Files/Blender Foundation/`). `FBX2glTF.exe`
  présent mais inadapté ici (recette props = Blender).

## 3. Périmètre et découpage

Une PR, branche `feat/demo-plains-foret-colline-collision`, comprenant :

1. Conversion d'assets (data) — local, Blender headless.
2. Édition de la scène (data) — `config.json` + scripts de génération.
3. Édition du heightmap (data) — script Python.
4. Collision props (code C++) — compilé en CI.

## 4. Conception détaillée

### A. Retrait des objets autour de l'eau

- Dans `config.json > world.interactables`, **supprimer les indices 2 à 9**.
- **Conserver** les indices 0 (Villageois) et 1 (Coffre).
- Mettre à jour `_comment_interactables` (il décrit encore le « chemin de barils/caisses »
  et les bannières autour du bassin).

### B. Conversion de tous les props manquants (244) via Blender headless

- **Script** : `tools/asset_pipeline/convert_props_blender.py`, lancé via
  `blender --background --python tools/asset_pipeline/convert_props_blender.py`.
- Pour chaque FBX de `inbox/meshes/props/` **sans** `.gltf` correspondant dans
  `game/data/meshes/props/` :
  1. Importer le FBX (reset de la scène entre chaque).
  2. Remapper les nœuds image des matériaux vers les `T_Trim_*.png` partagés déjà
     présents dans `game/data/meshes/props/` (par nom de texture).
  3. Exporter en `.gltf` séparé (`.gltf` + `.bin`) dans `game/data/meshes/props/`,
     avec les réglages reproduisant l'export d'origine : matériaux `MI_Trim_*`,
     `doubleSided`, ORM packé (occlusion-roughness-metallic), attribut `COLOR_0`,
     textures **référencées** (pas redupliquées) par URI relatif.
- **On ne retouche pas les 94 props déjà convertis** (aucun churn).
- **Verrou de validation (sans build client)** : re-convertir un prop déjà commité
  (`Barrel`) vers un dossier temporaire, puis **differ** le `.gltf` produit contre
  `game/data/meshes/props/Barrel.gltf` (structure : matériaux, noms, URIs de textures,
  `doubleSided`, attributs de primitives). Ajuster les réglages d'export Blender
  jusqu'à correspondance structurelle, **avant** de lancer le batch sur les 244.
- **Volume** : ~488 fichiers ajoutés (`.gltf` + `.bin`). Textures *trim* déjà présentes.
- **Risque** : un FBX peut échouer à l'import ou produire une géométrie/matériau hors
  norme. Le script **logge** chaque échec et **continue** ; la liste des échecs est
  reportée en fin de run pour traitement manuel (pas de blocage du batch).

### C. Forêt riveraine (~80 arbres) — bloc de données `world.scenery`

- **Nouveau bloc** `config.json > world.scenery` : tableau d'objets décoratifs **solides
  mais non interactifs**, distinct de `world.interactables`. Schéma par entrée :
  ```json
  { "mesh": "meshes/props/CommonTree_2.gltf", "x": 30.0, "z": -22.0,
    "yaw_deg": 137.0, "scale": 1.0, "collision_radius": 0.6 }
  ```
  - `mesh` : chemin relatif à `paths.content`.
  - `x`, `z` : position monde (m) ; Y posé au sol au runtime (déjà géré par le baking).
  - `yaw_deg`, `scale` : orientation et échelle.
  - `collision_radius` : rayon du **cylindre de collision** (m). Pour les arbres = rayon
    de **tronc** (≈ 0.4–0.8 m selon l'essence), **pas** le rayon du feuillage.
- **Chargement** : généraliser `LoadInteractableProps()` pour traiter aussi
  `world.scenery` (même chemin de baking/rendu), ou extraire une `LoadScenery()` qui
  réutilise la même logique. Les entrées scenery sont rendues par le même
  `RecordPropsGeometry` et enregistrent un collisionneur (cf. E).
- **Génération** : script Python `tools/world_gen/scatter_forest.py` (déterministe,
  graine fixe) qui :
  - Disperse ~80 arbres en mélangeant les 4 essences (CommonTree_1..5, Pine_1..5,
    DeadTree_1..5, TwistedTree_1..5).
  - Zone de dispersion : **pourtour du bassin** (berges juste à l'extérieur du rectangle
    eau `x∈[15,95] z∈[-40,40]`) + terres environnantes jusqu'à ~`x∈[-25,125]`,
    `z∈[-75,75]`.
  - **Exclusions** : intérieur du rectangle d'eau ; disque de ~6 m autour du spawn (0,0),
    du villageois (4,0) et du coffre (-4,0) ; distance minimale entre arbres (~3 m) pour
    éviter l'interpénétration.
  - Variations : `yaw_deg` aléatoire 0–360, `scale` aléatoire ~0.8–1.3, essence aléatoire.
  - `collision_radius` par essence (tronc) : valeurs de départ CommonTree 0.6, Pine 0.5,
    DeadTree 0.5, TwistedTree 0.8 (ajustables).
  - **Sortie** : écrit le bloc `world.scenery` dans `config.json` (réinscriptible : le
    script remplace le bloc existant, idempotent). Rejouable pour densifier/éclaircir.
- **Knob perf** : le compte d'arbres est un paramètre du script. ~80 arbres = ~80 meshes
  CPU bakés + ~80 draw calls de meshes lourds (200–560 ko). Le système actuel tournait à
  ~10 props ; **risque perf à valider au runtime**. Si trop lourd : réduire le compte.
  L'instanciation GPU est hors périmètre (YAGNI).

### D. Colline prononcée (~15 m)

- **Script** : `tools/world_gen/raise_hill.py` modifie
  `game/data/zones/demo_plains/terrain_height.r16h`.
- **Forme** : bosse gaussienne lisse, hauteur de crête ~15 m, rayon ~40 m.
- **Emplacement** : **nord-ouest**, centre monde ≈ **(-60, -55)** (x négatif = ouest,
  z négatif = nord), **hors** du rectangle d'eau et loin du spawn — repère visible depuis
  le spawn en se retournant.
- **Mapping monde → texel** : le script lit la grille (`width`/`height` de l'en-tête) et
  résout l'**échelle monde** (mètres/texel) et l'**échelle de hauteur** (mètres pour
  `h=65535`) ainsi que l'**origine** du terrain. Ces constantes ne sont **pas** dans
  `config.json` : à localiser dans `TerrainRenderer` et/ou `zone.meta` (36 octets) lors
  de l'implémentation (première étape du plan). Le script convertit
  (centre, rayon, hauteur) monde en gaussienne texel et **ajoute** Δh au heightmap
  existant (préserve le bol d'eau et le reste du relief), en clampant `[0,65535]`.
- **Idempotence** : le script part d'une copie de référence du heightmap (sauvegarde
  `.r16h.bak` au premier run) pour pouvoir rejouer sans empiler les collines.
- **Collision** : aucune ; `TerrainCollider` échantillonne le heightmap → la colline est
  automatiquement marchable. Au sommet, pente > 45° (`maxSlopeDeg`) → sommet non
  marchable, flancs OK (comportement réaliste).
- **Normales** : le terrain régénère sa normal map au boot depuis le heightmap
  (`GenerateAndUploadNormalMap`) → éclairage de la colline correct sans action.

### E. Collision props ↔ personnage (code C++)

- **Nouvelle classe** `CompositeWorldCollider`
  (`src/client/gameplay/CompositeWorldCollider.{h,cpp}`), implémente `IWorldCollider` :
  - Détient un `const TerrainCollider*` (sol) et un `std::vector<PropCylinder>`.
  - `PropCylinder { float cx, cz, radius, baseY, topY; }` (cylindre vertical).
  - `SweepCapsule` : exécute le sweep terrain **puis** teste la capsule contre chaque
    cylindre (test 2D cercle-vs-cercle sur XZ avec rayons additionnés, borné en Y par
    `[baseY, topY]`), et retourne le **hit de plus petite `fraction`** (le plus proche).
    Normale = direction horizontale capsule→axe cylindre.
  - `QueryWater` : délégué au `TerrainCollider`.
- **Construction des cylindres** : dans `LoadInteractableProps`/`LoadScenery`, après le
  baking de chaque prop solide, calculer `baseY = groundY`, `topY = groundY + hauteurMesh`
  (la hauteur vient des bornes du mesh baké, déjà parcouru pour `minY` — ajouter `maxY`),
  `radius = collision_radius` (ou, à défaut, rayon d'empreinte XZ du mesh). Enregistrer le
  cylindre dans le `CompositeWorldCollider`.
- **Objets solides** :
  - Arbres (scenery) : `collision_radius` du tronc.
  - Coffre (interactable 1) : rayon d'empreinte XZ.
  - Villageois (interactable 0) : NPC skinné (hors `m_props`), mais possède position +
    rayon ; lui ajouter un cylindre (rayon ~0.4 m, hauteur ~1.8 m).
  - Les futurs props décoratifs ajoutés via scenery sont solides par défaut.
- **Branchement** : `Engine` détient un `CompositeWorldCollider m_worldCollider`
  initialisé avec `&m_terrainCollider` + les cylindres construits au chargement.
  L'appel `m_characterController.Update(dt, input, ...)` (~`Engine.cpp:5018`) reçoit
  `m_worldCollider` au lieu de `m_terrainCollider`.
- **Tests** (CI `build-linux` lance ctest) : `CompositeWorldColliderTests` —
  - capsule traversant un cylindre est arrêtée (`fraction < 1`, normale horizontale) ;
  - capsule passant à côté n'est pas affectée ;
  - capsule au-dessus (`> topY`) ou en dessous (`< baseY`) du cylindre n'est pas affectée ;
  - délégation `QueryWater` au terrain ;
  - sans cylindre, comportement identique au `TerrainCollider` seul.

### F. Documentation

- Mettre à jour `tools/asset_pipeline/README.md` : section conversion props via Blender
  headless (script, recette trim, validation par diff).
- Documenter les deux scripts `tools/world_gen/*.py` (rôle, paramètres, idempotence) en
  en-tête de fichier.
- Les fonctions C++ nouvelles/modifiées sont commentées (convention repo : commentaires
  en français, `///` pour l'API publique).

## 5. Déploiement

✅ **Client uniquement — aucun redéploiement serveur.** Le serveur ne lit ni
`world.scenery`, ni `world.interactables`, ni le heightmap, ni la collision (purement
rendu/gameplay client). Aucun opcode, handler, migration DB ni clé de config serveur
touchés.

## 6. Conventions et garde-fous

- **Winding/culling** : aucune modification des pipelines de rasterisation. Les props
  réutilisent `GeometryPass` existant ; la collision est CPU. (Rappel CLAUDE.md : ne
  jamais toucher `frontFace`/`cullMode` sans vérifier le winding réel.)
- **Nommage** : nouveau code/fichier en PascalCase (`CompositeWorldCollider`,
  `PropCylinder`) ; scripts Python en `snake_case` (convention Python du repo) ; docs en
  kebab-case.
- **Anti-doublon** : `CompositeWorldCollider` est nouveau (vérifié : aucune collision
  props existante). On réutilise `TerrainCollider`, `StaticMeshLoader`,
  `LoadInteractableProps` plutôt que de dupliquer.
- **Legacy** : aucun fichier de `legacy/` touché.

## 7. Risques et points ouverts

1. **Perf forêt** (~80 meshes lourds, draw calls) : à valider au runtime ; knob = compte
   d'arbres. Pas de build local → vérification par l'utilisateur après CI.
2. **Fidélité conversion Blender** : réglages d'export à caler par diff contre les 94
   existants avant batch. FBX problématiques loggés et traités à part.
3. **Constantes d'échelle terrain** (world-scale / height-scale / origine) à localiser
   (TerrainRenderer / zone.meta) — première tâche du plan, bloque le script colline.
4. **Composition** : forêt à l'est (eau) et colline au nord-ouest sont spatialement
   disjointes (choix utilisateur : forêt « autour de l'eau » + colline « prononcée »).
   Option ultérieure : parsemer quelques arbres sur les flancs bas de la colline.

## 8. Critères d'acceptation

- `world.interactables` ne contient plus que le Villageois et le Coffre.
- Les 244 props manquants sont convertis en `.gltf` + `.bin` dans
  `game/data/meshes/props/`, structurellement conformes aux 94 existants.
- `config.json > world.scenery` contient ~80 arbres répartis autour de l'eau.
- `terrain_height.r16h` présente une colline ~15 m au nord-ouest sans casser le bol d'eau.
- Le client compile en CI ; `CompositeWorldColliderTests` passe sous ctest.
- Au runtime (vérif utilisateur) : objets autour de l'eau retirés, coffre + villageois
  présents et **solides**, forêt visible, arbres **solides**, colline marchable.
