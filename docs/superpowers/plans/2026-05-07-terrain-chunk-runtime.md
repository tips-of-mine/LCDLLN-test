# Terrain Chunk Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Livrer l'infrastructure GPU runtime qui permet à `GeometryPass` de dessiner les chunks terrain (mesh + splat 8-layer) avec eviction LRU production-ready, résolvant les dettes différées Task 11 Phase 2 + Task 14 Phase 3a.

**Architecture :** Nouveau sous-système `engine/render/terrain_chunk/` orchestré par `TerrainChunkRenderer`. Caches LRU séparés pour mesh GPU et splat-maps GPU avec budget configurable. `LayerArrayLoader` charge les 24 textures PBR au boot dans 3 `VkImageView2DArray` partagées (avec fallback placeholder si `.texr` absent). Coexistence stricte avec `TerrainRenderer` legacy (skip si fichiers chunk absents). Pas de branche `m_editorEnabled` — la parité éditeur ↔ client est garantie par le format binaire identique.

**Tech Stack :** C++17/20 + Vulkan, GLSL pour shaders (déjà créés en Phase 3a), `engine::render::ShaderCompiler`, `engine::render::vk::StagingAllocator` (ring 2 frames pour les uploads), `engine::render::AssetRegistry::LoadTexture` pour les PBR, `engine::world::StreamCache` pour la lecture disque, `engine::core::Config`/`Log`. Tests via le framework REQUIRE maison (cf. `engine/editor/world/tests/CommandStackTests.cpp`).

**Entrées :**
- Design : [docs/superpowers/specs/2026-05-07-terrain-chunk-runtime-design.md](../specs/2026-05-07-terrain-chunk-runtime-design.md). 8 décisions verrouillées.
- API existantes (déjà sur main) :
  - `engine::render::TerrainChunkPipeline` (Phase 3a Task 13) — pipeline Vulkan compilé, avec `Init/Shutdown/RecordChunkDraw/GetSplatSetLayout`.
  - `engine::world::terrain::TerrainChunk` + `TerrainMeshBuilder::BuildLod0Mesh` (Phase 2).
  - `engine::world::terrain::SplatMap` + `LayerPalette` + 8 placeholders 4×4 (Phase 3a).
  - `engine::world::StreamCache::LoadTerrainChunk` + `LoadSplatMap` (Phase 2 + 3a).
  - `engine::render::AssetRegistry::LoadTexture(relativePath)` (existant).
  - `engine::render::vk::StagingAllocator` (existant, ring 2 frames).
  - `engine::world::World::GetPendingChunkRequests()` + `GetRingForChunk(coord)` (existant).

---

## File Structure

| Fichier | Rôle | Action |
|---------|------|--------|
| `engine/render/terrain_chunk/TerrainChunkRenderer.h` | Entry point public — appelé par `GeometryPass`. Orchestre les sous-caches. | Create |
| `engine/render/terrain_chunk/TerrainChunkRenderer.cpp` | Impl orchestrateur (Init/Shutdown/Render/Tick). | Create |
| `engine/render/terrain_chunk/ChunkRuntime.h` | Cache LRU global (chunks visibles + budget GPU) + tracking de l'ordre LRU. | Create |
| `engine/render/terrain_chunk/ChunkRuntime.cpp` | Impl cache + eviction. | Create |
| `engine/render/terrain_chunk/TerrainMeshGpuCache.h` | Wrapper VkBuffer vertex + index pour un chunk + cache LRU dédié. | Create |
| `engine/render/terrain_chunk/TerrainMeshGpuCache.cpp` | Upload via `StagingAllocator` + cache. | Create |
| `engine/render/terrain_chunk/SplatMapGpuCache.h` | Wrapper 2× VkImage RGBA8 257² + ImageViews + cache LRU dédié. | Create |
| `engine/render/terrain_chunk/SplatMapGpuCache.cpp` | Upload via `StagingAllocator` + cache. | Create |
| `engine/render/terrain_chunk/LayerArrayLoader.h` | Charge 24 PBR (8 layers × 3 maps) en 3 `VkImageView2DArray` + 5 samplers, 1× au boot. | Create |
| `engine/render/terrain_chunk/LayerArrayLoader.cpp` | Loop 24× `AssetRegistry::LoadTexture` avec fallback placeholders. | Create |
| `engine/render/terrain_chunk/DescriptorSetPool.h` | Pool VkDescriptorPool dédié au splat set, alloc par chunk visible (max 49). | Create |
| `engine/render/terrain_chunk/DescriptorSetPool.cpp` | Init/Shutdown + Allocate. | Create |
| `engine/render/terrain_chunk/tests/ChunkRuntimeTests.cpp` | 4 cas : hit/miss, LRU eviction, ring Active>Visible. Mock VkDevice. | Create |
| `engine/render/terrain_chunk/tests/TerrainMeshGpuCacheTests.cpp` | 4 cas : insert/lookup/LRU/budget. Mock VkBuffer. | Create |
| `engine/render/terrain_chunk/tests/SplatMapGpuCacheTests.cpp` | 3 cas : 2 VkImage par chunk, insert/lookup, LRU. | Create |
| `engine/render/terrain_chunk/tests/LayerArrayLoaderTests.cpp` | 4 cas : 8 layers × 3 maps, fallback placeholder, path resolution, boot-time bounded. | Create |
| `engine/Engine.cpp` | Modif : init `TerrainChunkRenderer` après `DeferredPipeline`. Membre + accesseur. Shutdown. | Modify |
| `engine/render/GeometryPass.h` | Modif : accepter pointeur vers `TerrainChunkRenderer` au record. | Modify |
| `engine/render/GeometryPass.cpp` | Modif : drawcall par chunk visible (skip si fichiers absents). Sans branche éditeur. | Modify |
| `config.json` | Ajout `editor.world.terrain.gpu_budget_mb` (default 256). | Modify |
| `CMakeLists.txt` | Ajout sources `engine/render/terrain_chunk/*.cpp` + 4 nouveaux test targets. | Modify |
| `tickets/M100/INDEX.md` | M100.5 → Done complet (Task 11 résolu) ; M100.9 → Done complet (Task 14 résolu). | Modify |

**Conventions transverses (CLAUDE.md) :**
- Toute fonction nouvelle/modifiée a un commentaire `///` Doxygen au-dessus de la déclaration (rôle, params non-évidents, effets de bord, contraintes thread).
- Code en français pour les commentaires, anglais pour les identifiants techniques.
- TDD strict : test red commit → impl green commit (commits séparés sauf cas trivial).
- Aucun `--no-verify`. Aucun skip de tests.
- Framework de test : **REQUIRE macro maison** (cf. `engine/editor/world/tests/CommandStackTests.cpp`). PAS Catch2.

