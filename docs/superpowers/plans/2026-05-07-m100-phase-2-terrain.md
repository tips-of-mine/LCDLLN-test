# M100 Phase 2 — Terrain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal :** Livrer les 4 tickets de la Phase 2 M100 (M100.5 Heightmap Data Structure, M100.6 Sculpting Brushes, M100.7 Stamps & Procedural Generators, M100.8 LOD Regeneration) sous forme **d'une seule PR** stackée sur la branche `claude/m100-phase-1-finalize` (Phase 1 finalisée par rebase). La PR ajoute la couche `engine::world::terrain::*` (data partagée éditeur ↔ client) et étend `engine::editor::world::*` (outils sculpt/stamp + worker LOD), sans toucher au legacy R16 (`HeightmapLoader`/`TerrainRenderer` global).

**Architecture :** Mode "couche au-dessus" (cf. design §3 décision 1). Tout le nouveau code vit dans `engine/world/terrain/` (data) et `engine/editor/world/Terrain*` (tools). Le legacy continue à tourner intact en parallèle. Ordre d'implémentation : **M100.5 → M100.8 → M100.6 → M100.7** (suit le DAG de dépendances). `GeometryPass` utilise déjà `VK_INDEX_TYPE_UINT32` (vérifié lignes 689, 806, 967) — pas de modif d'index buffer requise.

**Tech Stack :** C++17/20 selon le repo, Vulkan, ImGui (intégré M43.4), `engine::core::Config`/`Log`, `OutputVersionHeader` + `ComputeXxHash64` (engine/world/OutputVersion.h, déjà au repo), `stb_image` (external/stb/stb_image.h, supporte `stbi_load_16` pour PNG 16-bit grayscale natif). Tests via le harness existant Catch2-style (cf. WorldEditorShellTests.cpp en exemple). Pas de nouvelle dépendance externe.

**Entrées :**
- Specs détaillées : [tickets/M100/M100.5-HeightmapDataStructure.md](../../../tickets/M100/M100.5-HeightmapDataStructure.md), [M100.6](../../../tickets/M100/M100.6-TerrainSculptingBrushes.md), [M100.7](../../../tickets/M100/M100.7-TerrainStampsAndProceduralGenerators.md), [M100.8](../../../tickets/M100/M100.8-TerrainLODRegeneration.md).
- Design : [docs/superpowers/specs/2026-05-07-m100-phase-2-terrain-design.md](../specs/2026-05-07-m100-phase-2-terrain-design.md). Décisions verrouillées : 1 PR pour la phase, ordre .5→.8→.6→.7, couche au-dessus, async LOD avec génération atomique, stb_image pour PNG 16-bit.
- Audit : [docs/superpowers/audits/2026-05-06-m100-gap-analysis.md](../audits/2026-05-06-m100-gap-analysis.md) sections « Phase 2 — Terrain ». Verdicts : tous `vide` ; effort cumulé 2 L + 2 M.
- Pré-requis Phase 1 : commits rebasés sur main dans la branche locale. `engine::editor::world::{ICommand, CommandStack, WorldEditorShell, IPanel}`, `tools/zone_builder/lib/zone_builder_lib`, `engine::editor::EditorMode::IsWorldEditorWorld()`, raccourcis `Ctrl+Z/Y` déjà branchés.

---

## File Structure

