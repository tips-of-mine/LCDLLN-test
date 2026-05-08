# M100.14 Water Render Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Livrer la passe Vulkan FG-intégrée qui rend les meshes d'eau gameplay (lacs/rivières de M100.13) avec normales animées 2 octaves, réfraction, SSR mince + skybox fallback, Fresnel. Inclut la suppression du M38 `WaterRenderer` (dead code).

**Architecture:** `engine/render/WaterMeshGpu` encapsule un VBO/IBO concaténé reconstruit depuis `WaterScene` à chaque mutation (live update via dirty flag). `engine/render/WaterPass` est FG-intégrée (pattern `UnderwaterPass` + `DecalPass`), 4 bindings (sceneColor + sceneDepth + normalMap + skyboxCube), push constants 128 B par-instance. Ping-pong rename `SceneColor_HDR → SceneColor_HDR_PostWater` (Bloom_Prefilter + Bloom_Combine renamés). Aucune branche éditeur dans la passe (contrat M100.14).

**Tech Stack:** C++20, Vulkan 1.3, VMA (staging buffer), GLSL 450 (vertex + fragment + SSR raymarch inline), hand-rolled REQUIRE test framework (pattern M100.13).

**Spec source :** [docs/superpowers/specs/2026-05-08-m100-14-water-render-pass-design.md](../specs/2026-05-08-m100-14-water-render-pass-design.md).

---

## File Structure

### Création (8 fichiers)

| Fichier | Rôle |
|---|---|
| `engine/render/WaterMeshGpu.h` | Struct `WaterInstanceDrawInfo` + classe `WaterMeshGpu` + helper CPU `BuildDrawInfos` |
| `engine/render/WaterMeshGpu.cpp` | `BuildDrawInfos` (CPU testable) + Init/Rebuild/Destroy (VMA staging) |
| `engine/render/WaterPass.h` | Struct `WaterPassPushConstants` (128 B) + classe `WaterPass` |
| `engine/render/WaterPass.cpp` | Init (render pass + descriptor + pipeline) + Record (loop drawInfos + push constants) |
| `engine/render/shaders/water.vert` | Vertex shader (pass-through viewProj) |
| `engine/render/shaders/water.frag` | Fragment : 2 octaves normales + refraction + SSR 32 steps + Fresnel |
| `engine/render/tests/WaterPassTests.cpp` | 2 tests offsetof (sizeof 128 B + offsets) |
| `engine/render/tests/WaterMeshGpuTests.cpp` | 3 tests CPU `BuildDrawInfos` |

### Suppression (2 fichiers + call sites)

| Fichier | Action |
|---|---|
| `engine/render/WaterRenderer.h` | DELETE (M38 dead code) |
| `engine/render/WaterRenderer.cpp` | DELETE (M38 dead code) |
| `engine/Engine.h` | Remove `m_waterRenderer` member + include |
| `engine/Engine.cpp` | Remove M38 init (lignes 1298-1321) + destroy (ligne 2738) + config keys |
| `CMakeLists.txt` | Remove `WaterRenderer.cpp` de engine_core |

### Modification (6 fichiers)

| Fichier | Modification |
|---|---|
| `engine/Engine.h` | + includes WaterPass/WaterMeshGpu/WaterSurfaces ; + `m_waterPass`, `m_waterMeshGpu`, `m_clientWaterScene`, `m_waterClientSceneDirty`, `m_fgSceneColorHDRPostWaterId` |
| `engine/Engine.cpp` | + boot init WaterPass après Lighting ; + per-frame dirty rebuild ; + `addPass("Water")` + `addPass("Water_Passthrough")` fallback ; + 2 renames downstream Bloom_Prefilter/Bloom_Combine |
| `game/data/shaders/water.vert` | OVERWRITE (build pipeline copie depuis engine/) |
| `game/data/shaders/water.frag` | OVERWRITE |
| `CMakeLists.txt` | + `WaterMeshGpu.cpp` + `WaterPass.cpp` + 2 test executables (`water_pass_tests`, `water_mesh_gpu_tests`) |
| `tickets/M100/INDEX.md` | M100.14 Ready → Done (CI pending) |

---

## Branch & TDD Workflow