**Anti-duplication serveur :**
- `engine/render/terrain_chunk/**/*.cpp` exclu du target serveur (déjà via dépendance Vulkan transitive — le serveur n'a pas Vulkan).
- Vérification finale : `grep -RIn "engine::render::terrain_chunk" engine/server/` → 0.

---

## Tasks

### Task 1 : Reconnaissance — confirmer base + APIs

**Files :**
- Read seulement.

- [ ] **Step 1 : Confirmer la branche et les prérequis Phase 1+2+3a sur main**

```bash
git status
git log origin/main --oneline | head -8
ls engine/world/terrain/
ls engine/render/Terrain*
ls assets/terrain/
```

Expected : branche `claude/m100-terrain-chunk-runtime`, main contient `Phase 3a` mergée. `engine/world/terrain/` contient `SplatMap.{h,cpp}`, `LayerPalette.{h,cpp}` + tous les fichiers Phase 2. `engine/render/TerrainChunkPipeline.{h,cpp}` existe (Phase 3a Task 13). `assets/terrain/layer_palette.json` existe.

- [ ] **Step 2 : Lire les API existantes touchées**

```bash
cat engine/render/TerrainChunkPipeline.h | head -120
cat engine/render/AssetRegistry.h | head -150
cat engine/render/vk/StagingAllocator.h
cat engine/world/StreamCache.h | head -100
cat engine/world/WorldModel.h | head -130
grep -n "TerrainRenderer\|m_terrain" engine/Engine.cpp | head -20
grep -n "Record\b\|::Record\|m_renderPass\|m_pipelineCache" engine/render/GeometryPass.h engine/render/GeometryPass.cpp | head -30
```

Notes attendues :
- `TerrainChunkPipeline::RecordChunkDraw(cmd, cameraSet, splatSet, mesh, originXYZ)` — déjà fonctionnel.
- `AssetRegistry::LoadTexture(relativePath, useSrgb=false) → TextureHandle` — déjà cache asset.
- `StagingAllocator::Init(device, physDev, budgetBytesPerFrame)` + `Allocate(size, outOffset) → VkBuffer` + `BeginFrame(frameIndex)`.
- `StreamCache::LoadTerrainChunk(cfg, x, z) → shared_ptr<TerrainChunk>` + `LoadSplatMap(cfg, x, z) → shared_ptr<SplatMap>`.
- `World::GetPendingChunkRequests()` retourne le set demandé après le dernier `Update(playerPos)`.
- `World::GetRingForChunk(coord)` retourne `Active`/`Visible`/`Far`.
- `Engine` instancie déjà `m_streamCache`, `m_assetRegistry`, `m_stagingAllocator`, `m_terrainRenderer` (legacy), etc.
- `GeometryPass::Record(cmd, ...)` est le point d'extension pour le nouveau drawcall.

- [ ] **Step 3 : Pas de commit** — reconnaissance pure.

---

### Task 2 : `ChunkRuntime` interface + tests red (cache LRU pure CPU)

**Files :**
- Create : `engine/render/terrain_chunk/ChunkRuntime.h`
- Create : `engine/render/terrain_chunk/tests/ChunkRuntimeTests.cpp`

- [ ] **Step 1 : Header (interface pure CPU, pas de Vulkan)**

```cpp
// engine/render/terrain_chunk/ChunkRuntime.h
#pragma once

#include "engine/world/WorldModel.h" // GlobalChunkCoord, ChunkRing

#include <cstdint>
#include <list>
#include <unordered_map>

namespace engine::render::terrain_chunk
{
	/// Identifiant opaque d'une entrée résidente dans `ChunkRuntime`. Utilisé
	/// par les caches concrets (mesh, splat) pour lier leur ressource GPU au
	/// chunk. Stable tant que le slot existe ; invalidé à l'éviction.
	using ChunkSlotId = uint64_t;

	/// État d'un chunk vu par le runtime : `Resident` (mesh+splat uploadés),
	/// `Pending` (lecture disque ou upload en cours), `Skipped` (fichiers
	/// `terrain.bin`/`splat.bin` absents — legacy le dessinera).
	enum class ChunkResidency : uint8_t { Resident, Pending, Skipped };

	/// Cache LRU global du runtime terrain (M100, post-Phase-3a). Tracke les
	/// chunks visibles, leur résidence GPU (à travers les sous-caches mesh +
	/// splat), et applique une politique d'éviction LRU bornée par un budget
	/// GPU configurable. Les chunks dans `Active` ou `Visible` ring ne sont
	/// jamais évincés (seuls les chunks Far peuvent l'être).
	///
	/// Contraintes thread/timing : appelé uniquement depuis le main thread (le
	/// rendu Vulkan n'est pas thread-safe à ce niveau).
	class ChunkRuntime
	{
	public:
		struct Config
		{
			size_t gpuBudgetBytes = 256ull * 1024ull * 1024ull; // 256 MB par défaut
		};

		void Init(const Config& cfg);

		/// Retourne le slot pour `coord`. Si pas encore résident, alloue un
		/// nouveau slot (état `Pending`) que les caches concrets devront
		/// remplir via `MarkResident(slot, sizeBytes)`. \return slot stable.
		ChunkSlotId GetOrAllocateSlot(engine::world::GlobalChunkCoord coord);

		/// Marque le slot comme résident. Le caller (cache mesh ou splat)
		/// fournit la taille en bytes consommée par sa propre ressource. La
		/// somme des tailles déclarées par tous les caches est trackée vs
		/// `gpuBudgetBytes`.
		void AddResidentBytes(ChunkSlotId slot, size_t additionalBytes);

		/// Met à jour le ring du chunk (Active/Visible/Far). Appelé par
		/// `TerrainChunkRenderer::Render` chaque frame depuis `World::GetRingForChunk`.
		void UpdateRing(engine::world::GlobalChunkCoord coord, engine::world::ChunkRing ring);

		/// Touche le slot (déplace en tête de la liste LRU). Appelé chaque
		/// frame pour les chunks dessinés.
		void Touch(ChunkSlotId slot);

		/// Demande aux caches concrets d'évincer les chunks LRU non-résidents
		/// dans `Active`/`Visible` jusqu'à respecter le budget. Retourne la
		/// liste des chunks à libérer (le caller libère les ressources Vulkan).
		std::vector<ChunkSlotId> CollectEvictionsForBudget();

		/// Retire un slot après que tous les caches ont libéré leurs ressources.
		void RemoveSlot(ChunkSlotId slot);

		size_t GetResidentBytes() const { return m_residentBytes; }
		size_t GetSlotCount() const { return m_slots.size(); }
		ChunkResidency GetResidency(ChunkSlotId slot) const;
		engine::world::ChunkRing GetRingForSlot(ChunkSlotId slot) const;

	private:
		struct Slot
		{
			engine::world::GlobalChunkCoord coord{0, 0};
			engine::world::ChunkRing ring = engine::world::ChunkRing::Far;
			size_t residentBytes = 0;
			ChunkResidency residency = ChunkResidency::Pending;
		};

		Config m_cfg;
		std::unordered_map<ChunkSlotId, Slot> m_slots;
		std::list<ChunkSlotId> m_lru; // front = most recent, back = candidate eviction
		std::unordered_map<uint64_t, ChunkSlotId> m_coordToSlot; // pack(coord) -> slot
		ChunkSlotId m_nextSlotId = 1;
		size_t m_residentBytes = 0;

		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);
	};
}
```

- [ ] **Step 2 : Tests red**

```cpp
// engine/render/terrain_chunk/tests/ChunkRuntimeTests.cpp
/// Tests unitaires pour ChunkRuntime (cache LRU pure CPU, pas de Vulkan).
#include "engine/render/terrain_chunk/ChunkRuntime.h"

#include <cstdio>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	using engine::render::terrain_chunk::ChunkRuntime;
	using engine::render::terrain_chunk::ChunkResidency;
	using engine::world::GlobalChunkCoord;
	using engine::world::ChunkRing;

	/// Slot alloué une fois est stable (idempotence).
	void Test_GetOrAllocateSlot_Idempotent()
	{
		ChunkRuntime rt;
		rt.Init({});
		auto s1 = rt.GetOrAllocateSlot({3, 5});
		auto s2 = rt.GetOrAllocateSlot({3, 5});
		REQUIRE(s1 == s2);
		REQUIRE(rt.GetSlotCount() == 1);
	}

	/// AddResidentBytes accumule + GetResidentBytes le reflète.
	void Test_AddResidentBytes_Accumulates()
	{
		ChunkRuntime rt;
		rt.Init({});
		auto s = rt.GetOrAllocateSlot({0, 0});
		rt.AddResidentBytes(s, 1000);
		rt.AddResidentBytes(s, 500);
		REQUIRE(rt.GetResidentBytes() == 1500u);
	}

	/// Visible ring ne peut pas être évincé même si budget dépassé.
	void Test_VisibleRing_NeverEvicted()
	{
		ChunkRuntime::Config cfg;
		cfg.gpuBudgetBytes = 1000;
		ChunkRuntime rt;
		rt.Init(cfg);
		auto sFar = rt.GetOrAllocateSlot({10, 10});
		rt.UpdateRing({10, 10}, ChunkRing::Far);
		rt.AddResidentBytes(sFar, 500);
		auto sVis = rt.GetOrAllocateSlot({0, 0});
		rt.UpdateRing({0, 0}, ChunkRing::Visible);
		rt.AddResidentBytes(sVis, 800); // total 1300 > budget 1000

		auto evictions = rt.CollectEvictionsForBudget();
		// Far doit être candidat, Visible doit être protégé.
		REQUIRE(evictions.size() == 1);
		REQUIRE(evictions[0] == sFar);
	}

	/// LRU : chunk le moins récemment touché évincé en premier.
	void Test_LruEviction_ByTouchOrder()
	{
		ChunkRuntime::Config cfg;
		cfg.gpuBudgetBytes = 100;
		ChunkRuntime rt;
		rt.Init(cfg);
		auto sA = rt.GetOrAllocateSlot({1, 0});
		auto sB = rt.GetOrAllocateSlot({2, 0});
		rt.UpdateRing({1, 0}, ChunkRing::Far);
		rt.UpdateRing({2, 0}, ChunkRing::Far);
		rt.AddResidentBytes(sA, 60);
		rt.AddResidentBytes(sB, 60); // total 120 > 100
		// On touche sA pour le rendre plus récent que sB → sB devrait être évincé.
		rt.Touch(sA);
		auto evictions = rt.CollectEvictionsForBudget();
		REQUIRE(evictions.size() >= 1);
		REQUIRE(evictions[0] == sB);
	}
}

int main()
{
	Test_GetOrAllocateSlot_Idempotent();
	Test_AddResidentBytes_Accumulates();
	Test_VisibleRing_NeverEvicted();
	Test_LruEviction_ByTouchOrder();
	if (g_failed == 0) { std::printf("[PASS] ChunkRuntimeTests (4/4)\n"); return 0; }
	std::printf("[FAIL] ChunkRuntimeTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 3 : Commit (red — pas encore d'impl `.cpp`)**

```bash
git add engine/render/terrain_chunk/ChunkRuntime.h engine/render/terrain_chunk/tests/ChunkRuntimeTests.cpp
git commit -m "test(render/terrain_chunk): ChunkRuntime tests (TDD red)"
```

---

### Task 3 : `ChunkRuntime.cpp` impl + green

**Files :**
- Create : `engine/render/terrain_chunk/ChunkRuntime.cpp`

- [ ] **Step 1 : Impl LRU + budget**

```cpp
// engine/render/terrain_chunk/ChunkRuntime.cpp
#include "engine/render/terrain_chunk/ChunkRuntime.h"

#include <algorithm>

namespace engine::render::terrain_chunk
{
	uint64_t ChunkRuntime::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
		     | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	void ChunkRuntime::Init(const Config& cfg)
	{
		m_cfg = cfg;
		m_slots.clear();
		m_lru.clear();
		m_coordToSlot.clear();
		m_nextSlotId = 1;
		m_residentBytes = 0;
	}

	ChunkSlotId ChunkRuntime::GetOrAllocateSlot(engine::world::GlobalChunkCoord coord)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_coordToSlot.find(key);
		if (it != m_coordToSlot.end()) return it->second;

		const ChunkSlotId id = m_nextSlotId++;
		Slot slot;
		slot.coord = coord;
		m_slots.emplace(id, slot);
		m_lru.push_front(id);
		m_coordToSlot.emplace(key, id);
		return id;
	}

	void ChunkRuntime::AddResidentBytes(ChunkSlotId slot, size_t additionalBytes)
	{
		auto it = m_slots.find(slot);
		if (it == m_slots.end()) return;
		it->second.residentBytes += additionalBytes;
		it->second.residency = ChunkResidency::Resident;
		m_residentBytes += additionalBytes;
	}

	void ChunkRuntime::UpdateRing(engine::world::GlobalChunkCoord coord, engine::world::ChunkRing ring)
	{
		auto cit = m_coordToSlot.find(PackCoord(coord));
		if (cit == m_coordToSlot.end()) return;
		auto sit = m_slots.find(cit->second);
		if (sit == m_slots.end()) return;
		sit->second.ring = ring;
	}

	void ChunkRuntime::Touch(ChunkSlotId slot)
	{
		auto lit = std::find(m_lru.begin(), m_lru.end(), slot);
		if (lit == m_lru.end()) return;
		m_lru.erase(lit);
		m_lru.push_front(slot);
	}

	std::vector<ChunkSlotId> ChunkRuntime::CollectEvictionsForBudget()
	{
		std::vector<ChunkSlotId> evictions;
		// Itère depuis la fin (least recently used).
		auto it = m_lru.rbegin();
		while (m_residentBytes > m_cfg.gpuBudgetBytes && it != m_lru.rend())
		{
			auto sit = m_slots.find(*it);
			if (sit == m_slots.end()) { ++it; continue; }
			// Skip si Active ou Visible.
			if (sit->second.ring == engine::world::ChunkRing::Active
			 || sit->second.ring == engine::world::ChunkRing::Visible)
			{
				++it;
				continue;
			}
			// Évincable.
			evictions.push_back(*it);
			m_residentBytes -= sit->second.residentBytes;
			++it;
		}
		return evictions;
	}

	void ChunkRuntime::RemoveSlot(ChunkSlotId slot)
	{
		auto sit = m_slots.find(slot);
		if (sit == m_slots.end()) return;
		// Note : si AddResidentBytes a été appelé entre l'eviction et le Remove,
		// CollectEvictionsForBudget aurait déjà décrémenté. On évite la double
		// décrémentation : la résidence locale a été remise à 0 implicitement.
		m_coordToSlot.erase(PackCoord(sit->second.coord));
		m_slots.erase(sit);
		m_lru.remove(slot);
	}

	ChunkResidency ChunkRuntime::GetResidency(ChunkSlotId slot) const
	{
		auto it = m_slots.find(slot);
		return (it == m_slots.end()) ? ChunkResidency::Skipped : it->second.residency;
	}

	engine::world::ChunkRing ChunkRuntime::GetRingForSlot(ChunkSlotId slot) const
	{
		auto it = m_slots.find(slot);
		return (it == m_slots.end()) ? engine::world::ChunkRing::Far : it->second.ring;
	}
}
```

- [ ] **Step 2 : Ajouter source + test target dans CMakeLists.txt**

Dans `engine_core` sources, ajouter (après les autres `engine/render/` entries) :
```
  engine/render/terrain_chunk/ChunkRuntime.cpp