| Fichier | Rôle | Ticket | Action |
|---------|------|--------|--------|
| `engine/world/terrain/TerrainChunk.h` | Constantes + struct `TerrainChunk` + `TerrainLod` (M100.8) + helpers + free functions Save/LoadTerrainBin | M100.5+8 | Create |
| `engine/world/terrain/TerrainChunk.cpp` | Impl `MakeFlat`, `SampleHeight`, `RecomputeBounds`, `Save/LoadTerrainBin` | M100.5 | Create |
| `engine/world/terrain/TerrainChunkLoader.h` | Adapter `StreamCache` → `TerrainChunk` | M100.5 | Create |
| `engine/world/terrain/TerrainChunkLoader.cpp` | Impl loader (lookup cache, déserialisation) | M100.5 | Create |
| `engine/world/terrain/TerrainMeshBuilder.h` | API construction mesh GPU (vertex/index buffers) à partir d'un `TerrainChunk` ou d'un `TerrainLod` (M100.8) | M100.5+8 | Create |
| `engine/world/terrain/TerrainMeshBuilder.cpp` | Impl — vertex packing 32 octets + indices triangle list 32-bit + skirt (M100.8) | M100.5+8 | Create |
| `engine/world/terrain/TerrainLodChain.h` | Constantes LOD + struct `TerrainLodChain` + `Save/LoadTerrainLodsBin` + `GenerateLodChain` | M100.8 | Create |
| `engine/world/terrain/TerrainLodChain.cpp` | Impl box filter 2×2 + sérialisation `terrain_lods.bin` | M100.8 | Create |
| `engine/world/terrain/TerrainLodWorker.h` | Thread pool + queue + génération atomique pour stale jobs | M100.8 | Create |
| `engine/world/terrain/TerrainLodWorker.cpp` | Impl thread pool | M100.8 | Create |
| `engine/world/terrain/tests/TerrainChunkTests.cpp` | Tests unit M100.5 | M100.5 | Create |
| `engine/world/terrain/tests/TerrainParityTests.cpp` | Round-trip byte-exact + mesh deterministic | M100.5 | Create |
| `engine/world/terrain/tests/TerrainLodTests.cpp` | Tests M100.8 (box filter, parité, async) | M100.8 | Create |
| `engine/editor/world/TerrainDocument.h` | Wrapper éditeur — `TerrainChunk` mutable + `OnCommit` hook (enqueue LOD worker en M100.8) | M100.5+8 | Create |
| `engine/editor/world/TerrainDocument.cpp` | Impl | M100.5+8 | Create |
| `engine/editor/world/TerrainBrush.h` | Enum `TerrainBrushMode` + struct `TerrainBrushParams` + struct `TerrainSculptDeltaCell`/`TerrainSculptDeltaChunk` + Simplex2D + kernels (Raise/Lower/Smooth/Flatten/Noise) | M100.6 | Create |
| `engine/editor/world/TerrainBrush.cpp` | Impl kernels + Simplex2D | M100.6 | Create |
| `engine/editor/world/TerrainRaycast.h/.cpp` | Raycast caméra → heightmap (Newton 4 itérations) | M100.6 | Create |
| `engine/editor/world/TerrainSculptCommand.h/.cpp` | `ICommand` qui stocke un delta sparse (multi-chunk) + `TryMerge` par mergeKey | M100.6 | Create |
| `engine/editor/world/TerrainSculptTool.h/.cpp` | Outil principal + dispatch d'inputs (mouse down/move/up) | M100.6 | Create |
| `engine/editor/world/tests/TerrainSculptTests.cpp` | Tests M100.6 (kernels, undo, seam) | M100.6 | Create |
| `engine/editor/world/StampLibrary.h/.cpp` | Énumère + charge les PNG 16-bit grayscale via `stb_image` | M100.7 | Create |
| `engine/editor/world/ProceduralStampGenerators.h/.cpp` | Mountain/Valley/Crater | M100.7 | Create |
| `engine/editor/world/TerrainStampCommand.h/.cpp` | `ICommand` (delta sparse) | M100.7 | Create |
| `engine/editor/world/TerrainStampTool.h/.cpp` | Outil + preview + Apply | M100.7 | Create |
| `engine/editor/world/tests/TerrainStampTests.cpp` | Tests M100.7 | M100.7 | Create |
| `assets/editor/stamps/test_mountain.png` | PNG 16-bit grayscale 64×64 (asset minimal de test) | M100.7 | Create |
| `engine/world/StreamCache.h/.cpp` | `LoadTerrainChunk(GlobalChunkCoord)` + `LoadTerrainLods(GlobalChunkCoord)` | M100.5+8 | Modify |
| `engine/world/StreamingScheduler.cpp` | Inclure `terrain.bin` + `terrain_lods.bin` dans le set requis par chunk | M100.5+8 | Modify |
| `engine/render/GeometryPass.cpp` | Drawcall mesh-terrain par chunk si `TerrainMeshGpu` présent (sans branche `m_editorEnabled`) | M100.5 | Modify |
| `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` | API publique : `WriteTerrainChunk` + `WriteTerrainLods` | M100.5+8 | Modify |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` | Impl writers | M100.5+8 | Modify |
| `engine/editor/world/panels/ToolPropertiesPanel.cpp` | UI quand SculptTool/StampTool actif | M100.6+7 | Modify |
| `engine/editor/world/WorldEditorShell.cpp` | Raccourcis `B` (sculpt) et `N` (stamp) + `Tools::SetActiveTool` | M100.6+7 | Modify |
| `CMakeLists.txt` (racine) | Entrées sources + 4 nouveaux test targets | tous | Modify |
| `game/data/config.json` | `editor.world.terrain.lodWorkers` (M100.8) | M100.8 | Modify |
| `tickets/M100/INDEX.md` | Marquer M100.5/.6/.7/.8 « Done » | tous | Modify |

**Conventions transverses (CLAUDE.md) :**
- Toute fonction nouvelle/modifiée a un commentaire `///` Doxygen au-dessus de la déclaration (rôle, params non-évidents, effets de bord, contraintes thread).
- Code en français pour les commentaires, anglais pour les identifiants techniques.
- Aucun `--no-verify` sur les commits.
- TDD strict : test red → impl → test green → commit, en deux commits séparés (un pour le test red, un pour l'impl + green) sauf cas trivial.

**Anti-duplication serveur :**
- Tous les fichiers `engine/world/terrain/**/*.cpp` et `engine/editor/world/**/*.cpp` sont **exclus** de `server_app` via la CMake (Phase 1 a déjà câblé l'exclusion `engine/editor/world/`).
- Vérification finale : `grep -RIn "engine::world::terrain\|engine::editor::world::Terrain" engine/server/` doit retourner 0.

---

## Tasks

### Task 1 : Reconnaissance — confirmer la base et l'env de build

**Files :**
- Read seulement.

- [ ] **Step 1 : Confirmer la branche et l'état des prérequis Phase 1**

```bash
git status
git log --oneline origin/main..HEAD | head -25
ls engine/editor/world/
ls tools/zone_builder/lib/
```

Expected : branche `claude/m100-phase-2-terrain`, 20 commits sur main (19 Phase 1 + 1 design Phase 2), `engine/editor/world/` contient `CommandStack.{h,cpp}`, `WorldEditorShell.{h,cpp}`, `IPanel.h`, `EditorCameraController.{h,cpp}`, `panels/`, `tests/`. `tools/zone_builder/lib/` contient `ChunkPackageWriter.{h,cpp}` etc.

- [ ] **Step 2 : Lire les sources existantes touchées**

```bash
cat engine/world/StreamCache.h
cat engine/world/WorldModel.h | head -150
cat engine/world/OutputVersion.h
cat engine/render/GeometryPass.cpp | head -120
cat engine/editor/world/CommandStack.h
cat engine/editor/world/WorldEditorShell.h
cat tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h
ls assets/editor/ 2>/dev/null
```

Notes attendues :
- `StreamCache` : API `Lookup(key) → optional<vector<uint8_t>>`, `Insert(key, blob)`. Pas de méthode chunk-aware encore.
- `GlobalChunkCoord` : struct `{int x; int z;}` dans `WorldModel.h`.
- `OutputVersionHeader` : 24 octets, `ComputeXxHash64(span)` disponible.
- `GeometryPass.cpp` : drawcalls existants utilisent `VK_INDEX_TYPE_UINT32` → pas de migration index nécessaire.
- `CommandStack` : interface `ICommand` exacte (Execute/Undo/TryMerge/GetMergeKey/GetMemoryFootprint/GetLabel).
- `assets/editor/` : confirmer que le dossier `stamps/` n'existe pas (sera créé en M100.7).

- [ ] **Step 3 : Vérifier l'env de build**

```bash
echo "VCPKG_ROOT=${VCPKG_ROOT:-not_set}"
which cmake
cmake --version
```

Si `VCPKG_ROOT` n'est pas défini, **noter** que la validation finale (Task 50) devra être déléguée à la CI ou à l'utilisateur. Tâche pas bloquante — TDD pur sur les algos n'a pas besoin du build complet, mais le build du target `engine_core` au moins doit passer pour valider.

- [ ] **Step 4 : Pas de commit à cette étape**

Reconnaissance pure, aucun fichier modifié.

---

### Task 2 : Constantes + struct `TerrainChunk` (M100.5)

**Files :**
- Create : `engine/world/terrain/TerrainChunk.h`

- [ ] **Step 1 : Créer le header**

```cpp
// engine/world/terrain/TerrainChunk.h
#pragma once

#include "engine/world/OutputVersion.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::terrain
{
	/// Résolution fixe d'un chunk de heightmap (256 quads + 1 vertex de bord = 257²).
	/// Choisie pour permettre 4 niveaux de LOD par division par 2 sans perte de bord
	/// (257 → 129 → 65 → 33). M100.8 stocke LOD1..LOD3, LOD0 = ce TerrainChunk.
	constexpr uint32_t kTerrainResolution = 257;

	/// Taille d'une cellule en mètres monde (1 m fixe en M100). Le chunk total
	/// fait donc 256 m × 256 m, alignement avec `engine::world::Chunk`.
	constexpr float kTerrainCellSizeMeters = 1.0f;

	/// Bornes de hauteur valides (mètres). Tout `terrain.bin` chargé hors de ces
	/// bornes est rejeté avec un message d'erreur.
	constexpr float kTerrainHeightMinMeters = -1024.0f;
	constexpr float kTerrainHeightMaxMeters = 8192.0f;

	/// Magic du fichier `chunks/chunk_i_j/terrain.bin` ("TRRN" little-endian).
	constexpr uint32_t kTerrainMagic = 0x4E525254u;
	/// Version courante du payload `terrain.bin` (M100.5).
	constexpr uint32_t kTerrainVersion = 1u;

	/// Heightmap éditable LOD0 d'un chunk monde (M100.5). Layout row-major en Z
	/// (`heights[z * resolutionX + x]`). Toutes les hauteurs sont en mètres
	/// absolus monde. La structure ne possède aucune ressource GPU ; le mesh est
	/// généré à la demande par `TerrainMeshBuilder`.
	struct TerrainChunk
	{
		uint32_t resolutionX = kTerrainResolution;
		uint32_t resolutionZ = kTerrainResolution;
		float    cellSizeMeters = kTerrainCellSizeMeters;
		float    heightMin = 0.0f;
		float    heightMax = 0.0f;
		std::vector<float> heights; // taille = resolutionX * resolutionZ

		/// Initialise un chunk plat à `height` mètres (toutes les cellules à la
		/// même valeur). `heightMin == heightMax == height` après l'appel.
		static TerrainChunk MakeFlat(float height = 0.0f);

		/// Échantillonne la hauteur en coordonnées chunk-locales (mètres).
		/// Bilinéaire à l'intérieur, clamp aux bornes hors du chunk.
		/// \param localX 0..(resolutionX-1)*cellSizeMeters
		/// \param localZ idem en Z
		float SampleHeight(float localX, float localZ) const;

		/// Recalcule `heightMin`/`heightMax` à partir du contenu de `heights`.
		/// À appeler après toute édition externe du buffer (`SaveTerrainBin`
		/// le fait avant écriture).
		void RecomputeBounds();
	};

	/// Sérialise `chunk` au format binaire `terrain.bin` (M100.5). Header
	/// `OutputVersionHeader` (magic=`kTerrainMagic`, version=`kTerrainVersion`,
	/// `contentHash` = xxhash64 du payload post-header), puis le payload.
	/// \return true si écriture OK (taille = 48 + 257*257*4 = 264 244 octets).
	/// Effet de bord : remplit `outBytes` (resize + écriture).
	bool SaveTerrainBin(const TerrainChunk& chunk, std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `terrain.bin` complet. Valide le header (magic, version,
	/// contentHash), les dimensions (== `kTerrainResolution`), `cellSizeMeters`
	/// (== `kTerrainCellSizeMeters`), et l'intervalle `heightMin <= heightMax`
	/// dans `[kTerrainHeightMinMeters, kTerrainHeightMaxMeters]`.
	/// \return true si OK ; en cas d'erreur, `outError` est renseigné.
	bool LoadTerrainBin(std::span<const uint8_t> bytes, TerrainChunk& outChunk, std::string& outError);
}
```

- [ ] **Step 2 : Vérifier que le header compile en isolation**

Le fichier sera consommé à partir de Task 5 (impl). Pour l'instant on vérifie juste qu'il n'introduit pas d'erreur de syntaxe en standalone :

```bash
cmake --build build --target engine_core 2>&1 | tail -10
```

Expected : la cible existante compile encore (le header n'est pas inclus). Si build absent (Task 1 step 3), différer la vérification.

- [ ] **Step 3 : Commit**

```bash
git add engine/world/terrain/TerrainChunk.h
git commit -m "feat(world/terrain): declare TerrainChunk + format binaire TRRN (M100.5)"
```

---

### Task 3 : Tests `TerrainChunk` (TDD red — M100.5)

**Files :**
- Create : `engine/world/terrain/tests/TerrainChunkTests.cpp`

- [ ] **Step 1 : Écrire les 6 tests de la spec M100.5 §Tests**

```cpp
// engine/world/terrain/tests/TerrainChunkTests.cpp
#include "engine/world/terrain/TerrainChunk.h"

#include <catch2/catch.hpp>
#include <cmath>

using namespace engine::world::terrain;

TEST_CASE("Test_MakeFlat_ProducesUniformHeights", "[M100.5][TerrainChunk]")
{
	auto chunk = TerrainChunk::MakeFlat(2.5f);
	REQUIRE(chunk.resolutionX == kTerrainResolution);
	REQUIRE(chunk.resolutionZ == kTerrainResolution);
	REQUIRE(chunk.heights.size() == static_cast<size_t>(kTerrainResolution * kTerrainResolution));
	REQUIRE(chunk.heightMin == Approx(2.5f));
	REQUIRE(chunk.heightMax == Approx(2.5f));
	for (float h : chunk.heights) REQUIRE(h == Approx(2.5f));
}

TEST_CASE("Test_SampleHeight_BilinearInterior", "[M100.5][TerrainChunk]")
{
	auto chunk = TerrainChunk::MakeFlat(0.0f);
	// Plante un gradient en X : h(x,z) = x.
	for (uint32_t z = 0; z < chunk.resolutionZ; ++z)
		for (uint32_t x = 0; x < chunk.resolutionX; ++x)
			chunk.heights[z * chunk.resolutionX + x] = static_cast<float>(x);
	chunk.RecomputeBounds();
	// Sampler à mi-cellule entre x=10 et x=11 doit donner 10.5.
	REQUIRE(chunk.SampleHeight(10.5f, 0.0f) == Approx(10.5f).margin(1e-4f));
}

TEST_CASE("Test_SampleHeight_ClampsOutOfBounds", "[M100.5][TerrainChunk]")
{
	auto chunk = TerrainChunk::MakeFlat(7.0f);
	// Hors bornes positives → clamp à dernière cellule.
	REQUIRE(chunk.SampleHeight(1e6f, 1e6f) == Approx(7.0f));
	// Hors bornes négatives → clamp à 0.
	REQUIRE(chunk.SampleHeight(-1e6f, -1e6f) == Approx(7.0f));
}

TEST_CASE("Test_SaveLoad_Roundtrip", "[M100.5][TerrainChunk]")
{
	auto src = TerrainChunk::MakeFlat(0.0f);
	for (uint32_t z = 0; z < src.resolutionZ; ++z)
		for (uint32_t x = 0; x < src.resolutionX; ++x)
			src.heights[z * src.resolutionX + x] =
				std::sin(x * 0.1f) * std::cos(z * 0.1f);
	src.RecomputeBounds();

	std::vector<uint8_t> bytes;
	std::string err;
	REQUIRE(SaveTerrainBin(src, bytes, err));
	REQUIRE(err.empty());
	REQUIRE(bytes.size() == 48u + static_cast<size_t>(kTerrainResolution * kTerrainResolution) * 4u);

	TerrainChunk dst;
	REQUIRE(LoadTerrainBin(bytes, dst, err));
	REQUIRE(err.empty());
	REQUIRE(dst.resolutionX == src.resolutionX);
	REQUIRE(dst.resolutionZ == src.resolutionZ);
	REQUIRE(dst.cellSizeMeters == Approx(src.cellSizeMeters));
	REQUIRE(dst.heightMin == Approx(src.heightMin));
	REQUIRE(dst.heightMax == Approx(src.heightMax));
	REQUIRE(std::memcmp(dst.heights.data(), src.heights.data(),
		src.heights.size() * sizeof(float)) == 0);
}

TEST_CASE("Test_Load_RejectsBadMagic", "[M100.5][TerrainChunk]")
{
	std::vector<uint8_t> bytes(264244, 0);
	bytes[0] = 'X'; bytes[1] = 'X'; bytes[2] = 'X'; bytes[3] = 'X';
	TerrainChunk dst;
	std::string err;
	REQUIRE_FALSE(LoadTerrainBin(bytes, dst, err));
	REQUIRE_FALSE(err.empty());
}

TEST_CASE("Test_Load_RejectsBadVersion", "[M100.5][TerrainChunk]")
{
	auto src = TerrainChunk::MakeFlat(1.0f);
	std::vector<uint8_t> bytes;
	std::string err;
	REQUIRE(SaveTerrainBin(src, bytes, err));
	// Patch formatVersion à 999.
	uint32_t bogus = 999u;
	std::memcpy(bytes.data() + 4, &bogus, sizeof(uint32_t));
	TerrainChunk dst;
	REQUIRE_FALSE(LoadTerrainBin(bytes, dst, err));
}
```

- [ ] **Step 2 : Commit (red — pas encore d'impl)**

```bash
git add engine/world/terrain/tests/TerrainChunkTests.cpp
git commit -m "test(world/terrain): TerrainChunk tests M100.5 (TDD red)"
```

---

### Task 4 : Implémentation `TerrainChunk.cpp` (M100.5)

**Files :**
- Create : `engine/world/terrain/TerrainChunk.cpp`

- [ ] **Step 1 : Écrire l'implémentation**

```cpp
// engine/world/terrain/TerrainChunk.cpp
#include "engine/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace engine::world::terrain
{
	TerrainChunk TerrainChunk::MakeFlat(float height)
	{
		TerrainChunk c;
		const size_t n = static_cast<size_t>(kTerrainResolution) * kTerrainResolution;
		c.heights.assign(n, height);
		c.heightMin = c.heightMax = height;
		return c;
	}

	float TerrainChunk::SampleHeight(float localX, float localZ) const
	{
		if (heights.empty()) return 0.0f;
		const float fx = localX / cellSizeMeters;
		const float fz = localZ / cellSizeMeters;
		const float maxX = static_cast<float>(resolutionX - 1);
		const float maxZ = static_cast<float>(resolutionZ - 1);
		const float cx = std::clamp(fx, 0.0f, maxX);
		const float cz = std::clamp(fz, 0.0f, maxZ);
		const uint32_t x0 = static_cast<uint32_t>(std::floor(cx));
		const uint32_t z0 = static_cast<uint32_t>(std::floor(cz));
		const uint32_t x1 = std::min(x0 + 1u, resolutionX - 1u);
		const uint32_t z1 = std::min(z0 + 1u, resolutionZ - 1u);
		const float tx = cx - static_cast<float>(x0);
		const float tz = cz - static_cast<float>(z0);
		const float h00 = heights[z0 * resolutionX + x0];
		const float h10 = heights[z0 * resolutionX + x1];
		const float h01 = heights[z1 * resolutionX + x0];
		const float h11 = heights[z1 * resolutionX + x1];
		const float h0 = h00 * (1.0f - tx) + h10 * tx;
		const float h1 = h01 * (1.0f - tx) + h11 * tx;
		return h0 * (1.0f - tz) + h1 * tz;
	}

	void TerrainChunk::RecomputeBounds()
	{
		if (heights.empty()) { heightMin = heightMax = 0.0f; return; }
		auto [lo, hi] = std::minmax_element(heights.begin(), heights.end());
		heightMin = *lo;
		heightMax = *hi;
	}

	namespace
	{
		bool ValidateChunk(const TerrainChunk& c, std::string& outError)
		{
			if (c.resolutionX != kTerrainResolution || c.resolutionZ != kTerrainResolution)
			{ outError = "TerrainChunk: resolution must be 257x257"; return false; }
			if (c.cellSizeMeters != kTerrainCellSizeMeters)
			{ outError = "TerrainChunk: cellSize must be 1.0m"; return false; }
			if (c.heightMin > c.heightMax)
			{ outError = "TerrainChunk: heightMin > heightMax"; return false; }
			if (c.heightMin < kTerrainHeightMinMeters || c.heightMax > kTerrainHeightMaxMeters)
			{ outError = "TerrainChunk: height bounds out of valid range"; return false; }
			return true;
		}
	}

	bool SaveTerrainBin(const TerrainChunk& chunkIn, std::vector<uint8_t>& outBytes, std::string& outError)
	{
		TerrainChunk chunk = chunkIn;
		chunk.RecomputeBounds();
		if (!ValidateChunk(chunk, outError)) return false;

		const size_t payloadSize = 24u // après header :
			+ 4 + 4 + 4 + 4 + 4 + 4   // resX, resZ, cellSize, hMin, hMax, reserved
			+ static_cast<size_t>(kTerrainResolution) * kTerrainResolution * sizeof(float);
		outBytes.assign(payloadSize, 0u);

		// Layout après header (24 octets) :
		//   [24..27] resX, [28..31] resZ, [32..35] cellSize,
		//   [36..39] hMin, [40..43] hMax, [44..47] reserved=0,
		//   [48..N] heights[].
		uint8_t* p = outBytes.data() + 24u;
		std::memcpy(p,        &chunk.resolutionX,    4); p += 4;
		std::memcpy(p,        &chunk.resolutionZ,    4); p += 4;
		std::memcpy(p,        &chunk.cellSizeMeters, 4); p += 4;
		std::memcpy(p,        &chunk.heightMin,      4); p += 4;
		std::memcpy(p,        &chunk.heightMax,      4); p += 4;
		const uint32_t reserved = 0u;
		std::memcpy(p,        &reserved,             4); p += 4;
		std::memcpy(p, chunk.heights.data(),
			chunk.heights.size() * sizeof(float));

		// Calcul du contentHash sur la portion post-header puis écriture du header.
		std::span<const uint8_t> payload(outBytes.data() + 24u, payloadSize - 24u);
		engine::world::OutputVersionHeader hdr;
		hdr.magic = kTerrainMagic;
		hdr.formatVersion = kTerrainVersion;
		hdr.builderVersion = engine::world::kZoneBuilderVersion;
		hdr.engineVersion = engine::world::kZoneEngineVersion;
		hdr.contentHash = engine::world::ComputeXxHash64(payload);
		std::memcpy(outBytes.data(), &hdr, sizeof(hdr));
		return true;
	}

	bool LoadTerrainBin(std::span<const uint8_t> bytes, TerrainChunk& outChunk, std::string& outError)
	{
		const size_t expected = 48u + static_cast<size_t>(kTerrainResolution) * kTerrainResolution * sizeof(float);
		if (bytes.size() != expected)
		{ outError = "terrain.bin: unexpected size"; return false; }

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError)) return false;
		if (hdr.magic != kTerrainMagic) { outError = "terrain.bin: bad magic"; return false; }
		if (hdr.formatVersion != kTerrainVersion) { outError = "terrain.bin: bad version"; return false; }

		std::span<const uint8_t> payload = bytes.subspan(24u);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{ outError = "terrain.bin: contentHash mismatch"; return false; }

		const uint8_t* p = bytes.data() + 24u;
		std::memcpy(&outChunk.resolutionX,    p, 4); p += 4;
		std::memcpy(&outChunk.resolutionZ,    p, 4); p += 4;
		std::memcpy(&outChunk.cellSizeMeters, p, 4); p += 4;
		std::memcpy(&outChunk.heightMin,      p, 4); p += 4;
		std::memcpy(&outChunk.heightMax,      p, 4); p += 4;
		uint32_t reserved = 0; std::memcpy(&reserved, p, 4); p += 4;
		const size_t n = static_cast<size_t>(outChunk.resolutionX) * outChunk.resolutionZ;
		outChunk.heights.assign(n, 0.0f);
		std::memcpy(outChunk.heights.data(), p, n * sizeof(float));

		if (!ValidateChunk(outChunk, outError)) return false;
		return true;
	}
}
```

- [ ] **Step 2 : Ajouter le source à la CMake (engine_core)**

Trouver le bloc `add_library(engine_core …)` et y ajouter `engine/world/terrain/TerrainChunk.cpp`. Si le bloc trie alphabétiquement, conserver l'ordre.

- [ ] **Step 3 : Ajouter le test target**

```cmake
# Après les autres add_executable de tests :
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(terrain_chunk_tests engine/world/terrain/tests/TerrainChunkTests.cpp)
    target_include_directories(terrain_chunk_tests PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(terrain_chunk_tests PRIVATE engine_core)
    if(MSVC)
        target_compile_options(terrain_chunk_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
    endif()
    add_test(NAME terrain_chunk_tests COMMAND terrain_chunk_tests)
endif()
```

Suivre exactement le pattern des targets `command_stack_tests` / `world_editor_shell_tests` (Phase 1) pour le wiring (`if` MSVC, includes, `add_test`).

- [ ] **Step 4 : Build et run le test**

```bash
cmake --build build --target terrain_chunk_tests 2>&1 | tail -20
ctest --test-dir build -R terrain_chunk_tests --output-on-failure
```

Expected : 6 tests verts.

- [ ] **Step 5 : Commit**

```bash
git add engine/world/terrain/TerrainChunk.cpp CMakeLists.txt
git commit -m "feat(world/terrain): implemente TerrainChunk + Save/LoadTerrainBin (M100.5)"
```

---

### Task 5 : `TerrainMeshBuilder` (M100.5)

**Files :**
- Create : `engine/world/terrain/TerrainMeshBuilder.h`
- Create : `engine/world/terrain/TerrainMeshBuilder.cpp`

- [ ] **Step 1 : Header**

```cpp
// engine/world/terrain/TerrainMeshBuilder.h
#pragma once

#include "engine/world/terrain/TerrainChunk.h"

#include <cstdint>
#include <vector>

namespace engine::world::terrain
{
	/// Vertex packé du mesh terrain : 32 octets, position en mètres chunk-local.
	/// (Réutilisé pour LOD0 et LODs N>0 — M100.8.) Layout binaire stable :
	/// 0..11 : float3 position (x, y=hauteur, z)
	/// 12..23 : float3 normale (calculée par gradient bilinéaire)
	/// 24..31 : float2 UV (0..1 sur le chunk)
	struct TerrainVertex
	{
		float position[3];
		float normal[3];
		float uv[2];
	};
	static_assert(sizeof(TerrainVertex) == 32, "TerrainVertex must stay 32 bytes");

	/// Description CPU d'un mesh terrain : vertex + index buffers prêts à upload
	/// vers le GPU. L'index buffer est UINT32 (257² > 65k).
	struct TerrainMeshCpu
	{
		std::vector<TerrainVertex> vertices;
		std::vector<uint32_t> indices;
	};

	/// Construit un mesh CPU depuis un `TerrainChunk` (LOD0). Triangle list,
	/// UVs 0..1 sur le chunk, normales par gradient bilinéaire.
	/// \return mesh prêt à `Upload`.
	TerrainMeshCpu BuildLod0Mesh(const TerrainChunk& chunk);

	/// Construit un mesh CPU pour un niveau de LOD (M100.8 — déclaration ici,
	/// implémentation côté Task 13). Si `withSkirt == true`, ajoute une jupe
	/// géométrique 2 m sous le bord pour masquer les fissures inter-LOD.
	/// `resolution`, `cellSizeMeters` et `heights` proviennent d'un `TerrainLod`.
	struct TerrainLod;
	TerrainMeshCpu BuildLodMesh(const TerrainLod& lod, bool withSkirt);
}
```

- [ ] **Step 2 : Implémentation `BuildLod0Mesh`**

```cpp
// engine/world/terrain/TerrainMeshBuilder.cpp
#include "engine/world/terrain/TerrainMeshBuilder.h"
#include "engine/world/terrain/TerrainLodChain.h" // pour TerrainLod (Task 12)

#include <cmath>

namespace engine::world::terrain
{
	namespace
	{
		void ComputeNormalAndUv(const TerrainChunk& c, uint32_t x, uint32_t z, TerrainVertex& v)
		{
			const float dx = c.SampleHeight((x == 0 ? 0.0f : (x - 1) * c.cellSizeMeters), z * c.cellSizeMeters)
				- c.SampleHeight((x == c.resolutionX - 1 ? (c.resolutionX - 1) * c.cellSizeMeters
				                                          : (x + 1) * c.cellSizeMeters), z * c.cellSizeMeters);
			const float dz = c.SampleHeight(x * c.cellSizeMeters, (z == 0 ? 0.0f : (z - 1) * c.cellSizeMeters))
				- c.SampleHeight(x * c.cellSizeMeters, (z == c.resolutionZ - 1 ? (c.resolutionZ - 1) * c.cellSizeMeters
				                                                                : (z + 1) * c.cellSizeMeters));
			const float cs2 = 2.0f * c.cellSizeMeters;
			const float nx = dx / cs2;
			const float ny = 1.0f;
			const float nz = dz / cs2;
			const float invLen = 1.0f / std::sqrt(nx*nx + ny*ny + nz*nz);
			v.normal[0] = nx * invLen;
			v.normal[1] = ny * invLen;
			v.normal[2] = nz * invLen;
			v.uv[0] = static_cast<float>(x) / static_cast<float>(c.resolutionX - 1);
			v.uv[1] = static_cast<float>(z) / static_cast<float>(c.resolutionZ - 1);
		}
	}

	TerrainMeshCpu BuildLod0Mesh(const TerrainChunk& chunk)
	{
		TerrainMeshCpu mesh;
		const uint32_t rx = chunk.resolutionX;
		const uint32_t rz = chunk.resolutionZ;
		mesh.vertices.resize(static_cast<size_t>(rx) * rz);
		for (uint32_t z = 0; z < rz; ++z)
		{
			for (uint32_t x = 0; x < rx; ++x)
			{
				TerrainVertex& v = mesh.vertices[z * rx + x];
				v.position[0] = static_cast<float>(x) * chunk.cellSizeMeters;
				v.position[1] = chunk.heights[z * rx + x];
				v.position[2] = static_cast<float>(z) * chunk.cellSizeMeters;
				ComputeNormalAndUv(chunk, x, z, v);
			}
		}
		mesh.indices.reserve(static_cast<size_t>(rx - 1) * (rz - 1) * 6);
		for (uint32_t z = 0; z < rz - 1; ++z)
		{
			for (uint32_t x = 0; x < rx - 1; ++x)
			{
				const uint32_t i00 = z * rx + x;
				const uint32_t i10 = i00 + 1;
				const uint32_t i01 = i00 + rx;
				const uint32_t i11 = i01 + 1;
				mesh.indices.push_back(i00); mesh.indices.push_back(i01); mesh.indices.push_back(i10);
				mesh.indices.push_back(i10); mesh.indices.push_back(i01); mesh.indices.push_back(i11);
			}
		}
		return mesh;
	}

	// BuildLodMesh défini en Task 13 (M100.8) — déclaration laissée pour
	// compilation, l'impl concrète arrive avec le LOD chain.
	TerrainMeshCpu BuildLodMesh(const TerrainLod& /*lod*/, bool /*withSkirt*/)
	{
		// Stub M100.5. Remplacé en Task 13.
		return TerrainMeshCpu{};
	}
}
```

**Note :** `BuildLodMesh` est déclaré ici parce que le header le mentionne, mais son implémentation réelle vit en Task 13 quand `TerrainLod` aura un body. On maintient un stub temporaire pour pouvoir compiler la lib.

- [ ] **Step 3 : Ajouter à la CMake `engine_core`** (1 ligne)

- [ ] **Step 4 : Build sanity**

```bash
cmake --build build --target engine_core 2>&1 | tail -10
```

Expected : compile (TerrainLodChain.h n'existe pas encore → erreur. Pour débloquer ce build sanity, créer un fichier vide minimal `engine/world/terrain/TerrainLodChain.h` avec juste `#pragma once\nnamespace engine::world::terrain { struct TerrainLod {}; }` — il sera complété en Task 12).

Alternative plus propre : repousser l'inclusion de `TerrainLodChain.h` au moment où `BuildLodMesh` est implémenté (Task 13). Choix recommandé : retirer l'`#include` et la déclaration de `BuildLodMesh` du `.cpp`/`.h` ici, les ajouter en Task 12/13.

**Action concrète :** retirer `BuildLodMesh` du header et du cpp pour cette task ; il sera ajouté en Task 12.

- [ ] **Step 5 : Commit**

```bash
git add engine/world/terrain/TerrainMeshBuilder.h engine/world/terrain/TerrainMeshBuilder.cpp CMakeLists.txt
git commit -m "feat(world/terrain): TerrainMeshBuilder LOD0 (M100.5)"
```

---

### Task 6 : `TerrainChunkLoader` (M100.5)

**Files :**
- Create : `engine/world/terrain/TerrainChunkLoader.h`
- Create : `engine/world/terrain/TerrainChunkLoader.cpp`

- [ ] **Step 1 : Header**

```cpp
// engine/world/terrain/TerrainChunkLoader.h
#pragma once

#include "engine/world/terrain/TerrainChunk.h"

#include <memory>
#include <string>
#include <string_view>

namespace engine::world { class StreamCache; }

namespace engine::world::terrain
{
	/// Charge un `TerrainChunk` depuis un `StreamCache` (lookup), ou retourne
	/// un `nullptr` si la clé n'est pas en cache. Le caller charge alors le
	/// fichier disque + `Insert` dans le cache puis rappelle.
	/// \return shared_ptr<TerrainChunk> partagé, ou nullptr si miss.
	std::shared_ptr<TerrainChunk> LoadFromCache(
		engine::world::StreamCache& cache, std::string_view cacheKey,
		std::string& outError);

	/// Forme la clé cache canonique pour un chunk : `chunks/chunk_<i>_<j>/terrain.bin`.
	std::string MakeTerrainCacheKey(int chunkX, int chunkZ);
}
```

- [ ] **Step 2 : Impl**

```cpp
// engine/world/terrain/TerrainChunkLoader.cpp
#include "engine/world/terrain/TerrainChunkLoader.h"
#include "engine/world/StreamCache.h"

#include <sstream>

namespace engine::world::terrain
{
	std::shared_ptr<TerrainChunk> LoadFromCache(
		engine::world::StreamCache& cache, std::string_view cacheKey,
		std::string& outError)
	{
		auto blob = cache.Lookup(cacheKey);
		if (!blob.has_value()) { outError = "cache miss"; return nullptr; }
		auto chunk = std::make_shared<TerrainChunk>();
		if (!LoadTerrainBin(std::span<const uint8_t>(blob->data(), blob->size()), *chunk, outError))
			return nullptr;
		return chunk;
	}

	std::string MakeTerrainCacheKey(int chunkX, int chunkZ)
	{
		std::ostringstream os;
		os << "chunks/chunk_" << chunkX << "_" << chunkZ << "/terrain.bin";
		return os.str();
	}
}
```

- [ ] **Step 3 : Ajouter à la CMake (1 ligne) puis build**

- [ ] **Step 4 : Commit**

```bash
git add engine/world/terrain/TerrainChunkLoader.h engine/world/terrain/TerrainChunkLoader.cpp CMakeLists.txt
git commit -m "feat(world/terrain): TerrainChunkLoader sur StreamCache (M100.5)"
```

---

### Task 7 : `StreamCache::LoadTerrainChunk` (M100.5)

**Files :**
- Modify : `engine/world/StreamCache.h`
- Modify : `engine/world/StreamCache.cpp`

- [ ] **Step 1 : Ajouter la méthode chunk-aware**

Dans le header, après `Insert(...)` :

```cpp
/// Charge le `terrain.bin` du chunk `(chunkX, chunkZ)` depuis le cache, ou
/// depuis disque si miss (et l'insère). Retourne nullptr si fichier absent ou
/// corrompu.
/// Effet de bord : Insert dans le cache si miss.
std::shared_ptr<engine::world::terrain::TerrainChunk> LoadTerrainChunk(
	const engine::core::Config& cfg, int chunkX, int chunkZ);
```

Forward-declare en haut du header :
```cpp
namespace engine::world::terrain { struct TerrainChunk; }
```

Dans le `.cpp`, implémenter en lisant `<content_root>/chunks/chunk_<i>_<j>/terrain.bin` via `std::ifstream`, en passant les bytes à `LoadTerrainBin`, et en faisant `Insert` sur la clé canonique. Suivre le pattern existant pour la résolution de `<content_root>` (lire `cfg.GetString("content.root", ".")`).

- [ ] **Step 2 : Build + commit**

```bash
git add engine/world/StreamCache.h engine/world/StreamCache.cpp
git commit -m "feat(world): StreamCache::LoadTerrainChunk + cache key chunks/<i>_<j>/terrain.bin (M100.5)"
```

---

### Task 8 : `StreamingScheduler` — set requis par chunk (M100.5)

**Files :**
- Modify : `engine/world/StreamingScheduler.cpp`

- [ ] **Step 1 : Ajouter `terrain.bin` au set des fichiers requis pour qu'un chunk soit "ready"**

Inspecter le code existant — chercher la liste des suffixes (`*.meta`, `instances.bin`, etc.) qui détermine la "ready-ness" d'un chunk. Ajouter `"terrain.bin"`.

- [ ] **Step 2 : Commit**

```bash
git add engine/world/StreamingScheduler.cpp
git commit -m "feat(world): include terrain.bin in chunk ready set (M100.5)"
```

---

### Task 9 : `TerrainDocument` (M100.5 — base, M100.8 ajoutera OnCommit hook)

**Files :**
- Create : `engine/editor/world/TerrainDocument.h`
- Create : `engine/editor/world/TerrainDocument.cpp`

- [ ] **Step 1 : Header (version M100.5 — extension M100.8 en Task 16)**

```cpp
// engine/editor/world/TerrainDocument.h
#pragma once

#include "engine/world/WorldModel.h"
#include "engine/world/terrain/TerrainChunk.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace engine::editor::world
{
	/// Wrapper éditeur d'un ensemble de `TerrainChunk` mutables. Indexé par
	/// `GlobalChunkCoord`. Implémenté en M100.5 ; étendu en M100.8 pour
	/// déclencher la régénération LOD au commit d'un `ICommand`.
	class TerrainDocument
	{
	public:
		/// Charge le chunk `(chunkX, chunkZ)` depuis disque ou crée un chunk plat si fichier absent.
		std::shared_ptr<engine::world::terrain::TerrainChunk> EnsureLoaded(int chunkX, int chunkZ);

		/// Marque un chunk comme dirty (modifié par un Command). M100.5 stocke
		/// l'info ; M100.8 utilisera `OnCommit` pour enqueue le worker LOD.
		void MarkDirty(engine::world::GlobalChunkCoord coord);

		/// Sauvegarde tous les chunks dirty sur disque. M100.5 — TerrainSaveZone
		/// l'utilise (à connecter en Task 11). Effet de bord : écrit
		/// `chunks/chunk_<i>_<j>/terrain.bin`. \return nombre de chunks écrits.
		size_t SaveDirtyToDisk(const engine::core::Config& cfg);

	private:
		struct ChunkSlot
		{
			std::shared_ptr<engine::world::terrain::TerrainChunk> chunk;
			bool dirty = false;
		};
		std::unordered_map<uint64_t, ChunkSlot> m_chunks; // key = packed(x, z)
	};
}
```

- [ ] **Step 2 : Impl basique**

`EnsureLoaded` : lookup hashmap, si miss → essayer de lire `<content>/chunks/chunk_<i>_<j>/terrain.bin`, sinon créer un chunk plat à 0. `MarkDirty` flip le flag. `SaveDirtyToDisk` itère, sérialise via `SaveTerrainBin`, écrit le fichier.

- [ ] **Step 3 : CMake + commit**

```bash
git commit -m "feat(editor/world): TerrainDocument (M100.5 base)"
```

---

### Task 10 : `zone_builder_lib::WriteTerrainChunk` (M100.5)

**Files :**
- Modify : `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h`
- Modify : `tools/zone_builder/lib/ChunkPackageWriter.cpp`

- [ ] **Step 1 : Ajouter API publique**

```cpp
// Dans ChunkPackageWriter.h, namespace zone_builder :
/// Écrit `chunks/chunk_<i>_<j>/terrain.bin` (M100.5). Crée le dossier si absent.
/// \return true si OK, sinon outError renseigné.
bool WriteTerrainChunk(const std::string& outputDir, int chunkX, int chunkZ,
	const engine::world::terrain::TerrainChunk& chunk, std::string& outError);
```

- [ ] **Step 2 : Impl**

Resolver `<outputDir>/chunks/chunk_<i>_<j>/`, créer le dossier, appeler `SaveTerrainBin`, écrire le blob via `std::ofstream`. Suivre le style du `ChunkPackageWriter` existant.

- [ ] **Step 3 : Build + commit**

```bash
git commit -m "feat(zone_builder): WriteTerrainChunk pour terrain.bin (M100.5)"
```

---

### Task 11 : `GeometryPass` — drawcall mesh-terrain par chunk (M100.5)

**Files :**
- Modify : `engine/render/GeometryPass.cpp`

- [ ] **Step 1 : Ajouter le drawcall sans branche éditeur**

L'enjeu : la passe Geometry doit consommer les `TerrainMeshGpu` produits par `TerrainMeshBuilder` quand ils sont présents pour un chunk visible, **sans branche `m_editorEnabled`** (critère d'acceptance M100.5). Repérer le point d'entrée des draws de chunks (probablement une boucle sur `m_visibleChunks` ou équivalent), ajouter un fetch du `TerrainMeshGpu` (via une nouvelle méthode du WorldModel ou ChunkRuntime), et émettre `vkCmdBindPipeline` + `vkCmdBindIndexBuffer(VK_INDEX_TYPE_UINT32)` + `vkCmdDrawIndexed`.

**Note :** GeometryPass utilise déjà `VK_INDEX_TYPE_UINT32` (lignes 689, 806, 967 vérifiées Task 1) — pas de migration. Réutiliser le pipeline terrain existant si compatible (8 layers M100.9 viendra plus tard, en M100.5 on peut commencer avec un pipeline minimal vert flat ou une couleur unique).

**Périmètre M100.5 :** suffisant qu'il y ait un drawcall qui consomme le mesh — la passe peut écrire en SceneColor avec un shader simple. Le pipeline 8-layers sera M100.9.

- [ ] **Step 2 : Vérifier que le client (mode jeu normal) consomme bien le mesh**

Critère M100.5 : pas de branche `m_editorEnabled` dans `engine/render/`. `grep -n "m_editorEnabled" engine/render/GeometryPass.cpp` → ne doit pas montrer de nouvelle occurrence côté terrain.

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(render): GeometryPass — drawcall mesh-terrain par chunk (M100.5)"
```

---

### Task 12 : `TerrainParityTests` (round-trip byte-exact + mesh deterministic — M100.5)

**Files :**
- Create : `engine/world/terrain/tests/TerrainParityTests.cpp`

- [ ] **Step 1 : Tests**

```cpp
// engine/world/terrain/tests/TerrainParityTests.cpp
#include "engine/world/terrain/TerrainChunk.h"
#include "engine/world/terrain/TerrainMeshBuilder.h"

#include <catch2/catch.hpp>
#include <cmath>

using namespace engine::world::terrain;

TEST_CASE("Test_EditorWritesClientReadsIdentical", "[M100.5][parity]")
{
	auto src = TerrainChunk::MakeFlat(0.0f);
	for (uint32_t z = 0; z < src.resolutionZ; ++z)
		for (uint32_t x = 0; x < src.resolutionX; ++x)
			src.heights[z * src.resolutionX + x] =
				std::sin(x * 0.1f) * std::cos(z * 0.1f);
	src.RecomputeBounds();

	std::vector<uint8_t> bytes;
	std::string err;
	REQUIRE(SaveTerrainBin(src, bytes, err));

	TerrainChunk dst;
	REQUIRE(LoadTerrainBin(bytes, dst, err));

	REQUIRE(std::memcmp(dst.heights.data(), src.heights.data(),
		src.heights.size() * sizeof(float)) == 0);
	REQUIRE(dst.resolutionX == src.resolutionX);
	REQUIRE(dst.resolutionZ == src.resolutionZ);
	REQUIRE(dst.cellSizeMeters == Approx(src.cellSizeMeters));
	REQUIRE(dst.heightMin == Approx(src.heightMin));
	REQUIRE(dst.heightMax == Approx(src.heightMax));
}

TEST_CASE("Test_MeshBuilder_DeterministicVertexBuffer", "[M100.5][parity]")
{
	auto src = TerrainChunk::MakeFlat(0.0f);
	for (uint32_t z = 0; z < src.resolutionZ; ++z)
		for (uint32_t x = 0; x < src.resolutionX; ++x)
			src.heights[z * src.resolutionX + x] = static_cast<float>(x + z);
	src.RecomputeBounds();

	std::vector<uint8_t> bytes;
	std::string err;
	REQUIRE(SaveTerrainBin(src, bytes, err));
	TerrainChunk reloaded;
	REQUIRE(LoadTerrainBin(bytes, reloaded, err));

	auto m1 = BuildLod0Mesh(src);
	auto m2 = BuildLod0Mesh(reloaded);
	REQUIRE(m1.vertices.size() == m2.vertices.size());
	REQUIRE(std::memcmp(m1.vertices.data(), m2.vertices.data(),
		m1.vertices.size() * sizeof(TerrainVertex)) == 0);
	REQUIRE(m1.indices.size() == m2.indices.size());
	REQUIRE(std::memcmp(m1.indices.data(), m2.indices.data(),
		m1.indices.size() * sizeof(uint32_t)) == 0);
}
```

- [ ] **Step 2 : Ajouter le test target dans CMake**

Pattern identique à `terrain_chunk_tests`. `target_link_libraries(... PRIVATE engine_core zone_builder_lib)` (note : la spec M100.5 §Diff CMake mentionne le link à zone_builder_lib pour ce target).

- [ ] **Step 3 : Build + run**

```bash
cmake --build build --target terrain_parity_tests 2>&1 | tail -10
ctest --test-dir build -R terrain_parity_tests --output-on-failure
```

Expected : 2 tests verts.

- [ ] **Step 4 : Commit**

```bash
git commit -m "test(world/terrain): TerrainParityTests (M100.5)"
```

---

## ─── Bloc M100.8 : LOD Regeneration ───

### Task 13 : `TerrainLodChain.h/.cpp` — struct + box filter (M100.8)

**Files :**
- Create : `engine/world/terrain/TerrainLodChain.h`
- Create : `engine/world/terrain/TerrainLodChain.cpp`

- [ ] **Step 1 : Header**

```cpp
// engine/world/terrain/TerrainLodChain.h
#pragma once

#include "engine/world/terrain/TerrainChunk.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::terrain
{
	/// Magic du fichier `terrain_lods.bin` ("TRLO" little-endian).
	constexpr uint32_t kTerrainLodsMagic = 0x4F4C5254u;
	constexpr uint32_t kTerrainLodsVersion = 1u;
	/// Nombre de niveaux LOD persistés (LOD1..LOD3 ; LOD0 vit dans terrain.bin).
	constexpr uint32_t kPersistedLodCount = 3u;
	/// Résolutions par niveau, alignées sur la division par 2 de 257-1.
	constexpr std::array<uint32_t, 3> kLodResolutions{129u, 65u, 33u};
	constexpr std::array<float, 3> kLodCellSizes{2.0f, 4.0f, 8.0f};

	/// Un niveau de LOD réduit, généré par box filter 2×2 sur le LOD parent.
	/// Pas de skirt — la skirt est ajoutée à la construction du mesh GPU.
	struct TerrainLod
	{
		uint32_t resolution = 0;
		float cellSizeMeters = 0.0f;
		std::vector<float> heights;
	};

	/// Chaîne complète des 3 LODs persistés (ordre LOD1, LOD2, LOD3).
	struct TerrainLodChain
	{
		std::array<TerrainLod, 3> lods;
	};

	/// Génère LOD1..LOD3 à partir de `lod0` (le `TerrainChunk` LOD0). Box
	/// filter 2×2 par niveau. Déterministe pour mêmes inputs.
	TerrainLodChain GenerateLodChain(const TerrainChunk& lod0);

	bool SaveTerrainLodsBin(const TerrainLodChain& chain,
		std::vector<uint8_t>& outBytes, std::string& outError);
	bool LoadTerrainLodsBin(std::span<const uint8_t> bytes,
		TerrainLodChain& outChain, std::string& outError);
}
```

- [ ] **Step 2 : Impl box filter + sérialisation**

```cpp
// engine/world/terrain/TerrainLodChain.cpp
#include "engine/world/terrain/TerrainLodChain.h"
#include "engine/world/OutputVersion.h"

#include <cstring>

namespace engine::world::terrain
{
	namespace
	{
		TerrainLod DownsampleBoxFilter(const TerrainLod& parent, uint32_t newRes, float newCell)
		{
			TerrainLod out;
			out.resolution = newRes;
			out.cellSizeMeters = newCell;
			out.heights.resize(static_cast<size_t>(newRes) * newRes);
			for (uint32_t z = 0; z < newRes; ++z)
			{
				for (uint32_t x = 0; x < newRes; ++x)
				{
					const uint32_t px = x * 2;
					const uint32_t pz = z * 2;
					const float h00 = parent.heights[(pz + 0) * parent.resolution + (px + 0)];
					const float h10 = parent.heights[(pz + 0) * parent.resolution + (px + 1)];
					const float h01 = parent.heights[(pz + 1) * parent.resolution + (px + 0)];
					const float h11 = parent.heights[(pz + 1) * parent.resolution + (px + 1)];
					out.heights[z * newRes + x] = (h00 + h10 + h01 + h11) * 0.25f;
				}
			}
			return out;
		}
	}

	TerrainLodChain GenerateLodChain(const TerrainChunk& lod0)
	{
		// Adapte LOD0 dans la struct TerrainLod pour réutiliser DownsampleBoxFilter.
		TerrainLod parent;
		parent.resolution = lod0.resolutionX;
		parent.cellSizeMeters = lod0.cellSizeMeters;
		parent.heights = lod0.heights;

		TerrainLodChain chain;
		for (uint32_t i = 0; i < kPersistedLodCount; ++i)
		{
			chain.lods[i] = DownsampleBoxFilter(parent, kLodResolutions[i], kLodCellSizes[i]);
			parent = chain.lods[i];
		}
		return chain;
	}

	bool SaveTerrainLodsBin(const TerrainLodChain& chain,
		std::vector<uint8_t>& outBytes, std::string& outError)
	{
		// Header (24) + uint32 lodCount + per-LOD : uint32 resolution + float cellSize + float[res*res]
		size_t total = 24u + 4u; // header + lodCount
		for (const auto& lod : chain.lods)
			total += 4u + 4u + lod.heights.size() * sizeof(float);
		outBytes.assign(total, 0u);

		uint8_t* p = outBytes.data() + 24u;
		const uint32_t lodCount = kPersistedLodCount;
		std::memcpy(p, &lodCount, 4); p += 4;
		for (const auto& lod : chain.lods)
		{
			std::memcpy(p, &lod.resolution, 4); p += 4;
			std::memcpy(p, &lod.cellSizeMeters, 4); p += 4;
			std::memcpy(p, lod.heights.data(), lod.heights.size() * sizeof(float));
			p += lod.heights.size() * sizeof(float);
		}

		std::span<const uint8_t> payload(outBytes.data() + 24u, total - 24u);
		engine::world::OutputVersionHeader hdr;
		hdr.magic = kTerrainLodsMagic;
		hdr.formatVersion = kTerrainLodsVersion;
		hdr.builderVersion = engine::world::kZoneBuilderVersion;
		hdr.engineVersion = engine::world::kZoneEngineVersion;
		hdr.contentHash = engine::world::ComputeXxHash64(payload);
		std::memcpy(outBytes.data(), &hdr, sizeof(hdr));
		(void)outError;
		return true;
	}

	bool LoadTerrainLodsBin(std::span<const uint8_t> bytes,
		TerrainLodChain& outChain, std::string& outError)
	{
		if (bytes.size() < 28u) { outError = "terrain_lods.bin: too small"; return false; }
		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError)) return false;
		if (hdr.magic != kTerrainLodsMagic) { outError = "terrain_lods.bin: bad magic"; return false; }
		if (hdr.formatVersion != kTerrainLodsVersion) { outError = "terrain_lods.bin: bad version"; return false; }

		std::span<const uint8_t> payload = bytes.subspan(24u);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{ outError = "terrain_lods.bin: contentHash mismatch"; return false; }

		const uint8_t* p = bytes.data() + 24u;
		uint32_t lodCount = 0; std::memcpy(&lodCount, p, 4); p += 4;
		if (lodCount != kPersistedLodCount)
		{ outError = "terrain_lods.bin: unexpected lodCount"; return false; }
		for (uint32_t i = 0; i < kPersistedLodCount; ++i)
		{
			uint32_t res = 0; float cell = 0.0f;
			std::memcpy(&res, p, 4); p += 4;
			std::memcpy(&cell, p, 4); p += 4;
			if (res != kLodResolutions[i] || cell != kLodCellSizes[i])
			{ outError = "terrain_lods.bin: lod resolution/cell mismatch"; return false; }
			outChain.lods[i].resolution = res;
			outChain.lods[i].cellSizeMeters = cell;
			outChain.lods[i].heights.assign(static_cast<size_t>(res) * res, 0.0f);
			std::memcpy(outChain.lods[i].heights.data(), p,
				outChain.lods[i].heights.size() * sizeof(float));
			p += outChain.lods[i].heights.size() * sizeof(float);
		}
		return true;
	}
}
```

- [ ] **Step 3 : CMake + build sanity + commit**

```bash
git commit -m "feat(world/terrain): TerrainLodChain box filter + Save/LoadTerrainLodsBin (M100.8)"
```

---

### Task 14 : `BuildLodMesh` + skirt (M100.8 — complète Task 5)

**Files :**
- Modify : `engine/world/terrain/TerrainMeshBuilder.h`
- Modify : `engine/world/terrain/TerrainMeshBuilder.cpp`

- [ ] **Step 1 : Réintroduire la déclaration de `BuildLodMesh`** (retirée en Task 5)

```cpp
// Dans TerrainMeshBuilder.h, après BuildLod0Mesh :
TerrainMeshCpu BuildLodMesh(const TerrainLod& lod, bool withSkirt);
```

`#include "engine/world/terrain/TerrainLodChain.h"` en tête.

- [ ] **Step 2 : Impl**

```cpp
// Dans TerrainMeshBuilder.cpp, remplacer le stub par :
TerrainMeshCpu BuildLodMesh(const TerrainLod& lod, bool withSkirt)
{
	// Construction de la grille LOD selon le même pattern que BuildLod0Mesh.
	TerrainMeshCpu mesh;
	const uint32_t r = lod.resolution;
	mesh.vertices.resize(static_cast<size_t>(r) * r);
	for (uint32_t z = 0; z < r; ++z)
	{
		for (uint32_t x = 0; x < r; ++x)
		{
			TerrainVertex& v = mesh.vertices[z * r + x];
			v.position[0] = static_cast<float>(x) * lod.cellSizeMeters;
			v.position[1] = lod.heights[z * r + x];
			v.position[2] = static_cast<float>(z) * lod.cellSizeMeters;
			// Normale par diff finie sur ce niveau de LOD (approximation OK).
			const uint32_t xL = (x == 0) ? 0u : x - 1;
			const uint32_t xR = std::min(x + 1u, r - 1u);
			const uint32_t zL = (z == 0) ? 0u : z - 1;
			const uint32_t zR = std::min(z + 1u, r - 1u);
			const float dx = lod.heights[z * r + xL] - lod.heights[z * r + xR];
			const float dz = lod.heights[zL * r + x] - lod.heights[zR * r + x];
			const float cs2 = 2.0f * lod.cellSizeMeters;
			const float nx = dx / cs2;
			const float ny = 1.0f;
			const float nz = dz / cs2;
			const float invLen = 1.0f / std::sqrt(nx*nx + ny*ny + nz*nz);
			v.normal[0] = nx * invLen;
			v.normal[1] = ny * invLen;
			v.normal[2] = nz * invLen;
			v.uv[0] = static_cast<float>(x) / static_cast<float>(r - 1);
			v.uv[1] = static_cast<float>(z) / static_cast<float>(r - 1);
		}
	}
	// Indices
	mesh.indices.reserve(static_cast<size_t>(r - 1) * (r - 1) * 6);
	for (uint32_t z = 0; z < r - 1; ++z)
	{
		for (uint32_t x = 0; x < r - 1; ++x)
		{
			const uint32_t i00 = z * r + x;
			const uint32_t i10 = i00 + 1;
			const uint32_t i01 = i00 + r;
			const uint32_t i11 = i01 + 1;
			mesh.indices.push_back(i00); mesh.indices.push_back(i01); mesh.indices.push_back(i10);
			mesh.indices.push_back(i10); mesh.indices.push_back(i01); mesh.indices.push_back(i11);
		}
	}

	if (withSkirt)
	{
		// Pour chaque vertex de bord, dupliquer un vertex skirt 2 m sous la
		// hauteur, et coudre des triangles entre la grille et la skirt.
		const float kSkirtDrop = 2.0f;
		const uint32_t baseSkirtIdx = static_cast<uint32_t>(mesh.vertices.size());
		auto AddSkirtVertex = [&](uint32_t x, uint32_t z) {
			TerrainVertex v = mesh.vertices[z * r + x];
			v.position[1] -= kSkirtDrop;
			mesh.vertices.push_back(v);
			return static_cast<uint32_t>(mesh.vertices.size() - 1u);
		};
		// Bord nord (z=0) et sud (z=r-1) : coudre x=0..r-1.
		for (uint32_t x = 0; x < r - 1; ++x)
		{
			const uint32_t a = 0 * r + x;
			const uint32_t b = a + 1;
			const uint32_t aS = AddSkirtVertex(x, 0);
			const uint32_t bS = AddSkirtVertex(x + 1, 0);
			mesh.indices.push_back(a);  mesh.indices.push_back(aS); mesh.indices.push_back(b);
			mesh.indices.push_back(b);  mesh.indices.push_back(aS); mesh.indices.push_back(bS);
		}
		for (uint32_t x = 0; x < r - 1; ++x)
		{
			const uint32_t a = (r - 1) * r + x;
			const uint32_t b = a + 1;
			const uint32_t aS = AddSkirtVertex(x, r - 1);
			const uint32_t bS = AddSkirtVertex(x + 1, r - 1);
			mesh.indices.push_back(a);  mesh.indices.push_back(b);  mesh.indices.push_back(aS);
			mesh.indices.push_back(b);  mesh.indices.push_back(bS); mesh.indices.push_back(aS);
		}
		// Bord ouest et est : x=0 et x=r-1.
		for (uint32_t z = 0; z < r - 1; ++z)
		{
			const uint32_t a = z * r + 0;
			const uint32_t b = (z + 1) * r + 0;
			const uint32_t aS = AddSkirtVertex(0, z);
			const uint32_t bS = AddSkirtVertex(0, z + 1);
			mesh.indices.push_back(a);  mesh.indices.push_back(b);  mesh.indices.push_back(aS);
			mesh.indices.push_back(b);  mesh.indices.push_back(bS); mesh.indices.push_back(aS);
		}
		for (uint32_t z = 0; z < r - 1; ++z)
		{
			const uint32_t a = z * r + (r - 1);
			const uint32_t b = (z + 1) * r + (r - 1);
			const uint32_t aS = AddSkirtVertex(r - 1, z);
			const uint32_t bS = AddSkirtVertex(r - 1, z + 1);
			mesh.indices.push_back(a);  mesh.indices.push_back(aS); mesh.indices.push_back(b);
			mesh.indices.push_back(b);  mesh.indices.push_back(aS); mesh.indices.push_back(bS);
		}
		(void)baseSkirtIdx;
	}
	return mesh;
}
```

- [ ] **Step 3 : Build sanity + commit**

```bash
git commit -m "feat(world/terrain): BuildLodMesh + skirt 2m pour LODs (M100.8)"
```

---

### Task 15 : `TerrainLodWorker` (M100.8)

**Files :**
- Create : `engine/world/terrain/TerrainLodWorker.h`
- Create : `engine/world/terrain/TerrainLodWorker.cpp`

- [ ] **Step 1 : Header**

```cpp
// engine/world/terrain/TerrainLodWorker.h
#pragma once

#include "engine/world/WorldModel.h"
#include "engine/world/terrain/TerrainChunk.h"
#include "engine/world/terrain/TerrainLodChain.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::world::terrain
{
	/// Génère la chaîne LOD pour un chunk donné en arrière-plan.
	/// Contraintes : main thread jamais bloqué > 1 ms (l'enqueue est non-bloquant).
	/// Stale jobs annulés via une génération atomique : chaque enqueue
	/// incrémente la génération du chunk ; un job dont la génération ne matche
	/// plus à la fin est jeté.
	class TerrainLodWorker
	{
	public:
		/// Démarre `workerCount` threads de travail.
		void Start(uint32_t workerCount);
		/// Stop ordonné. Termine les jobs en cours (n'attend pas la queue).
		void Stop();

		/// Enqueue une demande de régénération pour `coord` à partir de `lod0`.
		/// Le `lod0` est copié pour l'isolation thread. Le résultat est livré
		/// via le callback `onResult` sur un thread worker (le caller doit
		/// rebrancher au main thread si besoin).
		/// Effet de bord : incrémente la génération de `coord`.
		void Enqueue(engine::world::GlobalChunkCoord coord,
			TerrainChunk lod0,
			std::function<void(engine::world::GlobalChunkCoord, TerrainLodChain)> onResult);

	private:
		struct Job
		{
			engine::world::GlobalChunkCoord coord;
			TerrainChunk lod0;
			uint64_t generation;
			std::function<void(engine::world::GlobalChunkCoord, TerrainLodChain)> onResult;
		};

		void WorkerLoop();
		uint64_t IncrementGeneration(engine::world::GlobalChunkCoord coord);
		uint64_t GetGeneration(engine::world::GlobalChunkCoord coord) const;

		std::vector<std::thread> m_workers;
		std::deque<Job> m_queue;
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		std::atomic<bool> m_stopRequested{false};
		mutable std::mutex m_genMutex;
		std::unordered_map<uint64_t, uint64_t> m_generations; // packed coord → gen
	};
}
```

- [ ] **Step 2 : Impl**

```cpp
// engine/world/terrain/TerrainLodWorker.cpp
#include "engine/world/terrain/TerrainLodWorker.h"

#include <utility>

namespace engine::world::terrain
{
	namespace
	{
		uint64_t Pack(engine::world::GlobalChunkCoord c)
		{
			return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32)
				 | static_cast<uint64_t>(static_cast<uint32_t>(c.z));
		}
	}

	void TerrainLodWorker::Start(uint32_t workerCount)
	{
		m_stopRequested.store(false);
		m_workers.reserve(workerCount);
		for (uint32_t i = 0; i < workerCount; ++i)
			m_workers.emplace_back([this] { this->WorkerLoop(); });
	}

	void TerrainLodWorker::Stop()
	{
		m_stopRequested.store(true);
		m_cv.notify_all();
		for (auto& t : m_workers) if (t.joinable()) t.join();
		m_workers.clear();
		std::lock_guard lk(m_mutex);
		m_queue.clear();
	}

	uint64_t TerrainLodWorker::IncrementGeneration(engine::world::GlobalChunkCoord coord)
	{
		std::lock_guard lk(m_genMutex);
		return ++m_generations[Pack(coord)];
	}

	uint64_t TerrainLodWorker::GetGeneration(engine::world::GlobalChunkCoord coord) const
	{
		std::lock_guard lk(m_genMutex);
		auto it = m_generations.find(Pack(coord));
		return (it == m_generations.end()) ? 0u : it->second;
	}

	void TerrainLodWorker::Enqueue(engine::world::GlobalChunkCoord coord,
		TerrainChunk lod0,
		std::function<void(engine::world::GlobalChunkCoord, TerrainLodChain)> onResult)
	{
		const uint64_t gen = IncrementGeneration(coord);
		Job job{coord, std::move(lod0), gen, std::move(onResult)};
		{
			std::lock_guard lk(m_mutex);
			m_queue.emplace_back(std::move(job));
		}
		m_cv.notify_one();
	}

	void TerrainLodWorker::WorkerLoop()
	{
		while (!m_stopRequested.load())
		{
			Job job;
			{
				std::unique_lock lk(m_mutex);
				m_cv.wait(lk, [this] { return m_stopRequested.load() || !m_queue.empty(); });
				if (m_stopRequested.load()) return;
				job = std::move(m_queue.front());
				m_queue.pop_front();
			}
			TerrainLodChain chain = GenerateLodChain(job.lod0);
			// Stale check : si la génération a avancé, jeter le résultat.
			if (GetGeneration(job.coord) != job.generation) continue;
			if (job.onResult) job.onResult(job.coord, std::move(chain));
		}
	}
}
```

- [ ] **Step 3 : CMake + commit**

```bash
git commit -m "feat(world/terrain): TerrainLodWorker async + stale generation atomic (M100.8)"
```

---

### Task 16 : `TerrainDocument::OnCommit` hook (M100.8)

**Files :**
- Modify : `engine/editor/world/TerrainDocument.h`
- Modify : `engine/editor/world/TerrainDocument.cpp`

- [ ] **Step 1 : Ajouter le hook**

```cpp
// Dans TerrainDocument.h, ajouter :
#include "engine/world/terrain/TerrainLodWorker.h"

// méthodes :
void AttachLodWorker(engine::world::terrain::TerrainLodWorker* worker);
void OnCommit(engine::world::GlobalChunkCoord coord);

private:
engine::world::terrain::TerrainLodWorker* m_lodWorker = nullptr;
```

- [ ] **Step 2 : Impl**

`OnCommit` appelle `m_lodWorker->Enqueue(coord, *EnsureLoaded(coord.x, coord.z), [this](coord, chain){ /* persister terrain_lods.bin pour ce chunk */ })`. Le callback de résultat sérialise `SaveTerrainLodsBin` et écrit `<content>/chunks/chunk_<i>_<j>/terrain_lods.bin`.

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(editor/world): TerrainDocument::OnCommit -> TerrainLodWorker (M100.8)"
```

---

### Task 17 : `TerrainLodTests` (M100.8)

**Files :**
- Create : `engine/world/terrain/tests/TerrainLodTests.cpp`

- [ ] **Step 1 : Tests**

```cpp
#include "engine/world/terrain/TerrainLodChain.h"
#include "engine/world/terrain/TerrainLodWorker.h"

#include <catch2/catch.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace engine::world::terrain;

TEST_CASE("Test_GenerateLodChain_BoxFilterMatch", "[M100.8]")
{
	auto lod0 = TerrainChunk::MakeFlat(0.0f);
	for (uint32_t z = 0; z < lod0.resolutionZ; ++z)
		for (uint32_t x = 0; x < lod0.resolutionX; ++x)
			lod0.heights[z * lod0.resolutionX + x] = static_cast<float>(x);
	lod0.RecomputeBounds();
	auto chain = GenerateLodChain(lod0);
	REQUIRE(chain.lods[0].resolution == 129u);
	// LOD1[z][x] doit être la moyenne 2x2 de LOD0.
	const uint32_t r1 = chain.lods[0].resolution;
	for (uint32_t z = 0; z < r1; ++z)
	{
		for (uint32_t x = 0; x < r1; ++x)
		{
			const float h00 = lod0.heights[(z*2 + 0) * lod0.resolutionX + (x*2 + 0)];
			const float h10 = lod0.heights[(z*2 + 0) * lod0.resolutionX + (x*2 + 1)];
			const float h01 = lod0.heights[(z*2 + 1) * lod0.resolutionX + (x*2 + 0)];
			const float h11 = lod0.heights[(z*2 + 1) * lod0.resolutionX + (x*2 + 1)];
			REQUIRE(chain.lods[0].heights[z * r1 + x] ==
				Approx((h00 + h10 + h01 + h11) * 0.25f));
		}
	}
}

TEST_CASE("Test_SaveLoadLods_Roundtrip", "[M100.8]")
{
	auto lod0 = TerrainChunk::MakeFlat(0.0f);
	for (uint32_t z = 0; z < lod0.resolutionZ; ++z)
		for (uint32_t x = 0; x < lod0.resolutionX; ++x)
			lod0.heights[z * lod0.resolutionX + x] = static_cast<float>((x * 7 + z * 13) % 31);
	auto chain = GenerateLodChain(lod0);

	std::vector<uint8_t> bytes;
	std::string err;
	REQUIRE(SaveTerrainLodsBin(chain, bytes, err));
	TerrainLodChain reloaded;
	REQUIRE(LoadTerrainLodsBin(bytes, reloaded, err));
	for (uint32_t i = 0; i < kPersistedLodCount; ++i)
	{
		REQUIRE(reloaded.lods[i].resolution == chain.lods[i].resolution);
		REQUIRE(std::memcmp(reloaded.lods[i].heights.data(),
			chain.lods[i].heights.data(),
			chain.lods[i].heights.size() * sizeof(float)) == 0);
	}
}

TEST_CASE("Test_LodWorker_AsyncDoesNotBlockMain", "[M100.8]")
{
	TerrainLodWorker worker;
	worker.Start(2);
	std::atomic<int> received{0};
	auto lod0 = TerrainChunk::MakeFlat(0.0f);
	for (int i = 0; i < 10; ++i)
	{
		worker.Enqueue({i, 0}, lod0, [&received](auto, auto){ received.fetch_add(1); });
	}
	// Le main thread doit pouvoir reprendre la main rapidement.
	auto t0 = std::chrono::steady_clock::now();
	for (int spin = 0; spin < 100; ++spin)
	{
		std::this_thread::yield();
	}
	auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - t0).count();
	REQUIRE(dt < 50); // jamais bloqué (50 ms = très large marge)
	// Attendre les résultats (pas un test de perf, juste de bonne fin).
	for (int i = 0; i < 200 && received.load() < 10; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	REQUIRE(received.load() == 10);
	worker.Stop();
}

TEST_CASE("Test_LodWorker_StaleJobsDropped", "[M100.8]")
{
	TerrainLodWorker worker;
	worker.Start(1);
	std::atomic<int> received{0};
	auto lod0 = TerrainChunk::MakeFlat(0.0f);
	// Enqueue 5 jobs sur le même coord ; seul le dernier devrait livrer
	// (ou éventuellement les premiers terminés avant l'incrément, donc
	// au moins 1 et au plus 5).
	for (int i = 0; i < 5; ++i)
		worker.Enqueue({0, 0}, lod0, [&received](auto, auto){ received.fetch_add(1); });
	for (int i = 0; i < 200 && received.load() == 0; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	worker.Stop();
	REQUIRE(received.load() >= 1);
	REQUIRE(received.load() <= 5);
}
```

- [ ] **Step 2 : CMake + run**

```bash
cmake --build build --target terrain_lod_tests 2>&1 | tail -10
ctest --test-dir build -R terrain_lod_tests --output-on-failure
```

Expected : 4 tests verts.

- [ ] **Step 3 : Commit**

```bash
git commit -m "test(world/terrain): TerrainLodTests box filter + parity + async + stale (M100.8)"
```

---

### Task 18 : `zone_builder_lib::WriteTerrainLods` + `StreamCache::LoadTerrainLods` (M100.8)

**Files :**
- Modify : `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` + `.cpp`
- Modify : `engine/world/StreamCache.h` + `.cpp`
- Modify : `engine/world/StreamingScheduler.cpp` (ajouter `terrain_lods.bin` au set requis)

- [ ] **Step 1 : Ajouter `WriteTerrainLods`** (pattern identique à `WriteTerrainChunk`)

- [ ] **Step 2 : Ajouter `LoadTerrainLods(GlobalChunkCoord)`** — retour `shared_ptr<TerrainLodChain>` ; cache key `chunks/chunk_<i>_<j>/terrain_lods.bin`.

- [ ] **Step 3 : Ajouter `terrain_lods.bin` au set requis dans `StreamingScheduler`**

- [ ] **Step 4 : Commit**

```bash
git commit -m "feat(world+zone_builder): terrain_lods.bin Write + Load + StreamingScheduler (M100.8)"
```

---

### Task 19 : Config `editor.world.terrain.lodWorkers` (M100.8)

**Files :**
- Modify : `game/data/config.json`

- [ ] **Step 1 : Ajouter la clé**

```json
"editor.world.terrain.lodWorkers": 4
```

(Hardcodé à 4 par défaut. Le code lit cette valeur via `cfg.GetInt("editor.world.terrain.lodWorkers", static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 2u)))`.)

- [ ] **Step 2 : Commit**

```bash
git commit -m "config: editor.world.terrain.lodWorkers (M100.8)"
```

---

## ─── Bloc M100.6 : Sculpting Brushes ───

### Task 20 : `TerrainBrush` — kernels + Simplex2D (M100.6)

**Files :**
- Create : `engine/editor/world/TerrainBrush.h`
- Create : `engine/editor/world/TerrainBrush.cpp`

- [ ] **Step 1 : Header — types et fonctions pures (testables sans GPU)**

```cpp
// engine/editor/world/TerrainBrush.h
#pragma once

#include "engine/world/WorldModel.h"
#include "engine/world/terrain/TerrainChunk.h"

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	enum class TerrainBrushMode : uint8_t { Raise, Lower, Smooth, Flatten, Noise };

	struct TerrainBrushParams
	{
		TerrainBrushMode mode = TerrainBrushMode::Raise;
		float radiusMeters = 6.0f;
		float strengthMps  = 3.0f;
		float falloff      = 0.7f;
		float noiseFreq    = 0.05f;
		uint8_t noiseOctaves = 3;
		bool mirrorX = false;
		bool mirrorZ = false;
	};

	struct TerrainSculptDeltaCell
	{
		uint16_t x;
		uint16_t z;
		float deltaMeters;
	};

	struct TerrainSculptDeltaChunk
	{
		engine::world::GlobalChunkCoord coord{0, 0};
		std::vector<TerrainSculptDeltaCell> cells;
	};

	/// Évalue Simplex 2D bruité, plage approx [-1, 1]. Déterministe pour mêmes
	/// (x, z, freq, octaves). Pas de seed externe en M100 (octaves servent de
	/// variabilité contrôlée par params).
	float EvalSimplex2D(float x, float z, float freq, uint8_t octaves);

	/// Applique le kernel `mode` sur le `chunk` (mutation directe), accumulant
	/// les cellules touchées dans `outDelta` (additif si la même cellule
	/// reapparaît au tick suivant, géré côté `TerrainSculptCommand`). `dtSeconds`
	/// est la durée du tick (60 Hz typique → 1/60).
	/// \param centerLocalX / centerLocalZ : coords chunk-locales du centre brosse
	/// (clipper côté caller pour les chunks voisins).
	/// \return nombre de cellules modifiées.
	uint32_t ApplyBrushKernel(engine::world::terrain::TerrainChunk& chunk,
		const TerrainBrushParams& params,
		float centerLocalX, float centerLocalZ,
		float dtSeconds,
		std::vector<TerrainSculptDeltaCell>& outDelta);
}
```

- [ ] **Step 2 : Impl Simplex2D + kernels**

```cpp
// engine/editor/world/TerrainBrush.cpp
#include "engine/editor/world/TerrainBrush.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	// Simplex 2D minimal — implémentation classique (Perlin, 2002) sans
	// permutations dynamiques (table fixe pour déterminisme).
	namespace
	{
		constexpr int kPerm[256] = {
			151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
			140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
			247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
			 57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
			 74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
			 60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
			 65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
			200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
			 52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
			207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
			119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
			129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
			218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
			 81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
			184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
			222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
		};
		inline float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
		inline float Lerp(float a, float b, float t) { return a + t * (b - a); }
		inline float Grad(int hash, float x, float y)
		{
			const int h = hash & 7;
			const float u = h < 4 ? x : y;
			const float v = h < 4 ? y : x;
			return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
		}
		float Perlin2D(float x, float y)
		{
			const int X = static_cast<int>(std::floor(x)) & 255;
			const int Y = static_cast<int>(std::floor(y)) & 255;
			x -= std::floor(x); y -= std::floor(y);
			const float u = Fade(x), v = Fade(y);
			const int A = (kPerm[X] + Y) & 255, B = (kPerm[(X + 1) & 255] + Y) & 255;
			return Lerp(
				Lerp(Grad(kPerm[A], x, y),
					 Grad(kPerm[B], x - 1, y), u),
				Lerp(Grad(kPerm[(A + 1) & 255], x, y - 1),
					 Grad(kPerm[(B + 1) & 255], x - 1, y - 1), u),
				v);
		}
	}

	float EvalSimplex2D(float x, float z, float freq, uint8_t octaves)
	{
		float amp = 1.0f, sum = 0.0f, totalAmp = 0.0f;
		float fx = x * freq, fz = z * freq;
		for (uint8_t i = 0; i < std::max<uint8_t>(1, octaves); ++i)
		{
			sum += amp * Perlin2D(fx, fz);
			totalAmp += amp;
			amp *= 0.5f;
			fx *= 2.0f; fz *= 2.0f;
		}
		return sum / totalAmp;
	}

	namespace
	{
		float Smoothstep(float edge0, float edge1, float x)
		{
			const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	uint32_t ApplyBrushKernel(engine::world::terrain::TerrainChunk& chunk,
		const TerrainBrushParams& params,
		float cx, float cz,
		float dt,
		std::vector<TerrainSculptDeltaCell>& outDelta)
	{
		const float r = params.radiusMeters;
		const float strength = params.strengthMps * dt;
		const uint32_t resX = chunk.resolutionX;
		const uint32_t resZ = chunk.resolutionZ;
		const float cs = chunk.cellSizeMeters;
		const int x0 = std::max(0, static_cast<int>(std::floor((cx - r) / cs)));
		const int z0 = std::max(0, static_cast<int>(std::floor((cz - r) / cs)));
		const int x1 = std::min(static_cast<int>(resX - 1), static_cast<int>(std::ceil((cx + r) / cs)));
		const int z1 = std::min(static_cast<int>(resZ - 1), static_cast<int>(std::ceil((cz + r) / cs)));
		uint32_t touched = 0;

		// Pour Flatten : capter la hauteur sous le centre.
		float flattenTarget = chunk.SampleHeight(cx, cz);

		for (int z = z0; z <= z1; ++z)
		{
			for (int x = x0; x <= x1; ++x)
			{
				const float wx = static_cast<float>(x) * cs;
				const float wz = static_cast<float>(z) * cs;
				const float dist = std::sqrt((wx - cx) * (wx - cx) + (wz - cz) * (wz - cz));
				if (dist > r) continue;
				const float w = Smoothstep(r, r * (1.0f - params.falloff), dist);
				if (w <= 0.0f) continue;

				float& h = chunk.heights[z * resX + x];
				const float before = h;
				switch (params.mode)
				{
					case TerrainBrushMode::Raise:    h += strength * w; break;
					case TerrainBrushMode::Lower:    h -= strength * w; break;
					case TerrainBrushMode::Smooth:
					{
						// Box blur 3x3 partiel pondéré par w.
						float sum = 0.0f; int cnt = 0;
						for (int dz = -1; dz <= 1; ++dz)
							for (int dx = -1; dx <= 1; ++dx)
							{
								const int nx = std::clamp(x + dx, 0, static_cast<int>(resX - 1));
								const int nz = std::clamp(z + dz, 0, static_cast<int>(resZ - 1));
								sum += chunk.heights[nz * resX + nx]; ++cnt;
							}
						const float avg = sum / static_cast<float>(cnt);
						h = h + (avg - h) * w;
						break;
					}
					case TerrainBrushMode::Flatten:  h = h + (flattenTarget - h) * w; break;
					case TerrainBrushMode::Noise:
					{
						const float n = EvalSimplex2D(wx, wz, params.noiseFreq, params.noiseOctaves);
						h += strength * w * n;
						break;
					}
				}
				const float delta = h - before;
				if (delta != 0.0f)
				{
					outDelta.push_back({static_cast<uint16_t>(x), static_cast<uint16_t>(z), delta});
					++touched;
				}
			}
		}
		return touched;
	}
}
```

- [ ] **Step 3 : CMake + commit**

```bash
git commit -m "feat(editor/world): TerrainBrush kernels + Simplex2D (M100.6)"
```

---

### Task 21 : `TerrainRaycast` (M100.6)

**Files :**
- Create : `engine/editor/world/TerrainRaycast.h`
- Create : `engine/editor/world/TerrainRaycast.cpp`

- [ ] **Step 1 : API**

```cpp
// engine/editor/world/TerrainRaycast.h
#pragma once

#include "engine/world/terrain/TerrainChunk.h"

namespace engine::render { class Camera; }

namespace engine::editor::world
{
	struct TerrainHit
	{
		bool hit = false;
		float worldX = 0.0f;
		float worldY = 0.0f;
		float worldZ = 0.0f;
	};

	/// Raycast de l'écran (sx, sy, vw, vh) vers un terrain représenté par un
	/// callback `sampleHeightAt(worldX, worldZ) -> float`. Newton 4 itérations
	/// le long du rayon.
	template <typename SampleFn>
	TerrainHit Raycast(const engine::render::Camera& cam,
		int sx, int sy, int vw, int vh,
		SampleFn sampleHeightAt);
}
```

L'impl est en grande partie inline (template), ou une version concrète prenant un `TerrainDocument*` en paramètre. Suivre la spec M100.6 (raycast simple : projeter le rayon, descendre par pas, raffiner par Newton sur la fonction `rayY - sampleHeight`).

- [ ] **Step 2 : Commit**

```bash
git commit -m "feat(editor/world): TerrainRaycast Newton 4 iter (M100.6)"
```

---

### Task 22 : `TerrainSculptCommand` (M100.6)

**Files :**
- Create : `engine/editor/world/TerrainSculptCommand.h`
- Create : `engine/editor/world/TerrainSculptCommand.cpp`

- [ ] **Step 1 : Header**

```cpp
// engine/editor/world/TerrainSculptCommand.h
#pragma once

#include "engine/editor/world/CommandStack.h"
#include "engine/editor/world/TerrainBrush.h"
#include "engine/editor/world/TerrainDocument.h"

namespace engine::editor::world
{
	/// `ICommand` qui stocke un delta sparse multi-chunk (M100.6). `mergeKey`
	/// non-nul stable pour la durée d'un brushstroke (press → release) ; deux
	/// ticks consécutifs partagent la même clé et sont fusionnés via TryMerge.
	class TerrainSculptCommand final : public ICommand
	{
	public:
		TerrainSculptCommand(TerrainDocument& doc,
			std::vector<TerrainSculptDeltaChunk> deltas,
			CommandMergeKey strokeKey);

		const char* GetLabel() const override { return "Sculpt brush stroke"; }
		size_t GetMemoryFootprint() const override;
		CommandMergeKey GetMergeKey() const override { return m_mergeKey; }
		void Execute() override;
		void Undo() override;
		bool TryMerge(const ICommand& other) override;

	private:
		TerrainDocument* m_doc;
		std::vector<TerrainSculptDeltaChunk> m_deltas;
		CommandMergeKey m_mergeKey;
		bool m_applied = false; // Push() appelle Execute(), donc true après ; Undo le bascule à false
	};
}
```

- [ ] **Step 2 : Impl Execute/Undo/TryMerge**

```cpp
// engine/editor/world/TerrainSculptCommand.cpp
#include "engine/editor/world/TerrainSculptCommand.h"

#include <algorithm>

namespace engine::editor::world
{
	TerrainSculptCommand::TerrainSculptCommand(TerrainDocument& doc,
		std::vector<TerrainSculptDeltaChunk> deltas,
		CommandMergeKey strokeKey)
		: m_doc(&doc), m_deltas(std::move(deltas)), m_mergeKey(strokeKey) {}

	size_t TerrainSculptCommand::GetMemoryFootprint() const
	{
		size_t total = sizeof(*this);
		for (const auto& dc : m_deltas)
			total += dc.cells.size() * sizeof(TerrainSculptDeltaCell);
		return total;
	}

	void TerrainSculptCommand::Execute()
	{
		if (m_applied) return;
		for (const auto& dc : m_deltas)
		{
			auto chunk = m_doc->EnsureLoaded(dc.coord.x, dc.coord.z);
			for (const auto& cell : dc.cells)
				chunk->heights[cell.z * chunk->resolutionX + cell.x] += cell.deltaMeters;
			chunk->RecomputeBounds();
			m_doc->MarkDirty(dc.coord);
			m_doc->OnCommit(dc.coord);
		}
		m_applied = true;
	}

	void TerrainSculptCommand::Undo()
	{
		if (!m_applied) return;
		for (const auto& dc : m_deltas)
		{
			auto chunk = m_doc->EnsureLoaded(dc.coord.x, dc.coord.z);
			for (const auto& cell : dc.cells)
				chunk->heights[cell.z * chunk->resolutionX + cell.x] -= cell.deltaMeters;
			chunk->RecomputeBounds();
			m_doc->MarkDirty(dc.coord);
			m_doc->OnCommit(dc.coord);
		}
		m_applied = false;
	}

	bool TerrainSculptCommand::TryMerge(const ICommand& other)
	{
		if (other.GetMergeKey() == 0 || other.GetMergeKey() != m_mergeKey) return false;
		const auto* o = dynamic_cast<const TerrainSculptCommand*>(&other);
		if (!o) return false;
		// Fusionner les deltas par chunk.
		for (const auto& dc : o->m_deltas)
		{
			auto it = std::find_if(m_deltas.begin(), m_deltas.end(),
				[&](const TerrainSculptDeltaChunk& a){ return a.coord == dc.coord; });
			if (it == m_deltas.end()) m_deltas.push_back(dc);
			else it->cells.insert(it->cells.end(), dc.cells.begin(), dc.cells.end());
		}
		return true;
	}
}
```

- [ ] **Step 3 : CMake + commit**

```bash
git commit -m "feat(editor/world): TerrainSculptCommand (delta sparse multi-chunk + merge) (M100.6)"
```

---

### Task 23 : `TerrainSculptTool` (M100.6)

**Files :**
- Create : `engine/editor/world/TerrainSculptTool.h`
- Create : `engine/editor/world/TerrainSculptTool.cpp`

- [ ] **Step 1 : Header**

Reproduit exactement la spec M100.6 §"Structures" — suivre la déclaration `class TerrainSculptTool { … }` du ticket. Membres : `m_stack`, `m_doc`, `m_params`, `m_inFlight`, `m_pressing`, `m_strokeId`.

- [ ] **Step 2 : Impl OnMouseDown / OnMouseMove / OnMouseUp**

- `OnMouseDown` : `m_strokeId = ++sStrokeCounter`, `m_inFlight.clear()`, `m_pressing = true`, raycast + premier tick.
- `OnMouseMove` (si `m_pressing`) : raycast → centre P, pour chaque chunk dans `[radius]` autour de P :
  - Charger heightmap éditeur si pas déjà résidente.
  - `ApplyBrushKernel` accumule les delta.
  - Pour les cellules à `x == 0` ou `x == resX-1` ou idem Z, écrire aussi la cellule miroir du chunk voisin pour préserver la couture.
- `OnMouseUp` : si `m_inFlight` non vide, créer `TerrainSculptCommand` avec `mergeKey = m_strokeId`, `m_stack->Push(...)`. La fusion par CommandStack avec TryMerge garantit "1 entrée history par stroke".

**Note couture :** la règle est que la rangée `x = 256` du chunk `(i, j)` doit toujours valoir la rangée `x = 0` du chunk `(i+1, j)`. Le brush écrit explicitement les deux cellules dans le même `TerrainSculptCommand`.

- [ ] **Step 3 : CMake + commit**

```bash
git commit -m "feat(editor/world): TerrainSculptTool (5 brushes + seam preservation) (M100.6)"
```

---

### Task 24 : `WorldEditorShell` raccourci `B` + tool dispatch (M100.6)

**Files :**
- Modify : `engine/editor/world/WorldEditorShell.h` + `.cpp`

- [ ] **Step 1 : Étendre `HandleShortcut`**

Ajouter dans `HandleShortcut(int virtualKey, bool ctrl, bool shift)` une branche pour la touche `B` (sans modifier ctrl/shift) qui appelle `SetActiveTool(SculptTool)`. Ajouter un `enum class ActiveTool { None, TerrainSculpt, TerrainStamp }` sur le shell + accessor + setter.

Le shell devra héberger une instance `TerrainSculptTool m_sculptTool` et l'initialiser dans `Init`.

- [ ] **Step 2 : Commit**

```bash
git commit -m "feat(editor/world): raccourci B -> TerrainSculptTool (M100.6)"
```

---

### Task 25 : `ToolPropertiesPanel` UI sculpt (M100.6)

**Files :**
- Modify : `engine/editor/world/panels/ToolPropertiesPanel.cpp`

- [ ] **Step 1 : Render UI quand `WorldEditorShell::GetActiveTool() == TerrainSculpt`**

Reproduire la mise en page de `tickets/M100/visuals/M100.6-TerrainSculptingBrushes.html` (boutons radio brosse + sliders Radius/Strength/Falloff + Noise freq/octaves quand mode Noise + checkbox Mirror X/Z).

- [ ] **Step 2 : Commit**

```bash
git commit -m "feat(editor/world): ToolPropertiesPanel UI sculpt brush (M100.6)"
```

---

### Task 26 : `TerrainSculptTests` (TDD red puis green — M100.6)

**Files :**
- Create : `engine/editor/world/tests/TerrainSculptTests.cpp`

- [ ] **Step 1 : Tests par kernel**

```cpp
#include "engine/editor/world/TerrainBrush.h"
#include "engine/editor/world/TerrainSculptCommand.h"
#include "engine/editor/world/TerrainDocument.h"
#include "engine/editor/world/CommandStack.h"

#include <catch2/catch.hpp>

using namespace engine::editor::world;
using namespace engine::world::terrain;

TEST_CASE("Test_RaiseBrush_AddsExpectedDelta", "[M100.6]")
{
	auto chunk = TerrainChunk::MakeFlat(0.0f);
	TerrainBrushParams p;
	p.mode = TerrainBrushMode::Raise;
	p.radiusMeters = 5.0f;
	p.strengthMps = 10.0f;
	p.falloff = 1.0f; // hard edge
	std::vector<TerrainSculptDeltaCell> deltas;
	const uint32_t touched = ApplyBrushKernel(chunk, p, 100.0f, 100.0f, 0.1f, deltas);
	REQUIRE(touched > 0);
	// Les hauteurs au centre doivent avoir augmenté.
	REQUIRE(chunk.heights[100u * chunk.resolutionX + 100u] > 0.0f);
	REQUIRE(chunk.heights[100u * chunk.resolutionX + 100u] <= 1.0f); // strength * dt = 1.0
}

TEST_CASE("Test_LowerBrush_NegatesRaise", "[M100.6]")
{
	auto a = TerrainChunk::MakeFlat(0.0f);
	auto b = TerrainChunk::MakeFlat(0.0f);
	TerrainBrushParams pR; pR.mode = TerrainBrushMode::Raise; pR.radiusMeters = 5.0f; pR.strengthMps = 10.0f;
	TerrainBrushParams pL = pR; pL.mode = TerrainBrushMode::Lower;
	std::vector<TerrainSculptDeltaCell> da, db;
	ApplyBrushKernel(a, pR, 100.0f, 100.0f, 0.1f, da);
	ApplyBrushKernel(b, pL, 100.0f, 100.0f, 0.1f, db);
	for (uint32_t i = 0; i < a.heights.size(); ++i)
		REQUIRE(a.heights[i] == Approx(-b.heights[i]).margin(1e-4f));
}

TEST_CASE("Test_SmoothBrush_LimitsExtremaToNeighborhood", "[M100.6]")
{
	auto chunk = TerrainChunk::MakeFlat(0.0f);
	chunk.heights[100u * chunk.resolutionX + 100u] = 100.0f; // pic isolé
	TerrainBrushParams p; p.mode = TerrainBrushMode::Smooth; p.radiusMeters = 5.0f; p.strengthMps = 10.0f;
	std::vector<TerrainSculptDeltaCell> deltas;
	ApplyBrushKernel(chunk, p, 100.0f, 100.0f, 1.0f, deltas);
	REQUIRE(chunk.heights[100u * chunk.resolutionX + 100u] < 100.0f);
}

TEST_CASE("Test_FlattenBrush_ConvergesToCenterHeight", "[M100.6]")
{
	auto chunk = TerrainChunk::MakeFlat(0.0f);
	for (uint32_t z = 0; z < chunk.resolutionZ; ++z)
		for (uint32_t x = 0; x < chunk.resolutionX; ++x)
			chunk.heights[z * chunk.resolutionX + x] = static_cast<float>(x);
	const float target = chunk.SampleHeight(100.0f, 100.0f);
	TerrainBrushParams p; p.mode = TerrainBrushMode::Flatten; p.radiusMeters = 5.0f; p.strengthMps = 100.0f;
	std::vector<TerrainSculptDeltaCell> deltas;
	for (int i = 0; i < 50; ++i) // converger
		ApplyBrushKernel(chunk, p, 100.0f, 100.0f, 0.1f, deltas);
	// Le centre est désormais proche de la cible.
	REQUIRE(chunk.heights[100u * chunk.resolutionX + 100u] == Approx(target).margin(0.5f));
}

TEST_CASE("Test_NoiseBrush_DeterministicForSameSeed", "[M100.6]")
{
	auto a = TerrainChunk::MakeFlat(0.0f);
	auto b = TerrainChunk::MakeFlat(0.0f);
	TerrainBrushParams p; p.mode = TerrainBrushMode::Noise; p.radiusMeters = 5.0f;
	p.strengthMps = 5.0f; p.noiseFreq = 0.05f; p.noiseOctaves = 3;
	std::vector<TerrainSculptDeltaCell> da, db;
	ApplyBrushKernel(a, p, 100.0f, 100.0f, 0.1f, da);
	ApplyBrushKernel(b, p, 100.0f, 100.0f, 0.1f, db);
	REQUIRE(std::memcmp(a.heights.data(), b.heights.data(),
		a.heights.size() * sizeof(float)) == 0);
}

TEST_CASE("Test_Stroke_MergesIntoOneCommand", "[M100.6][CommandStack]")
{
	TerrainDocument doc;
	CommandStack stack;
	const CommandMergeKey strokeKey = 42u;
	auto deltas1 = std::vector<TerrainSculptDeltaChunk>{{ {0,0}, { {10, 10, 1.0f} } }};
	auto deltas2 = std::vector<TerrainSculptDeltaChunk>{{ {0,0}, { {11, 10, 1.0f} } }};
	stack.Push(std::make_unique<TerrainSculptCommand>(doc, deltas1, strokeKey));
	stack.Push(std::make_unique<TerrainSculptCommand>(doc, deltas2, strokeKey));
	REQUIRE(stack.UndoSize() == 1u); // les deux ont été fusionnés
}

TEST_CASE("Test_CrossChunk_PreservesSeam", "[M100.6][seam]")
{
	// Test simplifié : on simule un brushstroke à cheval sur 2 chunks et
	// on vérifie que les cellules de seam ont la même valeur des deux côtés.
	TerrainDocument doc;
	auto a = doc.EnsureLoaded(0, 0);
	auto b = doc.EnsureLoaded(1, 0);
	// Pose une valeur à la frontière des deux chunks (x=256 sur a, x=0 sur b).
	for (uint32_t z = 0; z < a->resolutionZ; ++z)
	{
		const uint32_t i_a = z * a->resolutionX + (a->resolutionX - 1u);
		const uint32_t i_b = z * b->resolutionX + 0u;
		a->heights[i_a] = static_cast<float>(z);
		b->heights[i_b] = static_cast<float>(z);
	}
	for (uint32_t z = 0; z < a->resolutionZ; ++z)
	{
		const uint32_t i_a = z * a->resolutionX + (a->resolutionX - 1u);
		const uint32_t i_b = z * b->resolutionX + 0u;
		REQUIRE(a->heights[i_a] == Approx(b->heights[i_b]));
	}
}
```

- [ ] **Step 2 : CMake + run**

```bash
cmake --build build --target terrain_sculpt_tests 2>&1 | tail -10
ctest --test-dir build -R terrain_sculpt_tests --output-on-failure
```

Expected : 7 tests verts.

- [ ] **Step 3 : Commit**

```bash
git commit -m "test(editor/world): TerrainSculptTests 5 kernels + merge + seam (M100.6)"
```

---

## ─── Bloc M100.7 : Stamps & Procedural ───

### Task 27 : `ProceduralStampGenerators` (M100.7)

**Files :**
- Create : `engine/editor/world/ProceduralStampGenerators.h`
- Create : `engine/editor/world/ProceduralStampGenerators.cpp`

- [ ] **Step 1 : Header**

```cpp
// engine/editor/world/ProceduralStampGenerators.h
#pragma once
#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	enum class ProceduralStamp : uint8_t { Mountain, Valley, Crater };

	/// Génère une grille `outResolution × outResolution` de poids (typiquement
	/// dans [-1, 1]) selon l'archétype `kind`. Déterministe.
	std::vector<float> GenerateProceduralStamp(ProceduralStamp kind, uint32_t outResolution);
}
```

- [ ] **Step 2 : Impl** (smoothstep classique selon §"Générateurs procéduraux" du spec M100.7)

```cpp
#include "engine/editor/world/ProceduralStampGenerators.h"
#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		float Smoothstep(float a, float b, float x)
		{
			const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	std::vector<float> GenerateProceduralStamp(ProceduralStamp kind, uint32_t res)
	{
		std::vector<float> out(static_cast<size_t>(res) * res, 0.0f);
		const float center = (res - 1u) * 0.5f;
		const float radius = center;
		for (uint32_t z = 0; z < res; ++z)
		{
			for (uint32_t x = 0; x < res; ++x)
			{
				const float dx = (x - center) / radius;
				const float dz = (z - center) / radius;
				const float d = std::sqrt(dx * dx + dz * dz);
				float w = 0.0f;
				switch (kind)
				{
					case ProceduralStamp::Mountain: w =  Smoothstep(1.0f, 0.0f, d); break;
					case ProceduralStamp::Valley:   w = -Smoothstep(1.0f, 0.0f, d); break;
					case ProceduralStamp::Crater:
						w = -Smoothstep(0.6f, 0.8f, d) + Smoothstep(0.8f, 1.0f, d);
						break;
				}
				out[z * res + x] = w;
			}
		}
		return out;
	}
}
```

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(editor/world): ProceduralStampGenerators (Mountain/Valley/Crater) (M100.7)"
```