Branche active : `claude/m100-14-water-render-pass` (créée sur `origin/main` post-M100.13 PR #478, contient le spec committé `5b5d5e2`).

Chaque task : red (test failing) → green (minimal impl) → commit. Build local non disponible (CMake/MSBuild absents) — verification deferred to CI.

**Note importante — API et types** :
- Le test framework est REQUIRE maison (defined inline en haut de chaque fichier `*Tests.cpp`, pattern M100.13).
- `engine::world::water::WaterScene` (de M100.13) contient `lakes` (vector<LakeInstance>) et `rivers` (vector<RiverInstance>).
- `engine::world::water::LakeInstance` contient `polygon` (vector<Vec3>), `bottomColor`, `turbidity`, `waterLevelY`.
- `engine::world::water::RiverInstance` contient `nodes` (vector<RiverNode>), `bottomColor`, `turbidity`, `flowSpeed`.
- `engine::world::water::RiverNode` contient `position` (Vec3), `width` (float).
- `engine::world::water::WaterMeshBuilder::BuildLakeMesh` / `BuildRiverMesh` produisent `WaterMeshCpu` avec `vertices` (de type `engine::world::water::WaterVertex` = `{Vec3 position}` seulement, 12 B). M100.14 enrichit le format à 28 B (`engine::render::WaterVertex` = pos3 + uv2 + flowDir2).
- `paramsIndex` unifié : `0..N_lakes-1` = lacs (offset dans `scene.lakes`), `N_lakes..N_lakes+N_rivers-1` = rivières (offset = `paramsIndex - N_lakes` dans `scene.rivers`).

---

## Task 1: Cleanup M38 WaterRenderer (préalable isolé)

**Objectif :** Supprimer entièrement le code mort `WaterRenderer` avant d'introduire le nouveau `WaterPass`. Pas de test à écrire — c'est une suppression vérifiée par compilation.

**Files:**
- Delete: `engine/render/WaterRenderer.h`
- Delete: `engine/render/WaterRenderer.cpp`
- Modify: `engine/Engine.h`
- Modify: `engine/Engine.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Supprimer les fichiers M38**

```bash
git rm engine/render/WaterRenderer.h engine/render/WaterRenderer.cpp
```

- [ ] **Step 2 : Retirer le membre + include de `engine/Engine.h`**

Localiser et supprimer :
```cpp
#include "engine/render/WaterRenderer.h"
```
et :
```cpp
engine::render::WaterRenderer m_waterRenderer;
```

Si nécessaire, utiliser :
```bash
grep -n "WaterRenderer" engine/Engine.h
```
pour confirmer les lignes exactes avant suppression.

- [ ] **Step 3 : Retirer le bloc init dans `engine/Engine.cpp` (lignes 1298-1321)**

Supprimer **intégralement** ce bloc (et son commentaire `// M37.1 — Water renderer (optional)`) :

```cpp
// M37.1 — Water renderer (optional: skipped if shaders not found).
if (pipelineOk)
{
    const uint32_t w = static_cast<uint32_t>(m_width);
    const uint32_t h = static_cast<uint32_t>(m_height);
    std::vector<uint32_t> waterVert = loadSpirv("shaders/water.vert.spv");
    std::vector<uint32_t> waterFrag = loadSpirv("shaders/water.frag.spv");
    const float waterLevel = static_cast<float>(m_cfg.GetDouble("render.water.level", 0.0));
    engine::render::WaterParams waterParams{};
    waterParams.waterLevel     = waterLevel;
    waterParams.gridResolution = static_cast<uint32_t>(m_cfg.GetInt("render.water.grid_resolution", 32));
    waterParams.gridHalfSize   = static_cast<float>(m_cfg.GetDouble("render.water.grid_half_size", 256.0));
    if (!m_waterRenderer.Init(
        m_vkDeviceContext.GetDevice(),
        m_vkDeviceContext.GetPhysicalDevice(),
        w, h,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        waterVert.empty() ? nullptr : waterVert.data(), waterVert.size(),
        waterFrag.empty() ? nullptr : waterFrag.data(), waterFrag.size(),
        waterParams))
    {
        LOG_WARN(Render, "[Boot] WaterRenderer init failed — water surface disabled");
    }
}
```

- [ ] **Step 4 : Retirer le bloc destroy dans `engine/Engine.cpp` (ligne 2738)**

Supprimer la ligne :

```cpp
m_waterRenderer.Destroy(m_vkDeviceContext.GetDevice());
```

- [ ] **Step 5 : Retirer la source `WaterRenderer.cpp` de `CMakeLists.txt`**

Localiser et supprimer la ligne :

```cmake
engine/render/WaterRenderer.cpp
```

dans la liste de sources de la cible `engine_core`.

- [ ] **Step 6 : Vérifier qu'aucune référence `WaterRenderer` ne subsiste**

```bash
grep -rn "WaterRenderer" engine/ tools/ CMakeLists.txt
```
Expected output : **vide** (aucune ligne). Si grep trouve quelque chose, retourner aux étapes précédentes.

Note : les anciens shaders `game/data/shaders/water.{vert,frag}` ne sont **pas** supprimés ici — ils seront **réécrits** par Task 7. Les fichiers `.spv` correspondants seront régénérés au prochain build.

- [ ] **Step 7 : Commit**

```bash
git add -A engine/render/ engine/Engine.h engine/Engine.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
chore(render): supprime WaterRenderer M38 dead code (M100.14 Task 1)

Le WaterRenderer M37/M38 était initialisé au boot (engine/Engine.cpp:1298-1321)
mais Record() n'était jamais appelé dans le frame graph. Pure dette technique :
~16 MB VRAM alloués (Reflection RT + Refraction RT) pour aucun pixel dessiné.

Suppression :
- engine/render/WaterRenderer.{h,cpp}        (~ -550 LOC)
- engine/Engine.{h,cpp} include + membre + init + destroy
- CMakeLists.txt source list entry
- Config keys render.water.level/grid_resolution/grid_half_size

Préalable à M100.14 qui introduit la nouvelle WaterPass FG-intégrée.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: WaterMeshGpu CPU helper (`BuildDrawInfos`) + 3 tests

**Objectif :** Helper pure-CPU `BuildDrawInfos(scene, outVerts, outIdx, outDrawInfos)` qui transforme une `WaterScene` en arrays vertex (28 B/vertex) + indices + table d'instances. Testable sans Vulkan device.

**Files:**
- Create: `engine/render/WaterMeshGpu.h`
- Create: `engine/render/WaterMeshGpu.cpp`
- Create: `engine/render/tests/WaterMeshGpuTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire `engine/render/WaterMeshGpu.h` (squelette public, sans Vulkan encore)**

```cpp
// engine/render/WaterMeshGpu.h
#pragma once

#include "engine/world/water/WaterSurfaces.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Vertex format pour les meshes d'eau M100.14 (28 B = pos3 + uv2 + flowDir2).
	/// Distinct de `engine::world::water::WaterVertex` (M100.13, position seule, 12 B)
	/// qui est le format CPU-only produit par WaterMeshBuilder.
	struct WaterVertex
	{
		float position[3];
		float uv[2];
		float flowDir[2];
	};
	static_assert(sizeof(WaterVertex) == 28, "WaterVertex must be 28 bytes");

	/// Info de draw call par instance (lac OU rivière).
	/// `paramsIndex` est unifié :
	///   0..N_lakes-1                            = lacs (index dans `scene.lakes`)
	///   N_lakes..N_lakes+N_rivers-1             = rivières (offset = paramsIndex - N_lakes dans `scene.rivers`)
	struct WaterInstanceDrawInfo
	{
		uint32_t firstIndex;   ///< Offset dans IBO global
		uint32_t indexCount;   ///< Nombre d'indices pour cette instance
		int32_t  vertexOffset; ///< Base vertex pour cette instance
		uint32_t paramsIndex;  ///< Voir doc struct
	};

	/// Helper CPU testable sans device : transforme une WaterScene en
	/// vertex + index arrays + drawInfos. Lacs en tête, rivières ensuite.
	/// \param outVertices Concatène 7 floats par vertex (position3 + uv2 + flowDir2).
	/// \param outIndices  Indices uint32_t globaux ; chaque instance lit
	///                    [firstIndex, firstIndex+indexCount) en y ajoutant vertexOffset.
	/// \param outDrawInfos Une entrée par lake puis par river.
	void BuildDrawInfos(const engine::world::water::WaterScene& scene,
		std::vector<float>& outVertices,
		std::vector<uint32_t>& outIndices,
		std::vector<WaterInstanceDrawInfo>& outDrawInfos);

	/// Buffer GPU contenant tous les meshes d'eau (lakes + rivers concaténés).
	/// API GPU complète ajoutée en Task 3.
	class WaterMeshGpu final
	{
	public:
		WaterMeshGpu() = default;
		WaterMeshGpu(const WaterMeshGpu&) = delete;
		WaterMeshGpu& operator=(const WaterMeshGpu&) = delete;

		// API GPU (Init/Rebuild/Destroy) en Task 3.
		// Pour l'instant : accesseurs pour les drawInfos calculés CPU.
		const std::vector<WaterInstanceDrawInfo>& GetDrawInfos() const { return m_drawInfos; }
		size_t GetInstanceCount() const { return m_drawInfos.size(); }

	private:
		std::vector<WaterInstanceDrawInfo> m_drawInfos;
		// Champs Vulkan ajoutés en Task 3.
	};
}
```

- [ ] **Step 2 : Écrire `engine/render/tests/WaterMeshGpuTests.cpp` (3 tests, qui doivent ÉCHOUER)**

```cpp
// engine/render/tests/WaterMeshGpuTests.cpp
#include "engine/render/WaterMeshGpu.h"

#include <cstdio>
#include <cstring>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::water::LakeInstance;
	using engine::world::water::RiverInstance;
	using engine::world::water::RiverNode;
	using engine::world::water::WaterScene;
	using engine::math::Vec3;
	using engine::render::BuildDrawInfos;
	using engine::render::WaterInstanceDrawInfo;

	void Test_BuildDrawInfos_EmptyScene_ZeroInstances()
	{
		WaterScene scene;
		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);
		REQUIRE(verts.empty());
		REQUIRE(idx.empty());
		REQUIRE(infos.empty());
	}

	void Test_BuildDrawInfos_OneLake_OneRiver_ProducesTwoInfos()
	{
		WaterScene scene;
		// Lac triangle simple (CCW vu du dessus)
		LakeInstance lake;
		lake.name = "lake0";
		lake.polygon = { Vec3{0,0,0}, Vec3{10,0,0}, Vec3{5,0,10} };
		lake.bottomColor = Vec3{ 0.1f, 0.2f, 0.3f };
		lake.turbidity = 0.5f;
		lake.waterLevelY = 0.0f;
		scene.lakes.push_back(std::move(lake));

		// Rivière 2 nœuds
		RiverInstance river;
		river.name = "river0";
		RiverNode n0; n0.position = Vec3{0,0,20}; n0.width = 1.0f;
		RiverNode n1; n1.position = Vec3{20,0,20}; n1.width = 1.0f;
		river.nodes = { n0, n1 };
		river.bottomColor = Vec3{ 0.0f, 0.1f, 0.2f };
		river.turbidity = 0.4f;
		river.flowSpeed = 0.5f;
		scene.rivers.push_back(std::move(river));

		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		REQUIRE(infos.size() == 2);
		REQUIRE(infos[0].paramsIndex == 0);  // Lake [0]
		REQUIRE(infos[1].paramsIndex == 1);  // River [0] : index unifié N_lakes + 0 = 1
		// Lac triangle : 3 indices, 3 vertices
		REQUIRE(infos[0].indexCount == 3);
		REQUIRE(infos[0].vertexOffset == 0);
		// Rivière 2 noeuds : 1 segment = 2 triangles = 6 indices, 4 vertices
		REQUIRE(infos[1].indexCount == 6);
		REQUIRE(infos[1].vertexOffset == 3);
		// Verts : (3 lake + 4 river) * 7 floats / vertex = 49 floats
		REQUIRE(verts.size() == 49u);
		// Idx : 3 lake + 6 river = 9 indices
		REQUIRE(idx.size() == 9u);
	}

	void Test_BuildDrawInfos_ParamsIndexOrdering_LakesFirst()
	{
		WaterScene scene;
		LakeInstance lake1, lake2;
		lake1.name = "lake1";
		lake1.polygon = { Vec3{0,0,0}, Vec3{1,0,0}, Vec3{0,0,1} };
		lake1.waterLevelY = 0.0f;
		lake2.name = "lake2";
		lake2.polygon = { Vec3{2,0,0}, Vec3{3,0,0}, Vec3{2,0,1} };
		lake2.waterLevelY = 0.0f;
		scene.lakes = { lake1, lake2 };

		RiverInstance r1;
		r1.name = "r1";
		RiverNode rn0; rn0.position = Vec3{0,0,5}; rn0.width = 1.0f;
		RiverNode rn1; rn1.position = Vec3{5,0,5}; rn1.width = 1.0f;
		r1.nodes = { rn0, rn1 };
		scene.rivers = { r1 };

		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		REQUIRE(infos.size() == 3);
		// Index unifié : lakes en tête (0..1), rivers ensuite (2).
		REQUIRE(infos[0].paramsIndex == 0);
		REQUIRE(infos[1].paramsIndex == 1);
		REQUIRE(infos[2].paramsIndex == 2);
		// Vertex offsets monotones croissants (concaténation lake1 | lake2 | river0).
		REQUIRE(infos[0].vertexOffset == 0);
		REQUIRE(infos[1].vertexOffset > infos[0].vertexOffset);
		REQUIRE(infos[2].vertexOffset > infos[1].vertexOffset);
	}
}

int main()
{
	Test_BuildDrawInfos_EmptyScene_ZeroInstances();
	Test_BuildDrawInfos_OneLake_OneRiver_ProducesTwoInfos();
	Test_BuildDrawInfos_ParamsIndexOrdering_LakesFirst();

	if (g_failed == 0)
	{
		std::printf("All WaterMeshGpu CPU tests passed.\n");
		return 0;
	}
	std::fprintf(stderr, "%d test(s) failed.\n", g_failed);
	return 1;
}
```

- [ ] **Step 3 : Vérification du fail (deferred — build CI)**

Local : impossible (CMake/MSBuild absents). En CI, le test devrait échouer avec un linker error (BuildDrawInfos non défini) ou un assert symbol error. C'est le red.

- [ ] **Step 4 : Implémenter `BuildDrawInfos` dans `engine/render/WaterMeshGpu.cpp`**

```cpp
// engine/render/WaterMeshGpu.cpp
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterMeshBuilder.h"
#include "engine/core/Log.h"

#include <cmath>
#include <cstring>

namespace engine::render
{
	namespace
	{
		// UV pour un lac : projection top-down (XZ) normalisée par la BBox du polygone.
		// Cela garantit des UV [0..1] cohérents quelle que soit la taille du lac.
		void EmitLakeVertices(const engine::world::water::LakeInstance& lake,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
			// Calcule la BBox XZ du polygone.
			float minX = lake.polygon[0].x, maxX = lake.polygon[0].x;
			float minZ = lake.polygon[0].z, maxZ = lake.polygon[0].z;
			for (const auto& p : lake.polygon)
			{
				minX = std::fmin(minX, p.x); maxX = std::fmax(maxX, p.x);
				minZ = std::fmin(minZ, p.z); maxZ = std::fmax(maxZ, p.z);
			}
			const float dx = std::fmax(1e-3f, maxX - minX);
			const float dz = std::fmax(1e-3f, maxZ - minZ);

			for (const auto& v : cpuMesh.vertices)
			{
				outVerts.push_back(v.position.x);
				outVerts.push_back(v.position.y);
				outVerts.push_back(v.position.z);
				outVerts.push_back((v.position.x - minX) / dx);  // u
				outVerts.push_back((v.position.z - minZ) / dz);  // v
				outVerts.push_back(0.0f);                         // flowDir.x = 0 (lac)
				outVerts.push_back(0.0f);                         // flowDir.y = 0 (lac)
			}
		}

		// UV pour une rivière : u le long du flow, v perpendiculaire.
		// Le ribbon mesh produit par BuildRiverMesh émet 2 vertices par node
		// (gauche/droite). On peut donc dériver u depuis l'index pair/impair.
		void EmitRiverVertices(const engine::world::water::RiverInstance& river,
			const engine::world::water::WaterMeshCpu& cpuMesh,
			std::vector<float>& outVerts)
		{
			// Flow direction par segment, depuis ComputeFlowDirections (M100.13).
			const auto flows = engine::world::water::ComputeFlowDirections(river);

			// Le ribbon BuildRiverMesh émet vertices dans l'ordre :
			//   node 0 left, node 0 right, node 1 left, node 1 right, ..., node N-1 left, node N-1 right.
			// Soit 2 * N_nodes vertices, où N_nodes = river.nodes.size().
			// Le vertex 2*i est "left" du node i, le vertex 2*i+1 est "right".
			const size_t nNodes = river.nodes.size();
			for (size_t vi = 0; vi < cpuMesh.vertices.size(); ++vi)
			{
				const auto& v = cpuMesh.vertices[vi];
				const size_t nodeIdx = vi / 2;
				const bool isLeft = (vi % 2) == 0;

				// u = nodeIdx / (nNodes - 1) (longueur le long du flow)
				const float u = (nNodes > 1) ? static_cast<float>(nodeIdx) / static_cast<float>(nNodes - 1) : 0.0f;
				const float vCoord = isLeft ? 0.0f : 1.0f;

				// flowDir : utilise le flow du segment courant (clamp au dernier pour le dernier node).
				const size_t segIdx = (nodeIdx < flows.size()) ? nodeIdx : (flows.empty() ? 0 : flows.size() - 1);
				const float fx = flows.empty() ? 0.0f : flows[segIdx].x;
				const float fz = flows.empty() ? 0.0f : flows[segIdx].z;

				outVerts.push_back(v.position.x);
				outVerts.push_back(v.position.y);
				outVerts.push_back(v.position.z);
				outVerts.push_back(u);
				outVerts.push_back(vCoord);
				outVerts.push_back(fx);
				outVerts.push_back(fz);
			}
		}
	} // namespace

	void BuildDrawInfos(const engine::world::water::WaterScene& scene,
		std::vector<float>& outVertices,
		std::vector<uint32_t>& outIndices,
		std::vector<WaterInstanceDrawInfo>& outDrawInfos)
	{
		outVertices.clear();
		outIndices.clear();
		outDrawInfos.clear();

		uint32_t globalParamIdx = 0;

		// 1) Lacs en tête.
		for (const auto& lake : scene.lakes)
		{
			engine::world::water::WaterMeshCpu cpuMesh;
			std::string err;
			if (!engine::world::water::BuildLakeMesh(lake, cpuMesh, err))
			{
				LOG_WARN(Render, "[WaterMeshGpu] BuildLakeMesh failed for '{}': {}", lake.name, err);
				++globalParamIdx;
				continue;
			}
			if (cpuMesh.indices.empty()) { ++globalParamIdx; continue; }

			const int32_t baseVertex = static_cast<int32_t>(outVertices.size() / 7);
			const uint32_t firstIndex = static_cast<uint32_t>(outIndices.size());

			EmitLakeVertices(lake, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex = firstIndex;
			info.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);

			++globalParamIdx;
		}

		// 2) Rivières ensuite.
		for (const auto& river : scene.rivers)
		{
			engine::world::water::WaterMeshCpu cpuMesh;
			std::string err;
			if (!engine::world::water::BuildRiverMesh(river, cpuMesh, err))
			{
				LOG_WARN(Render, "[WaterMeshGpu] BuildRiverMesh failed for '{}': {}", river.name, err);
				++globalParamIdx;
				continue;
			}
			if (cpuMesh.indices.empty()) { ++globalParamIdx; continue; }

			const int32_t baseVertex = static_cast<int32_t>(outVertices.size() / 7);
			const uint32_t firstIndex = static_cast<uint32_t>(outIndices.size());

			EmitRiverVertices(river, cpuMesh, outVertices);
			for (uint32_t idx : cpuMesh.indices) outIndices.push_back(idx);

			WaterInstanceDrawInfo info{};
			info.firstIndex = firstIndex;
			info.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());
			info.vertexOffset = baseVertex;
			info.paramsIndex = globalParamIdx;
			outDrawInfos.push_back(info);

			++globalParamIdx;
		}
	}
}
```

- [ ] **Step 5 : Ajouter le test executable à `CMakeLists.txt`**

Trouver le bloc d'enregistrement du `water_mesh_builder_tests` (cf. `CMakeLists.txt:1005-1011`) et ajouter juste après (préserver le pattern à l'identique) :