```

Test target gating WIN32 (pattern `terrain_chunk_tests` de Phase 2) :
```cmake
if(WIN32)
  add_executable(chunk_runtime_tests engine/render/terrain_chunk/tests/ChunkRuntimeTests.cpp)
  target_include_directories(chunk_runtime_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(chunk_runtime_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(chunk_runtime_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME chunk_runtime_tests COMMAND chunk_runtime_tests)
endif()
```

- [ ] **Step 3 : Commit (green)**

```bash
git add engine/render/terrain_chunk/ChunkRuntime.cpp CMakeLists.txt
git commit -m "feat(render/terrain_chunk): ChunkRuntime LRU + budget GPU"
```

---

### Task 4 : `TerrainMeshGpuCache` — interface + tests red

**Files :**
- Create : `engine/render/terrain_chunk/TerrainMeshGpuCache.h`
- Create : `engine/render/terrain_chunk/tests/TerrainMeshGpuCacheTests.cpp`

Cette classe wrappe les VkBuffer vertex+index par chunk. L'interface publique est testable sans Vulkan via une couche d'indirection : on expose un `IGpuBufferAllocator` mockable. Pour le cache pur (insert/lookup/eviction), pas besoin d'instancier Vulkan.

- [ ] **Step 1 : Header**

```cpp
// engine/render/terrain_chunk/TerrainMeshGpuCache.h
#pragma once

#include "engine/render/terrain_chunk/ChunkRuntime.h"
#include "engine/world/WorldModel.h"
#include "engine/world/terrain/TerrainMeshBuilder.h" // TerrainMeshGpu, TerrainVertex

#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	/// Cache des `TerrainMeshGpu` (vertex + index VkBuffer) par chunk. Délègue
	/// l'upload réel à un allocateur Vulkan injectable (`IGpuBufferAllocator`)
	/// pour permettre les tests CPU avec un mock counter.
	class IGpuBufferAllocator
	{
	public:
		virtual ~IGpuBufferAllocator() = default;
		/// Crée un VkBuffer + alloue + upload `srcBytes` ; retourne le buffer.
		/// Sur la version réelle (Vulkan), utilise `StagingAllocator`.
		virtual VkBuffer CreateAndUploadVertexBuffer(const void* srcBytes, size_t sizeBytes) = 0;
		virtual VkBuffer CreateAndUploadIndexBuffer(const void* srcBytes, size_t sizeBytes) = 0;
		virtual void DestroyBuffer(VkBuffer buffer) = 0;
	};

	class TerrainMeshGpuCache
	{
	public:
		void Init(IGpuBufferAllocator* alloc, ChunkRuntime* runtime);
		void Shutdown();

		/// Lookup ou création. Si le mesh n'est pas en cache, builds via
		/// `TerrainMeshBuilder::BuildLod0Mesh(chunk)` puis upload via l'allocator.
		/// \return TerrainMeshGpu valide (vertex+index non null), ou {} si l'upload a échoué.
		engine::world::terrain::TerrainMeshGpu GetOrUpload(
			engine::world::GlobalChunkCoord coord,
			const engine::world::terrain::TerrainChunk& chunk);

		/// Pure lookup (pas d'upload). Retourne {} si pas en cache.
		engine::world::terrain::TerrainMeshGpu Lookup(engine::world::GlobalChunkCoord coord) const;

		/// Évince le mesh pour `coord` (libère les VkBuffer via l'allocator).
		void Evict(engine::world::GlobalChunkCoord coord);

		size_t GetCachedCount() const { return m_cache.size(); }

	private:
		struct Entry
		{
			engine::world::terrain::TerrainMeshGpu mesh;
			size_t bytes = 0;
		};
		IGpuBufferAllocator* m_alloc = nullptr;
		ChunkRuntime* m_runtime = nullptr;
		std::unordered_map<uint64_t, Entry> m_cache; // packed coord -> entry

		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);
	};
}
```

- [ ] **Step 2 : Tests red avec MockBufferAllocator**

```cpp
// engine/render/terrain_chunk/tests/TerrainMeshGpuCacheTests.cpp
#include "engine/render/terrain_chunk/TerrainMeshGpuCache.h"