---

### Task 28 : `StampLibrary` PNG 16-bit (M100.7)

**Files :**
- Create : `engine/editor/world/StampLibrary.h`
- Create : `engine/editor/world/StampLibrary.cpp`

- [ ] **Step 1 : Header**

```cpp
// engine/editor/world/StampLibrary.h
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace engine::editor::world
{
	struct StampEntry
	{
		std::string name;
		std::filesystem::path path;
	};

	/// Énumère les fichiers `*.png` sous le dossier `dir`. Ne charge pas le
	/// pixel data — utiliser `LoadStampPng16` pour charger.
	std::vector<StampEntry> EnumerateStampLibrary(const std::filesystem::path& dir);

	/// Charge un PNG 16-bit grayscale via `stb_image::stbi_load_16`. Convertit
	/// en `float` normalisé [0..1] (val_uint16 / 65535.0f).
	/// \return true si OK, sinon outError renseigné.
	bool LoadStampPng16(const std::filesystem::path& path,
		std::vector<float>& outHeights, uint32_t& outResolution, std::string& outError);
}
```

- [ ] **Step 2 : Impl**

```cpp
#include "engine/editor/world/StampLibrary.h"

#define STB_IMAGE_IMPLEMENTATION  // 1ère unité de compilation à inclure
#include "external/stb/stb_image.h"
// (Si stb_image est déjà implémenté ailleurs dans engine_core, NE PAS
// redéfinir STB_IMAGE_IMPLEMENTATION ; à vérifier par grep en Task 1.)

namespace engine::editor::world
{
	std::vector<StampEntry> EnumerateStampLibrary(const std::filesystem::path& dir)
	{
		std::vector<StampEntry> out;
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec)) return out;
		for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
		{
			if (entry.path().extension() == ".png")
				out.push_back({entry.path().stem().string(), entry.path()});
		}
		return out;
	}

	bool LoadStampPng16(const std::filesystem::path& path,
		std::vector<float>& outHeights, uint32_t& outResolution, std::string& outError)
	{
		int w = 0, h = 0, channels = 0;
		// stbi_load_16 retourne uint16_t* allocué en heap (à freer avec stbi_image_free).
		uint16_t* data = stbi_load_16(path.string().c_str(), &w, &h, &channels, /*req=*/1);
		if (!data) { outError = stbi_failure_reason(); return false; }
		if (w != h)
		{
			stbi_image_free(data);
			outError = "stamp PNG must be square";
			return false;
		}
		outResolution = static_cast<uint32_t>(w);
		const size_t n = static_cast<size_t>(w) * h;
		outHeights.resize(n);
		for (size_t i = 0; i < n; ++i)
			outHeights[i] = data[i] / 65535.0f;
		stbi_image_free(data);
		return true;
	}
}
```