```cmake
  add_executable(water_mesh_gpu_tests engine/render/tests/WaterMeshGpuTests.cpp)
  target_include_directories(water_mesh_gpu_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_mesh_gpu_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_mesh_gpu_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_mesh_gpu_tests COMMAND water_mesh_gpu_tests)
```

Et ajouter `engine/render/WaterMeshGpu.cpp` à la liste des sources de `engine_core` (chercher l'entrée `engine/render/UnderwaterPass.cpp` et ajouter juste après alphabétiquement).

- [ ] **Step 6 : Commit**

```bash
git add engine/render/WaterMeshGpu.h engine/render/WaterMeshGpu.cpp \
        engine/render/tests/WaterMeshGpuTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(render): WaterMeshGpu helper CPU BuildDrawInfos (M100.14 Task 2)

Helper testable sans Vulkan device : transforme une WaterScene
(M100.13) en arrays vertex (28 B = pos3+uv2+flowDir2) + indices +
table d'instances. Lacs en tête (paramsIndex 0..N_lakes-1), rivières
ensuite (N_lakes..N_total-1).

UV lac : projection top-down XZ normalisée par BBox du polygone.
UV rivière : u le long du flow (interpolation node), v perpendiculaire
(0=left, 1=right). flowDir per-vertex depuis ComputeFlowDirections.

3 tests CPU (REQUIRE maison) :
- empty scene → 0 instance
- 1 lake + 1 river → 2 infos cohérents (paramsIndex 0, 1)
- 2 lakes + 1 river → ordre lakes-puis-river, vertexOffsets monotones

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: WaterMeshGpu GPU (`Init` / `Rebuild` / `Destroy` avec VMA staging)

**Objectif :** Ajouter l'API Vulkan complète à `WaterMeshGpu` : Init (alloue buffers vides), Rebuild (upload via staging buffer host-visible), Destroy. Pas de test direct (nécessite GPU device) — la logique CPU est déjà couverte par Task 2.

**Files:**
- Modify: `engine/render/WaterMeshGpu.h`
- Modify: `engine/render/WaterMeshGpu.cpp`

- [ ] **Step 1 : Étendre `WaterMeshGpu.h` avec l'API GPU**

Remplacer la section `class WaterMeshGpu` par :

```cpp
	/// Buffer GPU contenant tous les meshes d'eau (lakes + rivers concaténés).
	/// Reconstruit à la demande depuis une WaterScene CPU via VMA staging.
	class WaterMeshGpu final
	{
	public:
		WaterMeshGpu() = default;
		WaterMeshGpu(const WaterMeshGpu&) = delete;
		WaterMeshGpu& operator=(const WaterMeshGpu&) = delete;

		/// Initialise l'objet. Pas d'allocation buffer ici — Rebuild s'en charge.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator);

		/// Reconstruit VBO/IBO depuis la scene. Réalloue si capacité insuffisante,
		/// réutilise l'allocation existante sinon. Retourne false en cas d'erreur
		/// (logue WARN, garde l'état précédent intact).
		///
		/// \param transferPool  Command pool sur la queue transfer (ou graphics).
		/// \param transferQueue Queue de soumission (sera attendue via fence).
		/// \return true si l'upload a réussi.
		bool Rebuild(VkCommandPool transferPool, VkQueue transferQueue,
		             const engine::world::water::WaterScene& scene);

		/// Détruit toutes les ressources Vulkan. Safe sur état non-init.
		void Destroy();

		VkBuffer GetVertexBuffer() const { return m_vbo; }
		VkBuffer GetIndexBuffer()  const { return m_ibo; }
		const std::vector<WaterInstanceDrawInfo>& GetDrawInfos() const { return m_drawInfos; }
		size_t GetInstanceCount() const { return m_drawInfos.size(); }
		bool IsValid() const { return m_vbo != VK_NULL_HANDLE && m_ibo != VK_NULL_HANDLE; }

	private:
		bool EnsureCapacity(VkDeviceSize newVboSize, VkDeviceSize newIboSize);

		VkDevice         m_device         = VK_NULL_HANDLE;
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		void*            m_vmaAllocator   = nullptr;  // VmaAllocator (opaque)

		VkBuffer       m_vbo            = VK_NULL_HANDLE;
		void*          m_vboAllocation  = nullptr;  // VmaAllocation
		VkDeviceSize   m_vboCapacity    = 0;

		VkBuffer       m_ibo            = VK_NULL_HANDLE;
		void*          m_iboAllocation  = nullptr;  // VmaAllocation
		VkDeviceSize   m_iboCapacity    = 0;

		std::vector<WaterInstanceDrawInfo> m_drawInfos;
	};
```

- [ ] **Step 2 : Implémenter Init/Rebuild/Destroy dans `WaterMeshGpu.cpp`**

Ajouter après `BuildDrawInfos` :

```cpp
#include <vk_mem_alloc.h>

namespace engine::render
{
	bool WaterMeshGpu::Init(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator)
	{
		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || vmaAllocator == nullptr)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] Init FAILED: invalid arguments");
			return false;
		}
		m_device         = device;
		m_physicalDevice = physicalDevice;
		m_vmaAllocator   = vmaAllocator;
		LOG_INFO(Render, "[WaterMeshGpu] Init OK");
		return true;
	}

	void WaterMeshGpu::Destroy()
	{
		if (m_vmaAllocator == nullptr) return;
		auto* allocator = static_cast<VmaAllocator>(m_vmaAllocator);

		if (m_vbo != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, m_vbo, static_cast<VmaAllocation>(m_vboAllocation));
			m_vbo = VK_NULL_HANDLE;
			m_vboAllocation = nullptr;
			m_vboCapacity = 0;
		}
		if (m_ibo != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, m_ibo, static_cast<VmaAllocation>(m_iboAllocation));
			m_ibo = VK_NULL_HANDLE;
			m_iboAllocation = nullptr;
			m_iboCapacity = 0;
		}
		m_drawInfos.clear();
		m_device = VK_NULL_HANDLE;
		m_physicalDevice = VK_NULL_HANDLE;
		m_vmaAllocator = nullptr;
		LOG_INFO(Render, "[WaterMeshGpu] Destroyed");
	}

	bool WaterMeshGpu::EnsureCapacity(VkDeviceSize newVboSize, VkDeviceSize newIboSize)
	{
		auto* allocator = static_cast<VmaAllocator>(m_vmaAllocator);

		auto allocBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
		                       VkBuffer& outBuf, void*& outAlloc) -> bool
		{
			VkBufferCreateInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size = size;
			bi.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo ai{};
			ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			VmaAllocation alloc = nullptr;
			VkResult r = vmaCreateBuffer(allocator, &bi, &ai, &outBuf, &alloc, nullptr);
			if (r != VK_SUCCESS) return false;
			outAlloc = alloc;
			return true;
		};

		if (newVboSize > m_vboCapacity)
		{
			if (m_vbo != VK_NULL_HANDLE)
				vmaDestroyBuffer(allocator, m_vbo, static_cast<VmaAllocation>(m_vboAllocation));
			m_vbo = VK_NULL_HANDLE;
			m_vboAllocation = nullptr;
			if (!allocBuffer(newVboSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vbo, m_vboAllocation))
			{
				LOG_ERROR(Render, "[WaterMeshGpu] vmaCreateBuffer (VBO) failed (size={})", newVboSize);
				m_vboCapacity = 0;
				return false;
			}
			m_vboCapacity = newVboSize;
		}
		if (newIboSize > m_iboCapacity)
		{
			if (m_ibo != VK_NULL_HANDLE)
				vmaDestroyBuffer(allocator, m_ibo, static_cast<VmaAllocation>(m_iboAllocation));
			m_ibo = VK_NULL_HANDLE;
			m_iboAllocation = nullptr;
			if (!allocBuffer(newIboSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_ibo, m_iboAllocation))
			{
				LOG_ERROR(Render, "[WaterMeshGpu] vmaCreateBuffer (IBO) failed (size={})", newIboSize);
				m_iboCapacity = 0;
				return false;
			}
			m_iboCapacity = newIboSize;
		}
		return true;
	}

	bool WaterMeshGpu::Rebuild(VkCommandPool transferPool, VkQueue transferQueue,
	                           const engine::world::water::WaterScene& scene)
	{
		if (m_device == VK_NULL_HANDLE || m_vmaAllocator == nullptr)
		{
			LOG_ERROR(Render, "[WaterMeshGpu] Rebuild FAILED: not initialised");
			return false;
		}

		std::vector<float> verts;
		std::vector<uint32_t> idx;
		std::vector<WaterInstanceDrawInfo> infos;
		BuildDrawInfos(scene, verts, idx, infos);

		// Cas vide : on conserve les buffers (sans les détruire) mais drawInfos = empty.
		// Record() fera no-op si GetInstanceCount() == 0.
		if (verts.empty() || idx.empty())
		{
			m_drawInfos.clear();
			return true;
		}

		const VkDeviceSize vboBytes = verts.size() * sizeof(float);
		const VkDeviceSize iboBytes = idx.size() * sizeof(uint32_t);

		if (!EnsureCapacity(vboBytes, iboBytes))
			return false;

		auto* allocator = static_cast<VmaAllocator>(m_vmaAllocator);

		// Staging buffer host-visible
		VkBuffer       staging       = VK_NULL_HANDLE;
		VmaAllocation  stagingAlloc  = nullptr;
		const VkDeviceSize stagingSize = vboBytes + iboBytes;
		{
			VkBufferCreateInfo bi{};
			bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bi.size = stagingSize;
			bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VmaAllocationCreateInfo ai{};
			ai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
			ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
			VmaAllocationInfo allocInfo{};
			if (vmaCreateBuffer(allocator, &bi, &ai, &staging, &stagingAlloc, &allocInfo) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterMeshGpu] staging vmaCreateBuffer failed (size={})", stagingSize);
				return false;
			}
			std::memcpy(allocInfo.pMappedData, verts.data(), vboBytes);
			std::memcpy(static_cast<char*>(allocInfo.pMappedData) + vboBytes, idx.data(), iboBytes);
		}

		// Command buffer pour copier staging → device-local
		VkCommandBufferAllocateInfo cmdAlloc{};
		cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAlloc.commandPool = transferPool;
		cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdAlloc.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(m_device, &cmdAlloc, &cmd) != VK_SUCCESS)
		{
			vmaDestroyBuffer(allocator, staging, stagingAlloc);
			LOG_ERROR(Render, "[WaterMeshGpu] vkAllocateCommandBuffers failed");
			return false;
		}

		VkCommandBufferBeginInfo beg{};
		beg.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beg.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(cmd, &beg);

		VkBufferCopy copyVbo{}; copyVbo.size = vboBytes; copyVbo.srcOffset = 0; copyVbo.dstOffset = 0;
		vkCmdCopyBuffer(cmd, staging, m_vbo, 1, &copyVbo);

		VkBufferCopy copyIbo{}; copyIbo.size = iboBytes; copyIbo.srcOffset = vboBytes; copyIbo.dstOffset = 0;
		vkCmdCopyBuffer(cmd, staging, m_ibo, 1, &copyIbo);

		vkEndCommandBuffer(cmd);

		VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VkFence fence = VK_NULL_HANDLE;
		vkCreateFence(m_device, &fci, nullptr, &fence);

		VkSubmitInfo sub{};
		sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		sub.commandBufferCount = 1;
		sub.pCommandBuffers = &cmd;
		vkQueueSubmit(transferQueue, 1, &sub, fence);
		vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);

		vkDestroyFence(m_device, fence, nullptr);
		vkFreeCommandBuffers(m_device, transferPool, 1, &cmd);
		vmaDestroyBuffer(allocator, staging, stagingAlloc);

		m_drawInfos = std::move(infos);
		LOG_INFO(Render, "[WaterMeshGpu] Rebuilt ({} instances, vbo={} B, ibo={} B)",
		         m_drawInfos.size(), static_cast<long long>(vboBytes), static_cast<long long>(iboBytes));
		return true;
	}
}
```

- [ ] **Step 3 : Pas de test à ajouter** — la logique CPU (`BuildDrawInfos`) est déjà testée par Task 2. Init/Rebuild/Destroy nécessitent un device Vulkan, validation différée à GPU CI.

- [ ] **Step 4 : Commit**

```bash
git add engine/render/WaterMeshGpu.h engine/render/WaterMeshGpu.cpp
git commit -m "$(cat <<'EOF'
feat(render): WaterMeshGpu API GPU Init/Rebuild/Destroy via VMA (M100.14 Task 3)

