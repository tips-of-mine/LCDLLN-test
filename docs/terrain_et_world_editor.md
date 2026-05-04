# Terrain décalé : jeu et World Editor

Ce document décrit le pipeline **terrain → G-buffer** partagé entre le **client jeu** et l’exécutable **World Editor** (Windows), ainsi que la configuration et les limites connues.

## Chaîne de rendu

1. **Passe `Terrain_GBuffer`** (frame graph) : si `GeometryPass` expose une sous-passe en **LOAD** sur le G-buffer (`HasLoadPass()`) et qu’un `TerrainRenderer` est valide (`m_terrain.IsValid()`), le terrain est dessiné **avant** la passe `Geometry`.
2. **Passe `Geometry`** : enregistre la géométrie classique avec `loadExistingGbuffer = true` lorsque le terrain est actif, pour **conserver** les attachements déjà remplis par le terrain (pas de clear intempestif).

Même logique pour le jeu et l’éditeur : un seul `TerrainRenderer` (`m_terrain`) ; seul le **mode d’initialisation** change.

## Client jeu

Pour les joueurs, le moteur **tente toujours** d’initialiser le terrain au boot (pas de case à cocher « activer le terrain »). Si le fichier heightmap est introuvable ou invalide, `TerrainRenderer::Init` échoue : le jeu continue sans sol décalé jusqu’à ce que l’asset soit présent.

- **`render.terrain.heightmap`** : chemin **relatif au content** (`paths.content`, souvent `game/data`). Si la clé est absente ou vide, défaut **`terrain/heightmap.r16h`**.
- **`render.terrain.splatmap`**, **`render.terrain.grass_mask`**, **`render.terrain.hole_mask`** : optionnels ; chaînes vides = comportements par défaut côté moteur (masque herbe absent → zéros).
- **`render.terrain.grass_mask_visual_strength`** : intensité de la teinte herbe en fragment (0 = désactivé ; défaut 0.35 si la clé est absente).
- Paramètres d’étendue / hauteur (origine, `world_size`, `height_scale`, tiling splat, etc.) : clés **`terrain.*`** lues par `TerrainRenderer::Init` (voir commentaires dans `TerrainRenderer.h`).

Les shaders terrain sont chargés comme les autres SPIR-V du content via `LoadTerrainSpirvWords` (fichiers `shaders/terrain.vert.spv` / `terrain.frag.spv`, etc.). Les sources GLSL sont dans le dépôt ; pour produire les `.spv` en local, utilisez `tools/compile_game_shaders.ps1` (Vulkan SDK). Le workflow GitHub **Build Windows** compile ces shaders avant d’empaqueter `game/data`.

## World Editor (Windows)

- **Répertoire de travail** : les heightmaps sont ouverts via `paths.content` (ex. `game/data`) **relatif au répertoire courant** au moment du lancement. Si le **répertoire de travail** au lancement ne contient pas `game/data` (ex. débogage VS avec un cwd projet), les heightmaps ne se résolvent pas. Au démarrage, l’exe **remonte** depuis son dossier (`build/.../pkg/world_editor/` typiquement) jusqu’au premier ancêtre qui contient `config.json` et `game/data/` ; le CMake copie aussi `game/data` à côté de l’exe au build. Sinon, lancez l’outil **depuis la racine du dépôt** ou fixez le cwd dans l’IDE.
- Le document monde fixe le chemin heightmap ; **`RebuildWorldEditorTerrainGpu()`** détruit puis réinitialise `m_terrain` et les **`TerrainEditingTools`** (sculpt CPU + upload GPU).
- **Splatmap** : le JSON d’édition porte `splatmap` (chemin relatif content). Fichier binaire **SLAP** (magic `0x50414C53`, uint32 LE largeur/hauteur, puis RGBA8 ligne par ligne) — même format que `TerrainEditingTools::SaveSplatMap`. Par défaut les nouvelles cartes créent `world_editor/maps/<zone>/splat.slap` (1024×1024, herbe 100 %). Sans fichier valide au chargement, `TerrainSplatting` retombe sur la splat par défaut.
- **Masque herbe (010)** : clé JSON `grass_mask` (défaut `grass.grms` à côté du SLAP). Format **GRMS** (`TerrainGrassDetail`, R8 même taille que la splat, UV identiques). Mode éditeur « Herbe » : brosse + option effacer ; sauvegarde écrit le GRMS via le hook terrain. Shader : binding 8, teinte albedo modulée par masque × `grass_mask_visual_strength`.
- **Routes (011, branche A livrée)** : mode terrain « Routes » — polyligne par clics gauches (points clampés au carré terrain), largeur + couche splat, puis « Appliquer sur splat » (`PaintSplatAlongPolyline`). Métadonnées dans le JSON (`routes` : `width_m`, `splat_layer`, `points` [[x,z],…]). L’aspect après redémarrage vient du **fichier SLAP** sauvegardé avec la carte ; le JSON sert à conserver / éditer la définition des routes. **Branche B** (mesh spline dédié) : non implémentée — extension future (voir ticket `011_world_editor_routes_couches_ou_splines.md`).
- **Recharger terrain GPU** (UI) : `WorldEditorSession::RequestTerrainGpuReload` → consommation dans `Update` → rebuild (height + splat depuis les chemins du document).
- **Upload heightmap** : préférence pour enregistrement dans le **command buffer** de la frame (staging + `RecordHeightmapR16UploadCommands`) ; repli sur `FlushHeightmap` si besoin.
- **Peinture splat** : mode UI « Splat » ; clic maintient `PaintSplat` puis `FlushSplatMap` pour l’aperçu GPU. **Sauvegarder l’édition** déclenche aussi l’écriture disque du SLAP (hook moteur avant le JSON).
- **Instances (tickets 009 + 013)** : tableau `instances` dans le JSON d’édition (`guid`, `gltf`, `position` monde XYZ, optionnels `yaw_deg`, `uniform_scale`). **013** : champs optionnels `species_id` (chaîne) et `shape_variant` (entier) pour les arbres issus du catalogue `world_editor/tree_species_catalog.json` (plusieurs espèces, ≥ deux glTF par espèce, plages `scale_min` / `scale_max` ; fichiers manquants → espèce ignorée, log). Le **layout exporté** `layout_from_editor.json` pour `zone_builder` reste au schéma minimal (`guid`, `gltf`, `position`) — pas de régression consommateur. Mode « Instances » (mode terrain 3) : clic simple sur le sol (`WasMousePressed`) pose ou déplace ; aperçu disques ImGui. **Exporter runtime** écrit `layout_from_editor.json` au format `zone_builder` (positions XZ converties avec `terrain.origin_x` / `terrain.origin_z`, clamp `[0, kZoneSize)`).
- Overlays ImGui : grille, preview brosse, marqueurs instances, picking rayon → hauteur via `HeightmapData::SampleBilinearNorm` (CPU).