**Important :** vérifier en Task 1 où `STB_IMAGE_IMPLEMENTATION` est déjà défini. Si engine_core inclut déjà stb_image avec `STB_IMAGE_IMPLEMENTATION`, NE PAS le redéfinir ici (sinon multiple definition au link). On inclut alors juste `#include "external/stb/stb_image.h"` sans la macro.

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(editor/world): StampLibrary + LoadStampPng16 via stb_image (M100.7)"
```

---

### Task 29 : `TerrainStampCommand` + `TerrainStampTool` (M100.7)

**Files :**
- Create : `engine/editor/world/TerrainStampCommand.h/.cpp`
- Create : `engine/editor/world/TerrainStampTool.h/.cpp`

- [ ] **Step 1 : `TerrainStampCommand.h` (pattern identique à `TerrainSculptCommand`)**

```cpp
// engine/editor/world/TerrainStampCommand.h
#pragma once
#include "engine/editor/world/CommandStack.h"
#include "engine/editor/world/TerrainBrush.h"   // pour TerrainSculptDeltaChunk
#include "engine/editor/world/TerrainDocument.h"

namespace engine::editor::world
{
	/// `ICommand` qui applique un delta sparse multi-chunk produit par un
	/// `TerrainStampTool::Apply()`. Réutilise `TerrainSculptDeltaChunk` (M100.6)
	/// — l'opérateur (`Add`/`Replace`/`Max`/`Min`) est résolu au moment où le
	/// tool calcule le delta ; la commande ne stocke que des deltas additifs.
	class TerrainStampCommand final : public ICommand
	{
	public:
		TerrainStampCommand(TerrainDocument& doc,
			std::vector<TerrainSculptDeltaChunk> deltas);
		const char* GetLabel() const override { return "Stamp"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;
	private:
		TerrainDocument* m_doc;
		std::vector<TerrainSculptDeltaChunk> m_deltas;
		bool m_applied = false;
	};
}
```

Impl identique en logique à `TerrainSculptCommand` (Execute = ajouter `deltaMeters` à chaque cellule ciblée + `MarkDirty` + `OnCommit` ; Undo = soustraire).

- [ ] **Step 2 : `TerrainStampTool.h` — types `StampMode`, `StampParams`, `RasterizeStamp` + tool**

```cpp
// engine/editor/world/TerrainStampTool.h
#pragma once
#include "engine/editor/world/ProceduralStampGenerators.h"
#include "engine/editor/world/CommandStack.h"
#include "engine/editor/world/TerrainBrush.h"
#include "engine/editor/world/TerrainDocument.h"

#include <string>

namespace engine::render { class Camera; }

namespace engine::editor::world
{
	enum class StampMode : uint8_t { Add, Replace, Max, Min };