Allocation device-local des VBO/IBO via VMA, upload via staging buffer
host-visible mappé. EnsureCapacity réalloue uniquement quand nécessaire
(évite churn quand l'utilisateur édite live).

Rebuild() :
1. BuildDrawInfos (CPU)
2. EnsureCapacity (réalloue si > capacité)
3. Staging buffer mapped + memcpy
4. vkCmdCopyBuffer × 2 (vbo + ibo) en command buffer one-shot
5. Submit avec fence + wait
6. Cleanup staging

Destroy : vmaDestroyBuffer + reset state.

Validation déférée à GPU CI (impossible en local sans device).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: WaterPassPushConstants struct + 2 tests offsetof

**Objectif :** Définir le struct `WaterPassPushConstants` (128 B exacts) et vérifier le layout via `static_assert(sizeof) == 128` + tests offsetof par champ. Ce sera la table de référence pour les shaders et `WaterPass::Record`.

**Files:**
- Create: `engine/render/WaterPass.h` (squelette public, sans Init/Record encore)
- Create: `engine/render/tests/WaterPassTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire `engine/render/WaterPass.h` (squelette)**

```cpp
// engine/render/WaterPass.h
#pragma once

#include "engine/render/FrameGraph.h"
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterSurfaces.h"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Push constants par-instance pour la passe water (128 B exacts).
	/// Layout aligné std140 (les vec3 prennent 16 B avec padding).
	/// Offsets vérifiés par tests offsetof — toute modification doit aussi
	/// mettre à jour le layout GLSL `push_constant` correspondant dans
	/// engine/render/shaders/water.vert + water.frag.
	struct WaterPassPushConstants
	{
		float viewProj[16];        // offset   0, size 64
		float cameraPos[3];        // offset  64, size 12
		float timeSeconds;         // offset  76, size  4
		float bottomColor[3];      // offset  80, size 12
		float turbidity;           // offset  92, size  4
		float flowDirection[2];    // offset  96, size  8
		float flowSpeed;           // offset 104, size  4
		float refractionAmount;    // offset 108, size  4
		float fresnelPower;        // offset 112, size  4
		float reflectionStrength;  // offset 116, size  4
		float screenSize[2];       // offset 120, size  8
	};
	static_assert(sizeof(WaterPassPushConstants) == 128, "WaterPassPushConstants must be exactly 128 bytes");

	// API complète Init/Record/Destroy ajoutée en Tasks 5-6.
	class WaterPass final
	{
	public:
		WaterPass() = default;
		WaterPass(const WaterPass&) = delete;
		WaterPass& operator=(const WaterPass&) = delete;

		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		// Champs Vulkan complets ajoutés en Tasks 5-6.
	};
}
```

- [ ] **Step 2 : Écrire `engine/render/tests/WaterPassTests.cpp` (2 tests, qui doivent passer immédiatement)**

```cpp
// engine/render/tests/WaterPassTests.cpp
#include "engine/render/WaterPass.h"

#include <cstddef>
#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::render::WaterPassPushConstants;

	void Test_WaterPassPushConstants_Is128Bytes()
	{
		REQUIRE(sizeof(WaterPassPushConstants) == 128);
	}

	void Test_WaterPassPushConstants_FieldOffsets_MatchSpec()
	{
		using PC = WaterPassPushConstants;
		REQUIRE(offsetof(PC, viewProj)            ==   0);
		REQUIRE(offsetof(PC, cameraPos)           ==  64);
		REQUIRE(offsetof(PC, timeSeconds)         ==  76);
		REQUIRE(offsetof(PC, bottomColor)         ==  80);
		REQUIRE(offsetof(PC, turbidity)           ==  92);
		REQUIRE(offsetof(PC, flowDirection)       ==  96);
		REQUIRE(offsetof(PC, flowSpeed)           == 104);
		REQUIRE(offsetof(PC, refractionAmount)    == 108);
		REQUIRE(offsetof(PC, fresnelPower)        == 112);
		REQUIRE(offsetof(PC, reflectionStrength)  == 116);
		REQUIRE(offsetof(PC, screenSize)          == 120);
	}
}

int main()
{
	Test_WaterPassPushConstants_Is128Bytes();
	Test_WaterPassPushConstants_FieldOffsets_MatchSpec();

	if (g_failed == 0)
	{
		std::printf("All WaterPass layout tests passed.\n");
		return 0;
	}
	std::fprintf(stderr, "%d test(s) failed.\n", g_failed);
	return 1;
}
```

Note : le test devrait passer du premier coup grâce au `static_assert`. Le test runtime offsetof valide en plus que les **offsets exacts** sont conformes (utile si un futur dev ajoute du padding accidentel via réordering).

- [ ] **Step 3 : Ajouter test executable + WaterPass.cpp à CMakeLists.txt**

Dans le bloc test executables, après `water_mesh_gpu_tests` :

```cmake
  add_executable(water_pass_tests engine/render/tests/WaterPassTests.cpp)
  target_include_directories(water_pass_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_pass_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_pass_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_pass_tests COMMAND water_pass_tests)
```

(Note : `WaterPass.cpp` sera ajouté à `engine_core` en Task 5, pas encore ici. Pour cette Task, le header seul suffit puisqu'on teste juste les offsets.)

- [ ] **Step 4 : Commit**

```bash
git add engine/render/WaterPass.h engine/render/tests/WaterPassTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(render): WaterPassPushConstants struct 128 B + tests offsetof (M100.14 Task 4)

Struct par-instance utilisé par WaterPass::Record (push constants Vulkan).
Layout aligné std140 (vec3 + float pour utiliser les 16 B sans waste).

Offsets vérifiés par 2 tests :
- sizeof(PC) == 128 (compile-time + runtime)
- offsetof par champ : 0, 64, 76, 80, 92, 96, 104, 108, 112, 116, 120

Toute modification du struct doit aussi mettre à jour le layout
push_constant des shaders water.vert/frag (Task 7).

WaterPass.cpp sera ajouté en Task 5 (Init render pass + descriptors + pipeline).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: WaterPass.h + WaterPass.cpp Init (render pass + descriptors + pipeline)

**Objectif :** Compléter `WaterPass.h` avec l'API Init et écrire l'implémentation Init dans `WaterPass.cpp`. Pattern strict miroir de `UnderwaterPass::Init` (cf. `engine/render/UnderwaterPass.cpp:35-...`).

**Files:**
- Modify: `engine/render/WaterPass.h`
- Create: `engine/render/WaterPass.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Étendre `WaterPass.h` avec l'API Init/Record/Destroy**

Remplacer la section `class WaterPass` complète par :

```cpp
	class WaterPass final
	{
	public:
		WaterPass() = default;
		WaterPass(const WaterPass&) = delete;
		WaterPass& operator=(const WaterPass&) = delete;

		/// Crée render pass + descriptor layout/pool + samplers + pipeline.
		/// \param normalMapView    Tile normale water (8-bit) ; VK_NULL_HANDLE → init échoue.
		/// \param skyboxCubeView   Skybox cube pour fallback réflexion ; VK_NULL_HANDLE → init échoue.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
			VkFormat sceneColorHDRFormat,
			const uint32_t* vertSpirv, size_t vertWordCount,
			const uint32_t* fragSpirv, size_t fragWordCount,
			VkImageView normalMapView, VkSampler normalMapSampler,
			VkImageView skyboxCubeView, VkSampler skyboxSampler,
			uint32_t maxFrames = 2,
			VkPipelineCache pipelineCache = VK_NULL_HANDLE);

		/// Enregistre la passe : begin render pass + bind pipeline + bind vertex/index +
		/// loop drawInfos avec push constants par-instance + draw indexed.
		///
		/// \param idSceneColorIn   Resource ID SceneColor_HDR (déclaré SampledRead par le caller).
		/// \param idSceneDepth    Resource ID SceneDepth (déclaré SampledRead par le caller).
		/// \param idSceneColorOut Resource ID SceneColor_HDR_PostWater (déclaré ColorWrite par le caller).
		/// \param mesh            Buffer GPU contenant VBO/IBO + drawInfos.
		/// \param paramsBase      Push constants partagés (viewProj, cameraPos, time, screenSize).
		///                        Les champs per-instance (bottomColor, turbidity, flowDirection,
		///                        flowSpeed, refractionAmount, fresnelPower, reflectionStrength)
		///                        sont écrasés par Record selon scene.lakes/rivers[paramsIndex].
		/// \param scene           WaterScene pour récupérer les params per-instance.
		/// \param frameIndex      0..maxFrames-1.
		void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
			VkExtent2D extent,
			ResourceId idSceneColorIn,
			ResourceId idSceneDepth,
			ResourceId idSceneColorOut,
			const WaterMeshGpu& mesh,
			const WaterPassPushConstants& paramsBase,
			const engine::world::water::WaterScene& scene,
			uint32_t frameIndex);

		void Destroy(VkDevice device);
		void InvalidateFramebufferCache(VkDevice device);
		bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

	private:
		struct FramebufferKey
		{
			VkImageView outputView = VK_NULL_HANDLE;
			uint32_t width = 0;
			uint32_t height = 0;
			bool operator==(const FramebufferKey& o) const noexcept
			{
				return outputView == o.outputView && width == o.width && height == o.height;
			}
		};
		struct FramebufferKeyHash
		{
			size_t operator()(const FramebufferKey& k) const noexcept
			{
				const size_t hView = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.outputView));
				const size_t hW = std::hash<uint32_t>{}(k.width);
				const size_t hH = std::hash<uint32_t>{}(k.height);
				return hView ^ (hW + 0x9e3779b9u) ^ (hH + 0x85ebca6bu);
			}
		};

		VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
		VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
		VkPipeline            m_pipeline            = VK_NULL_HANDLE;
		VkSampler             m_sceneColorSampler   = VK_NULL_HANDLE;  // Linear clamp
		VkSampler             m_sceneDepthSampler   = VK_NULL_HANDLE;  // Nearest clamp

		// Vues externes mémorisées au Init (pour bindWriteDescriptorSet en Record).
		VkImageView m_normalMapView   = VK_NULL_HANDLE;
		VkSampler   m_normalMapSampler = VK_NULL_HANDLE;
		VkImageView m_skyboxCubeView  = VK_NULL_HANDLE;
		VkSampler   m_skyboxSampler   = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> m_descriptorSets;  // 1 par frame
		std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> m_framebufferCache;
		uint32_t m_maxFrames = 2;
	};
