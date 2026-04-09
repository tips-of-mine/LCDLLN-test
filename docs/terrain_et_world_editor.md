# Terrain décalé : jeu et World Editor

Ce document décrit le pipeline **terrain → G-buffer** partagé entre le **client jeu** et l’exécutable **World Editor** (Windows), ainsi que la configuration et les limites connues.

## Chaîne de rendu

1. **Passe `Terrain_GBuffer`** (frame graph) : si `GeometryPass` expose une sous-passe en **LOAD** sur le G-buffer (`HasLoadPass()`) et qu’un `TerrainRenderer` est valide (`m_terrain.IsValid()`), le terrain est dessiné **avant** la passe `Geometry`.
2. **Passe `Geometry`** : enregistre la géométrie classique avec `loadExistingGbuffer = true` lorsque le terrain est actif, pour **conserver** les attachements déjà remplis par le terrain (pas de clear intempestif).

Même logique pour le jeu et l’éditeur : un seul `TerrainRenderer` (`m_terrain`) ; seul le **mode d’initialisation** change.

## Client jeu

Pour les joueurs, le moteur **tente toujours** d’initialiser le terrain au boot (pas de case à cocher « activer le terrain »). Si le fichier heightmap est introuvable ou invalide, `TerrainRenderer::Init` échoue : le jeu continue sans sol décalé jusqu’à ce que l’asset soit présent.

- **`render.terrain.heightmap`** : chemin **relatif au content** (`paths.content`, souvent `game/data`). Si la clé est absente ou vide, défaut **`terrain/heightmap.r16h`**.
- **`render.terrain.splatmap`**, **`render.terrain.hole_mask`** : optionnels ; chaînes vides = comportements par défaut côté moteur.
- Paramètres d’étendue / hauteur (origine, `world_size`, `height_scale`, tiling splat, etc.) : clés **`terrain.*`** lues par `TerrainRenderer::Init` (voir commentaires dans `TerrainRenderer.h`).

Les shaders terrain sont chargés comme les autres SPIR-V du content via `LoadTerrainSpirvWords`.

## World Editor (Windows)

- Le document monde fixe le chemin heightmap ; **`RebuildWorldEditorTerrainGpu()`** détruit puis réinitialise `m_terrain` et les **`TerrainEditingTools`** (sculpt CPU + upload GPU).
- **Recharger terrain GPU** (UI) : `WorldEditorSession::RequestTerrainGpuReload` → consommation dans `Update` → rebuild.
- **Upload heightmap** : préférence pour enregistrement dans le **command buffer** de la frame (staging + `RecordHeightmapR16UploadCommands`) ; repli sur `FlushHeightmap` si besoin.
- Overlays ImGui : grille, preview brosse, picking rayon → hauteur via `HeightmapData::SampleBilinearNorm` (CPU).

## Limites / pistes

- **Splat** : réservé / partiel selon assets ; voir `TerrainSplatting`.
- **Falaises** : liste de meshes `.cliff` ; le jeu peut étendre la config plus tard pour des chemins multiples.
- **Linux** : l’UI World Editor reste Windows ; le **rendu terrain jeu** utilise le même code Vulkan (init systématique côté client jeu, comme sur Windows).

## Fichiers utiles

- `engine/Engine.cpp` — frame graph `Terrain_GBuffer`, init jeu `render.terrain`, rebuild éditeur.
- `engine/render/GeometryPass.{h,cpp}` — bit `loadExistingGbuffer` / passe LOAD.
- `engine/render/terrain/TerrainRenderer.{h,cpp}` — draw terrain dans le G-buffer.
- `engine/render/terrain/TerrainEditingTools.{h,cpp}` — sculpt + upload heightmap.
- `engine/editor/WorldEditorImGui.{h,cpp}` — overlays et bouton reload.