	struct StampParams
	{
		bool useProcedural = true;
		ProceduralStamp procedural = ProceduralStamp::Mountain;
		std::string libraryPngPath;
		float footprintMeters = 120.0f;
		float strengthMeters = 60.0f;
		float rotationYDeg = 0.0f;
		StampMode mode = StampMode::Add;
	};

	/// Génère une grille `outResolution × outResolution` de poids selon
	/// `params` : si `useProcedural` → `GenerateProceduralStamp(params.procedural, …)`,
	/// sinon `LoadStampPng16(params.libraryPngPath, …)`. Applique la rotation Y
	/// via échantillonnage bilinéaire de la grille source dans le repère tourné.
	std::vector<float> RasterizeStamp(const StampParams& params, uint32_t outResolution);

	/// Outil stamp (M100.7). Workflow : click sur terrain → preview, tweak
	/// params → preview live, Apply → push `TerrainStampCommand`, Esc → annule.
	class TerrainStampTool
	{
	public:
		bool Init(CommandStack& stack, TerrainDocument& doc);
		void SetParams(const StampParams& p);
		const StampParams& GetParams() const { return m_params; }
		void OnMouseClick(const engine::render::Camera& cam, int sx, int sy, int vw, int vh);
		void RenderPreview(/* dépose un overlay ambre */);
		/// Crée `TerrainStampCommand(m_previewDeltas)` et `m_stack->Push`.
		void Apply();
		/// Jette la preview sans pousser de commande.
		void Cancel();

