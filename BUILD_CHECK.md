# Controle build vs2022-x64-release

Checklist rapide avant push / CI pour eviter les echecs de compilation, les regressions
de boot et les erreurs liees aux migrations Vulkan brut de cette branche.

## 1. Conflits de merge
- [ ] Aucun marqueur `<<<<<<<`, `=======`, `>>>>>>>` dans le repo, surtout dans `engine/Engine.cpp`, `engine/Engine.h`, `engine/render/AssetRegistry.*`, `engine/render/DeferredPipeline.cpp`, `engine/render/GeometryPass.*`.

## 2. Boot pipeline
- [ ] Le moteur demarre sans `SEH EXCEPTION`.
- [ ] Le log de boot contient bien :
- [ ] `DeferredPipeline BRDF LUT OK`
- [ ] `DeferredPipeline SpecularPrefilter OK`
- [ ] `M06.1: SSAO kernel+noise ready`
- [ ] `DeferredPipeline AutoExposure OK`
- [ ] `DeferredPipeline TAA OK`
- [ ] `DeferredPipeline all passes init done`

## 3. Stabilisation Vulkan brut
- [ ] `engine/render/vk/StagingAllocator.cpp` utilise `vkCreateBuffer` / `vkAllocateMemory` / `vkBindBufferMemory`, pas `vmaCreateBuffer`.
- [ ] `engine/render/BrdfLutPass.cpp` utilise `vkCreateImage` / `vkAllocateMemory` / `vkBindImageMemory`, pas `vmaCreateImage`.
- [ ] `engine/render/SpecularPrefilterPass.cpp` utilise le meme schema Vulkan brut pour le cubemap prefilter.
- [ ] `engine/render/SsaoKernelNoise.cpp` utilise Vulkan brut pour le kernel buffer et la noise texture.
- [ ] `engine/render/AutoExposure.cpp` utilise Vulkan brut pour les buffers luminance / staging / exposure.
- [ ] `engine/render/AssetRegistry.cpp` utilise Vulkan brut pour les buffers mesh et l'image texture de test.

## 4. AssetRegistry et assets de test
- [ ] `engine/render/AssetRegistry.cpp` compare les magics little-endian corrects :
- [ ] `kMeshMagic = 0x4853454D` pour les bytes `MESH`
- [ ] `kTexrMagic = 0x52584554` pour les bytes `TEXR`
- [ ] `engine/render/AssetRegistry.h` stocke `VkDeviceMemory` dans les `void*` opaques (`vertexAlloc`, `indexAlloc`, `allocation`), plus des `VmaAllocation`.
- [ ] Les fichiers existent dans `game/data/...` :
- [ ] `game/data/meshes/test.mesh`
- [ ] `game/data/textures/test.texr`
- [ ] Le log contient `AssetRegistry: loaded mesh meshes/test.mesh`
- [ ] Le log contient `AssetRegistry: loaded texture textures/test.texr`
- [ ] Le log ne contient plus `invalid mesh format` ni `invalid texture format`.

## 5. HlodRuntime / WorldModel
- [ ] `engine/world/HlodRuntime.h` garde la declaration `class HlodRuntime;` et le type `HlodDebugString`.
- [ ] `engine/world/HlodRuntime.h` declare `BuildChunkDrawList(... const ChunkRequest* requestedChunks, size_t requestedCount, ...)` sans `std::span` dans la signature publique.
- [ ] `engine/world/HlodRuntime.cpp` garde la meme signature et compile sous MSVC.
- [ ] `engine/world/WorldModel.cpp` garde la forme `struct ChunkBounds World::ChunkBounds(ChunkCoord c)` pour eviter l'ambiguite MSVC.
- [ ] `engine/Engine.cpp` appelle bien `BuildChunkDrawList(pending.data(), pending.size(), ...)`.

## 6. GeometryPass
- [ ] `engine/render/GeometryPass.cpp` compile et initialise correctement l'identity instance buffer.
- [ ] `engine/render/GeometryPass.cpp` garde `FramebufferKey::operator==` et `FramebufferKeyHash::operator()`.
- [ ] Attention : le ticket `M09.6` demande un retour vers VMA pour `GeometryPass`, a reevaluer avant application sur cette branche stabilisee en Vulkan brut.

## 7. Rendu final
- [ ] La fenetre n'est pas bloquee sur un ecran entierement blanc sans investigation.
- [ ] Si l'image est blanche, verifier en priorite `Lighting`, `AutoExposure`, `Tonemap`, puis `TAA` / `CopyPresent`.
- [ ] La couleur de clear de base reste sombre (`0.15, 0.15, 0.18`) : un ecran blanc vient donc d'une passe de fin, pas du clear.

## Commandes utiles
### Verification des conflits
```bash
git grep -n "<<<<<<< \|======= \|>>>>>>>" || true
```
(Aucun resultat attendu.)

### Build
```bash
cmake --build --preset vs2022-x64-release
```

### Run
```bash
.\build\vs2022-x64-release\engine_app\lcdlln.exe -log
```