```

- [ ] **Step 2 : Écrire `engine/render/WaterPass.cpp` — Init**

```cpp
// engine/render/WaterPass.cpp
#include "engine/render/WaterPass.h"
#include "engine/render/PipelineCache.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <array>
#include <cstring>

namespace engine::render
{
	namespace
	{
		VkShaderModule CreateShaderModule(VkDevice device, const uint32_t* code, size_t wordCount)
		{
			VkShaderModuleCreateInfo info{};
			info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			info.codeSize = wordCount * sizeof(uint32_t);
			info.pCode    = code;
			VkShaderModule mod = VK_NULL_HANDLE;
			vkCreateShaderModule(device, &info, nullptr, &mod);
			return mod;
		}
	} // namespace

	bool WaterPass::Init(VkDevice device, VkPhysicalDevice /*physicalDevice*/,
		VkFormat sceneColorHDRFormat,
		const uint32_t* vertSpirv, size_t vertWordCount,
		const uint32_t* fragSpirv, size_t fragWordCount,
		VkImageView normalMapView, VkSampler normalMapSampler,
		VkImageView skyboxCubeView, VkSampler skyboxSampler,
		uint32_t maxFrames,
		VkPipelineCache pipelineCache)
	{
		if (device == VK_NULL_HANDLE || !vertSpirv || !fragSpirv
		    || vertWordCount == 0 || fragWordCount == 0
		    || normalMapView == VK_NULL_HANDLE || normalMapSampler == VK_NULL_HANDLE
		    || skyboxCubeView == VK_NULL_HANDLE || skyboxSampler == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "[WaterPass] Init FAILED: invalid arguments");
			return false;
		}

		m_maxFrames        = (maxFrames > 0) ? maxFrames : 1;
		m_normalMapView    = normalMapView;
		m_normalMapSampler = normalMapSampler;
		m_skyboxCubeView   = skyboxCubeView;
		m_skyboxSampler    = skyboxSampler;

		// 1. Render pass : 1 color attachment (SceneColor_HDR_PostWater, LOAD_OP_DONT_CARE).
		{
			VkAttachmentDescription colorAtt{};
			colorAtt.format         = sceneColorHDRFormat;
			colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
			colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // overwrite intégral via blend ou copy preview
			colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
			colorAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAtt.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments    = &colorRef;

			VkSubpassDependency dep{};
			dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
			dep.dstSubpass    = 0;
			dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			                  | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
			                  | VK_ACCESS_SHADER_READ_BIT;
			dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo rpInfo{};
			rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.attachmentCount = 1;
			rpInfo.pAttachments    = &colorAtt;
			rpInfo.subpassCount    = 1;
			rpInfo.pSubpasses      = &subpass;
			rpInfo.dependencyCount = 1;
			rpInfo.pDependencies   = &dep;

			if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateRenderPass failed");
				return false;
			}
		}

		// 2. Descriptor set layout : 4 bindings (sceneColor, sceneDepth, normalMap, skybox).
		{
			VkDescriptorSetLayoutBinding bindings[4]{};
			for (int i = 0; i < 4; ++i)
			{
				bindings[i].binding         = static_cast<uint32_t>(i);
				bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				bindings[i].descriptorCount = 1;
				bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 4;
			layoutInfo.pBindings    = bindings;
			if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateDescriptorSetLayout failed");
				return false;
			}
		}

		// 3. Descriptor pool : 4 samplers × maxFrames sets.
		{
			VkDescriptorPoolSize sizes[1]{};
			sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			sizes[0].descriptorCount = 4 * m_maxFrames;
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes    = sizes;
			poolInfo.maxSets       = m_maxFrames;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateDescriptorPool failed");
				return false;
			}
			std::vector<VkDescriptorSetLayout> layouts(m_maxFrames, m_descriptorSetLayout);
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool     = m_descriptorPool;
			allocInfo.descriptorSetCount = m_maxFrames;
			allocInfo.pSetLayouts        = layouts.data();
			m_descriptorSets.resize(m_maxFrames);
			if (vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkAllocateDescriptorSets failed");
				return false;
			}
		}

		// 4. Samplers : linear clamp (sceneColor), nearest clamp (sceneDepth).
		{
			VkSamplerCreateInfo s{};
			s.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			s.magFilter = VK_FILTER_LINEAR;
			s.minFilter = VK_FILTER_LINEAR;
			s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			s.minLod = 0.0f; s.maxLod = 0.0f;
			vkCreateSampler(device, &s, nullptr, &m_sceneColorSampler);

			s.magFilter = VK_FILTER_NEAREST;
			s.minFilter = VK_FILTER_NEAREST;
			vkCreateSampler(device, &s, nullptr, &m_sceneDepthSampler);
		}

		// 5. Pipeline layout : push constants 128 B en fragment + vertex stages.
		{
			VkPushConstantRange pcRange{};
			pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			pcRange.offset = 0;
			pcRange.size = static_cast<uint32_t>(sizeof(WaterPassPushConstants));

			VkPipelineLayoutCreateInfo li{};
			li.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			li.setLayoutCount         = 1;
			li.pSetLayouts            = &m_descriptorSetLayout;
			li.pushConstantRangeCount = 1;
			li.pPushConstantRanges    = &pcRange;
			if (vkCreatePipelineLayout(device, &li, nullptr, &m_pipelineLayout) != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreatePipelineLayout failed");
				return false;
			}
		}

		// 6. Pipeline : vertex format WaterVertex (28 B), alpha blend, depth LEQ no write, cull back.
		{
			VkShaderModule vert = CreateShaderModule(device, vertSpirv, vertWordCount);
			VkShaderModule frag = CreateShaderModule(device, fragSpirv, fragWordCount);
			if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE)
			{
				LOG_ERROR(Render, "[WaterPass] CreateShaderModule failed");
				if (vert != VK_NULL_HANDLE) vkDestroyShaderModule(device, vert, nullptr);
				if (frag != VK_NULL_HANDLE) vkDestroyShaderModule(device, frag, nullptr);
				return false;
			}

			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = vert;
			stages[0].pName  = "main";
			stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = frag;
			stages[1].pName  = "main";

			VkVertexInputBindingDescription vboBinding{};
			vboBinding.binding   = 0;
			vboBinding.stride    = 28;  // sizeof(WaterVertex)
			vboBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

			VkVertexInputAttributeDescription attribs[3]{};
			attribs[0].location = 0; attribs[0].binding = 0; attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attribs[0].offset = 0;
			attribs[1].location = 1; attribs[1].binding = 0; attribs[1].format = VK_FORMAT_R32G32_SFLOAT;    attribs[1].offset = 12;
			attribs[2].location = 2; attribs[2].binding = 0; attribs[2].format = VK_FORMAT_R32G32_SFLOAT;    attribs[2].offset = 20;

			VkPipelineVertexInputStateCreateInfo vi{};
			vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vi.vertexBindingDescriptionCount = 1;
			vi.pVertexBindingDescriptions    = &vboBinding;
			vi.vertexAttributeDescriptionCount = 3;
			vi.pVertexAttributeDescriptions  = attribs;

			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			VkPipelineViewportStateCreateInfo vp{};
			vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.scissorCount  = 1;

			VkPipelineRasterizationStateCreateInfo rs{};
			rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode    = VK_CULL_MODE_BACK_BIT;
			rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo ds{};
			ds.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable   = VK_FALSE;  // Pas de depth attachment dans cette passe (output color seul)
			ds.depthWriteEnable  = VK_FALSE;
			ds.depthCompareOp    = VK_COMPARE_OP_LESS_OR_EQUAL;

			VkPipelineColorBlendAttachmentState blend{};
			blend.blendEnable         = VK_TRUE;
			blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			blend.colorBlendOp        = VK_BLEND_OP_ADD;
			blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blend.alphaBlendOp        = VK_BLEND_OP_ADD;
			blend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
			                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			VkPipelineColorBlendStateCreateInfo cb{};
			cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			cb.attachmentCount = 1;
			cb.pAttachments    = &blend;

			VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn{};
			dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
			dyn.dynamicStateCount = 2;
			dyn.pDynamicStates    = dynStates;

			VkGraphicsPipelineCreateInfo pi{};
			pi.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pi.stageCount          = 2;
			pi.pStages             = stages;
			pi.pVertexInputState   = &vi;
			pi.pInputAssemblyState = &ia;
			pi.pViewportState      = &vp;
			pi.pRasterizationState = &rs;
			pi.pMultisampleState   = &ms;
			pi.pDepthStencilState  = &ds;
			pi.pColorBlendState    = &cb;
			pi.pDynamicState       = &dyn;
			pi.layout              = m_pipelineLayout;
			pi.renderPass          = m_renderPass;
			pi.subpass             = 0;

			VkResult r = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pi, nullptr, &m_pipeline);
			vkDestroyShaderModule(device, vert, nullptr);
			vkDestroyShaderModule(device, frag, nullptr);
			if (r != VK_SUCCESS)
			{
				LOG_ERROR(Render, "[WaterPass] vkCreateGraphicsPipelines failed: {}", static_cast<int>(r));
				return false;
			}
		}

		LOG_INFO(Render, "[WaterPass] Init OK");
		return true;
	}

	void WaterPass::Destroy(VkDevice device)
	{
		InvalidateFramebufferCache(device);
		if (m_pipeline != VK_NULL_HANDLE)            { vkDestroyPipeline(device, m_pipeline, nullptr);                 m_pipeline = VK_NULL_HANDLE; }
		if (m_pipelineLayout != VK_NULL_HANDLE)      { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);     m_pipelineLayout = VK_NULL_HANDLE; }
		if (m_descriptorPool != VK_NULL_HANDLE)      { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);     m_descriptorPool = VK_NULL_HANDLE; }
		if (m_descriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr); m_descriptorSetLayout = VK_NULL_HANDLE; }
		if (m_sceneColorSampler != VK_NULL_HANDLE)   { vkDestroySampler(device, m_sceneColorSampler, nullptr);         m_sceneColorSampler = VK_NULL_HANDLE; }
		if (m_sceneDepthSampler != VK_NULL_HANDLE)   { vkDestroySampler(device, m_sceneDepthSampler, nullptr);         m_sceneDepthSampler = VK_NULL_HANDLE; }
		if (m_renderPass != VK_NULL_HANDLE)          { vkDestroyRenderPass(device, m_renderPass, nullptr);             m_renderPass = VK_NULL_HANDLE; }
		m_descriptorSets.clear();
		LOG_INFO(Render, "[WaterPass] Destroyed");
	}

	void WaterPass::InvalidateFramebufferCache(VkDevice device)
	{
		for (auto& kv : m_framebufferCache)
		{
			if (kv.second != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device, kv.second, nullptr);
		}
		m_framebufferCache.clear();
	}

	// Record() implementation ajoutée en Task 6.
}
```

- [ ] **Step 2 : Ajouter `WaterPass.cpp` à la liste des sources `engine_core` dans `CMakeLists.txt`**

Chercher l'entrée `engine/render/UnderwaterPass.cpp` et ajouter (alphabétiquement juste après `WaterMeshGpu.cpp` ajouté en Task 2) :

```cmake
    engine/render/WaterPass.cpp
```

- [ ] **Step 3 : Commit**

```bash
git add engine/render/WaterPass.h engine/render/WaterPass.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(render): WaterPass Init render pass + descriptors + pipeline (M100.14 Task 5)

Pattern strict miroir UnderwaterPass / DecalPass.
- Render pass 1 color attachment LOAD_OP_DONT_CARE
- 4 bindings combined image sampler (sceneColor + sceneDepth + normalMap + skybox)
- Descriptor pool maxFrames sets
- 2 samplers : linear clamp (sceneColor) + nearest clamp (sceneDepth)
- Pipeline layout : push constants 128 B vertex+fragment stages
- Vertex format WaterVertex 28 B (location 0=pos3, 1=uv2, 2=flowDir2)
- Pipeline : alpha blend, depth test/write OFF, cull BACK, dynamic viewport/scissor