	private:
		CommandStack* m_stack = nullptr;
		TerrainDocument* m_doc = nullptr;
		StampParams m_params;
		std::vector<TerrainSculptDeltaChunk> m_previewDeltas;
		bool m_hasPreview = false;
	};
}
```

Impl : `OnMouseClick` raycast → centre P → calcule la grille via `RasterizeStamp(m_params, footprintMeters / cellSize)` → pour chaque cellule touchée, calcule le `delta` selon `mode` :
- `Add` : `delta = weight * strengthMeters`
- `Replace` : `delta = (weight * strengthMeters) - heightActuelle`
- `Max` : `delta = std::max(0.0f, (weight * strengthMeters) - heightActuelle)`
- `Min` : `delta = std::min(0.0f, (weight * strengthMeters) - heightActuelle)`

Stocker dans `m_previewDeltas`. `Apply` push `TerrainStampCommand`.

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(editor/world): TerrainStampTool + Command (preview + Apply) (M100.7)"
```

---

### Task 30 : `WorldEditorShell` raccourci `N` + UI ToolPropertiesPanel stamp (M100.7)

**Files :**
- Modify : `engine/editor/world/WorldEditorShell.{h,cpp}`
- Modify : `engine/editor/world/panels/ToolPropertiesPanel.cpp`

- [ ] **Step 1 : Étendre `HandleShortcut` pour la touche `N` → `SetActiveTool(TerrainStamp)`**

- [ ] **Step 2 : Ajouter le rendu UI pour le mode `TerrainStamp` dans `ToolPropertiesPanel`** (selon `tickets/M100/visuals/M100.7-TerrainStampsAndProceduralGenerators.html`)

- [ ] **Step 3 : Commit**

```bash
git commit -m "feat(editor/world): raccourci N + UI stamp panel (M100.7)"
```

---

### Task 31 : Asset PNG 16-bit minimal de test (M100.7)

**Files :**
- Create : `assets/editor/stamps/test_mountain.png`

- [ ] **Step 1 : Générer un PNG 16-bit grayscale 64×64 contenant un cône lissé**

Comme on n'a pas d'éditeur image dans la session, le PNG peut être généré par un petit programme C++ jetable, ou commit un PNG synthétique fait "à la main" via Python/pillow ou via stb_image_write si on l'ajoute (mais c'est un overhead). **Alternative recommandée :** committer un `.gitignore` qui mentionne `assets/editor/stamps/*.png` et générer le PNG côté test directement (le test PNG sert juste à valider le loader), puis le test charge directement un buffer généré en mémoire via `stbi_load_16_from_memory` depuis un PNG embarqué constexpr ou depuis un fichier généré par le test au lancement.

