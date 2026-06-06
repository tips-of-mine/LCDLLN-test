# Terrain Phase 2 — Collision via `IHeightField` — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Brancher la collision terrain sur la source réellement rendue, via une interface `IHeightField` (source chunkée prioritaire + repli heightmap), pour que « le joueur marche sur ce qu'il voit » dès que des zones chunkées seront embarquées.

**Architecture:** Interface pure `IHeightField` avec deux implémentations — `HeightmapHeightField` (wrappe `TerrainRenderer`) et `ChunkHeightField` (chunks `terrain.bin` résidents via `StreamCache`, grille 256 m de la Phase 1). `TerrainCollider` prend une stratégie « chunk si résident, sinon heightmap ». Refactor **sûr** : aucune carte ne livre de `terrain.bin` aujourd'hui → `ChunkHeightField::IsLoadedAt` toujours faux → repli heightmap → comportement strictement inchangé sur le contenu actuel.

**Tech Stack:** C++17. Tests : exécutables C++ autonomes (`lcdlln_add_simple_test`, exécutés par ctest Linux). **Pas de build local** : compile + ctest validés en CI ; effet runtime invisible tant qu'aucun chunk livré → valider surtout la **non-régression heightmap** + le test unitaire des height fields.

**Référence spec :** `docs/superpowers/specs/2026-06-05-unification-terrain-design.md` (Phase 2).

**Portée :** client uniquement — **pas de redéploiement serveur**. Ces `.cpp` ne vont PAS dans `server_app`. Aucune modif `frontFace`/`cullMode`/winding.

---

## Ordre

Task 1 (interface + impls + test) → Task 2 (TerrainCollider) → Task 3 (Engine injection) → Task 4 (validation). Chaque commit compile.

## File Structure

- **Create** `src/client/world/terrain/IHeightField.h` — interface pure.
- **Create** `src/client/world/terrain/HeightmapHeightField.{h,cpp}` — wrappe `TerrainRenderer`.
- **Create** `src/client/world/terrain/ChunkHeightField.{h,cpp}` — chunks résidents (StreamCache + grille 256).
- **Create** `src/client/world/terrain/tests/HeightFieldTests.cpp` — test unitaire.
- **Modify** `CMakeLists.txt` — enregistrer les 2 `.cpp` (engine_core) + le test.
- **Modify** `src/client/gameplay/TerrainCollider.{h,cpp}` — `BindTerrain` → `BindHeightFields`, `GroundHeightAt` stratégie.
- **Modify** `src/client/app/Engine.{h,cpp}` — construire/injecter les height fields.

---

### Task 1: Interface `IHeightField` + implémentations + test