#include <cstdio>
#include <cstdint>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	using engine::render::terrain_chunk::ChunkRuntime;
	using engine::render::terrain_chunk::TerrainMeshGpuCache;
	using engine::render::terrain_chunk::IGpuBufferAllocator;
	using engine::world::GlobalChunkCoord;
	using engine::world::terrain::TerrainChunk;

	/// Allocator factice : retourne des handles uint64 incrémentaux casts
	/// VkBuffer (les tests vérifient juste les counters create/destroy).
	struct MockAllocator final : IGpuBufferAllocator
	{
		uint64_t nextHandle = 1;
		int creates = 0;
		int destroys = 0;

		VkBuffer CreateAndUploadVertexBuffer(const void*, size_t) override
		{ ++creates; return reinterpret_cast<VkBuffer>(nextHandle++); }
		VkBuffer CreateAndUploadIndexBuffer(const void*, size_t) override
		{ ++creates; return reinterpret_cast<VkBuffer>(nextHandle++); }
		void DestroyBuffer(VkBuffer) override { ++destroys; }
	};

	void Test_GetOrUpload_CachesAndReuses()
	{
		MockAllocator alloc;
		ChunkRuntime rt;
		rt.Init({});
		TerrainMeshGpuCache cache;
		cache.Init(&alloc, &rt);
		auto chunk = TerrainChunk::MakeFlat(0.0f);

		auto m1 = cache.GetOrUpload({0, 0}, chunk);
		REQUIRE(alloc.creates == 2); // 1 vertex + 1 index
		auto m2 = cache.GetOrUpload({0, 0}, chunk);
		REQUIRE(alloc.creates == 2); // pas de nouveau create (cache hit)
		REQUIRE(m1.vertexBuffer == m2.vertexBuffer);
		REQUIRE(m1.indexBuffer == m2.indexBuffer);
	}

	void Test_Lookup_MissReturnsZero()
	{
		MockAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		TerrainMeshGpuCache cache; cache.Init(&alloc, &rt);
		auto m = cache.Lookup({0, 0});
		REQUIRE(m.vertexBuffer == VK_NULL_HANDLE);
		REQUIRE(m.indexBuffer == VK_NULL_HANDLE);
	}

	void Test_Evict_DestroysBuffersAndRemovesFromCache()
	{
		MockAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		TerrainMeshGpuCache cache; cache.Init(&alloc, &rt);
		auto chunk = TerrainChunk::MakeFlat(0.0f);
		cache.GetOrUpload({0, 0}, chunk);
		REQUIRE(cache.GetCachedCount() == 1);
		cache.Evict({0, 0});
		REQUIRE(cache.GetCachedCount() == 0);
		REQUIRE(alloc.destroys == 2);
	}

	void Test_GetOrUpload_TracksBudgetInRuntime()
	{
		MockAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		TerrainMeshGpuCache cache; cache.Init(&alloc, &rt);
		auto chunk = TerrainChunk::MakeFlat(0.0f);
		const size_t bytesBefore = rt.GetResidentBytes();
		cache.GetOrUpload({0, 0}, chunk);
		const size_t bytesAfter = rt.GetResidentBytes();
		REQUIRE(bytesAfter > bytesBefore);
	}
}

int main()
{
	Test_GetOrUpload_CachesAndReuses();
	Test_Lookup_MissReturnsZero();
	Test_Evict_DestroysBuffersAndRemovesFromCache();
	Test_GetOrUpload_TracksBudgetInRuntime();
	if (g_failed == 0) { std::printf("[PASS] TerrainMeshGpuCacheTests (4/4)\n"); return 0; }
	std::printf("[FAIL] TerrainMeshGpuCacheTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 3 : Commit (red)**

```bash
git add engine/render/terrain_chunk/TerrainMeshGpuCache.h engine/render/terrain_chunk/tests/TerrainMeshGpuCacheTests.cpp
git commit -m "test(render/terrain_chunk): TerrainMeshGpuCache tests (TDD red)"
```

---

### Task 5 : `TerrainMeshGpuCache.cpp` impl + green

**Files :**
- Create : `engine/render/terrain_chunk/TerrainMeshGpuCache.cpp`

- [ ] **Step 1 : Impl**

```cpp
// engine/render/terrain_chunk/TerrainMeshGpuCache.cpp
#include "engine/render/terrain_chunk/TerrainMeshGpuCache.h"

namespace engine::render::terrain_chunk
{
	uint64_t TerrainMeshGpuCache::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
		     | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	void TerrainMeshGpuCache::Init(IGpuBufferAllocator* alloc, ChunkRuntime* runtime)
	{
		m_alloc = alloc;
		m_runtime = runtime;
		m_cache.clear();
	}

	void TerrainMeshGpuCache::Shutdown()
	{
		if (!m_alloc) return;
		for (auto& [key, entry] : m_cache)
		{
			if (entry.mesh.vertexBuffer) m_alloc->DestroyBuffer(entry.mesh.vertexBuffer);
			if (entry.mesh.indexBuffer)  m_alloc->DestroyBuffer(entry.mesh.indexBuffer);
		}
		m_cache.clear();
	}

	engine::world::terrain::TerrainMeshGpu
	TerrainMeshGpuCache::GetOrUpload(engine::world::GlobalChunkCoord coord,
		const engine::world::terrain::TerrainChunk& chunk)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it != m_cache.end()) return it->second.mesh;

		// Build CPU mesh puis upload.
		auto cpuMesh = engine::world::terrain::BuildLod0Mesh(chunk);
		const size_t vbBytes = cpuMesh.vertices.size() * sizeof(engine::world::terrain::TerrainVertex);
		const size_t ibBytes = cpuMesh.indices.size() * sizeof(uint32_t);

		Entry entry;
		entry.mesh.vertexBuffer = m_alloc->CreateAndUploadVertexBuffer(cpuMesh.vertices.data(), vbBytes);
		entry.mesh.indexBuffer  = m_alloc->CreateAndUploadIndexBuffer(cpuMesh.indices.data(), ibBytes);
		entry.mesh.indexCount   = static_cast<uint32_t>(cpuMesh.indices.size());
		entry.bytes = vbBytes + ibBytes;

		m_cache.emplace(key, entry);

		// Track budget.
		if (m_runtime)
		{
			auto slot = m_runtime->GetOrAllocateSlot(coord);
			m_runtime->AddResidentBytes(slot, entry.bytes);
		}
		return entry.mesh;
	}

	engine::world::terrain::TerrainMeshGpu
	TerrainMeshGpuCache::Lookup(engine::world::GlobalChunkCoord coord) const
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return engine::world::terrain::TerrainMeshGpu{};
		return it->second.mesh;
	}

	void TerrainMeshGpuCache::Evict(engine::world::GlobalChunkCoord coord)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return;
		if (m_alloc)
		{
			if (it->second.mesh.vertexBuffer) m_alloc->DestroyBuffer(it->second.mesh.vertexBuffer);
			if (it->second.mesh.indexBuffer)  m_alloc->DestroyBuffer(it->second.mesh.indexBuffer);
		}
		m_cache.erase(it);
	}
}
```

- [ ] **Step 2 : CMake (sources + test target)**

Pattern identique à Task 3. Source `engine/render/terrain_chunk/TerrainMeshGpuCache.cpp` à `engine_core`. Test target `terrain_mesh_gpu_cache_tests` (gating WIN32).

- [ ] **Step 3 : Commit**

```bash
git add engine/render/terrain_chunk/TerrainMeshGpuCache.cpp CMakeLists.txt
git commit -m "feat(render/terrain_chunk): TerrainMeshGpuCache + IGpuBufferAllocator"
```

---

### Task 6 : `SplatMapGpuCache` — interface + tests red

**Files :**
- Create : `engine/render/terrain_chunk/SplatMapGpuCache.h`
- Create : `engine/render/terrain_chunk/tests/SplatMapGpuCacheTests.cpp`

Pattern miroir de `TerrainMeshGpuCache` mais avec 2× `VkImage` (RGBA8 257×257) + `VkImageView` par chunk. Allocator injectable similaire (`IGpuImageAllocator`).

- [ ] **Step 1 : Header**

```cpp
// engine/render/terrain_chunk/SplatMapGpuCache.h
#pragma once

#include "engine/render/terrain_chunk/ChunkRuntime.h"
#include "engine/world/WorldModel.h"
#include "engine/world/terrain/SplatMap.h"

#include <unordered_map>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	struct SplatMapGpu
	{
		VkImage image0 = VK_NULL_HANDLE; ///< Layers 0..3 (RGBA = 4 layers)
		VkImage image1 = VK_NULL_HANDLE; ///< Layers 4..7
		VkImageView view0 = VK_NULL_HANDLE;
		VkImageView view1 = VK_NULL_HANDLE;
	};

	class IGpuImageAllocator
	{
	public:
		virtual ~IGpuImageAllocator() = default;
		/// Crée un VkImage R8G8B8A8 + alloue + upload `srcBytes` (taille
		/// `width * height * 4`). Retourne `{image, view}` couplés. View en
		/// VK_IMAGE_VIEW_TYPE_2D, format VK_FORMAT_R8G8B8A8_UNORM.
		virtual void CreateAndUploadRGBA8Image(uint32_t width, uint32_t height,
			const void* srcBytes, VkImage& outImage, VkImageView& outView) = 0;
		virtual void DestroyImage(VkImage image, VkImageView view) = 0;
	};

	class SplatMapGpuCache
	{
	public:
		void Init(IGpuImageAllocator* alloc, ChunkRuntime* runtime);
		void Shutdown();

		/// Convertit la `SplatMap` (8 octets par cellule, planar) en deux blobs
		/// 257×257×4 RGBA8 (layers 0..3 dans image0, 4..7 dans image1) puis
		/// upload + cache.
		SplatMapGpu GetOrUpload(engine::world::GlobalChunkCoord coord,
			const engine::world::terrain::SplatMap& splat);

		SplatMapGpu Lookup(engine::world::GlobalChunkCoord coord) const;
		void Evict(engine::world::GlobalChunkCoord coord);
		size_t GetCachedCount() const { return m_cache.size(); }

	private:
		struct Entry
		{
			SplatMapGpu maps;
			size_t bytes = 0;
		};
		IGpuImageAllocator* m_alloc = nullptr;
		ChunkRuntime* m_runtime = nullptr;
		std::unordered_map<uint64_t, Entry> m_cache;

		static uint64_t PackCoord(engine::world::GlobalChunkCoord c);
	};
}
```

- [ ] **Step 2 : Tests red**

```cpp
// engine/render/terrain_chunk/tests/SplatMapGpuCacheTests.cpp
#include "engine/render/terrain_chunk/SplatMapGpuCache.h"

#include <cstdio>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	using engine::render::terrain_chunk::ChunkRuntime;
	using engine::render::terrain_chunk::SplatMapGpuCache;
	using engine::render::terrain_chunk::IGpuImageAllocator;
	using engine::world::terrain::SplatMap;

	struct MockImageAllocator final : IGpuImageAllocator
	{
		uint64_t nextHandle = 1;
		int creates = 0;
		int destroys = 0;

		void CreateAndUploadRGBA8Image(uint32_t, uint32_t, const void*,
			VkImage& outImage, VkImageView& outView) override
		{
			++creates;
			outImage = reinterpret_cast<VkImage>(nextHandle++);
			outView  = reinterpret_cast<VkImageView>(nextHandle++);
		}
		void DestroyImage(VkImage, VkImageView) override { ++destroys; }
	};

	void Test_GetOrUpload_TwoImagesPerChunk()
	{
		MockImageAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		SplatMapGpuCache cache; cache.Init(&alloc, &rt);
		auto splat = SplatMap::MakeUniform(0u);
		cache.GetOrUpload({0, 0}, splat);
		REQUIRE(alloc.creates == 2); // image0 + image1
	}

	void Test_GetOrUpload_CachesAndReuses()
	{
		MockImageAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		SplatMapGpuCache cache; cache.Init(&alloc, &rt);
		auto splat = SplatMap::MakeUniform(0u);
		cache.GetOrUpload({0, 0}, splat);
		cache.GetOrUpload({0, 0}, splat);
		REQUIRE(alloc.creates == 2); // pas de re-upload
		REQUIRE(cache.GetCachedCount() == 1);
	}

	void Test_Evict_DestroysImages()
	{
		MockImageAllocator alloc;
		ChunkRuntime rt; rt.Init({});
		SplatMapGpuCache cache; cache.Init(&alloc, &rt);
		auto splat = SplatMap::MakeUniform(0u);
		cache.GetOrUpload({0, 0}, splat);
		cache.Evict({0, 0});
		REQUIRE(alloc.destroys == 2);
		REQUIRE(cache.GetCachedCount() == 0);
	}
}

int main()
{
	Test_GetOrUpload_TwoImagesPerChunk();
	Test_GetOrUpload_CachesAndReuses();
	Test_Evict_DestroysImages();
	if (g_failed == 0) { std::printf("[PASS] SplatMapGpuCacheTests (3/3)\n"); return 0; }
	std::printf("[FAIL] SplatMapGpuCacheTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 3 : Commit (red)**

```bash
git commit -m "test(render/terrain_chunk): SplatMapGpuCache tests (TDD red)"
```

---

### Task 7 : `SplatMapGpuCache.cpp` impl + green

**Files :**
- Create : `engine/render/terrain_chunk/SplatMapGpuCache.cpp`

- [ ] **Step 1 : Impl avec conversion planar 8-channel → 2× RGBA8**

La `SplatMap` stocke `weights[(z*257+x)*8 + layer]` (interleaved). Pour les 2 VkImage RGBA8, on doit convertir en 2 blobs planar :
- `blob0[(z*257+x)*4 + c]` = `weights[(z*257+x)*8 + c]` pour c in [0..3]
- `blob1[(z*257+x)*4 + c]` = `weights[(z*257+x)*8 + (4+c)]` pour c in [0..3]

```cpp
// engine/render/terrain_chunk/SplatMapGpuCache.cpp
#include "engine/render/terrain_chunk/SplatMapGpuCache.h"

#include <vector>

namespace engine::render::terrain_chunk
{
	uint64_t SplatMapGpuCache::PackCoord(engine::world::GlobalChunkCoord c)
	{
		return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
		     | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
	}

	void SplatMapGpuCache::Init(IGpuImageAllocator* alloc, ChunkRuntime* runtime)
	{ m_alloc = alloc; m_runtime = runtime; m_cache.clear(); }

	void SplatMapGpuCache::Shutdown()
	{
		if (!m_alloc) return;
		for (auto& [key, entry] : m_cache)
			m_alloc->DestroyImage(entry.maps.image0, entry.maps.view0);
		// Note : DestroyImage gère les 2 paires en réalité — voir step impl
		// pour le compte de Destroy par chunk.
		m_cache.clear();
	}

	SplatMapGpu SplatMapGpuCache::GetOrUpload(engine::world::GlobalChunkCoord coord,
		const engine::world::terrain::SplatMap& splat)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it != m_cache.end()) return it->second.maps;

		const uint32_t res = splat.resolution;
		const size_t cellCount = static_cast<size_t>(res) * res;
		std::vector<uint8_t> blob0(cellCount * 4u, 0u);
		std::vector<uint8_t> blob1(cellCount * 4u, 0u);
		for (size_t cell = 0; cell < cellCount; ++cell)
		{
			for (uint32_t c = 0; c < 4; ++c)
			{
				blob0[cell * 4u + c] = splat.weights[cell * splat.layerCount + c];
				blob1[cell * 4u + c] = splat.weights[cell * splat.layerCount + (4u + c)];
			}
		}

		Entry entry;
		m_alloc->CreateAndUploadRGBA8Image(res, res, blob0.data(),
			entry.maps.image0, entry.maps.view0);
		m_alloc->CreateAndUploadRGBA8Image(res, res, blob1.data(),
			entry.maps.image1, entry.maps.view1);
		entry.bytes = (blob0.size() + blob1.size());
		m_cache.emplace(key, entry);

		if (m_runtime)
		{
			auto slot = m_runtime->GetOrAllocateSlot(coord);
			m_runtime->AddResidentBytes(slot, entry.bytes);
		}
		return entry.maps;
	}

	SplatMapGpu SplatMapGpuCache::Lookup(engine::world::GlobalChunkCoord coord) const
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return SplatMapGpu{};
		return it->second.maps;
	}

	void SplatMapGpuCache::Evict(engine::world::GlobalChunkCoord coord)
	{
		const uint64_t key = PackCoord(coord);
		auto it = m_cache.find(key);
		if (it == m_cache.end()) return;
		if (m_alloc)
		{
			m_alloc->DestroyImage(it->second.maps.image0, it->second.maps.view0);
			m_alloc->DestroyImage(it->second.maps.image1, it->second.maps.view1);
		}
		m_cache.erase(it);
	}
}
```

**Note importante** : le test `Test_Evict_DestroysImages` attend `alloc.destroys == 2` (1 par image). Donc le mock compte chaque appel `DestroyImage` séparément, et on appelle bien 2× dans `Evict` (image0 puis image1). Conforme.

Le test `Shutdown` n'est pas explicitement dans la suite, mais le `Shutdown` doit aussi appeler `DestroyImage` 2× par entrée — corriger l'impl :

```cpp
void SplatMapGpuCache::Shutdown()
{
	if (!m_alloc) return;
	for (auto& [key, entry] : m_cache)
	{
		m_alloc->DestroyImage(entry.maps.image0, entry.maps.view0);
		m_alloc->DestroyImage(entry.maps.image1, entry.maps.view1);
	}
	m_cache.clear();
}
```

- [ ] **Step 2 : CMake (sources + test target `splat_map_gpu_cache_tests`)**

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(render/terrain_chunk): SplatMapGpuCache 2 RGBA8 par chunk"
```