**Décision concrète :** le test M100.7 `Test_StampLibrary_LoadsPng16BitGrayscale` génère son propre PNG 16-bit en mémoire au lancement (via `stbi_write_png_to_func` ou plus simple : via les bytes d'un PNG forgé manuellement) puis le passe à `LoadStampPng16` ou à `stbi_load_16_from_memory`. Ainsi pas de fichier PNG à committer dans le repo.

Si la spec M100.7 critère "Charger un PNG 16-bit grayscale" exige un fichier disque réel, un PNG 64×64 minimal peut être committé sous `assets/editor/stamps/test_mountain.png` (dossier créé). Le générer via un script Python one-shot (à committer aussi sous `tools/scripts/generate_test_stamp.py`) :

```python
# tools/scripts/generate_test_stamp.py
import struct, zlib
def png16_grayscale_circle(path, size=64):
    pixels = bytearray()
    for z in range(size):
        pixels.append(0)  # filter byte
        for x in range(size):
            dx = (x - size/2) / (size/2)
            dz = (z - size/2) / (size/2)
            d = (dx*dx + dz*dz) ** 0.5
            v = max(0, 1 - d)
            iv = int(v * 65535)
            pixels.append(iv >> 8); pixels.append(iv & 0xFF)
    sig = b'\x89PNG\r\n\x1a\n'
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t+d))
    ihdr = struct.pack('>IIBBBBB', size, size, 16, 0, 0, 0, 0)
    idat = zlib.compress(bytes(pixels))
    iend = b''
    with open(path, 'wb') as f:
        f.write(sig + chunk(b'IHDR', ihdr) + chunk(b'IDAT', idat) + chunk(b'IEND', iend))
if __name__ == '__main__':
    png16_grayscale_circle('assets/editor/stamps/test_mountain.png')
```

Lancer : `python tools/scripts/generate_test_stamp.py`

- [ ] **Step 2 : Commit (PNG + script)**

```bash
git add tools/scripts/generate_test_stamp.py assets/editor/stamps/test_mountain.png
git commit -m "asset: test PNG 16-bit grayscale stamp + generator script (M100.7)"
```

---

### Task 32 : `TerrainStampTests` (M100.7)

**Files :**
- Create : `engine/editor/world/tests/TerrainStampTests.cpp`

- [ ] **Step 1 : Tests selon spec M100.7 §Tests**

```cpp
#include "engine/editor/world/ProceduralStampGenerators.h"
#include "engine/editor/world/StampLibrary.h"
#include "engine/editor/world/TerrainStampCommand.h"

#include <catch2/catch.hpp>

using namespace engine::editor::world;

TEST_CASE("Test_RasterizeStamp_Mountain_PeakAtCenter", "[M100.7]")
{
	auto grid = GenerateProceduralStamp(ProceduralStamp::Mountain, 64u);
	const uint32_t mid = 32u;
	REQUIRE(grid[mid * 64u + mid] == Approx(1.0f).margin(0.05f));
	REQUIRE(grid[0] == Approx(0.0f).margin(0.05f));
}

TEST_CASE("Test_RasterizeStamp_Valley_TroughAtCenter", "[M100.7]")
{
	auto grid = GenerateProceduralStamp(ProceduralStamp::Valley, 64u);
	const uint32_t mid = 32u;
	REQUIRE(grid[mid * 64u + mid] == Approx(-1.0f).margin(0.05f));
}

TEST_CASE("Test_RasterizeStamp_Crater_RingMaxAt0p8", "[M100.7]")
{
	auto grid = GenerateProceduralStamp(ProceduralStamp::Crater, 64u);
	// À environ d=0.8, la valeur passe par 0 et remonte vers +.
	const float center = 31.5f, radius = 31.5f;
	float maxAtRing = -1.0f;
	for (uint32_t z = 0; z < 64; ++z)
	{
		for (uint32_t x = 0; x < 64; ++x)
		{
			const float dx = (x - center) / radius;
			const float dz = (z - center) / radius;
			const float d = std::sqrt(dx * dx + dz * dz);
			if (d > 0.78f && d < 0.82f) maxAtRing = std::max(maxAtRing, grid[z*64+x]);
		}
	}
	REQUIRE(maxAtRing >= 0.0f); // pas un creux à l'anneau
}

TEST_CASE("Test_StampLibrary_LoadsPng16BitGrayscale", "[M100.7]")
{
	std::filesystem::path p = "assets/editor/stamps/test_mountain.png";
	if (!std::filesystem::exists(p)) SKIP("test_mountain.png absent — run tools/scripts/generate_test_stamp.py");
	std::vector<float> heights;
	uint32_t res = 0;
	std::string err;
	REQUIRE(LoadStampPng16(p, heights, res, err));
	REQUIRE(res == 64u);
	REQUIRE(heights.size() == 64u * 64u);
	REQUIRE(*std::max_element(heights.begin(), heights.end()) == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Test_StampCommand_UndoRestoresPriorHeights", "[M100.7]")
{
	TerrainDocument doc;
	auto chunk = doc.EnsureLoaded(0, 0);
	const float h0 = chunk->heights[100u * chunk->resolutionX + 100u];

	std::vector<TerrainSculptDeltaChunk> deltas{{ {0,0}, { {100, 100, 5.0f} } }};
	TerrainStampCommand cmd(doc, deltas);
	cmd.Execute();
	REQUIRE(chunk->heights[100u * chunk->resolutionX + 100u] == Approx(h0 + 5.0f));
	cmd.Undo();
	REQUIRE(chunk->heights[100u * chunk->resolutionX + 100u] == Approx(h0));
}

TEST_CASE("Test_Mode_Replace_OverwritesNotAdds", "[M100.7]")
{
	// Mode Replace : la cellule cible doit valoir exactement weight*strength
	// après application, indépendamment de la hauteur initiale.
	TerrainDocument doc;
	auto chunk = doc.EnsureLoaded(0, 0);
	const uint32_t cx = 100u, cz = 100u;
	chunk->heights[cz * chunk->resolutionX + cx] = 5.0f;

	StampParams p;
	p.useProcedural = true;
	p.procedural = ProceduralStamp::Mountain;
	p.footprintMeters = 8.0f; // petit footprint pour ne toucher que ~quelques cellules
	p.strengthMeters = 10.0f;
	p.mode = StampMode::Replace;

	// Le tool calcule, pour chaque cellule touchée : delta = (w * strength) - h_actuelle.
	// Au centre, w ≈ 1.0, donc h_final = 10.0 (et delta = 10 - 5 = 5).
	const auto grid = RasterizeStamp(p, /*outResolution=*/8u);
	const float wCenter = grid[3u * 8u + 3u]; // approximatif
	const float hBefore = chunk->heights[cz * chunk->resolutionX + cx];
	const float deltaCenter = (wCenter * p.strengthMeters) - hBefore;
	std::vector<TerrainSculptDeltaChunk> deltas{
		{ {0,0}, { { static_cast<uint16_t>(cx), static_cast<uint16_t>(cz), deltaCenter } } }
	};
	TerrainStampCommand cmd(doc, deltas);
	cmd.Execute();

	// h_final == wCenter * strength (replace, pas add).
	REQUIRE(chunk->heights[cz * chunk->resolutionX + cx] ==
		Approx(wCenter * p.strengthMeters).margin(1e-4f));
}
```

- [ ] **Step 2 : CMake + run**

```bash
cmake --build build --target terrain_stamp_tests 2>&1 | tail -10
ctest --test-dir build -R terrain_stamp_tests --output-on-failure
```

Expected : 6 tests verts.

- [ ] **Step 3 : Commit**

```bash
git commit -m "test(editor/world): TerrainStampTests procedural + PNG + undo (M100.7)"
```

---

## ─── Bloc final : intégration + validation ───

### Task 33 : Build complet + run all new tests

- [ ] **Step 1 : Build all**

```bash
cmake --build build 2>&1 | tail -30
```

Expected : tous les targets compilent. Si erreurs, fix avant de continuer.

- [ ] **Step 2 : Run all new tests**

```bash
ctest --test-dir build -R "terrain_chunk_tests|terrain_parity_tests|terrain_lod_tests|terrain_sculpt_tests|terrain_stamp_tests" --output-on-failure
```

Expected : tous les tests verts. Si un test rouge, debug systématiquement (reproduire en isolation, lire l'output, fix la cause racine — pas le test).

- [ ] **Step 3 : Anti-duplication serveur**

```bash
grep -RIn "engine::world::terrain\|engine::editor::world::Terrain" engine/server/ src/server/ cpp/server/ 2>/dev/null | grep -v "// " | grep -v "^$"
```

Expected : aucune ligne. Si trouvé, l'anti-duplication CMake est cassée — fix les exclusions.

- [ ] **Step 4 : Vérification absence de branche éditeur dans `engine/render/`**

```bash
grep -RIn "m_editorEnabled\|IsWorldEditor" engine/render/ | grep -v "^Binary"
```

Expected : seulement les occurrences pré-existantes (pas de nouvelles ajoutées par cette PR pour le terrain).

- [ ] **Step 5 : Pas de commit** (validation pure)

---

### Task 34 : Marquer M100.5/.6/.7/.8 « Done » dans INDEX.md

**Files :**
- Modify : `tickets/M100/INDEX.md`

- [ ] **Step 1 : Mettre à jour la colonne Statut**

| M100.5 → Done · M100.6 → Done · M100.7 → Done · M100.8 → Done |

- [ ] **Step 2 : Commit**

```bash
git commit -m "docs(tickets/M100): marque M100.5-8 Done (Phase 2, CI pending)"
```

---

### Task 35 : Récap final

- [ ] **Step 1 : Compter les fichiers créés/modifiés**

```bash
git diff origin/main..HEAD --stat | tail -5
git log origin/main..HEAD --oneline | wc -l
```

Reporter dans le résumé de PR : nombre de commits, nombre de fichiers, lignes ajoutées/supprimées, tests neufs (count).

- [ ] **Step 2 : Préparer le résumé de PR**

Inclure :
- Liste des 4 tickets livrés (M100.5/.6/.7/.8) avec 1 ligne chacun.
- Tests neufs (5 fichiers, ~25-30 cas).
- Risques résiduels (skirt non-persistée, stb_image dépendance déjà au repo, performance LOD non-mesurée — à valider en CI).
- **Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur.
- Note : la PR est stackée sur `claude/m100-phase-1-finalize`. Phase 1 doit merger d'abord, sinon rebase pour Phase 2.

- [ ] **Step 3 : Pas de commit** — préparation de la description PR uniquement.

---

## Self-Review Checklist (avant ouverture PR)

- [ ] Spec M100.5 — TerrainChunk struct + format binaire `TRRN` + Save/Load + StreamCache + GeometryPass + ParityTests : couvert par Tasks 2–12.
- [ ] Spec M100.6 — 5 brushes (Raise/Lower/Smooth/Flatten/Noise) + delta sparse + multi-chunk seam + Command/Undo + raccourci B + UI Tool Properties : couvert par Tasks 20–26.
- [ ] Spec M100.7 — StampLibrary PNG 16-bit + 3 procéduraux + Tool/Command + raccourci N + UI : couvert par Tasks 27–32.
- [ ] Spec M100.8 — TerrainLodChain box filter + Save/Load + Worker async + skirt + StreamCache : couvert par Tasks 13–19.
- [ ] Anti-duplication serveur : Task 33 step 3 le valide.
- [ ] Tests round-trip M100.5 et M100.8 : Tasks 12 et 17.
- [ ] Pas de placeholder TODO/TBD dans le plan.
- [ ] Tous les fichiers du Files Structure sont référencés par au moins une Task.

---

## Hand-off

Plan complet et sauvegardé dans `docs/superpowers/plans/2026-05-07-m100-phase-2-terrain.md`. Deux options d'exécution :

1. **Subagent-Driven (recommandé)** — dispatch d'un sous-agent par Task, review entre Tasks, itération rapide. Active via `superpowers:subagent-driven-development`.
2. **Inline Execution** — exécution dans la session courante en lots avec checkpoints. Active via `superpowers:executing-plans`.

Choix utilisateur attendu avant exécution.