### Reset caméra au create / load (chantier 2026-05-04)

À chaque appel de `Engine::RebuildWorldEditorTerrainGpu` (création de carte, chargement, ou « Recharger terrain GPU »), la caméra est **repositionnée au centre du terrain** : `(centerX, midGroundY + 80 m, centerZ)`, pitch `0.35 rad` (~20 deg), `farZ = max(5000, ws*1.5)`. Cette logique est gardée par `m_worldEditorExe` (le client jeu n'est jamais affecté). Sans ce reset, la caméra par défaut peut se retrouver hors du terrain (cas `ws = 10 km`) et le terrain reste hors champ après « Creer une nouvelle carte ».

### Fallback orange pour cartes sans texture utilisateur (chantier 2026-05-04)

Lorsque l'éditeur ouvre une carte dont **aucune** couche splat n'a reçu de mapping texture utilisateur (cas typique : juste après `Creer une nouvelle carte`), `RebuildWorldEditorTerrainGpu` lève le flag `noUserTextures` du `TerrainRenderer`. Le shader fragment terrain (`terrain.frag`) substitue alors l'albedo par un orange vif `vec4(1.0, 0.55, 0.1, 1.0)` ; les autres GBuffer outputs (normal, ORM, velocity) restent inchangés, donc la lighting pass applique un shading correct sur l'orange (relief lisible). Le push-constant terrain passe de 16 à 20 octets (un `int noUserTextures` ajouté en queue). Dès qu'une couche splat reçoit un mapping texture utilisateur dans le document, le prochain rebuild bascule sur le rendu normal. **Côté client jeu (`lcdlln.exe`)** : `m_worldEditorExe = false`, le flag n'est jamais levé, le rendu officiel reste **strictement inchangé**.

## Limites / pistes

- **Splat** : résolution pilotée par le fichier SLAP (typ. 1024²) ; le sculpt heightmap reste à la résolution `size` du document.
- **Falaises** : liste de meshes `.cliff` ; le jeu peut étendre la config plus tard pour des chemins multiples.
- **Linux** : l’UI World Editor reste Windows ; le **rendu terrain jeu** utilise le même code Vulkan (init systématique côté client jeu, comme sur Windows).

## Fichiers utiles

- `engine/Engine.cpp` — frame graph `Terrain_GBuffer`, init jeu `render.terrain`, rebuild éditeur.
- `engine/render/GeometryPass.{h,cpp}` — bit `loadExistingGbuffer` / passe LOAD.
- `engine/render/terrain/TerrainRenderer.{h,cpp}` — draw terrain dans le G-buffer.
- `engine/render/terrain/TerrainEditingTools.{h,cpp}` — sculpt heightmap + peinture splat + masque herbe GRMS + sauvegarde.
- `engine/editor/WorldEditorImGui.{h,cpp}` — overlays et bouton reload.