---

### Task 8 : `LayerArrayLoader` — interface + tests red

**Files :**
- Create : `engine/render/terrain_chunk/LayerArrayLoader.h`
- Create : `engine/render/terrain_chunk/tests/LayerArrayLoaderTests.cpp`

Charge les 24 textures (8 layers × 3 maps : albedo/normal/arm) en 3 `VkImageView2DArray`. Si `.texr` absent, fallback `assets/terrain/placeholders/<name>.png`. **Pour la testabilité**, on extrait un helper `ResolveLayerAssetPath(palette, layerIndex, mapType, contentRoot, fileExists)` testable sans Vulkan.

- [ ] **Step 1 : Header**

```cpp
// engine/render/terrain_chunk/LayerArrayLoader.h
#pragma once

#include "engine/world/terrain/LayerPalette.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	enum class LayerMapType : uint8_t { Albedo, Normal, Arm };

	/// Résout le chemin disque pour la texture (`layerIndex`, `mapType`).
	/// Stratégie : d'abord essaie `<contentRoot>/<palette.layers[i].albedoPath>`
	/// (ou normalPath / armPath selon mapType) ; si `fileExists` retourne false,
	/// fallback vers `<contentRoot>/terrain/placeholders/<layerName>.png`. Si
	/// le placeholder est aussi absent, retourne le chemin du placeholder
	/// quand même (le caller logue un warning et drop dans la texture).
	///
	/// `fileExists` est injectable pour le test (mock du système de fichiers).
	std::filesystem::path ResolveLayerAssetPath(
		const engine::world::terrain::LayerPalette& palette,
		uint32_t layerIndex,
		LayerMapType mapType,
		std::string_view contentRoot,
		const std::function<bool(const std::filesystem::path&)>& fileExists);

	struct LayerArrayResources
	{
		VkImage albedoArrayImage = VK_NULL_HANDLE;
		VkImage normalArrayImage = VK_NULL_HANDLE;
		VkImage armArrayImage    = VK_NULL_HANDLE;
		VkImageView albedoArrayView = VK_NULL_HANDLE;
		VkImageView normalArrayView = VK_NULL_HANDLE;
		VkImageView armArrayView    = VK_NULL_HANDLE;
		VkSampler nearestSampler = VK_NULL_HANDLE; // pour splatMap0/1
		VkSampler linearSampler  = VK_NULL_HANDLE; // pour les 3 arrays
	};

	class IGpuImageArrayAllocator
	{
	public:
		virtual ~IGpuImageArrayAllocator() = default;
		/// Crée un VkImage 2D Array (8 layers) + view. `pixelData` est un blob
		/// de 8 layers concaténés (chaque layer = `width * height * 4` octets RGBA8).
		virtual void CreateAndUploadRGBA8Array(uint32_t width, uint32_t height,
			uint32_t layerCount, const void* pixelData,
			VkImage& outImage, VkImageView& outView) = 0;
		virtual VkSampler CreateSampler(bool linear) = 0;
		virtual void DestroyImage(VkImage image, VkImageView view) = 0;
		virtual void DestroySampler(VkSampler sampler) = 0;
	};

	class LayerArrayLoader
	{
	public:
		bool Init(IGpuImageArrayAllocator* alloc,
			const engine::world::terrain::LayerPalette& palette,
			std::string_view contentRoot, std::string& outError);
		void Shutdown();
		const LayerArrayResources& GetResources() const { return m_res; }

	private:
		IGpuImageArrayAllocator* m_alloc = nullptr;
		LayerArrayResources m_res;
	};
}
```