Destroy/InvalidateFramebufferCache aussi inclus. Record() ajouté en Task 6.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: WaterPass.cpp Record (framebuffer cache + draw loop)

**Objectif :** Ajouter `WaterPass::Record` qui begin la render pass, bind pipeline + descriptors + buffers, et boucle sur `mesh.GetDrawInfos()` en pushant les push constants per-instance avant chaque draw.

**Files:**
- Modify: `engine/render/WaterPass.cpp`

- [ ] **Step 1 : Ajouter `Record` à la fin de `WaterPass.cpp` (avant la fermeture du namespace)**

```cpp
	void WaterPass::Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
		VkExtent2D extent,
		ResourceId idSceneColorIn,
		ResourceId idSceneDepth,
		ResourceId idSceneColorOut,
		const WaterMeshGpu& mesh,
		const WaterPassPushConstants& paramsBase,
		const engine::world::water::WaterScene& scene,
		uint32_t frameIndex)
	{
		if (!IsValid()) return;
		if (mesh.GetInstanceCount() == 0) return;
		if (frameIndex >= m_maxFrames) return;

		VkImage     colorOut     = registry.getImage(idSceneColorOut);
		VkImageView colorOutView = registry.getImageView(idSceneColorOut);
		VkImageView sceneInView  = registry.getImageView(idSceneColorIn);
		VkImageView depthView    = registry.getImageView(idSceneDepth);
		if (colorOut == VK_NULL_HANDLE || colorOutView == VK_NULL_HANDLE
		    || sceneInView == VK_NULL_HANDLE || depthView == VK_NULL_HANDLE)
			return;

		// 1. Update descriptor set pour cette frame.
		{
			VkDescriptorImageInfo info[4]{};
			info[0].sampler     = m_sceneColorSampler;
			info[0].imageView   = sceneInView;
			info[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info[1].sampler     = m_sceneDepthSampler;
			info[1].imageView   = depthView;
			info[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info[2].sampler     = m_normalMapSampler;
			info[2].imageView   = m_normalMapView;
			info[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info[3].sampler     = m_skyboxSampler;
			info[3].imageView   = m_skyboxCubeView;
			info[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkWriteDescriptorSet writes[4]{};
			for (int i = 0; i < 4; ++i)
			{
				writes[i].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writes[i].dstSet           = m_descriptorSets[frameIndex];
				writes[i].dstBinding       = static_cast<uint32_t>(i);
				writes[i].descriptorCount  = 1;
				writes[i].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writes[i].pImageInfo       = &info[i];
			}
			vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
		}

		// 2. Framebuffer (cache keyé par output view + extent).
		FramebufferKey key{ colorOutView, extent.width, extent.height };
		VkFramebuffer fb = VK_NULL_HANDLE;
		auto it = m_framebufferCache.find(key);
		if (it != m_framebufferCache.end())
		{
			fb = it->second;
		}
		else
		{
			VkFramebufferCreateInfo fbi{};
			fbi.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbi.renderPass      = m_renderPass;
			fbi.attachmentCount = 1;
			fbi.pAttachments    = &colorOutView;
			fbi.width           = extent.width;
			fbi.height          = extent.height;
			fbi.layers          = 1;
			if (vkCreateFramebuffer(device, &fbi, nullptr, &fb) != VK_SUCCESS)
			{
				LOG_WARN(Render, "[WaterPass] vkCreateFramebuffer failed");
				return;
			}
			m_framebufferCache[key] = fb;
		}

		// 3. Begin render pass.
		VkRenderPassBeginInfo rpb{};
		rpb.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpb.renderPass        = m_renderPass;
		rpb.framebuffer       = fb;
		rpb.renderArea.extent = extent;
		rpb.clearValueCount   = 0;  // LOAD_OP_DONT_CARE
		vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport vp{};
		vp.x = 0; vp.y = 0;
		vp.width  = static_cast<float>(extent.width);
		vp.height = static_cast<float>(extent.height);
		vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &vp);
		VkRect2D sc{ {0,0}, extent };
		vkCmdSetScissor(cmd, 0, 1, &sc);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
		                        0, 1, &m_descriptorSets[frameIndex], 0, nullptr);

		VkBuffer vbo = mesh.GetVertexBuffer();
		VkBuffer ibo = mesh.GetIndexBuffer();
		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &zero);
		vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);

		// 4. Loop drawInfos avec push constants per-instance.
		const auto& drawInfos = mesh.GetDrawInfos();
		const uint32_t nLakes = static_cast<uint32_t>(scene.lakes.size());

		for (const auto& info : drawInfos)
		{
			WaterPassPushConstants pc = paramsBase;

			if (info.paramsIndex < nLakes)
			{
				const auto& lake = scene.lakes[info.paramsIndex];
				pc.bottomColor[0]      = lake.bottomColor.x;
				pc.bottomColor[1]      = lake.bottomColor.y;
				pc.bottomColor[2]      = lake.bottomColor.z;
				pc.turbidity           = lake.turbidity;
				pc.flowDirection[0]    = 0.0f;
				pc.flowDirection[1]    = 0.0f;
				pc.flowSpeed           = 0.0f;  // Lac : pas de flow direct (vent constant via timeSeconds)
				pc.refractionAmount    = 0.02f;
				pc.fresnelPower        = 5.0f;
				pc.reflectionStrength  = 0.5f;
			}
			else
			{
				const uint32_t riverIdx = info.paramsIndex - nLakes;
				if (riverIdx >= scene.rivers.size()) continue;
				const auto& river = scene.rivers[riverIdx];
				pc.bottomColor[0]      = river.bottomColor.x;
				pc.bottomColor[1]      = river.bottomColor.y;
				pc.bottomColor[2]      = river.bottomColor.z;
				pc.turbidity           = river.turbidity;
				// flowDirection est déjà encodé per-vertex ; on passe (1,0) en push
				// pour être consistent — le shader utilise vFlowDir interpolé.
				pc.flowDirection[0]    = 1.0f;
				pc.flowDirection[1]    = 0.0f;
				pc.flowSpeed           = river.flowSpeed;
				pc.refractionAmount    = 0.015f;
				pc.fresnelPower        = 5.0f;
				pc.reflectionStrength  = 0.4f;
			}

			vkCmdPushConstants(cmd, m_pipelineLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0, sizeof(WaterPassPushConstants), &pc);

			vkCmdDrawIndexed(cmd, info.indexCount, 1, info.firstIndex, info.vertexOffset, 0);
		}

		vkCmdEndRenderPass(cmd);
	}
```

- [ ] **Step 2 : Pas de test ajouté** — Record nécessite GPU, validation visuelle manuelle + future GPU CI.

- [ ] **Step 3 : Commit**

```bash
git add engine/render/WaterPass.cpp
git commit -m "$(cat <<'EOF'
feat(render): WaterPass Record loop drawInfos + push constants per-instance (M100.14 Task 6)

Begin render pass + bind pipeline/descriptors/buffers + loop par-instance.
Framebuffer cache keyé par (output view, width, height) — pattern DecalPass.
Update descriptor set par frameIndex (4 bindings sceneColor/sceneDepth/normalMap/skybox).

Push constants par-instance :
- Lac : bottomColor + turbidity + flowSpeed=0 + refractionAmount=0.02 + fresnel=5 + reflectionStrength=0.5
- Rivière : bottomColor + turbidity + flowSpeed scene + refractionAmount=0.015 + fresnel=5 + reflectionStrength=0.4

Le flowDirection per-vertex (encodé dans WaterVertex.flowDir, Task 2) prend
le pas sur le push constant — le shader utilise vFlowDir interpolé.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Shaders water.vert + water.frag

**Objectif :** Réécrire les shaders water (qui étaient les anciens M38) avec le nouveau layout push constant + 4 bindings + 2 octaves normales + refraction + SSR + Fresnel.

**Files:**
- Modify: `engine/render/shaders/water.vert` (CREATE — chemin canonique source absent jusqu'ici)
- Modify: `engine/render/shaders/water.frag` (CREATE)
- Modify: `game/data/shaders/water.vert` (OVERWRITE — l'ancien M38)
- Modify: `game/data/shaders/water.frag` (OVERWRITE)

**Note importante :** Le repository a la convention que `engine/render/shaders/` contient les sources canoniques (compilées en SPIR-V au build) et `game/data/shaders/` contient les artefacts copiés/compilés. Comme actuellement seul `game/data/shaders/water.{vert,frag}` existe (orphelin M38), il faut **créer** les deux versions et **synchroniser** leur contenu.

- [ ] **Step 1 : Vérifier l'existence du dossier `engine/render/shaders/`**

```bash
ls -la engine/render/shaders/ 2>&1 | head -5
```

Si le dossier n'existe pas (probable), il sera créé automatiquement à l'écriture des fichiers.

- [ ] **Step 2 : Écrire `engine/render/shaders/water.vert`**

```glsl
#version 450

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  cameraPos;
    float timeSeconds;
    vec3  bottomColor;
    float turbidity;
    vec2  flowDirection;
    float flowSpeed;
    float refractionAmount;
    float fresnelPower;
    float reflectionStrength;
    vec2  screenSize;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inFlowDir;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec2 vFlowDir;

void main()
{
    vUv       = inUv;
    vWorldPos = inPosition;
    vFlowDir  = inFlowDir;
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
}
```

- [ ] **Step 3 : Écrire `engine/render/shaders/water.frag`**

```glsl
#version 450

layout(set=0, binding=0) uniform sampler2D u_sceneColor;
layout(set=0, binding=1) uniform sampler2D u_sceneDepth;
layout(set=0, binding=2) uniform sampler2D u_normalMap;
layout(set=0, binding=3) uniform samplerCube u_skybox;

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  cameraPos;
    float timeSeconds;
    vec3  bottomColor;
    float turbidity;
    vec2  flowDirection;
    float flowSpeed;
    float refractionAmount;
    float fresnelPower;
    float reflectionStrength;
    vec2  screenSize;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec2 vFlowDir;

layout(location = 0) out vec4 fragColor;

vec3 unpackNormal(vec3 n) { return normalize(n * 2.0 - 1.0); }

// SSR mince : raymarch en screen-space, max 32 steps, fallback skybox.
vec3 ssrTrace(vec3 worldPos, vec3 reflectDir, vec3 fallback)
{
    const int   kMaxSteps  = 32;
    const float kStepSize  = 0.5;
    const float kThickness = 0.25;

    vec3 rayPos = worldPos + reflectDir * 0.05;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        rayPos += reflectDir * kStepSize;
        vec4 clip = pc.viewProj * vec4(rayPos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        if (any(lessThan(ndc.xy, vec2(-1.0))) || any(greaterThan(ndc.xy, vec2(1.0))))
            break;
        vec2 screenUv = ndc.xy * 0.5 + 0.5;
        float sampledDepth = texture(u_sceneDepth, screenUv).r;
        float rayDepthNorm = ndc.z * 0.5 + 0.5;
        if (sampledDepth < rayDepthNorm - 1e-4)
        {
            float depthDiff = rayDepthNorm - sampledDepth;
            if (depthDiff > 0.0 && depthDiff * length(rayPos - worldPos) < kThickness)
                return texture(u_sceneColor, screenUv).rgb;
        }
    }
    return fallback;
}

