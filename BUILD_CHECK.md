# Contrôle build vs2022-x64-release

Checklist rapide avant push / CI pour éviter les échecs de compilation.

## 1. Conflits de merge
- [ ] Aucun marqueur `<<<<<<<`, `=======`, `>>>>>>>` dans le repo (surtout Engine.cpp, Engine.h, GeometryPass.cpp, AssetRegistry.h).

## 2. HlodRuntime (M09.5)
- [ ] **HlodRuntime.h** : forward declaration `class HlodRuntime;` en tête du namespace. Déclaration `BuildChunkDrawList` avec `HlodDebugString`, `Vec3Ref`, `FrustumRef`, `const ChunkRequest*`, `size_t` (pas de `std::span` dans le .h). Include `Frustum.h` et `WorldModel.h`.
- [ ] **HlodRuntime.cpp** : signature de `BuildChunkDrawList` identique (pointeur + size). Variable locale `struct ChunkBounds bounds2d = World::ChunkBounds(req.chunkId);` (mot-clé `struct` pour MSVC). Pas d’accolade orpheline dans la boucle for.
- [ ] **Engine.cpp** : appel `BuildChunkDrawList(pending.data(), pending.size(), ...)` avec `std::span<...> pending = m_world.GetPendingChunkRequests();`. Présence de `#include <span>`. Utilisation de `m_pipeline->GetGeometryPass().Record(...)` (pas `m_geometryPass`). Bloc VMA : `if (!m_vmaAllocator) { } else { ... }` (pas de `continue` hors boucle).

## 3. WorldModel
- [ ] **WorldModel.cpp** : définition `struct ChunkBounds World::ChunkBounds(ChunkCoord c)` et variable locale `struct ChunkBounds b;` (mot-clé `struct` pour éviter ambiguïté avec le nom de fonction sous MSVC).

## 4. AssetRegistry
- [ ] **AssetRegistry.h** : pas de conflit de merge. `MeshAsset` avec `kMeshLodLevelCount`, LOD (lodIndexOffset/Count, GetLodIndexCount/Offset) et champs VMA (vertexAlloc, indexAlloc).

## 5. GeometryPass
- [ ] **GeometryPass.cpp** : `Init` avec paramètre `physicalDevice` (pas commenté). `Destroy` : libération identity buffer/memory + appel `InvalidateFramebufferCache(device)`. Présence de `FramebufferKey::operator==` et `FramebufferKeyHash::operator()`.

## Commande de vérification des conflits
```bash
git grep -n "<<<<<<< \|======= \|>>>>>>>" || true
```
(Aucun résultat attendu.)

## Build
```bash
cmake --build --preset vs2022-x64-release
```