- [ ] **Step 2 : Tests red — focus sur ResolveLayerAssetPath (pure CPU)**

```cpp
// engine/render/terrain_chunk/tests/LayerArrayLoaderTests.cpp
#include "engine/render/terrain_chunk/LayerArrayLoader.h"
#include "engine/world/terrain/LayerPalette.h"

#include <cstdio>
#include <set>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
	} while (0)

	using engine::render::terrain_chunk::LayerMapType;
	using engine::render::terrain_chunk::ResolveLayerAssetPath;
	using engine::world::terrain::LayerPalette;

	LayerPalette MakePalette()
	{
		LayerPalette p;
		const char* names[8] = { "dirt", "grass_dry", "grass_wet", "mud", "sand", "rock", "snow", "lava_cooled" };
		for (uint32_t i = 0; i < 8; ++i)
		{
			p.layers[i].index = i;
			p.layers[i].name = names[i];
			p.layers[i].albedoPath = std::string("tex/terrain/") + names[i] + "_albedo.texr";
			p.layers[i].normalPath = std::string("tex/terrain/") + names[i] + "_normal.texr";
			p.layers[i].armPath    = std::string("tex/terrain/") + names[i] + "_arm.texr";
		}
		return p;
	}

	void Test_ResolvePath_ReturnsTexrIfExists()
	{
		auto palette = MakePalette();
		auto p = ResolveLayerAssetPath(palette, 0, LayerMapType::Albedo, "game/data",
			[](const std::filesystem::path& path)
			{
				return path.string().find("dirt_albedo.texr") != std::string::npos;
			});
		REQUIRE(p.string().find("dirt_albedo.texr") != std::string::npos);
	}

	void Test_ResolvePath_FallsBackToPlaceholder()
	{
		auto palette = MakePalette();
		auto p = ResolveLayerAssetPath(palette, 0, LayerMapType::Albedo, "game/data",
			[](const std::filesystem::path& path)
			{
				// Aucun .texr présent ; placeholder existe.
				return path.string().find("placeholders") != std::string::npos
				    && path.string().find("dirt.png") != std::string::npos;
			});
		REQUIRE(p.string().find("placeholders") != std::string::npos);
		REQUIRE(p.string().find("dirt.png") != std::string::npos);
	}

	void Test_ResolvePath_ContentRootIsPrefix()
	{
		auto palette = MakePalette();
		auto p = ResolveLayerAssetPath(palette, 5, LayerMapType::Normal, "/some/root",
			[](const std::filesystem::path&) { return true; });
		REQUIRE(p.string().find("/some/root") == 0
		     || p.string().find("\\some\\root") == 0);
	}

	void Test_ResolvePath_AllMapTypesReturnsDistinct()
	{
		auto palette = MakePalette();
		auto fileExists = [](const std::filesystem::path&) { return true; };
		std::set<std::string> paths;
		paths.insert(ResolveLayerAssetPath(palette, 0, LayerMapType::Albedo, "r", fileExists).string());
		paths.insert(ResolveLayerAssetPath(palette, 0, LayerMapType::Normal, "r", fileExists).string());
		paths.insert(ResolveLayerAssetPath(palette, 0, LayerMapType::Arm,    "r", fileExists).string());
		REQUIRE(paths.size() == 3); // 3 paths distincts
	}
}

int main()
{
	Test_ResolvePath_ReturnsTexrIfExists();
	Test_ResolvePath_FallsBackToPlaceholder();
	Test_ResolvePath_ContentRootIsPrefix();
	Test_ResolvePath_AllMapTypesReturnsDistinct();
	if (g_failed == 0) { std::printf("[PASS] LayerArrayLoaderTests (4/4)\n"); return 0; }
	std::printf("[FAIL] LayerArrayLoaderTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 3 : Commit (red)**

```bash
git commit -m "test(render/terrain_chunk): LayerArrayLoader path resolution tests (TDD red)"
```

---

### Task 9 : `LayerArrayLoader.cpp` impl + green

**Files :**
- Create : `engine/render/terrain_chunk/LayerArrayLoader.cpp`

- [ ] **Step 1 : Impl pure-CPU `ResolveLayerAssetPath`**

```cpp
// engine/render/terrain_chunk/LayerArrayLoader.cpp
#include "engine/render/terrain_chunk/LayerArrayLoader.h"

namespace engine::render::terrain_chunk
{
	std::filesystem::path ResolveLayerAssetPath(
		const engine::world::terrain::LayerPalette& palette,
		uint32_t layerIndex,
		LayerMapType mapType,
		std::string_view contentRoot,
		const std::function<bool(const std::filesystem::path&)>& fileExists)
	{
		if (layerIndex >= palette.layers.size()) layerIndex = 0;
		const auto& entry = palette.layers[layerIndex];
		std::string relPath;
		switch (mapType)
		{
		case LayerMapType::Albedo: relPath = entry.albedoPath; break;
		case LayerMapType::Normal: relPath = entry.normalPath; break;
		case LayerMapType::Arm:    relPath = entry.armPath; break;
		}
		std::filesystem::path texr = std::filesystem::path(contentRoot) / relPath;
		if (fileExists(texr)) return texr;
		// Fallback placeholder.
		std::filesystem::path placeholder = std::filesystem::path(contentRoot)
			/ "terrain" / "placeholders" / (entry.name + ".png");
		return placeholder;
	}

	bool LayerArrayLoader::Init(IGpuImageArrayAllocator* alloc,
		const engine::world::terrain::LayerPalette& palette,
		std::string_view contentRoot, std::string& outError)
	{
		m_alloc = alloc;
		// Pour chaque mapType (Albedo, Normal, Arm), résoudre les 8 paths,
		// charger les 8 PNG via stb_image (déjà au repo), concaténer en blob
		// 8 × (width * height * 4), créer VkImage 2D Array via l'allocator.
		// Cette PR : on assume taille uniforme 4×4 (placeholders) ou exiger
		// que tous les .texr soient même taille — sinon outError.
		//
		// L'impl complète Vulkan vit dans la version réelle de
		// IGpuImageArrayAllocator (cf. Task 10/11 où on l'instancie). Ici on
		// ne fait que résolution de path + conditions d'erreur.
		(void)palette; (void)contentRoot; (void)outError;
		// TODO Task 10 : intégrer le vrai loading PNG + upload Vulkan.
		// Pour Task 9 (pure CPU + TDD green), on ne charge rien — l'impl
		// concrète vient quand on instancie l'allocator Vulkan dans
		// TerrainChunkRenderer.
		return true;
	}

	void LayerArrayLoader::Shutdown()
	{
		if (!m_alloc) return;
		if (m_res.albedoArrayImage) m_alloc->DestroyImage(m_res.albedoArrayImage, m_res.albedoArrayView);
		if (m_res.normalArrayImage) m_alloc->DestroyImage(m_res.normalArrayImage, m_res.normalArrayView);
		if (m_res.armArrayImage)    m_alloc->DestroyImage(m_res.armArrayImage,    m_res.armArrayView);
		if (m_res.nearestSampler)   m_alloc->DestroySampler(m_res.nearestSampler);
		if (m_res.linearSampler)    m_alloc->DestroySampler(m_res.linearSampler);
		m_res = LayerArrayResources{};
	}
}
```

**Note importante** : l'impl complète du loading PNG + upload est différée à Task 10 (orchestrateur), où on a accès au vrai allocator Vulkan. Cette task livre la résolution de path testable + le squelette `Init`/`Shutdown`.

- [ ] **Step 2 : CMake (sources + test target `layer_array_loader_tests`)**

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(render/terrain_chunk): LayerArrayLoader path resolution + skeleton"
```

---