void main()
{
    // Flow effective : prend la direction per-vertex (rivière) ou le push constant (lac).
    vec2 flowEff = (length(vFlowDir) > 0.001) ? vFlowDir : pc.flowDirection;
    vec2 flow = flowEff * pc.flowSpeed * pc.timeSeconds;

    vec2 uv1 = vUv * 8.0  + flow;
    vec2 uv2 = vUv * 16.0 - flow * 0.5;
    vec3 n1 = unpackNormal(texture(u_normalMap, uv1).xyz);
    vec3 n2 = unpackNormal(texture(u_normalMap, uv2).xyz);
    vec3 n  = normalize(n1 + n2);

    vec2 screenUv = gl_FragCoord.xy / pc.screenSize;
    vec2 refrUv   = clamp(screenUv + n.xz * pc.refractionAmount, vec2(0.0), vec2(1.0));
    vec3 refr     = texture(u_sceneColor, refrUv).rgb;
    refr          = mix(refr, pc.bottomColor, pc.turbidity);

    vec3 viewDir    = normalize(pc.cameraPos - vWorldPos);
    vec3 surfaceN   = vec3(n.x, 1.0, n.z);
    vec3 reflectDir = reflect(-viewDir, surfaceN);
    vec3 skyFallback = texture(u_skybox, reflectDir).rgb;
    vec3 refl = ssrTrace(vWorldPos, reflectDir, skyFallback);

    float NdotV = max(0.0, dot(surfaceN, viewDir));
    float f = pow(1.0 - NdotV, pc.fresnelPower);

    vec3 color = mix(refr, refl, f * pc.reflectionStrength);
    fragColor = vec4(color, 1.0);
}
```

- [ ] **Step 4 : Synchroniser `game/data/shaders/water.vert` et `game/data/shaders/water.frag`**

Copier le même contenu dans les fichiers de `game/data/shaders/`. Le build pipeline les compilera en `.spv` (ou il y a un script qui copie depuis `engine/render/shaders/` — à vérifier dans la CI). Pour être robuste, écrire les deux paires identiques.

```bash
cp engine/render/shaders/water.vert game/data/shaders/water.vert
cp engine/render/shaders/water.frag game/data/shaders/water.frag
```

- [ ] **Step 5 : Pas de test à ajouter** — la cohérence offsetof C++ ↔ GLSL est garantie par le test Task 4 ; le rendu visuel sera validé manuellement après merge.

- [ ] **Step 6 : Commit**

```bash
git add engine/render/shaders/water.vert engine/render/shaders/water.frag \
        game/data/shaders/water.vert game/data/shaders/water.frag
git commit -m "$(cat <<'EOF'
feat(shaders): water.vert + water.frag réécrits pour M100.14 (Task 7)

Push constant block 128 B aligné std140 (offsets cohérents avec
WaterPassPushConstants C++, vérifié par tests offsetof Task 4).

water.vert : pass-through viewProj, exporte uv/worldPos/flowDir vers fragment.

water.frag :
- 2 octaves normales scrollées (uv*8 + uv*16, scroll ±0.5)
- flowEff = vFlowDir per-vertex si non nul (rivière), sinon flowDirection push (lac)
- Refraction : sample u_sceneColor à screenUv + n.xz * refractionAmount + mix bottomColor turbidity
- SSR mince : 32 raymarch steps, kStepSize=0.5 m, kThickness=0.25 m
- Fallback skybox cube quand ray hors écran ou sans hit
- Fresnel : pow(1-NdotV, fresnelPower) * reflectionStrength
- Output alpha = 1.0 (lacs/rivières opaques sur leur polygone)

Anciens shaders M38 (game/data/shaders/water.{vert,frag}) écrasés par
le nouveau contenu — synchro engine/render/shaders/ ↔ game/data/shaders/.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Engine wiring (boot + per-frame + addPass + renames downstream)

**Objectif :** Câbler `WaterPass` et `WaterMeshGpu` dans `engine/Engine.{h,cpp}` : init au boot après Lighting, rebuild GPU à chaque dirty frame, addPass Water (avec fallback Water_Passthrough si Init failed), 2 renames downstream (Bloom_Prefilter + Bloom_Combine).

**Files:**
- Modify: `engine/Engine.h`
- Modify: `engine/Engine.cpp`

- [ ] **Step 1 : Modifier `engine/Engine.h`**

Ajouter les includes près des autres render passes :

```cpp
#include "engine/render/WaterMeshGpu.h"
#include "engine/render/WaterPass.h"
#include "engine/world/water/WaterSurfaces.h"
```

Ajouter dans la classe (après les autres members liés au rendu, ex. `m_underwaterPass` ou `m_decalPass`) :

```cpp
		// M100.14 Water rendering (FG-intégré)
		engine::render::WaterPass    m_waterPass;
		engine::render::WaterMeshGpu m_waterMeshGpu;
		// Source courante : éditeur lit m_waterDocument->Get() ; client lit m_clientWaterScene.
		std::shared_ptr<engine::world::water::WaterScene> m_clientWaterScene;
		bool                                              m_waterClientSceneDirty = false;
		// Resource ID FG après ping-pong (Lighting écrit ...HDR, Water écrit ...PostWater).
		engine::render::ResourceId m_fgSceneColorHDRPostWaterId = engine::render::kInvalidResourceId;
```

- [ ] **Step 2 : `engine/Engine.cpp` — créer la ressource FG `SceneColor_HDR_PostWater`**

Localiser la création de `m_fgSceneColorHDRId` (ligne ~1132) :

```cpp
m_fgSceneColorHDRId = m_frameGraph.createImage("SceneColor_HDR", sceneColorHDRDesc);
```

Ajouter juste après (réutilise le même `sceneColorHDRDesc`) :

```cpp
m_fgSceneColorHDRPostWaterId = m_frameGraph.createImage("SceneColor_HDR_PostWater", sceneColorHDRDesc);
```

- [ ] **Step 3 : `engine/Engine.cpp` — init WaterPass au boot**

À ajouter à l'endroit où l'ancien `m_waterRenderer.Init` était (que Task 1 a supprimé), donc juste après le bloc TerrainChunkRenderer :

```cpp
// M100.14 — Water render pass (FG-intégré).
if (pipelineOk)
{
    std::vector<uint32_t> waterVert = loadSpirv("shaders/water.vert.spv");
    std::vector<uint32_t> waterFrag = loadSpirv("shaders/water.frag.spv");

    // Texture normale (placeholder gris si absente — l'eau ne sera pas animée mais ne crashe pas).
    VkImageView normalView = VK_NULL_HANDLE;
    VkSampler   normalSamp = VK_NULL_HANDLE;
    if (m_assetRegistry.HasTexture("textures/water_normal.ktx2"))
    {
        normalView = m_assetRegistry.GetTextureView("textures/water_normal.ktx2");
        normalSamp = m_assetRegistry.GetLinearClampSampler();
    }
    // Skybox cube : prendre depuis l'atmosphère de zone (peut être null si zone non chargée — Init échouera et fallback passthrough).
    VkImageView skyView = m_zoneAtmosphere.skyboxCubeView;
    VkSampler   skySamp = m_assetRegistry.GetLinearClampSampler();

    if (!waterVert.empty() && !waterFrag.empty()
        && normalView != VK_NULL_HANDLE && skyView != VK_NULL_HANDLE)
    {
        if (!m_waterPass.Init(
            m_vkDeviceContext.GetDevice(),
            m_vkDeviceContext.GetPhysicalDevice(),
            VK_FORMAT_R16G16B16A16_SFLOAT,
            waterVert.data(), waterVert.size(),
            waterFrag.data(), waterFrag.size(),
            normalView, normalSamp,
            skyView, skySamp,
            2, m_pipelineCache))
        {
            LOG_WARN(Render, "[Boot] WaterPass init failed — water rendering disabled (passthrough fallback)");
        }
    }
    else
    {
        LOG_WARN(Render, "[Boot] WaterPass : prerequisites missing (shaders={}/{}, normalMap={}, skybox={}) — passthrough fallback",
                 !waterVert.empty(), !waterFrag.empty(),
                 normalView != VK_NULL_HANDLE, skyView != VK_NULL_HANDLE);
    }

    m_waterMeshGpu.Init(
        m_vkDeviceContext.GetDevice(),
        m_vkDeviceContext.GetPhysicalDevice(),
        m_vmaAllocator);
}
```

**Note** : `m_assetRegistry.HasTexture(...)`, `m_assetRegistry.GetLinearClampSampler()`, `m_zoneAtmosphere.skyboxCubeView` et `m_vmaAllocator` sont supposés exister. Si l'API exacte diffère, adapter (le pattern à reprendre est celui utilisé par `m_underwaterPass.Init` ou `m_decalPass.Init` plus haut dans Engine.cpp).

- [ ] **Step 4 : `engine/Engine.cpp` — destroy au shutdown**

À l'endroit où l'ancien `m_waterRenderer.Destroy` était (Task 1 supprimé) — juste après les autres `Destroy()` de passes :

```cpp
m_waterPass.Destroy(m_vkDeviceContext.GetDevice());
m_waterMeshGpu.Destroy();
```

- [ ] **Step 5 : `engine/Engine.cpp` — per-frame dirty rebuild (avant `FrameGraph::execute()`)**

Localiser l'appel `m_frameGraph.execute(...)` et ajouter juste avant :

```cpp
// M100.14 — Live update WaterMeshGpu si la WaterScene est dirty.
{
    const engine::world::water::WaterScene* scene = nullptr;
    bool dirty = false;
    if (m_worldEditorExe && m_waterDocument)
    {
        scene = &m_waterDocument->Get();
        dirty = m_waterDocument->IsDirty();
    }
    else if (m_clientWaterScene)
    {
        scene = m_clientWaterScene.get();
        dirty = m_waterClientSceneDirty;
    }
    if (scene && dirty && m_waterMeshGpu.IsValid())
    {
        m_waterMeshGpu.Rebuild(m_transferCommandPool, m_transferQueue, *scene);
        if (m_worldEditorExe && m_waterDocument) m_waterDocument->ClearDirty();
        else                                      m_waterClientSceneDirty = false;
    }
}
```

**Note** : `m_transferCommandPool` et `m_transferQueue` sont supposés exister (ou prendre la queue/pool graphics si pas de queue dédiée). Adapter selon le contexte.

- [ ] **Step 6 : `engine/Engine.cpp` — addPass Water + Water_Passthrough fallback**

Localiser le `m_frameGraph.addPass("Lighting", ...)` (vers ligne 1886). Ajouter **juste après** la fermeture du addPass Lighting :

```cpp
// M100.14 — Water render pass (FG-intégré, ping-pong SceneColor_HDR → SceneColor_HDR_PostWater).
if (m_waterPass.IsValid())
{
    m_frameGraph.addPass("Water",
        [this](engine::render::PassBuilder& b) {
            b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::SampledRead);
            b.read(m_fgDepthId,                   engine::render::ImageUsage::SampledRead);
            b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::ColorWrite);
        },
        [this](VkCommandBuffer cmd, engine::render::Registry& reg) {
            const auto* scene = (m_worldEditorExe && m_waterDocument)
                ? &m_waterDocument->Get()
                : (m_clientWaterScene ? m_clientWaterScene.get() : nullptr);
            if (!scene || !m_waterMeshGpu.IsValid()) return;

            const uint32_t readIdx = m_renderReadIndex.load(std::memory_order_acquire);
            const engine::RenderState& rs = m_renderStates[readIdx];

            engine::render::WaterPassPushConstants base{};
            std::memcpy(base.viewProj, rs.viewProjMatrix.m, sizeof(float) * 16);
            base.cameraPos[0] = rs.camera.position.x;
            base.cameraPos[1] = rs.camera.position.y;
            base.cameraPos[2] = rs.camera.position.z;
            base.timeSeconds = static_cast<float>(rs.timeSecondsTotal);
            base.screenSize[0] = static_cast<float>(m_vkSwapchain.GetExtent().width);
            base.screenSize[1] = static_cast<float>(m_vkSwapchain.GetExtent().height);
            // bottomColor/turbidity/flowDirection/flowSpeed/refractionAmount/fresnelPower/reflectionStrength
            // sont écrasés par WaterPass::Record selon scene.lakes/rivers[paramsIndex].

            const uint32_t frameIdx = m_currentFrame % 2;
            m_waterPass.Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
                               m_fgSceneColorHDRId, m_fgDepthId, m_fgSceneColorHDRPostWaterId,
                               m_waterMeshGpu, base, *scene, frameIdx);
        });
}
else
{
    // Fallback : copie SceneColor_HDR → SceneColor_HDR_PostWater pour que Bloom_* trouve ses inputs.
    m_frameGraph.addPass("Water_Passthrough",
        [this](engine::render::PassBuilder& b) {
            b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::TransferSrc);
            b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::TransferDst);
        },
        [this](VkCommandBuffer cmd, engine::render::Registry& reg) {
            VkImage src = reg.getImage(m_fgSceneColorHDRId);
            VkImage dst = reg.getImage(m_fgSceneColorHDRPostWaterId);
            if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
            VkImageCopy copy{};
            copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.srcSubresource.layerCount = 1;
            copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.dstSubresource.layerCount = 1;
            VkExtent2D ext = m_vkSwapchain.GetExtent();
            copy.extent = { ext.width, ext.height, 1 };
            vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        });
}
```