> Vérif Task 1 : pas de build local → CI compile + ctest. Avant d'écrire le test, VÉRIFIER l'existence réelle des helpers utilisés (`StreamCache::Insert`/`Lookup`, `terrain::LoadFromCache`, `MakeTerrainCacheKey`, `TerrainChunk::SaveTerrainBin`/`MakeFlat`/`SampleHeight`) et ADAPTER aux signatures réelles ; sinon construire le test avec les primitives qui existent (l'objectif : prouver IsLoadedAt false→true quand un terrain.bin est inséré, et HeightAt = hauteur du chunk).

- [ ] **Step 1 — `src/client/world/terrain/IHeightField.h`** (interface pure, aucun include lourd) :
```cpp
#pragma once

namespace engine::world::terrain
{
    /// Phase 2 (chantier C) — abstraction d'une source de hauteur de sol
    /// échantillonnable en coordonnées monde (mètres). Découple la collision
    /// (`TerrainCollider`) de la source concrète (heightmap legacy ou chunks
    /// streamés). Implémentations NON-BLOQUANTES, appelables en main thread
    /// (aucune lecture disque synchrone dans le chemin collision).
    class IHeightField
    {
    public:
        virtual ~IHeightField() = default;

        /// Altitude du sol (m monde) en (worldX, worldZ), filtrage bilinéaire.
        /// Toujours fini (jamais NaN) : repli sûr 0.0 si indisponible.
        virtual float HeightAt(float worldX, float worldZ) const = 0;

        /// Vrai si cette source fournit une hauteur fiable en (worldX, worldZ)
        /// SANS déclencher de chargement (aucune I/O). Sert d'aiguillage.
        virtual bool IsLoadedAt(float worldX, float worldZ) const = 0;
    };
}
```

- [ ] **Step 2 — `HeightmapHeightField.{h,cpp}`** (wrappe `TerrainRenderer`).

`.h` :
```cpp
#pragma once
#include "src/client/world/terrain/IHeightField.h"

namespace engine::render::terrain { class TerrainRenderer; }  // forward

namespace engine::world::terrain
{
    /// `IHeightField` adossé à la heightmap legacy (`TerrainRenderer`). Non-owning.
    class HeightmapHeightField final : public IHeightField
    {
    public:
        explicit HeightmapHeightField(const engine::render::terrain::TerrainRenderer* renderer);
        float HeightAt(float worldX, float worldZ) const override;
        bool  IsLoadedAt(float worldX, float worldZ) const override;
    private:
        const engine::render::terrain::TerrainRenderer* m_renderer = nullptr;
    };
}
```

`.cpp` :
```cpp
#include "src/client/world/terrain/HeightmapHeightField.h"
#include "src/client/render/terrain/TerrainRenderer.h"

namespace engine::world::terrain
{
    HeightmapHeightField::HeightmapHeightField(
        const engine::render::terrain::TerrainRenderer* renderer)
        : m_renderer(renderer) {}

    float HeightmapHeightField::HeightAt(float worldX, float worldZ) const
    {
        if (m_renderer == nullptr) return 0.0f;
        return m_renderer->SampleHeightAtWorldXZ(worldX, worldZ);
    }

    bool HeightmapHeightField::IsLoadedAt(float /*worldX*/, float /*worldZ*/) const
    {
        return m_renderer != nullptr && m_renderer->IsValid();
    }
}
```
> Vérifier les signatures réelles : `SampleHeightAtWorldXZ(float,float) const` (`TerrainRenderer.h:185`), `IsValid()` (`.h:135`). `SampleHeightAtWorldXZ` retombe déjà sur 0 si pas de heightmap (pas de NaN).

- [ ] **Step 3 — `ChunkHeightField.{h,cpp}`** (chunks résidents, grille 256).

`.h` :
```cpp
#pragma once
#include "src/client/world/terrain/IHeightField.h"
#include <memory>

namespace engine::core { class Config; }
namespace engine::world { class StreamCache; }

namespace engine::world::terrain
{
    class TerrainChunk;

    /// `IHeightField` adossé aux chunks `terrain.bin` streamés (grille 256 m).
    /// Échantillonne UNIQUEMENT des chunks DÉJÀ résidents (lookup LRU pur, 0 I/O).
    /// Main-thread (StreamCache main-thread-only). Aujourd'hui : aucun terrain.bin
    /// livré → IsLoadedAt toujours faux → repli heightmap (comportement inchangé).
    class ChunkHeightField final : public IHeightField
    {
    public:
        ChunkHeightField(engine::world::StreamCache* cache, const engine::core::Config* config);
        float HeightAt(float worldX, float worldZ) const override;
        bool  IsLoadedAt(float worldX, float worldZ) const override;
    private:
        std::shared_ptr<const TerrainChunk> ResidentChunkAt(float worldX, float worldZ) const;
        engine::world::StreamCache* m_cache  = nullptr;
        const engine::core::Config* m_config = nullptr;
    };
}
```

`.cpp` :
```cpp
#include "src/client/world/terrain/ChunkHeightField.h"

#include "src/client/world/StreamCache.h"
#include "src/client/world/WorldModel.h"                  // WorldToTerrainChunkCoord, kTerrainChunkSizeMeters
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/terrain/TerrainChunkLoader.h"  // LoadFromCache, MakeTerrainCacheKey

#include <string>

namespace engine::world::terrain
{
    ChunkHeightField::ChunkHeightField(engine::world::StreamCache* cache,
                                       const engine::core::Config* config)
        : m_cache(cache), m_config(config) {}

    std::shared_ptr<const TerrainChunk>
    ChunkHeightField::ResidentChunkAt(float worldX, float worldZ) const
    {
        if (m_cache == nullptr) return nullptr;
        const engine::world::GlobalChunkCoord c =
            engine::world::WorldToTerrainChunkCoord(worldX, worldZ);
        const std::string key = MakeTerrainCacheKey(c.x, c.z);
        std::string err;
        return LoadFromCache(*m_cache, key, err);  // lookup PUR (LRU), nullptr si absent
    }

    bool ChunkHeightField::IsLoadedAt(float worldX, float worldZ) const
    {
        return ResidentChunkAt(worldX, worldZ) != nullptr;
    }

    float ChunkHeightField::HeightAt(float worldX, float worldZ) const
    {
        auto chunk = ResidentChunkAt(worldX, worldZ);
        if (!chunk) return 0.0f;
        const engine::world::GlobalChunkCoord c =
            engine::world::WorldToTerrainChunkCoord(worldX, worldZ);
        const float originX = static_cast<float>(c.x) * static_cast<float>(engine::world::kTerrainChunkSizeMeters);
        const float originZ = static_cast<float>(c.z) * static_cast<float>(engine::world::kTerrainChunkSizeMeters);
        return chunk->SampleHeight(worldX - originX, worldZ - originZ);  // bilinéaire, clamp aux bords
    }
}
```
> VÉRIFIER les signatures réelles avant d'écrire : `WorldToTerrainChunkCoord` / `kTerrainChunkSizeMeters` (`WorldModel.h`), `MakeTerrainCacheKey(int,int)` + `LoadFromCache(StreamCache&, const std::string&, std::string&)` (`TerrainChunkLoader.h`), `TerrainChunk::SampleHeight(float,float) const` (`TerrainChunk.h`). Si `LoadFromCache` ne renvoie pas un `shared_ptr<TerrainChunk>`, adapter le type de retour de `ResidentChunkAt`. CONFIRMER que `LoadFromCache` ne fait AUCUNE I/O (juste `Lookup` + désérialisation) ; si la seule primitive disponible déclenche un load disque, NE PAS l'utiliser et signaler (NEEDS_CONTEXT).

- [ ] **Step 4 — Test `src/client/world/terrain/tests/HeightFieldTests.cpp`** (CPU pur). Macro `REQUIRE` maison + `main()` retournant le nombre d'échecs (motif des tests du repo). Couvre :
  - `HeightmapHeightField(nullptr)` → `HeightAt==0`, `IsLoadedAt==false`.
  - `ChunkHeightField` avec `StreamCache` réel : insérer un `terrain.bin` synthétique (chunk plat de hauteur 42) pour la coord (0,0) ; vérifier `IsLoadedAt(10,10)==true`, `IsLoadedAt(300,10)==false` (chunk (1,0) absent), `HeightAt(10,10) ≈ 42`. Prouve la chaîne monde→chunk→local + l'aiguillage résident.

  Adapter aux primitives réelles (insertion cache, construction d'un `TerrainChunk` plat, sérialisation). Si l'insertion d'un blob dans le `StreamCache` n'est pas possible simplement, tester au minimum `HeightmapHeightField(nullptr)` + la conversion via une sonde indirecte, et documenter la limite.

- [ ] **Step 5 — CMake.** Ajouter les 2 `.cpp` aux sources `engine_core` (racine `CMakeLists.txt`, près des `src/client/world/terrain/*.cpp`). Enregistrer le test via `lcdlln_add_simple_test` (motif `terrain_collider_tests`, sans garde `WIN32`, exécuté par ctest Linux) — vérifier le nom exact du helper dans `cmake/` ou `src/CMakeLists.txt`. **Ne PAS** ajouter ces fichiers à `server_app`/`shard_app`.

- [ ] **Step 6 — Commit**
```bash
git add src/client/world/terrain/IHeightField.h \
        src/client/world/terrain/HeightmapHeightField.h src/client/world/terrain/HeightmapHeightField.cpp \
        src/client/world/terrain/ChunkHeightField.h src/client/world/terrain/ChunkHeightField.cpp \
        src/client/world/terrain/tests/HeightFieldTests.cpp CMakeLists.txt
git commit -m "feat(terrain): IHeightField + heightmap/chunk height fields (Phase 2)"
```

---

### Task 2: Rebrancher `TerrainCollider` sur `IHeightField`

> Vérif Task 2 : compile CI. `SweepCapsule`/`QueryWater` n'appellent que `GroundHeightAt` en interne → bénéficient transparemment. Aucun test à modifier (les tests collision testent le cas non-bound). VÉRIFIER qu'aucun test n'appelle `BindTerrain`.

- [ ] **Step 1 — `TerrainCollider.h`.** Remplacer le forward-declare `TerrainRenderer`, l'API `BindTerrain` et le membre `m_terrain` par :
```cpp
namespace engine::world::terrain { class IHeightField; }  // forward
...
public:
    /// Bind les sources de hauteur (non-owning). chunkField = prioritaire si
    /// résident ; heightmapField = repli. L'un/l'autre peut être nullptr.
    /// Remplace BindTerrain(TerrainRenderer*) (Phase 2, chantier C).
    void BindHeightFields(const engine::world::terrain::IHeightField* chunkField,
                          const engine::world::terrain::IHeightField* heightmapField);
...
private:
    const engine::world::terrain::IHeightField* m_chunkField = nullptr;
    const engine::world::terrain::IHeightField* m_heightmapField = nullptr;
```
(Conserver `GroundHeightAt`, `SweepCapsule`, `QueryWater`, `BindWater` à l'identique.)

- [ ] **Step 2 — `TerrainCollider.cpp`.** Remplacer `BindTerrain` + `GroundHeightAt` :
```cpp
void TerrainCollider::BindHeightFields(
    const engine::world::terrain::IHeightField* chunkField,
    const engine::world::terrain::IHeightField* heightmapField)
{
    m_chunkField     = chunkField;
    m_heightmapField = heightmapField;
}

float TerrainCollider::GroundHeightAt(float worldX, float worldZ) const
{
    // Phase 2 : chunk résident prioritaire (autorité quand zones chunkées
    // embarquées), sinon repli heightmap, sinon plat 0. Aucune I/O (IsLoadedAt
    // = lookup cache pur). Aujourd'hui : pas de terrain.bin → toujours repli
    // heightmap → comportement inchangé vs Phase 1.
    if (m_chunkField != nullptr && m_chunkField->IsLoadedAt(worldX, worldZ))
        return m_chunkField->HeightAt(worldX, worldZ);
    if (m_heightmapField != nullptr)
        return m_heightmapField->HeightAt(worldX, worldZ);
    return 0.0f;
}
```
Remplacer l'include `TerrainRenderer.h` (devenu inutile si plus référencé) par `#include "src/client/world/terrain/IHeightField.h"`. Vérifier que `WaterSurfaces.h` et les autres includes restent.

- [ ] **Step 3 — Commit**
```bash
git add src/client/gameplay/TerrainCollider.h src/client/gameplay/TerrainCollider.cpp
git commit -m "feat(terrain): TerrainCollider échantillonne via IHeightField (Phase 2)"
```

---

### Task 3: Engine — construire et injecter les height fields

> Vérif Task 3 : compile CI. VÉRIFIER l'ordre de boot (m_streamCache.Init et m_terrain.Init avant le bind) et les noms réels (`m_streamCache`, `m_terrain`, `m_cfg`, le call-site `BindTerrain`).

- [ ] **Step 1 — `Engine.h`.** Inclure les 2 headers ; ajouter 2 membres `unique_ptr` près de `m_terrainCollider` :
```cpp
#include "src/client/world/terrain/HeightmapHeightField.h"
#include "src/client/world/terrain/ChunkHeightField.h"
...
    std::unique_ptr<engine::world::terrain::HeightmapHeightField> m_heightmapField;
    std::unique_ptr<engine::world::terrain::ChunkHeightField>     m_chunkField;
```

- [ ] **Step 2 — `Engine.cpp`.** Remplacer le bind au boot (chercher `m_terrainCollider.BindTerrain(&m_terrain);`) :
```cpp
// Phase 2 (chantier C) — construire les IHeightField et les injecter.
// Chunk (terrain.bin résidents, grille 256) prioritaire si résident ; repli
// heightmap (m_terrain) sinon. m_streamCache et m_terrain déjà Init à ce point.
m_heightmapField = std::make_unique<engine::world::terrain::HeightmapHeightField>(&m_terrain);
m_chunkField     = std::make_unique<engine::world::terrain::ChunkHeightField>(&m_streamCache, &m_cfg);
m_terrainCollider.BindHeightFields(m_chunkField.get(), m_heightmapField.get());
```
VÉRIFIER : le nom réel du membre StreamCache (`m_streamCache` ?) et du Config (`m_cfg`), et que ce bloc s'exécute APRÈS `m_streamCache.Init(...)` et l'init de `m_terrain`. Si le StreamCache n'est pas un membre Engine accessible (ex. possédé par `TerrainChunkRenderer`), retourner NEEDS_CONTEXT avec l'emplacement réel.

- [ ] **Step 3 — Aucun autre call-site.** Tous les `m_terrainCollider.GroundHeightAt(...)` restent inchangés. Laisser le `m_terrain.SampleHeightAtWorldXZ(...)` direct (cadrage caméra/HLOD, hors collision) tel quel.

- [ ] **Step 4 — Commit**
```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(terrain): Engine injecte les height fields dans le collider (Phase 2)"
```

---

### Task 4: Validation (non-régression + contrat futur)

- [ ] **CI** : `build-windows` (compile) + `build-linux` (compile + ctest, dont `height_field_tests`, `terrain_collider_tests`) verts.
- [ ] **Non-régression heightmap** (cœur de la Phase 2) sur `demo_plains` : spawn au sol, marche/saut/pente, snap des entités (coffre/villageois/props), nage — **comportement visuellement identique** à avant (aucun terrain.bin → repli heightmap systématique).
- [ ] **Contrat futur** : le test unitaire prouve qu'un chunk résident fait basculer `IsLoadedAt`→true et `HeightAt` sur la hauteur du chunk → la collision suivra une zone chunkée dès qu'embarquée.
- [ ] **Anti-NaN** : les 3 chemins de `GroundHeightAt` retournent fini.

---

## Self-Review

- **Couverture spec** : `IHeightField` (HeightAt/IsLoadedAt) ; `HeightmapHeightField` (wrappe TerrainRenderer) ; `ChunkHeightField` (StreamCache résident + grille 256 + SampleHeight) ; `TerrainCollider` stratégie chunk+fallback ; injection Engine. ✅
- **Sûreté** : aucune I/O en collision (lookup cache pur) ; main-thread (frame loop) ; repli sûr 3 niveaux (chunk→heightmap→0), jamais de NaN. ✅
- **Refactor sans régression** : aucun terrain.bin livré → IsLoadedAt toujours faux → repli heightmap → comportement inchangé. ✅
- **Périmètre** : 1 seul call-site change de signature (`BindTerrain`→`BindHeightFields`) ; tous les `GroundHeightAt` inchangés ; `9230` (cadrage) non touché ; client-only (pas de server_app). ✅
- **Risques à surveiller (exécutant)** : (1) signatures réelles `LoadFromCache`/`MakeTerrainCacheKey`/`SampleHeight` à confirmer ; (2) StreamCache accessible depuis Engine ; (3) helper CMake de test (`lcdlln_add_simple_test`) et exécution ctest Linux ; (4) coût CPU de `LoadFromCache` par appel (nul aujourd'hui ; cache 1-entrée à différer) ; (5) si un `GroundHeightAt` hors main-thread apparaissait, ajouter une synchro (hors périmètre).