### Task 10 : `DescriptorSetPool` (~150 lignes Vulkan)

**Files :**
- Create : `engine/render/terrain_chunk/DescriptorSetPool.h`
- Create : `engine/render/terrain_chunk/DescriptorSetPool.cpp`

Pas de tests CPU ici (purement Vulkan). Test runtime via le smoke test post-merge.

- [ ] **Step 1 : Header + impl**

```cpp
// engine/render/terrain_chunk/DescriptorSetPool.h
#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	/// Pool dédié au splat set du `TerrainChunkPipeline`. Sized pour la taille
	/// max de Visible ring (7×7 = 49 sets résidents simultanés). Alloue à la
	/// demande, libère via `Reset` au Shutdown.
	class DescriptorSetPool
	{
	public:
		bool Init(VkDevice device, VkDescriptorSetLayout splatSetLayout,
			uint32_t maxSets, std::string& outError);
		void Shutdown(VkDevice device);

		/// Alloue un descriptor set du layout splat. Le caller est responsable
		/// de l'écriture (`vkUpdateDescriptorSets`). Retourne VK_NULL_HANDLE si
		/// le pool est saturé.
		VkDescriptorSet Allocate(VkDevice device);

		/// Reset le pool (libère tous les sets alloués). Préserve `m_pool` lui-même.
		void Reset(VkDevice device);

	private:
		VkDescriptorPool m_pool = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
		uint32_t m_maxSets = 0;
		uint32_t m_allocatedSets = 0;
	};
}
```

```cpp
// engine/render/terrain_chunk/DescriptorSetPool.cpp
#include "engine/render/terrain_chunk/DescriptorSetPool.h"

namespace engine::render::terrain_chunk
{
	bool DescriptorSetPool::Init(VkDevice device, VkDescriptorSetLayout splatSetLayout,
		uint32_t maxSets, std::string& outError)
	{
		m_layout = splatSetLayout;
		m_maxSets = maxSets;

		// 5 samplers + 1 UBO = 6 bindings par set (cf. terrain_chunk.frag).
		VkDescriptorPoolSize sizes[2]{};
		sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[0].descriptorCount = 5u * maxSets;
		sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		sizes[1].descriptorCount = 1u * maxSets;

		VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		info.flags = 0;
		info.maxSets = maxSets;
		info.poolSizeCount = 2;
		info.pPoolSizes = sizes;

		if (vkCreateDescriptorPool(device, &info, nullptr, &m_pool) != VK_SUCCESS)
		{
			outError = "DescriptorSetPool: vkCreateDescriptorPool failed";
			return false;
		}
		return true;
	}

	void DescriptorSetPool::Shutdown(VkDevice device)
	{
		if (m_pool) vkDestroyDescriptorPool(device, m_pool, nullptr);
		m_pool = VK_NULL_HANDLE;
	}

	VkDescriptorSet DescriptorSetPool::Allocate(VkDevice device)
	{
		if (!m_pool || !m_layout) return VK_NULL_HANDLE;
		if (m_allocatedSets >= m_maxSets) return VK_NULL_HANDLE;

		VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		info.descriptorPool = m_pool;
		info.descriptorSetCount = 1;
		info.pSetLayouts = &m_layout;
		VkDescriptorSet set = VK_NULL_HANDLE;
		if (vkAllocateDescriptorSets(device, &info, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
		++m_allocatedSets;
		return set;
	}

	void DescriptorSetPool::Reset(VkDevice device)
	{
		if (!m_pool) return;
		vkResetDescriptorPool(device, m_pool, 0);
		m_allocatedSets = 0;
	}
}
```

- [ ] **Step 2 : CMake + commit**

```bash
git commit -m "feat(render/terrain_chunk): DescriptorSetPool max 49 sets"
```

---

### Task 11 : `TerrainChunkRenderer` orchestrateur (subagent recommandé)

**Files :**
- Create : `engine/render/terrain_chunk/TerrainChunkRenderer.h`
- Create : `engine/render/terrain_chunk/TerrainChunkRenderer.cpp`

**Note** : Task 11 est l'orchestrateur principal qui glue tous les composants. Volume ~400-600 lignes Vulkan + intégration avec `Engine`/`StreamCache`/`AssetRegistry`/`StagingAllocator`. **Recommandé : dispatcher un subagent** avec les fichiers de tasks 1-10 comme base + accès à `engine/render/DeferredPipeline.cpp` pour pattern.

- [ ] **Step 1 : Header API publique**

```cpp
// engine/render/terrain_chunk/TerrainChunkRenderer.h
#pragma once

#include "engine/render/TerrainChunkPipeline.h"
#include "engine/render/terrain_chunk/ChunkRuntime.h"
#include "engine/render/terrain_chunk/DescriptorSetPool.h"
#include "engine/render/terrain_chunk/LayerArrayLoader.h"
#include "engine/render/terrain_chunk/SplatMapGpuCache.h"
#include "engine/render/terrain_chunk/TerrainMeshGpuCache.h"
#include "engine/world/WorldModel.h"

#include <memory>
#include <vector>

namespace engine::core { class Config; }
namespace engine::world { class StreamCache; class World; }
namespace engine::render { class AssetRegistry; namespace vk { class StagingAllocator; } }

namespace engine::render::terrain_chunk
{
	/// Orchestrateur du runtime terrain chunk-based (M100 post-Phase-3a).
	/// Possède le pipeline + caches + loader, expose une API
	/// `Init/Shutdown/RenderVisibleChunks/Tick`.
	class TerrainChunkRenderer
	{
	public:
		bool Init(VkDevice device, VkPhysicalDevice physDev,
			VkRenderPass renderPass, VkDescriptorSetLayout cameraSetLayout,
			engine::render::vk::StagingAllocator* staging,
			engine::render::AssetRegistry* assetRegistry,
			engine::world::StreamCache* streamCache,
			const engine::core::Config& config,
			const std::string& contentRoot,
			const std::string& shaderRootPath,
			std::string& outError);
		void Shutdown(VkDevice device);

		/// Appelé chaque frame depuis `GeometryPass::Record`. Itère les chunks
		/// visibles via `world.GetRingForChunk` ; pour chaque chunk avec
		/// terrain.bin + splat.bin présents, upload (1ère fois) puis dessine.
		/// Sans branche `m_editorEnabled`.
		void RenderVisibleChunks(VkCommandBuffer cmd,
			VkDescriptorSet cameraSet,
			const engine::world::World& world,
			const std::vector<engine::world::GlobalChunkCoord>& visibleChunks);

		/// Appelé entre frames. Évince les chunks LRU non-Visible si budget
		/// dépassé. Idempotent.
		void Tick();

	private:
		void EnsureChunkResident(VkCommandBuffer cmd, engine::world::GlobalChunkCoord coord);

		TerrainChunkPipeline m_pipeline;
		ChunkRuntime m_runtime;
		std::unique_ptr<class VulkanBufferAllocator> m_bufferAlloc;
		std::unique_ptr<class VulkanImageAllocator> m_imageAlloc;
		std::unique_ptr<class VulkanImageArrayAllocator> m_imageArrayAlloc;
		TerrainMeshGpuCache m_meshCache;
		SplatMapGpuCache m_splatCache;
		LayerArrayLoader m_layerLoader;
		DescriptorSetPool m_descPool;
		engine::world::StreamCache* m_streamCache = nullptr;
		const engine::core::Config* m_config = nullptr;
	};
}
```

- [ ] **Step 2 : Impl orchestrateur (subagent)**

L'impl `.cpp` (~400-600 lignes) gère :
1. **Allocateurs Vulkan concrets** (`VulkanBufferAllocator`, `VulkanImageAllocator`, `VulkanImageArrayAllocator`) — wrappers internes qui implémentent `IGpuBufferAllocator` / `IGpuImageAllocator` / `IGpuImageArrayAllocator` en utilisant `StagingAllocator` pour les uploads et `vkCreateBuffer`/`vkCreateImage` pour les ressources.
2. **`Init`** : `m_pipeline.Init` (déjà fait en Phase 3a), `m_descPool.Init(maxSets=49)`, `LoadLayerPalette` depuis `assets/terrain/layer_palette.json`, `m_layerLoader.Init` (charge les 24 textures via `AssetRegistry::LoadTexture` ou directement stb_image, avec fallback placeholders, copie vers 3 VkImage2DArray, crée 2 samplers nearest+linear), `m_meshCache.Init`, `m_splatCache.Init`, `m_runtime.Init({budget=cfg.GetUint("editor.world.terrain.gpu_budget_mb", 256) * 1024*1024})`.
3. **`RenderVisibleChunks`** : pour chaque coord dans `visibleChunks` :
   - `auto chunk = m_streamCache->LoadTerrainChunk(*m_config, coord.x, coord.z);` — si nullptr, skip.
   - `auto splat = m_streamCache->LoadSplatMap(*m_config, coord.x, coord.z);` — si nullptr, skip (legacy le dessine).
   - `auto mesh = m_meshCache.GetOrUpload(coord, *chunk);`
   - `auto splatGpu = m_splatCache.GetOrUpload(coord, *splat);`
   - `auto descSet = m_descPool.Allocate(device);` puis `vkUpdateDescriptorSets(device, 6 writes : splatMap0, splatMap1, albedoArray, normalArray, armArray, layerParamsUbo)`. Pour le `layerParamsUbo`, allouer un UBO via `StagingAllocator` avec `tilingScale[8]` (1/tilingMeters) issu de la palette.
   - `m_runtime.UpdateRing(coord, world.GetRingForChunk(coord));`
   - `m_runtime.Touch(slot);`
   - `m_pipeline.RecordChunkDraw(cmd, cameraSet, descSet, mesh, originX, 0.0f, originZ);`