**Note** : `m_renderStates`, `m_renderReadIndex`, `m_currentFrame`, `rs.timeSecondsTotal`, `rs.viewProjMatrix.m`, `rs.camera.position` sont supposés exister (cf. les autres `addPass` qui les utilisent dans Engine.cpp). Si `timeSecondsTotal` n'existe pas exactement, prendre l'équivalent (par ex. `m_dayNight.GetCurrentTimeSeconds()` ou `m_clock.GetElapsedSeconds()`).

- [ ] **Step 7 : `engine/Engine.cpp` — RENAMES downstream Bloom_Prefilter + Bloom_Combine**

**Bloom_Prefilter** (ligne ~1971) — modifier `b.read()` :
```cpp
// AVANT :
b.read(m_fgSceneColorHDRId, engine::render::ImageUsage::SampledRead);
// APRÈS :
b.read(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::SampledRead);
```

Et dans le `Record()` du même addPass, modifier l'argument source :
```cpp
// AVANT :
m_pipeline->GetBloomPrefilterPass().Record(
    m_vkDeviceContext.GetDevice(), cmd, reg,
    m_vkSwapchain.GetExtent(),
    m_fgSceneColorHDRId, m_fgBloomDownMipIds[0], pp, frameIdx);
// APRÈS :
m_pipeline->GetBloomPrefilterPass().Record(
    m_vkDeviceContext.GetDevice(), cmd, reg,
    m_vkSwapchain.GetExtent(),
    m_fgSceneColorHDRPostWaterId, m_fgBloomDownMipIds[0], pp, frameIdx);
```

**Bloom_Combine** (ligne ~2030) — modifier `b.read()` et le Record :
```cpp
// AVANT :
b.read(m_fgSceneColorHDRId, engine::render::ImageUsage::SampledRead);
// APRÈS :
b.read(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::SampledRead);

// Dans Record :
// AVANT :
m_pipeline->GetBloomCombinePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorHDRId, m_fgBloomUpMipIds[0], m_fgSceneColorHDRWithBloomId, cp, frameIdx);
// APRÈS :
m_pipeline->GetBloomCombinePass().Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(), m_fgSceneColorHDRPostWaterId, m_fgBloomUpMipIds[0], m_fgSceneColorHDRWithBloomId, cp, frameIdx);
```

- [ ] **Step 8 : Vérification — confirmer qu'aucune autre passe downstream n'a été oubliée**

```bash
grep -n "m_fgSceneColorHDRId" engine/Engine.cpp
```

Sites attendus après modifications :
- Création de la ressource (ligne ~1132)
- `Lighting` writes (ligne ~1894 : `b.write(m_fgSceneColorHDRId, ColorWrite)`) — INCHANGÉ
- `Water` reads (nouveau bloc Step 6) — `b.read(m_fgSceneColorHDRId, SampledRead)`
- `Water_Passthrough` reads (nouveau bloc Step 6) — `b.read(m_fgSceneColorHDRId, TransferSrc)`

Tous les autres reads doivent être sur `m_fgSceneColorHDRPostWaterId`.

Si le grep révèle un site inattendu (par ex. AutoExposure_Luminance), faire le rename là aussi (cf. ligne ~2046).

```bash
# Vérification dédiée AutoExposure_Luminance
grep -n -A1 "AutoExposure_Luminance" engine/Engine.cpp | head -10
```

Si AutoExposure_Luminance lit `m_fgSceneColorHDRId` (ou `m_fgSceneColorHDRWithBloomId`), c'est OK — il lit après Bloom donc déjà downstream. Si AutoExposure lit `m_fgSceneColorHDRId` directement (ce qui est le cas selon Engine.cpp:2046), il faut aussi le renommer. **Vérifier à l'œil et adapter en conséquence**.

- [ ] **Step 9 : Commit**

```bash
git add engine/Engine.h engine/Engine.cpp
git commit -m "$(cat <<'EOF'
feat(engine): wire WaterPass + WaterMeshGpu dans frame graph (M100.14 Task 8)

Boot :
- Init WaterPass après Lighting (charge water.{vert,frag}.spv + texture
  water_normal + skybox cube atmosphère). Si prerequisites absents
  → log WARN, passthrough fallback en runtime.
- Init WaterMeshGpu (buffer vide).

Per-frame avant FrameGraph::execute() :
- Détecte WaterDocument::IsDirty() (mode éditeur) ou m_waterClientSceneDirty
  (mode client) → Rebuild GPU live + ClearDirty.

Frame graph :
- addPass("Water") : reads SceneColor_HDR + SceneDepth, writes
  SceneColor_HDR_PostWater. Push constants base (viewProj/cameraPos/
  timeSeconds/screenSize) ; per-instance écrasé par WaterPass::Record.
- Fallback addPass("Water_Passthrough") (vkCmdCopyImage) si Init failed
  pour garantir que Bloom_* trouve toujours son input.

Ping-pong renames downstream :
- Bloom_Prefilter (b.read + Record source) : SceneColor_HDR → SceneColor_HDR_PostWater
- Bloom_Combine   (b.read + Record source) : idem
- AutoExposure_Luminance : à valider — voir grep gardien Task 9.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Validation finale (CMakeLists + INDEX.md + grep gardien)

**Objectif :** S'assurer que tout est cohérent avant push : sources CMakeLists ajoutées, M38 entièrement supprimé, INDEX.md mis à jour, aucune référence orpheline restante.

**Files:**
- Modify: `CMakeLists.txt` (consolidation finale)
- Modify: `tickets/M100/INDEX.md`

- [ ] **Step 1 : Vérifier que `CMakeLists.txt` a tous les ajouts/retraits attendus**

Vérifications :

```bash
# Doit trouver les nouvelles sources
grep -n "WaterMeshGpu.cpp\|WaterPass.cpp" CMakeLists.txt
# Doit trouver les nouveaux test executables
grep -n "water_mesh_gpu_tests\|water_pass_tests" CMakeLists.txt
# Doit NE PAS trouver l'ancien WaterRenderer
grep -n "WaterRenderer" CMakeLists.txt
```

Expected :
- Premier grep : 2 lignes (engine_core source list)
- Deuxième grep : ≥ 8 lignes (chaque test executable a 4-5 lignes : add_executable + target_include + target_link + add_test + optionnel compile_options)
- Troisième grep : **vide** (sinon Task 1 incomplet — corriger)

Si le 3e grep n'est pas vide, retourner à Task 1 Step 5.

- [ ] **Step 2 : Vérifier qu'aucune référence à WaterRenderer ne subsiste dans le code source**

```bash
grep -rn "WaterRenderer" engine/ tools/
```

Expected : **vide**. Si grep trouve quelque chose, supprimer ces références (sites manqués au Task 1).

- [ ] **Step 3 : Vérifier que `m_fgSceneColorHDRPostWaterId` est bien câblé**

```bash
grep -n "m_fgSceneColorHDRPostWaterId\|fgSceneColorHDRPostWater" engine/Engine.cpp engine/Engine.h
```

Expected sites :
- `engine/Engine.h` : déclaration membre (1 ligne)
- `engine/Engine.cpp` : createImage (1 ligne) + Water write (1) + Bloom_Prefilter read+record (2) + Bloom_Combine read+record (2) ≥ 6 lignes total

Si moins, sites Task 8 manqués — corriger.

- [ ] **Step 4 : Mettre à jour `tickets/M100/INDEX.md`**

Localiser la ligne :

```markdown
| M100.14 | Water Render Pass | 4 — Hydrologie & Hazards | M100.13 | Ready |
```

Remplacer `Ready` par `Done (CI pending)` :

```markdown
| M100.14 | Water Render Pass | 4 — Hydrologie & Hazards | M100.13 | Done (CI pending) |
```

- [ ] **Step 5 : Lecture relue de la PR diff (sanity check)**

```bash
git log --oneline main..HEAD
```

Expected : 8 commits (un par task 1-8) + bientôt celui de Task 9.

```bash
git diff --stat main..HEAD | tail -5
```

Expected stats approximatives :
- ~ -550 LOC (M38 cleanup)
- ~ +1200 LOC ajout (WaterMeshGpu + WaterPass + shaders + tests + Engine wiring + plan/spec déjà committés en upstream)
- Net delta ~ +650 LOC après cleanup M38.

- [ ] **Step 6 : Commit final + push**

```bash
git add CMakeLists.txt tickets/M100/INDEX.md
git commit -m "$(cat <<'EOF'
chore(M100): finalisation M100.14 — INDEX + grep gardien (Task 9)

- tickets/M100/INDEX.md : M100.14 Ready → Done (CI pending)
- CMakeLists.txt : consolidation (validation des ajouts/retraits)
- Vérifications grep :
  * Aucune référence WaterRenderer M38 restante (engine/ + tools/ + CMakeLists.txt)
  * m_fgSceneColorHDRPostWaterId câblé partout (write Water + 2 reads downstream)

Pas de redéploiement serveur (client/éditeur uniquement).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"

# Push pour PR
git push -u origin claude/m100-14-water-render-pass
```

- [ ] **Step 7 : Annoncer à l'utilisateur**

Récap pour message final :
- 9 tasks committées sur `claude/m100-14-water-render-pass`
- Build local non disponible — verification CI obligatoire
- Validation visuelle manuelle après merge :
  - Lancer `lcdlln_world_editor.exe`
  - Appuyer L (LakeTool) → tracer un polygone, fermer
  - Appuyer R (RiverTool) → tracer 3-4 nodes
  - Vérifier que l'eau apparaît avec animation des normales (le fait que ça bouge prouve que le shader tourne)
  - Note : si `textures/water_normal.ktx2` ou skybox cube absents au boot → log WARN + passthrough fallback (eau invisible mais pas de crash)
- **Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur

---

## Self-review checklist (à compléter par le contrôleur avant exécution)

- [ ] Spec coverage : tous les composants section 4 (WaterMeshGpu CPU/GPU + WaterPass + shaders + Engine wiring + tests) sont couverts par au moins une task.
- [ ] Pas de placeholder ("TBD", "TODO", "implement later") — la grep ci-dessus a confirmé.
- [ ] Type consistency :
  - `engine::render::WaterVertex` (28 B) défini Task 2, utilisé Task 5 (vertex format pipeline) — cohérent.
  - `engine::render::WaterPassPushConstants` (128 B) défini Task 4, utilisé Task 6 (push constants) et Task 7 (shader uniform block) — cohérent.
  - `paramsIndex` unifié 0..N_lakes-1 lacs / N_lakes..N_total-1 rivières : doc Task 2 + impl Task 6 — cohérent.
- [ ] Tests : 5 cas CPU (3 Mesh + 2 Layout) — tous écrits.
- [ ] Renames downstream : Bloom_Prefilter + Bloom_Combine spécifiés Task 8 + verification AutoExposure_Luminance Task 8 Step 8.
- [ ] Cleanup M38 : Task 1 isolé, vérification grep Task 9 Step 2.