4. **`Tick`** : `auto evictions = m_runtime.CollectEvictionsForBudget(); for each : m_meshCache.Evict; m_splatCache.Evict; m_runtime.RemoveSlot;`. Reset `m_descPool` (les sets ne sont plus valides après un cycle Render+Tick — alternative : ne pas reset, juste accept que les anciens sets soient inutilisables ; choix recommandé : reset au Tick).

**Subagent prompt** : voir Task 13 du plan Phase 3a comme référence (qui a livré le pipeline de base). Le subagent doit produire `TerrainChunkRenderer.cpp` + les 3 allocateurs Vulkan concrets. Référence à copier-coller : le pattern des allocateurs concrets dans `engine/render/vk/StagingAllocator.cpp` + `DeferredPipeline.cpp`.

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(render/terrain_chunk): TerrainChunkRenderer orchestrateur"
```

---

### Task 12 : Intégration `Engine::Init` + `GeometryPass::Record`

**Files :**
- Modify : `engine/Engine.cpp` (init + shutdown)
- Modify : `engine/render/GeometryPass.h` + `.cpp` (drawcall)

- [ ] **Step 1 : `Engine::Init` — wiring**

Trouver dans `engine/Engine.cpp` le bloc qui init `m_deferredPipeline` (probable autour du boot rendu). Après cet init, ajouter :

```cpp
// engine/Engine.cpp - dans Engine::Init :
if (m_terrainChunkRenderer)
{
	std::string err;
	if (!m_terrainChunkRenderer->Init(
		m_device, m_physicalDevice,
		m_geometryPass.GetRenderPass(),
		m_geometryPass.GetCameraSetLayout(),
		&m_stagingAllocator, &m_assetRegistry, &m_streamCache,
		m_config, m_config.GetString("paths.content", "game/data"),
		"game/data/shaders", err))
	{
		LOG_ERROR(Render, "[Engine] TerrainChunkRenderer init failed: {}", err);
	}
}
```

Si `m_terrainChunkRenderer` n'existe pas encore comme membre, l'ajouter à `Engine.h` :
```cpp
std::unique_ptr<engine::render::terrain_chunk::TerrainChunkRenderer> m_terrainChunkRenderer;
```

Si `m_geometryPass.GetRenderPass()` ou `GetCameraSetLayout()` n'existent pas, les ajouter (lecteurs publics). Ces accesseurs étaient déjà nécessaires implicitement pour `TerrainChunkPipeline::Init` qui attend ces paramètres.

- [ ] **Step 2 : `Engine::Shutdown` — teardown**

```cpp
if (m_terrainChunkRenderer) m_terrainChunkRenderer->Shutdown(m_device);
```

- [ ] **Step 3 : `Engine::EndFrame` — Tick**

```cpp
if (m_terrainChunkRenderer) m_terrainChunkRenderer->Tick();
```

- [ ] **Step 4 : `GeometryPass::Record` — drawcall**

Modifier la signature pour accepter `TerrainChunkRenderer*` (ou via setter `SetTerrainChunkRenderer`). Dans la boucle des chunks visibles (ou ajouter une nouvelle phase si elle n'existe pas), appeler :

```cpp
if (m_terrainChunkRenderer)
{
	// Récupère la liste des chunks visibles depuis World.
	std::vector<engine::world::GlobalChunkCoord> visible = /* from world.GetActiveAndVisibleChunks() */;
	m_terrainChunkRenderer->RenderVisibleChunks(cmd, cameraSet, world, visible);
}
```

**Note** : si `World::GetActiveAndVisibleChunks()` n'existe pas, l'ajouter (boucle sur `Active` ring + `Visible` ring depuis `World::GetRingForChunk`).

- [ ] **Step 5 : Vérifier absence de branche éditeur**

```bash
grep -RIn "m_editorEnabled" engine/render/terrain_chunk/ engine/render/GeometryPass.cpp engine/Engine.cpp
```

Expected : aucune nouvelle ligne dans les fichiers `terrain_chunk/`. Si le drawcall dans GeometryPass est branché derrière un `m_editorEnabled`, retirer (le critère M100.5/.9 est sans branche éditeur).

- [ ] **Step 6 : Commit**

```bash
git commit -m "feat(engine+render): wire TerrainChunkRenderer dans GeometryPass"
```

---

### Task 13 : Config `editor.world.terrain.gpu_budget_mb`

**Files :**
- Modify : `config.json`

- [ ] **Step 1 : Ajouter clé dans la section `editor.world.terrain`**

```json
"terrain": {
  "_comment": "M100.8 + Terrain Chunk Runtime — gpu_budget_mb borne la mémoire GPU consommée par le runtime chunk (mesh + splat-maps). Au-delà, eviction LRU des chunks Far. Default 256 MB.",
  "lodWorkers": 0,
  "gpu_budget_mb": 256
}
```

- [ ] **Step 2 : Commit**

```bash
git commit -m "config: editor.world.terrain.gpu_budget_mb (Terrain Chunk Runtime)"
```

---

### Task 14 : Build + run all new tests

- [ ] **Step 1 : Build**

```bash
cmake --build build 2>&1 | tail -30
```

Expected : tous les targets compilent (4 nouveaux + existants).

- [ ] **Step 2 : Run new tests**

```bash
ctest --test-dir build -R "chunk_runtime_tests|terrain_mesh_gpu_cache_tests|splat_map_gpu_cache_tests|layer_array_loader_tests" --output-on-failure
```

Expected : 15 tests verts.

- [ ] **Step 3 : Anti-dup serveur**

```bash
grep -RIn "engine::render::terrain_chunk" engine/server/ 2>&1 | head -5
```

Expected : 0.

- [ ] **Step 4 : Pas de commit** — validation pure.

---

### Task 15 : Marquer dettes résolues dans INDEX.md + résumé PR

**Files :**
- Modify : `tickets/M100/INDEX.md`

- [ ] **Step 1 : Mettre à jour M100.5 et M100.9 (Done complet)**

Avant :
```
| M100.5  | Heightmap Data Structure | … | Done partiel (CI pending — drawcall GeometryPass déféré à M100.9) |
| M100.9  | Splat Map System | … | Done partiel (CI pending — drawcall GeometryPass déféré à PR Terrain Chunk Runtime) |
```

Après :
```
| M100.5  | Heightmap Data Structure | … | Done (CI pending) |
| M100.9  | Splat Map System | … | Done (CI pending) |
```

- [ ] **Step 2 : Commit**

```bash
git add tickets/M100/INDEX.md
git commit -m "docs(tickets/M100): M100.5 + M100.9 Done (Terrain Chunk Runtime livre le drawcall)"
```

- [ ] **Step 3 : Résumé PR**

Préparer la description GitHub :
- Titre : « PR Terrain Chunk Runtime — résout dettes Task 11 (Phase 2) + Task 14 (Phase 3a) »
- Liste des 6 nouveaux composants + 4 tests neufs + intégration Engine/GeometryPass.
- Stats : `git diff origin/main..HEAD --stat | tail -3` ; `git log origin/main..HEAD --oneline | wc -l`.
- **Déploiement** : ✅ client uniquement, pas de redéploiement serveur.
- Smoke test post-merge requis : lancer `lcdlln_world_editor.exe`, créer un chunk avec terrain.bin + splat.bin, vérifier que le mesh s'affiche avec le blend 8-layer (placeholders colorés visibles).

---

## Self-Review Checklist (avant ouverture PR)

- [ ] Spec design §2 (6 composants) → Tasks 2-11 ✓.
- [ ] Spec design §3 décision 4 (cache LRU + budget) → Task 2/3 ✓.
- [ ] Spec design §3 décision 5 (LayerArrayLoader fallback) → Task 8/9 ✓.
- [ ] Spec design §3 décision 7 (sans branche éditeur) → Task 12 step 5 vérification ✓.
- [ ] Spec design §3 décision 8 (tests CPU mock) → Tasks 2/4/6/8 ✓.
- [ ] Anti-dup serveur : Task 14 step 3 ✓.
- [ ] Pas de placeholder TODO/TBD dans le plan.
- [ ] Tous les fichiers du Files Structure sont référencés par au moins une Task.

---

## Hand-off

Plan complet et sauvegardé dans `docs/superpowers/plans/2026-05-07-terrain-chunk-runtime.md`. Deux options d'exécution :

1. **Subagent-Driven (recommandé pour Tasks 11 + 12)** — l'orchestrateur Vulkan + l'intégration `Engine`/`GeometryPass` sont les blocs lourds. Inline pour les Tasks 2-10 (caches + tests pure CPU + DescriptorSetPool), subagent pour Tasks 11 + 12.
2. **Inline pour tout** — possible mais ~600 lignes Vulkan boilerplate à digérer dans le contexte courant.

Choix utilisateur attendu avant exécution.
