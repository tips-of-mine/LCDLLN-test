# M100.13 Water Surfaces Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Livrer la chaîne complète d'authoring des plans d'eau (lacs polygonaux + rivières polyline) pour M100.13 (Phase 4a) : structures + serialization binary `instances/water.bin`, ear clipping pour lacs + ribbon pour rivières, WaterDocument + LakeTool + RiverTool éditeur avec mini-canvas 2D top-down dans ToolPropertiesPanel, helpers StreamCache::LoadWater + zone_builder WriteWater.

**Architecture:** `engine/world/water/` est pure CPU (pas de Vulkan, pas d'ImGui). Outils éditeur dans `engine/editor/world/` consomment via TopDownCanvas helper inline dans `ToolPropertiesPanel`. Pas de modification de FrameGraph ou GeometryPass (rendu = M100.14). Pattern de sérialisation aligné sur terrain.bin/splat.bin/collision.bin (`OutputVersionHeader` 24 bytes + payload + xxhash64).

**Tech Stack:** C++20, hand-rolled REQUIRE test framework, ImGui DrawList pour le canvas 2D, `engine::math::Vec3` (existant), `OutputVersionHeader` + `ComputeXxHash64` (existants).

**Spec source:** `docs/superpowers/specs/2026-05-08-m100-13-water-surfaces-design.md`.

---

## File Structure

### Création (16 fichiers)

| Fichier | Rôle |
|---|---|
| `engine/world/water/WaterSurfaces.h` | Structures `LakeInstance/RiverInstance/RiverNode/WaterScene` + magic/version |
| `engine/world/water/WaterSurfaces.cpp` | `SaveWaterBin` / `LoadWaterBin` via `OutputVersionHeader` |
| `engine/world/water/WaterMeshBuilder.h` | `BuildLakeMesh` / `BuildRiverMesh` / `ComputeFlowDirections` |
| `engine/world/water/WaterMeshBuilder.cpp` | Ear clipping + ribbon polyline |
| `engine/world/water/tests/WaterSurfacesTests.cpp` | 6 cas (3 roundtrip + 2 failure + flow) |
| `engine/world/water/tests/WaterMeshBuilderTests.cpp` | 5 cas (ear clipping + ribbon + error) |
| `engine/editor/world/WaterDocument.h` | WaterScene + dirty flag + Save/LoadFromDisk |
| `engine/editor/world/WaterDocument.cpp` | Wrapper sur SaveWaterBin/LoadWaterBin |
| `engine/editor/world/LakeTool.h` | Polygon en cours + commit via undo |
| `engine/editor/world/LakeTool.cpp` | AddPoint/ClosePolygon/Cancel |
| `engine/editor/world/RiverTool.h` | Nodes en cours + commit via undo |
| `engine/editor/world/RiverTool.cpp` | AddNode (sample heightmap) / EndSpline / Cancel |
| `engine/editor/world/AddLakeCommand.h` | ICommand : push_back/pop_back lake |
| `engine/editor/world/AddLakeCommand.cpp` | impl |
| `engine/editor/world/AddRiverCommand.h` | ICommand : push_back/pop_back river |
| `engine/editor/world/AddRiverCommand.cpp` | impl |

### Modification (6 fichiers)

| Fichier | Modification |
|---|---|
| `engine/editor/world/WorldEditorShell.h` | + `ActiveTool::Lake (4)` / `River (5)` ; + `m_lakeTool, m_riverTool, m_waterDoc` ; + accesseurs |
| `engine/editor/world/WorldEditorShell.cpp` | + raccourcis L/R ; + Init wiring (3 lignes) ; + LoadFromDisk au boot |
| `engine/editor/world/panels/ToolPropertiesPanel.h` | + `RenderLakeParams` / `RenderRiverParams` declarations |
| `engine/editor/world/panels/ToolPropertiesPanel.cpp` | + 2 cases dans switch ; + impl des 2 méthodes ; + helper canvas top-down 2D |
| `engine/world/StreamCache.h/.cpp` | + `LoadWater(cfg, zone) → shared_ptr<WaterScene>` |
| `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` | + `WriteWater` signature |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` | + `WriteWater` impl (utilise SaveWaterBin) |
| `CMakeLists.txt` | + sources engine_core + 2 test executables |

---

## Branch & TDD Workflow

Branche active : `claude/m100-phase-4-water-and-hazards` (créée sur origin/main avec M100.11/M100.12 mergés, contient le spec committé `23ef9ef`).

Chaque task : red → green → commit. Build local non disponible (CMake/MSBuild absents) — verification deferred to CI.

**Important — API ICommand corrigée vs spec :** L'interface ICommand existante (`engine/editor/world/CommandStack.h:23`) utilise `Execute()` / `Undo()` (NOT `Apply()` / `Revert()`) et `GetMemoryFootprint() const` (NOT `SizeBytes()`). Le plan utilise les bons noms.

---

## Task 1: WaterSurfaces struct + binary serialize (6 round-trip tests)

**Files:**
- Create: `engine/world/water/WaterSurfaces.h`
- Create: `engine/world/water/WaterSurfaces.cpp`
- Create: `engine/world/water/tests/WaterSurfacesTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le header `engine/world/water/WaterSurfaces.h`**

```cpp
// engine/world/water/WaterSurfaces.h
#pragma once

#include "engine/math/Math.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::water
{
	/// Magic du fichier `instances/water.bin` ("WATR" little-endian).
	constexpr uint32_t kWaterMagic   = 0x52544157u;
	constexpr uint32_t kWaterVersion = 1u;

	/// Lac : polygone fermé CCW dans XZ, surface plate à `waterLevelY`.
	struct LakeInstance
	{
		std::string name;
		std::vector<engine::math::Vec3> polygon;          // CCW dans XZ
		engine::math::Vec3 bottomColor{ 0.05f, 0.20f, 0.30f };
		float turbidity   = 0.4f;
		float waterLevelY = 0.0f;
	};

	/// Node d'une rivière : position 3D (Y typiquement = terrain height),
	/// largeur et profondeur locale.
	struct RiverNode
	{
		engine::math::Vec3 position;
		float widthMeters = 4.0f;
		float depthMeters = 1.0f;
	};

	struct RiverInstance
	{
		std::string name;
		std::vector<RiverNode> nodes;                     // au moins 2 pour produire un mesh
	};

	struct WaterScene
	{
		std::vector<LakeInstance>  lakes;
		std::vector<RiverInstance> rivers;
	};

	/// Sérialise au format `water.bin` (M100.13). Header OutputVersionHeader
	/// (magic=kWaterMagic, version=1, contentHash=xxhash64 du payload).
	bool SaveWaterBin(const WaterScene& scene,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise. Valide magic, version, contentHash. Reset outScene.
	bool LoadWaterBin(std::span<const uint8_t> bytes,
		WaterScene& outScene, std::string& outError);
}
```

- [ ] **Step 2 : Écrire les 6 tests dans `engine/world/water/tests/WaterSurfacesTests.cpp`**

```cpp
// engine/world/water/tests/WaterSurfacesTests.cpp
#include "engine/world/water/WaterSurfaces.h"
#include "engine/world/water/WaterMeshBuilder.h"

#include <cmath>
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
	using engine::world::water::SaveWaterBin;
	using engine::world::water::LoadWaterBin;
	using engine::world::water::ComputeFlowDirections;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
	bool VecEq(const Vec3& a, const Vec3& b, float eps = 1e-5f)
	{
		return ApproxEq(a.x, b.x, eps) && ApproxEq(a.y, b.y, eps) && ApproxEq(a.z, b.z, eps);
	}

	WaterScene MakeLakeScene()
	{
		WaterScene s;
		LakeInstance lake;
		lake.name = "lake_test";
		lake.polygon = { {0,10,0}, {5,10,0}, {5,10,5}, {2,10,7}, {0,10,5} };
		lake.bottomColor = Vec3{ 0.1f, 0.2f, 0.3f };
		lake.turbidity = 0.5f;
		lake.waterLevelY = 10.0f;
		s.lakes.push_back(std::move(lake));
		return s;
	}

	WaterScene MakeRiverScene()
	{
		WaterScene s;
		RiverInstance r;
		r.name = "river_test";
		r.nodes = {
			RiverNode{ Vec3{ 0, 0, 0}, 3.0f, 1.0f },
			RiverNode{ Vec3{10, 0, 0}, 4.0f, 1.5f },
			RiverNode{ Vec3{20,-1, 5}, 5.0f, 1.5f },
			RiverNode{ Vec3{30,-2,10}, 4.0f, 1.0f },
		};
		s.rivers.push_back(std::move(r));
		return s;
	}

	void Test_Roundtrip_LakeOnly()
	{
		WaterScene src = MakeLakeScene();
		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));
		REQUIRE(err.empty());

		WaterScene dst;
		REQUIRE(LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.lakes.size() == 1);
		REQUIRE(dst.rivers.size() == 0);
		REQUIRE(dst.lakes[0].name == src.lakes[0].name);
		REQUIRE(dst.lakes[0].polygon.size() == src.lakes[0].polygon.size());
		REQUIRE(std::memcmp(dst.lakes[0].polygon.data(), src.lakes[0].polygon.data(),
			src.lakes[0].polygon.size() * sizeof(Vec3)) == 0);
		REQUIRE(VecEq(dst.lakes[0].bottomColor, src.lakes[0].bottomColor));
		REQUIRE(ApproxEq(dst.lakes[0].turbidity, src.lakes[0].turbidity));
		REQUIRE(ApproxEq(dst.lakes[0].waterLevelY, src.lakes[0].waterLevelY));
	}

	void Test_Roundtrip_RiverOnly()
	{
		WaterScene src = MakeRiverScene();
		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));

		WaterScene dst;
		REQUIRE(LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.lakes.size() == 0);
		REQUIRE(dst.rivers.size() == 1);
		REQUIRE(dst.rivers[0].name == src.rivers[0].name);
		REQUIRE(dst.rivers[0].nodes.size() == 4);
		for (size_t i = 0; i < 4; ++i)
		{
			REQUIRE(VecEq(dst.rivers[0].nodes[i].position, src.rivers[0].nodes[i].position));
			REQUIRE(ApproxEq(dst.rivers[0].nodes[i].widthMeters, src.rivers[0].nodes[i].widthMeters));
			REQUIRE(ApproxEq(dst.rivers[0].nodes[i].depthMeters, src.rivers[0].nodes[i].depthMeters));
		}
	}

	void Test_Roundtrip_LakeAndRiver()
	{
		WaterScene src;
		src.lakes = MakeLakeScene().lakes;
		LakeInstance lake2;
		lake2.name = "lake_pond";
		lake2.polygon = { {-5,2,-5}, {0,2,-5}, {0,2,0}, {-5,2,0} };
		lake2.waterLevelY = 2.0f;
		src.lakes.push_back(std::move(lake2));
		src.rivers = MakeRiverScene().rivers;

		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));

		WaterScene dst;
		REQUIRE(LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.lakes.size() == 2);
		REQUIRE(dst.rivers.size() == 1);
		REQUIRE(dst.lakes[0].name == "lake_test");
		REQUIRE(dst.lakes[1].name == "lake_pond");
		REQUIRE(dst.rivers[0].name == "river_test");
	}

	void Test_Load_BadMagic_Fails()
	{
		// Construit un buffer avec header magic invalide
		std::vector<uint8_t> bytes(24 + 8, 0u);
		const uint32_t badMagic = 0xDEADBEEFu;
		std::memcpy(bytes.data(), &badMagic, 4);
		// version, builderVer, engineVer, hash : laissés à 0
		// Counts 0/0
		WaterScene dst; std::string err;
		REQUIRE(!LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.find("magic") != std::string::npos);
	}

	void Test_Load_BadContentHash_Fails()
	{
		WaterScene src = MakeLakeScene();
		std::vector<uint8_t> bytes; std::string err;
		REQUIRE(SaveWaterBin(src, bytes, err));
		// Flip un byte du payload (offset 24 + 8 = lakeCount/riverCount frontière)
		bytes[32] ^= 0xFFu;
		WaterScene dst;
		REQUIRE(!LoadWaterBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.find("contentHash") != std::string::npos);
	}

	void Test_FlowDirection_AlignsWithSlope()
	{
		// Rivière 3 nodes descendant en +X
		RiverInstance r;
		r.nodes = {
			RiverNode{ Vec3{ 0, 10, 0}, 4.0f, 1.0f },
			RiverNode{ Vec3{10,  5, 0}, 4.0f, 1.0f },
			RiverNode{ Vec3{20,  0, 0}, 4.0f, 1.0f },
		};
		auto flows = ComputeFlowDirections(r);
		REQUIRE(flows.size() == 2);
		// flow[0] et flow[1] devraient pointer en +X (1, 0, 0) — flow XZ uniquement
		REQUIRE(ApproxEq(flows[0].x, 1.0f, 1e-3f));
		REQUIRE(ApproxEq(flows[0].z, 0.0f, 1e-3f));
		REQUIRE(ApproxEq(flows[1].x, 1.0f, 1e-3f));
		REQUIRE(ApproxEq(flows[1].z, 0.0f, 1e-3f));
	}
}

int main()
{
	Test_Roundtrip_LakeOnly();
	Test_Roundtrip_RiverOnly();
	Test_Roundtrip_LakeAndRiver();
	Test_Load_BadMagic_Fails();
	Test_Load_BadContentHash_Fails();
	Test_FlowDirection_AlignsWithSlope();
	return g_failed;
}
```

- [ ] **Step 3 : Ajouter test exécutable au CMakeLists.txt**

Dans `CMakeLists.txt`, après le bloc `proxy_wireframe_tests` (M100.12), ajouter :

```cmake
# M100.13 — Tests WaterSurfaces serialization + flow direction (Phase 4a).
if(WIN32)
  add_executable(water_surfaces_tests engine/world/water/tests/WaterSurfacesTests.cpp)
  target_include_directories(water_surfaces_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_surfaces_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_surfaces_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_surfaces_tests COMMAND water_surfaces_tests)
endif()
```

- [ ] **Step 4 : Créer `engine/world/water/WaterSurfaces.cpp`**

```cpp
// engine/world/water/WaterSurfaces.cpp
#include "engine/world/water/WaterSurfaces.h"

#include "engine/world/OutputVersion.h"

#include <cstring>

namespace engine::world::water
{
	namespace
	{
		uint8_t* Write32(uint8_t* dst, uint32_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}
		uint8_t* Write64(uint8_t* dst, uint64_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}
		uint8_t* WriteVec3(uint8_t* dst, const engine::math::Vec3& v)
		{
			std::memcpy(dst,     &v.x, 4);
			std::memcpy(dst + 4, &v.y, 4);
			std::memcpy(dst + 8, &v.z, 4);
			return dst + 12;
		}

		size_t LakeSize(const LakeInstance& l)
		{
			// nameLen(4) + name + vertexCount(4) + polygon*(12) + bottomColor(12) + turbidity(4) + waterLevelY(4)
			return 4u + l.name.size() + 4u + l.polygon.size() * 12u + 12u + 4u + 4u;
		}
		size_t RiverSize(const RiverInstance& r)
		{
			// nameLen(4) + name + nodeCount(4) + nodes*(12+4+4)
			return 4u + r.name.size() + 4u + r.nodes.size() * 20u;
		}

		size_t ComputePayloadSize(const WaterScene& s)
		{
			size_t total = 4u + 4u;  // lakeCount + riverCount
			for (const auto& l : s.lakes)  total += LakeSize(l);
			for (const auto& r : s.rivers) total += RiverSize(r);
			return total;
		}
	}

	bool SaveWaterBin(const WaterScene& scene,
		std::vector<uint8_t>& outBytes, std::string& outError)
	{
		const size_t headerSize = 24u;
		const size_t payloadSize = ComputePayloadSize(scene);
		const size_t totalSize = headerSize + payloadSize;

		outBytes.assign(totalSize, 0u);
		uint8_t* p = outBytes.data() + headerSize;

		p = Write32(p, static_cast<uint32_t>(scene.lakes.size()));
		p = Write32(p, static_cast<uint32_t>(scene.rivers.size()));

		for (const auto& lake : scene.lakes)
		{
			p = Write32(p, static_cast<uint32_t>(lake.name.size()));
			std::memcpy(p, lake.name.data(), lake.name.size());
			p += lake.name.size();
			p = Write32(p, static_cast<uint32_t>(lake.polygon.size()));
			for (const auto& v : lake.polygon)
				p = WriteVec3(p, v);
			p = WriteVec3(p, lake.bottomColor);
			std::memcpy(p, &lake.turbidity, 4);   p += 4;
			std::memcpy(p, &lake.waterLevelY, 4); p += 4;
		}
		for (const auto& river : scene.rivers)
		{
			p = Write32(p, static_cast<uint32_t>(river.name.size()));
			std::memcpy(p, river.name.data(), river.name.size());
			p += river.name.size();
			p = Write32(p, static_cast<uint32_t>(river.nodes.size()));
			for (const auto& node : river.nodes)
			{
				p = WriteVec3(p, node.position);
				std::memcpy(p, &node.widthMeters, 4); p += 4;
				std::memcpy(p, &node.depthMeters, 4); p += 4;
			}
		}

		// ContentHash xxhash64 sur payload post-header
		std::span<const uint8_t> payload(outBytes.data() + headerSize, totalSize - headerSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		uint8_t* h = outBytes.data();
		h = Write32(h, kWaterMagic);
		h = Write32(h, kWaterVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;
		return true;
	}

	bool LoadWaterBin(std::span<const uint8_t> bytes,
		WaterScene& outScene, std::string& outError)
	{
		outScene.lakes.clear();
		outScene.rivers.clear();

		if (bytes.size() < 24u + 8u)
		{
			outError = "WaterScene: file too small";
			return false;
		}

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError))
			return false;
		if (hdr.magic != kWaterMagic)
		{
			outError = "WaterScene: bad magic (expected WATR)";
			return false;
		}
		if (hdr.formatVersion != kWaterVersion)
		{
			outError = "WaterScene: bad version";
			return false;
		}
		std::span<const uint8_t> payload = bytes.subspan(24);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{
			outError = "WaterScene: contentHash mismatch (file corrupted)";
			return false;
		}

		const uint8_t* p = bytes.data() + 24;
		const uint8_t* end = bytes.data() + bytes.size();

		auto readU32 = [&](uint32_t& v) -> bool {
			if (end - p < 4) return false;
			std::memcpy(&v, p, 4); p += 4; return true;
		};
		auto readF32 = [&](float& v) -> bool {
			if (end - p < 4) return false;
			std::memcpy(&v, p, 4); p += 4; return true;
		};
		auto readVec3 = [&](engine::math::Vec3& v) -> bool {
			if (end - p < 12) return false;
			std::memcpy(&v.x, p,     4);
			std::memcpy(&v.y, p + 4, 4);
			std::memcpy(&v.z, p + 8, 4);
			p += 12; return true;
		};

		uint32_t lakeCount = 0, riverCount = 0;
		if (!readU32(lakeCount) || !readU32(riverCount))
		{ outError = "WaterScene: header truncated"; return false; }

		outScene.lakes.reserve(lakeCount);
		for (uint32_t i = 0; i < lakeCount; ++i)
		{
			LakeInstance lake;
			uint32_t nameLen = 0, vertCount = 0;
			if (!readU32(nameLen)) { outError = "WaterScene: lake nameLen truncated"; return false; }
			if (static_cast<size_t>(end - p) < nameLen)
			{ outError = "WaterScene: lake name truncated"; return false; }
			lake.name.assign(reinterpret_cast<const char*>(p), nameLen);
			p += nameLen;
			if (!readU32(vertCount)) { outError = "WaterScene: lake vertCount truncated"; return false; }
			if (static_cast<size_t>(end - p) < static_cast<size_t>(vertCount) * 12u)
			{ outError = "WaterScene: lake polygon truncated"; return false; }
			lake.polygon.resize(vertCount);
			for (uint32_t v = 0; v < vertCount; ++v)
				readVec3(lake.polygon[v]);
			if (!readVec3(lake.bottomColor))   { outError = "WaterScene: lake bottomColor truncated"; return false; }
			if (!readF32(lake.turbidity))      { outError = "WaterScene: lake turbidity truncated"; return false; }
			if (!readF32(lake.waterLevelY))    { outError = "WaterScene: lake waterLevelY truncated"; return false; }
			outScene.lakes.push_back(std::move(lake));
		}

		outScene.rivers.reserve(riverCount);
		for (uint32_t i = 0; i < riverCount; ++i)
		{
			RiverInstance river;
			uint32_t nameLen = 0, nodeCount = 0;
			if (!readU32(nameLen)) { outError = "WaterScene: river nameLen truncated"; return false; }
			if (static_cast<size_t>(end - p) < nameLen)
			{ outError = "WaterScene: river name truncated"; return false; }
			river.name.assign(reinterpret_cast<const char*>(p), nameLen);
			p += nameLen;
			if (!readU32(nodeCount)) { outError = "WaterScene: river nodeCount truncated"; return false; }
			if (static_cast<size_t>(end - p) < static_cast<size_t>(nodeCount) * 20u)
			{ outError = "WaterScene: river nodes truncated"; return false; }
			river.nodes.resize(nodeCount);
			for (uint32_t n = 0; n < nodeCount; ++n)
			{
				readVec3(river.nodes[n].position);
				readF32(river.nodes[n].widthMeters);
				readF32(river.nodes[n].depthMeters);
			}
			outScene.rivers.push_back(std::move(river));
		}

		return true;
	}
}
```

- [ ] **Step 5 : Ajouter `WaterSurfaces.cpp` à engine_core dans CMakeLists.txt**

Dans la liste des sources `engine_core`, près des autres `engine/world/*` (autour ligne 367 où `surface/SurfaceType.cpp` etc. sont listés) :

```cmake
  engine/world/water/WaterSurfaces.cpp
```

- [ ] **Step 6 : Commit**

```bash
git add engine/world/water/WaterSurfaces.h \
        engine/world/water/WaterSurfaces.cpp \
        engine/world/water/tests/WaterSurfacesTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/water): WaterSurfaces structs + binary serialize (M100.13 Task 1)"
```

---

## Task 2: WaterMeshBuilder (ear clipping + ribbon, 5 tests)

**Files:**
- Create: `engine/world/water/WaterMeshBuilder.h`
- Create: `engine/world/water/WaterMeshBuilder.cpp`
- Create: `engine/world/water/tests/WaterMeshBuilderTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `engine/world/water/WaterMeshBuilder.h`**

```cpp
// engine/world/water/WaterMeshBuilder.h
#pragma once

#include "engine/math/Math.h"
#include "engine/world/water/WaterSurfaces.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::world::water
{
	struct WaterVertex
	{
		engine::math::Vec3 position;
	};

	struct WaterMeshCpu
	{
		std::vector<WaterVertex> vertices;
		std::vector<uint32_t>    indices;          // 3 par triangle
	};

	/// Triangulation du polygone d'un lac via ear clipping (M100.13).
	/// Précondition : polygon a >= 3 vertices, simple (non auto-intersectant).
	/// Si CW, inverse l'ordre interne avant traitement (CCW imposé).
	/// Tous les vertices output ont Y = lake.waterLevelY (mesh plat).
	bool BuildLakeMesh(const LakeInstance& lake,
		WaterMeshCpu& outMesh, std::string& outError);

	/// Ribbon mesh d'une rivière. N nodes → N-1 segments → 2*(N-1) triangles.
	/// 2*N vertices total (2 par node, perpendiculaires au tangent local).
	/// Y de chaque vertex = node.position.y. Précondition : nodes.size() >= 2.
	bool BuildRiverMesh(const RiverInstance& river,
		WaterMeshCpu& outMesh, std::string& outError);

	/// Calcule les directions de flot par segment de rivière.
	/// flow[i] = normalize((node[i+1] - node[i]).xz), Y forcé à 0.
	/// Précondition : river.nodes.size() >= 2. Output size = nodes.size() - 1.
	std::vector<engine::math::Vec3> ComputeFlowDirections(const RiverInstance& river);
}
```

- [ ] **Step 2 : Écrire les 5 tests dans `engine/world/water/tests/WaterMeshBuilderTests.cpp`**

```cpp
// engine/world/water/tests/WaterMeshBuilderTests.cpp
#include "engine/world/water/WaterMeshBuilder.h"

#include <cmath>
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

	using engine::world::water::LakeInstance;
	using engine::world::water::RiverInstance;
	using engine::world::water::RiverNode;
	using engine::world::water::WaterMeshCpu;
	using engine::world::water::BuildLakeMesh;
	using engine::world::water::BuildRiverMesh;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

	void Test_BuildLakeMesh_ConvexQuad_Produces2Triangles()
	{
		LakeInstance lake;
		lake.waterLevelY = 5.0f;
		lake.polygon = { {0,5,0}, {2,5,0}, {2,5,2}, {0,5,2} };  // CCW square
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildLakeMesh(lake, mesh, err));
		REQUIRE(mesh.vertices.size() == 4);
		REQUIRE(mesh.indices.size() == 6);  // 2 triangles
		for (const auto& v : mesh.vertices)
			REQUIRE(ApproxEq(v.position.y, 5.0f));
	}

	void Test_BuildLakeMesh_ConvexPentagon_Produces3Triangles()
	{
		LakeInstance lake;
		lake.waterLevelY = 0.0f;
		// Pentagon CCW
		const float r = 1.0f;
		for (int i = 0; i < 5; ++i)
		{
			const float t = static_cast<float>(i) / 5.0f * 6.28318f;
			lake.polygon.push_back({ std::cos(t) * r, 0.0f, std::sin(t) * r });
		}
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildLakeMesh(lake, mesh, err));
		REQUIRE(mesh.vertices.size() == 5);
		REQUIRE(mesh.indices.size() == 9);  // 3 triangles
	}

	void Test_BuildLakeMesh_ConcaveLShape_ProducesCorrectTris()
	{
		LakeInstance lake;
		lake.waterLevelY = 0.0f;
		// L-shape CCW : 6 vertices, concave at (2,1)
		lake.polygon = {
			{0,0,0}, {2,0,0}, {2,0,1},
			{1,0,1}, {1,0,2}, {0,0,2},
		};
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildLakeMesh(lake, mesh, err));
		REQUIRE(mesh.vertices.size() == 6);
		REQUIRE(mesh.indices.size() == 12);  // 4 triangles
	}

	void Test_BuildRiverMesh_4Nodes_Produces6Quads()
	{
		RiverInstance river;
		river.nodes = {
			RiverNode{ Vec3{0,0,0}, 2.0f, 1.0f },
			RiverNode{ Vec3{2,0,0}, 2.0f, 1.0f },
			RiverNode{ Vec3{4,0,0}, 2.0f, 1.0f },
			RiverNode{ Vec3{6,0,0}, 2.0f, 1.0f },
		};
		WaterMeshCpu mesh; std::string err;
		REQUIRE(BuildRiverMesh(river, mesh, err));
		REQUIRE(mesh.vertices.size() == 8);   // 2 par node
		REQUIRE(mesh.indices.size() == 18);   // 3 segments × 2 triangles × 3 indices
		// Y de chaque vertex = node.y = 0
		for (const auto& v : mesh.vertices)
			REQUIRE(ApproxEq(v.position.y, 0.0f));
	}

	void Test_BuildLakeMesh_TooFewVertices_Fails()
	{
		LakeInstance lake;
		lake.polygon = { {0,0,0}, {1,0,0} };  // 2 vertices
		WaterMeshCpu mesh; std::string err;
		REQUIRE(!BuildLakeMesh(lake, mesh, err));
		REQUIRE(err.find(">= 3") != std::string::npos);
	}
}

int main()
{
	Test_BuildLakeMesh_ConvexQuad_Produces2Triangles();
	Test_BuildLakeMesh_ConvexPentagon_Produces3Triangles();
	Test_BuildLakeMesh_ConcaveLShape_ProducesCorrectTris();
	Test_BuildRiverMesh_4Nodes_Produces6Quads();
	Test_BuildLakeMesh_TooFewVertices_Fails();
	return g_failed;
}
```

- [ ] **Step 3 : Ajouter test exécutable au CMakeLists.txt**

```cmake
# M100.13 — Tests WaterMeshBuilder ear clipping + ribbon polyline.
if(WIN32)
  add_executable(water_mesh_builder_tests engine/world/water/tests/WaterMeshBuilderTests.cpp)
  target_include_directories(water_mesh_builder_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_mesh_builder_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_mesh_builder_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_mesh_builder_tests COMMAND water_mesh_builder_tests)
endif()
```

- [ ] **Step 4 : Créer `engine/world/water/WaterMeshBuilder.cpp`**

```cpp
// engine/world/water/WaterMeshBuilder.cpp
#include "engine/world/water/WaterMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace engine::world::water
{
	namespace
	{
		/// Aire signée 2D du polygone (XZ). Positive si CCW.
		float SignedArea2D(const std::vector<engine::math::Vec3>& poly)
		{
			float a = 0.0f;
			for (size_t i = 0; i < poly.size(); ++i)
			{
				const auto& cur  = poly[i];
				const auto& next = poly[(i + 1) % poly.size()];
				a += (cur.x * next.z - next.x * cur.z);
			}
			return a * 0.5f;
		}

		/// True si l'angle au coin (a, b, c) est convexe (CCW polygon).
		/// Cross product 2D (XZ) du segment ab et bc > 0.
		bool IsConvexCorner(const engine::math::Vec3& a,
		                    const engine::math::Vec3& b,
		                    const engine::math::Vec3& c)
		{
			const float ux = b.x - a.x, uz = b.z - a.z;
			const float vx = c.x - b.x, vz = c.z - b.z;
			return (ux * vz - uz * vx) > 0.0f;
		}

		/// True si le point p est strictement à l'intérieur du triangle (a, b, c)
		/// dans XZ. Utilise les barycentric coordinates.
		bool PointInTriangleXZ(const engine::math::Vec3& p,
		                       const engine::math::Vec3& a,
		                       const engine::math::Vec3& b,
		                       const engine::math::Vec3& c)
		{
			const float d1 = (p.x - b.x) * (a.z - b.z) - (a.x - b.x) * (p.z - b.z);
			const float d2 = (p.x - c.x) * (b.z - c.z) - (b.x - c.x) * (p.z - c.z);
			const float d3 = (p.x - a.x) * (c.z - a.z) - (c.x - a.x) * (p.z - a.z);
			const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
			const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
			return !(hasNeg && hasPos);
		}

		/// Pour la boucle ear clipping : test si un autre vertex restant
		/// (sauf prev/curr/next) est dans le triangle.
		bool TriangleContainsAnyOther(const std::vector<uint32_t>& remaining,
		                              size_t earIdx,
		                              const std::vector<engine::math::Vec3>& poly)
		{
			const size_t n = remaining.size();
			const uint32_t prev = remaining[(earIdx + n - 1) % n];
			const uint32_t curr = remaining[earIdx];
			const uint32_t next = remaining[(earIdx + 1) % n];
			for (size_t i = 0; i < n; ++i)
			{
				const uint32_t v = remaining[i];
				if (v == prev || v == curr || v == next) continue;
				if (PointInTriangleXZ(poly[v], poly[prev], poly[curr], poly[next]))
					return true;
			}
			return false;
		}
	}

	bool BuildLakeMesh(const LakeInstance& lake,
		WaterMeshCpu& out, std::string& err)
	{
		out.vertices.clear();
		out.indices.clear();

		if (lake.polygon.size() < 3)
		{
			err = "BuildLakeMesh: polygon needs >= 3 vertices";
			return false;
		}

		// Force CCW
		std::vector<engine::math::Vec3> poly = lake.polygon;
		if (SignedArea2D(poly) < 0.0f)
			std::reverse(poly.begin(), poly.end());

		// Indices restants à découper
		std::vector<uint32_t> remaining(poly.size());
		std::iota(remaining.begin(), remaining.end(), 0u);

		while (remaining.size() > 3)
		{
			bool foundEar = false;
			for (size_t i = 0; i < remaining.size(); ++i)
			{
				const size_t n = remaining.size();
				const uint32_t prev = remaining[(i + n - 1) % n];
				const uint32_t curr = remaining[i];
				const uint32_t next = remaining[(i + 1) % n];
				if (!IsConvexCorner(poly[prev], poly[curr], poly[next])) continue;
				if (TriangleContainsAnyOther(remaining, i, poly)) continue;
				out.indices.push_back(prev);
				out.indices.push_back(curr);
				out.indices.push_back(next);
				remaining.erase(remaining.begin() + i);
				foundEar = true;
				break;
			}
			if (!foundEar)
			{
				err = "BuildLakeMesh: ear-clipping failed (auto-intersecting polygon?)";
				return false;
			}
		}
		// Dernier triangle
		out.indices.push_back(remaining[0]);
		out.indices.push_back(remaining[1]);
		out.indices.push_back(remaining[2]);

		// Vertices : positions XZ du polygone, Y = waterLevelY
		out.vertices.reserve(poly.size());
		for (const auto& p : poly)
			out.vertices.push_back({ engine::math::Vec3{ p.x, lake.waterLevelY, p.z } });

		return true;
	}

	bool BuildRiverMesh(const RiverInstance& river,
		WaterMeshCpu& out, std::string& err)
	{
		out.vertices.clear();
		out.indices.clear();

		if (river.nodes.size() < 2)
		{
			err = "BuildRiverMesh: river needs >= 2 nodes";
			return false;
		}

		out.vertices.reserve(2 * river.nodes.size());
		out.indices.reserve(6 * (river.nodes.size() - 1));

		for (size_t i = 0; i < river.nodes.size(); ++i)
		{
			engine::math::Vec3 tangent;
			if (i == 0)
				tangent = river.nodes[1].position - river.nodes[0].position;
			else if (i == river.nodes.size() - 1)
				tangent = river.nodes[i].position - river.nodes[i - 1].position;
			else
				tangent = river.nodes[i + 1].position - river.nodes[i - 1].position;

			const float tlen = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
			const float perpX = (tlen > 0.0f) ? (-tangent.z / tlen) : 1.0f;
			const float perpZ = (tlen > 0.0f) ? ( tangent.x / tlen) : 0.0f;
			const float halfW = river.nodes[i].widthMeters * 0.5f;

			const auto& n = river.nodes[i];
			out.vertices.push_back({ engine::math::Vec3{
				n.position.x + perpX * halfW, n.position.y, n.position.z + perpZ * halfW } });
			out.vertices.push_back({ engine::math::Vec3{
				n.position.x - perpX * halfW, n.position.y, n.position.z - perpZ * halfW } });
		}

		for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(river.nodes.size()); ++i)
		{
			const uint32_t a = i * 2 + 0;
			const uint32_t b = i * 2 + 1;
			const uint32_t c = (i + 1) * 2 + 0;
			const uint32_t d = (i + 1) * 2 + 1;
			out.indices.push_back(a); out.indices.push_back(c); out.indices.push_back(d);
			out.indices.push_back(a); out.indices.push_back(d); out.indices.push_back(b);
		}
		return true;
	}

	std::vector<engine::math::Vec3> ComputeFlowDirections(const RiverInstance& river)
	{
		std::vector<engine::math::Vec3> flows;
		if (river.nodes.size() < 2) return flows;
		flows.reserve(river.nodes.size() - 1);
		for (size_t i = 0; i + 1 < river.nodes.size(); ++i)
		{
			const float dx = river.nodes[i + 1].position.x - river.nodes[i].position.x;
			const float dz = river.nodes[i + 1].position.z - river.nodes[i].position.z;
			const float len = std::sqrt(dx * dx + dz * dz);
			if (len > 0.0f)
				flows.push_back({ dx / len, 0.0f, dz / len });
			else
				flows.push_back({ 1.0f, 0.0f, 0.0f });
		}
		return flows;
	}
}
```

- [ ] **Step 5 : Ajouter `WaterMeshBuilder.cpp` à engine_core**

À côté de `WaterSurfaces.cpp` :

```cmake
  engine/world/water/WaterMeshBuilder.cpp
```

- [ ] **Step 6 : Commit**

```bash
git add engine/world/water/WaterMeshBuilder.h \
        engine/world/water/WaterMeshBuilder.cpp \
        engine/world/water/tests/WaterMeshBuilderTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/water): WaterMeshBuilder ear clipping + ribbon polyline (M100.13 Task 2)"
```

---

## Task 3: WaterDocument + zone_builder WriteWater + StreamCache::LoadWater

**Files:**
- Create: `engine/editor/world/WaterDocument.h`
- Create: `engine/editor/world/WaterDocument.cpp`
- Modify: `engine/world/StreamCache.h`
- Modify: `engine/world/StreamCache.cpp`
- Modify: `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h`
- Modify: `tools/zone_builder/lib/ChunkPackageWriter.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `engine/editor/world/WaterDocument.h`**

```cpp
// engine/editor/world/WaterDocument.h
#pragma once

#include "engine/world/water/WaterSurfaces.h"

#include <string>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// État des plans d'eau de la zone éditée (M100.13). Persiste dans
	/// `<paths.content>/instances/water.bin`. Un seul WaterScene par éditeur
	/// (chunk-level partitioning vient avec M100.34).
	class WaterDocument
	{
	public:
		engine::world::water::WaterScene&       Mutable()       { return m_scene; }
		const engine::world::water::WaterScene& Get()     const { return m_scene; }

		bool IsDirty() const noexcept { return m_dirty; }
		void MarkDirty() noexcept     { m_dirty = true; }

		/// Sauvegarde dans `<paths.content>/instances/water.bin`. Reset m_dirty.
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

		/// Charge depuis `<paths.content>/instances/water.bin`. Si fichier absent,
		/// retourne true avec scene vide. Reset m_dirty.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

	private:
		engine::world::water::WaterScene m_scene;
		bool m_dirty = false;
	};
}
```

- [ ] **Step 2 : Créer `engine/editor/world/WaterDocument.cpp`**

```cpp
// engine/editor/world/WaterDocument.cpp
#include "engine/editor/world/WaterDocument.h"

#include "engine/core/Config.h"

#include <filesystem>
#include <fstream>

namespace engine::editor::world
{
	bool WaterDocument::SaveToDisk(const engine::core::Config& cfg, std::string& outError)
	{
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "instances" / "water.bin";

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) { outError = "WaterDocument::SaveToDisk: mkdir failed: " + ec.message(); return false; }

		std::vector<uint8_t> bytes;
		if (!engine::world::water::SaveWaterBin(m_scene, bytes, outError))
			return false;

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "WaterDocument::SaveToDisk: cannot open " + path.string();
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "WaterDocument::SaveToDisk: write failed";
			return false;
		}
		m_dirty = false;
		return true;
	}

	bool WaterDocument::LoadFromDisk(const engine::core::Config& cfg, std::string& outError)
	{
		const std::string contentRoot = cfg.GetString("paths.content", "game/data");
		const std::filesystem::path path =
			std::filesystem::path(contentRoot) / "instances" / "water.bin";

		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			// Fichier absent : pas une erreur, scene reste vide.
			m_scene.lakes.clear();
			m_scene.rivers.clear();
			m_dirty = false;
			return true;
		}
		const std::streamsize size = f.tellg();
		f.seekg(0);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);

		if (!engine::world::water::LoadWaterBin(
			std::span<const uint8_t>(bytes), m_scene, outError))
			return false;

		m_dirty = false;
		return true;
	}
}
```

- [ ] **Step 3 : Ajouter `LoadWater` à `engine/world/StreamCache.h`**

Après la déclaration de `LoadSplatMap` (autour ligne 72-73), ajouter :

```cpp
		/// Charge le `instances/water.bin` global de la zone (M100.13). Si
		/// fichier absent, retourne nullptr sans warning. Pas de cache (zone
		/// boot-time only). \param zoneName réservé pour multi-zone (M100.34).
		std::shared_ptr<engine::world::water::WaterScene> LoadWater(
			const engine::core::Config& config, std::string_view zoneName);
```

Ajouter forward declaration en haut :

```cpp
namespace engine::world
{
    namespace terrain { struct TerrainChunk; struct TerrainLodChain; struct SplatMap; }
    namespace water { struct WaterScene; }  // <- nouveau
    // ...
```

- [ ] **Step 4 : Ajouter impl dans `engine/world/StreamCache.cpp`**

À la fin du fichier (avant la dernière `}` de namespace), ajouter :

```cpp
	std::shared_ptr<engine::world::water::WaterScene>
	StreamCache::LoadWater(const engine::core::Config& config, std::string_view zoneName)
	{
		(void)zoneName;  // M100.13 : single global file, pas de partitioning
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string fullPath = contentRoot + "/instances/water.bin";

		std::ifstream f(fullPath, std::ios::binary | std::ios::ate);
		if (!f.good()) return nullptr;  // Optionnel : silencieux si absent
		const std::streamsize fileSize = f.tellg();
		f.seekg(0);
		if (fileSize <= 0) return nullptr;
		std::vector<uint8_t> blob(static_cast<size_t>(fileSize));
		f.read(reinterpret_cast<char*>(blob.data()), fileSize);
		if (!f.good() && !f.eof()) return nullptr;

		auto scene = std::make_shared<engine::world::water::WaterScene>();
		std::string err;
		if (!engine::world::water::LoadWaterBin(
			std::span<const uint8_t>(blob), *scene, err))
		{
			LOG_WARN(World, "[StreamCache] LoadWaterBin fail ({}): {}", fullPath, err);
			return nullptr;
		}
		return scene;
	}
```

Ajouter include en haut : `#include "engine/world/water/WaterSurfaces.h"`.

- [ ] **Step 5 : Ajouter `WriteWater` à `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h`**

Après `WriteSplatMap` (autour ligne 53), ajouter :

```cpp
	/// Écrit `instances/water.bin` pour la scene fournie (M100.13). Crée le
	/// dossier `instances/` au besoin. Cohérent avec WriteTerrainChunk /
	/// WriteSplatMap.
	bool WriteWater(std::string_view outputRootDir,
		const engine::world::water::WaterScene& scene, std::string& outError);
```

Forward-declare WaterScene en haut du header :
```cpp
namespace engine::world::water { struct WaterScene; }
```

- [ ] **Step 6 : Ajouter impl dans `tools/zone_builder/lib/ChunkPackageWriter.cpp`**

À la fin du fichier (avant la dernière `}` de namespace), ajouter :

```cpp
	bool WriteWater(std::string_view outputRootDir,
		const engine::world::water::WaterScene& scene, std::string& outError)
	{
		const std::filesystem::path dir =
			std::filesystem::path(outputRootDir) / "instances";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		if (ec)
		{
			outError = "WriteWater: mkdir failed: " + ec.message();
			return false;
		}

		std::vector<uint8_t> bytes;
		if (!engine::world::water::SaveWaterBin(scene, bytes, outError))
			return false;

		const std::filesystem::path file = dir / "water.bin";
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		if (!out.good())
		{
			outError = "WriteWater: open failed: " + file.string();
			return false;
		}
		out.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out.good())
		{
			outError = "WriteWater: write failed: " + file.string();
			return false;
		}
		return true;
	}
```

Ajouter include en haut : `#include "engine/world/water/WaterSurfaces.h"`.

- [ ] **Step 7 : Ajouter `WaterDocument.cpp` à engine_core dans CMakeLists.txt**

Près des autres fichiers `engine/editor/world/*.cpp` (autour ligne 277-280) :

```cmake
  engine/editor/world/WaterDocument.cpp
```

Note : `tools/zone_builder/lib/CMakeLists.txt` peut nécessiter ajout des sources water si `WaterScene` est utilisé. Vérifier le pattern : si `tools/zone_builder/lib/` duplique des sources de `engine/world/...`, ajouter `engine/world/water/WaterSurfaces.cpp` à la liste de sources zone_builder_lib.

- [ ] **Step 8 : Commit**

```bash
git add engine/editor/world/WaterDocument.h \
        engine/editor/world/WaterDocument.cpp \
        engine/world/StreamCache.h \
        engine/world/StreamCache.cpp \
        tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h \
        tools/zone_builder/lib/ChunkPackageWriter.cpp \
        CMakeLists.txt \
        tools/zone_builder/lib/CMakeLists.txt
git commit -m "feat(world+editor+tools): WaterDocument + StreamCache::LoadWater + zone_builder WriteWater (M100.13 Task 3)"
```

---

## Task 4: AddLakeCommand + AddRiverCommand (ICommand impls, no tests)

**Files:**
- Create: `engine/editor/world/AddLakeCommand.h`
- Create: `engine/editor/world/AddLakeCommand.cpp`
- Create: `engine/editor/world/AddRiverCommand.h`
- Create: `engine/editor/world/AddRiverCommand.cpp`
- Modify: `CMakeLists.txt`

Pas de tests dédiés (pattern testé via `TerrainSculptCommand` M100.6).

- [ ] **Step 1 : Créer `engine/editor/world/AddLakeCommand.h`**

```cpp
// engine/editor/world/AddLakeCommand.h
#pragma once

#include "engine/editor/world/CommandStack.h"
#include "engine/editor/world/WaterDocument.h"
#include "engine/world/water/WaterSurfaces.h"

namespace engine::editor::world
{
	/// Commande undoable : ajoute une `LakeInstance` au WaterDocument (M100.13).
	/// Execute → push_back ; Undo → pop_back. Marque le doc dirty.
	class AddLakeCommand : public ICommand
	{
	public:
		AddLakeCommand(WaterDocument& doc, engine::world::water::LakeInstance lake) noexcept
			: m_doc(&doc), m_lake(std::move(lake)) {}

		const char* GetLabel() const override { return "Add Lake"; }
		size_t      GetMemoryFootprint() const override;
		void        Execute() override;
		void        Undo() override;

	private:
		WaterDocument*                          m_doc;
		engine::world::water::LakeInstance      m_lake;
	};
}
```

- [ ] **Step 2 : Créer `engine/editor/world/AddLakeCommand.cpp`**

```cpp
// engine/editor/world/AddLakeCommand.cpp
#include "engine/editor/world/AddLakeCommand.h"

namespace engine::editor::world
{
	size_t AddLakeCommand::GetMemoryFootprint() const
	{
		// Approximation : lake.name + lake.polygon vertices + struct overhead
		return sizeof(*this) + m_lake.name.size() + m_lake.polygon.size() * sizeof(engine::math::Vec3);
	}

	void AddLakeCommand::Execute()
	{
		m_doc->Mutable().lakes.push_back(m_lake);
		m_doc->MarkDirty();
	}

	void AddLakeCommand::Undo()
	{
		// Précondition (cf. ICommand) : Execute a été appelé en dernier ;
		// le lake en sommet est notre lake.
		if (!m_doc->Mutable().lakes.empty())
			m_doc->Mutable().lakes.pop_back();
		m_doc->MarkDirty();
	}
}
```

- [ ] **Step 3 : Créer `engine/editor/world/AddRiverCommand.h`**

```cpp
// engine/editor/world/AddRiverCommand.h
#pragma once

#include "engine/editor/world/CommandStack.h"
#include "engine/editor/world/WaterDocument.h"
#include "engine/world/water/WaterSurfaces.h"

namespace engine::editor::world
{
	/// Commande undoable : ajoute une `RiverInstance` au WaterDocument (M100.13).
	/// Execute → push_back ; Undo → pop_back. Marque le doc dirty.
	class AddRiverCommand : public ICommand
	{
	public:
		AddRiverCommand(WaterDocument& doc, engine::world::water::RiverInstance river) noexcept
			: m_doc(&doc), m_river(std::move(river)) {}

		const char* GetLabel() const override { return "Add River"; }
		size_t      GetMemoryFootprint() const override;
		void        Execute() override;
		void        Undo() override;

	private:
		WaterDocument*                          m_doc;
		engine::world::water::RiverInstance     m_river;
	};
}
```

- [ ] **Step 4 : Créer `engine/editor/world/AddRiverCommand.cpp`**

```cpp
// engine/editor/world/AddRiverCommand.cpp
#include "engine/editor/world/AddRiverCommand.h"

namespace engine::editor::world
{
	size_t AddRiverCommand::GetMemoryFootprint() const
	{
		return sizeof(*this) + m_river.name.size()
			+ m_river.nodes.size() * sizeof(engine::world::water::RiverNode);
	}

	void AddRiverCommand::Execute()
	{
		m_doc->Mutable().rivers.push_back(m_river);
		m_doc->MarkDirty();
	}

	void AddRiverCommand::Undo()
	{
		if (!m_doc->Mutable().rivers.empty())
			m_doc->Mutable().rivers.pop_back();
		m_doc->MarkDirty();
	}
}
```

- [ ] **Step 5 : Ajouter sources à engine_core dans CMakeLists.txt**

Près des autres `engine/editor/world/*.cpp` :

```cmake
  engine/editor/world/AddLakeCommand.cpp
  engine/editor/world/AddRiverCommand.cpp
```

- [ ] **Step 6 : Commit**

```bash
git add engine/editor/world/AddLakeCommand.h \
        engine/editor/world/AddLakeCommand.cpp \
        engine/editor/world/AddRiverCommand.h \
        engine/editor/world/AddRiverCommand.cpp \
        CMakeLists.txt
git commit -m "feat(editor/world): AddLakeCommand + AddRiverCommand undoable (M100.13 Task 4)"
```

---

## Task 5: LakeTool + RiverTool (state containers, no tests)

**Files:**
- Create: `engine/editor/world/LakeTool.h`
- Create: `engine/editor/world/LakeTool.cpp`
- Create: `engine/editor/world/RiverTool.h`
- Create: `engine/editor/world/RiverTool.cpp`
- Modify: `CMakeLists.txt`

Pas de tests (state containers triviaux, validation visuelle dans le panel).

- [ ] **Step 1 : Créer `engine/editor/world/LakeTool.h`**

```cpp
// engine/editor/world/LakeTool.h
#pragma once

#include "engine/editor/world/WaterDocument.h"
#include "engine/math/Math.h"

#include <vector>

namespace engine::editor::world
{
	class CommandStack;

	/// Outil d'édition d'un lac (M100.13). État : un polygone en cours de
	/// construction (pas encore committé) + référence au document partagé.
	/// Workflow : AddPoint(xz) répété → ClosePolygon() commit le lac dans
	/// le document via AddLakeCommand. Cancel() abandonne sans commit.
	class LakeTool
	{
	public:
		bool Init(CommandStack& stack, WaterDocument& waterDoc) noexcept;

		/// Ajoute un point au polygone en cours. Y = m_currentWaterLevelY.
		void AddPoint(float worldX, float worldZ);

		/// Ferme le polygone et commit comme nouveau lac via AddLakeCommand.
		/// No-op si < 3 points.
		void ClosePolygon();

		/// Abandonne le polygone en cours (vide la liste de points).
		void Cancel() noexcept;

		bool   HasActivePolygon() const noexcept { return !m_currentPoints.empty(); }
		size_t GetPointCount()    const noexcept { return m_currentPoints.size(); }
		const std::vector<engine::math::Vec3>& GetCurrentPoints() const { return m_currentPoints; }

		float& MutableWaterLevelY() noexcept { return m_currentWaterLevelY; }
		engine::math::Vec3& MutableBottomColor() noexcept { return m_currentBottomColor; }
		float& MutableTurbidity() noexcept { return m_currentTurbidity; }

	private:
		CommandStack*  m_stack = nullptr;
		WaterDocument* m_doc   = nullptr;
		std::vector<engine::math::Vec3> m_currentPoints;
		float m_currentWaterLevelY = 0.0f;
		engine::math::Vec3 m_currentBottomColor{ 0.05f, 0.20f, 0.30f };
		float m_currentTurbidity = 0.4f;
	};
}
```

- [ ] **Step 2 : Créer `engine/editor/world/LakeTool.cpp`**

```cpp
// engine/editor/world/LakeTool.cpp
#include "engine/editor/world/LakeTool.h"
#include "engine/editor/world/AddLakeCommand.h"
#include "engine/editor/world/CommandStack.h"

#include <memory>
#include <string>

namespace engine::editor::world
{
	bool LakeTool::Init(CommandStack& stack, WaterDocument& waterDoc) noexcept
	{
		m_stack = &stack;
		m_doc   = &waterDoc;
		return true;
	}

	void LakeTool::AddPoint(float worldX, float worldZ)
	{
		m_currentPoints.push_back({ worldX, m_currentWaterLevelY, worldZ });
	}

	void LakeTool::ClosePolygon()
	{
		if (!m_stack || !m_doc) return;
		if (m_currentPoints.size() < 3) return;

		engine::world::water::LakeInstance lake;
		lake.name = "lake_" + std::to_string(m_doc->Get().lakes.size() + 1);
		lake.polygon = m_currentPoints;
		lake.waterLevelY = m_currentWaterLevelY;
		lake.bottomColor = m_currentBottomColor;
		lake.turbidity = m_currentTurbidity;

		m_stack->Push(std::make_unique<AddLakeCommand>(*m_doc, std::move(lake)));
		m_currentPoints.clear();
	}

	void LakeTool::Cancel() noexcept
	{
		m_currentPoints.clear();
	}
}
```

- [ ] **Step 3 : Créer `engine/editor/world/RiverTool.h`**

```cpp
// engine/editor/world/RiverTool.h
#pragma once

#include "engine/editor/world/WaterDocument.h"
#include "engine/world/water/WaterSurfaces.h"

#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class CommandStack;
	class TerrainDocument;

	/// Outil d'édition d'une rivière (M100.13). État : nodes en cours de
	/// construction. AddNode(xz) sample la heightmap via TerrainDocument
	/// pour fixer Y. EndSpline() commit comme nouveau river via AddRiverCommand.
	class RiverTool
	{
	public:
		bool Init(CommandStack& stack,
			WaterDocument& waterDoc,
			TerrainDocument& terrainDoc,
			const engine::core::Config& cfg) noexcept;

		/// Ajoute un node à la rivière en cours.
		/// Y = TerrainDocument::EnsureLoaded(...)→SampleHeight.
		/// Si chunk pas chargeable → fallback Y=0.0.
		void AddNode(float worldX, float worldZ);

		/// Termine la spline et commit via AddRiverCommand. No-op si < 2 nodes.
		void EndSpline();

		void Cancel() noexcept;

		bool   HasActiveRiver() const noexcept { return !m_currentNodes.empty(); }
		size_t GetNodeCount()   const noexcept { return m_currentNodes.size(); }
		const std::vector<engine::world::water::RiverNode>& GetCurrentNodes() const { return m_currentNodes; }

		float& MutableDefaultWidth() noexcept { return m_defaultWidth; }
		float& MutableDefaultDepth() noexcept { return m_defaultDepth; }

	private:
		CommandStack*               m_stack       = nullptr;
		WaterDocument*              m_doc         = nullptr;
		TerrainDocument*            m_terrainDoc  = nullptr;
		const engine::core::Config* m_cfg         = nullptr;
		std::vector<engine::world::water::RiverNode> m_currentNodes;
		float m_defaultWidth = 4.0f;
		float m_defaultDepth = 1.0f;
	};
}
```

- [ ] **Step 4 : Créer `engine/editor/world/RiverTool.cpp`**

```cpp
// engine/editor/world/RiverTool.cpp
#include "engine/editor/world/RiverTool.h"
#include "engine/editor/world/AddRiverCommand.h"
#include "engine/editor/world/CommandStack.h"
#include "engine/editor/world/TerrainDocument.h"
#include "engine/world/WorldModel.h"
#include "engine/world/terrain/TerrainChunk.h"

#include <memory>
#include <string>

namespace engine::editor::world
{
	bool RiverTool::Init(CommandStack& stack,
		WaterDocument& waterDoc, TerrainDocument& terrainDoc,
		const engine::core::Config& cfg) noexcept
	{
		m_stack       = &stack;
		m_doc         = &waterDoc;
		m_terrainDoc  = &terrainDoc;
		m_cfg         = &cfg;
		return true;
	}

	void RiverTool::AddNode(float worldX, float worldZ)
	{
		float y = 0.0f;
		if (m_terrainDoc && m_cfg)
		{
			const auto coord = engine::world::WorldToGlobalChunkCoord(worldX, worldZ);
			auto chunk = m_terrainDoc->EnsureLoaded(*m_cfg, coord.x, coord.z);
			if (chunk)
			{
				const auto bounds = engine::world::ChunkBounds(coord);
				const float localX = worldX - bounds.minX;
				const float localZ = worldZ - bounds.minZ;
				y = chunk->SampleHeight(localX, localZ);
			}
		}

		engine::world::water::RiverNode node;
		node.position = engine::math::Vec3{ worldX, y, worldZ };
		node.widthMeters = m_defaultWidth;
		node.depthMeters = m_defaultDepth;
		m_currentNodes.push_back(node);
	}

	void RiverTool::EndSpline()
	{
		if (!m_stack || !m_doc) return;
		if (m_currentNodes.size() < 2) return;

		engine::world::water::RiverInstance river;
		river.name = "river_" + std::to_string(m_doc->Get().rivers.size() + 1);
		river.nodes = m_currentNodes;

		m_stack->Push(std::make_unique<AddRiverCommand>(*m_doc, std::move(river)));
		m_currentNodes.clear();
	}

	void RiverTool::Cancel() noexcept
	{
		m_currentNodes.clear();
	}
}
```

- [ ] **Step 5 : Ajouter sources à engine_core dans CMakeLists.txt**

```cmake
  engine/editor/world/LakeTool.cpp
  engine/editor/world/RiverTool.cpp
```

- [ ] **Step 6 : Commit**

```bash
git add engine/editor/world/LakeTool.h \
        engine/editor/world/LakeTool.cpp \
        engine/editor/world/RiverTool.h \
        engine/editor/world/RiverTool.cpp \
        CMakeLists.txt
git commit -m "feat(editor/world): LakeTool + RiverTool state containers (M100.13 Task 5)"
```

---

## Task 6: WorldEditorShell wiring (ActiveTool::Lake/River + raccourcis L/R + Init)

**Files:**
- Modify: `engine/editor/world/WorldEditorShell.h`
- Modify: `engine/editor/world/WorldEditorShell.cpp`

- [ ] **Step 1 : Modifier `engine/editor/world/WorldEditorShell.h`**

a) Ajouter includes en haut (avec les autres tool includes) :
```cpp
#include "engine/editor/world/LakeTool.h"
#include "engine/editor/world/RiverTool.h"
#include "engine/editor/world/WaterDocument.h"
```

b) Étendre l'enum `ActiveTool` (autour ligne 25-31). Avant :
```cpp
enum class ActiveTool : uint8_t
{
    None          = 0,
    TerrainSculpt = 1,
    TerrainStamp  = 2,
    SplatPaint    = 3,
};
```

Après :
```cpp
enum class ActiveTool : uint8_t
{
    None          = 0,
    TerrainSculpt = 1,
    TerrainStamp  = 2,
    SplatPaint    = 3,
    Lake          = 4,  // M100.13 — raccourci L
    River         = 5,  // M100.13 — raccourci R
};
```

c) Ajouter membres dans la section privée (après `m_splatPaintTool`) :
```cpp
		LakeTool       m_lakeTool;       // M100.13
		RiverTool      m_riverTool;      // M100.13
		WaterDocument  m_waterDoc;       // M100.13
```

d) Ajouter accesseurs publics (après `MutableSplatPaintTool`) :
```cpp
		LakeTool&             MutableLakeTool()        { return m_lakeTool; }
		const LakeTool&       GetLakeTool()      const { return m_lakeTool; }
		RiverTool&            MutableRiverTool()       { return m_riverTool; }
		const RiverTool&      GetRiverTool()     const { return m_riverTool; }
		WaterDocument&        MutableWaterDocument()       { return m_waterDoc; }
		const WaterDocument&  GetWaterDocument()     const { return m_waterDoc; }
```

- [ ] **Step 2 : Modifier `engine/editor/world/WorldEditorShell.cpp`**

a) Mettre à jour le commentaire Doxygen `Init()` (autour ligne 53-58). Avant :
```cpp
/// est figé : 0=Scene, 1=Inspector, 2=AssetBrowser, 3=Outliner, 4=Console,
/// 5=ToolProperties, 6=History (M100.2), 7=SurfaceTable (M100.11),
/// 8=CollisionEditor (M100.12) — référencé par les tests M100.1.
```

(Pas de modification : Phase 4a n'ajoute pas de panel, juste 2 outils dans ToolPropertiesPanel.)

b) Dans `Init()`, après l'init du `m_splatPaintTool` (autour ligne 123-126), ajouter :

```cpp
		// M100.13 — Init des outils Water (Lake + River) et chargement initial
		// du WaterDocument depuis instances/water.bin. LoadFromDisk retourne
		// true silencieusement si le fichier n'existe pas (premier lancement).
		m_lakeTool.Init(m_commandStack, m_waterDoc);
		m_riverTool.Init(m_commandStack, m_waterDoc, m_terrainDoc, cfg);
		std::string waterErr;
		if (!m_waterDoc.LoadFromDisk(cfg, waterErr))
		{
			LOG_WARN(EditorWorld, "[WorldEditorShell] Water LoadFromDisk failed: {}", waterErr);
		}
```

c) Étendre `SetActiveTool` switch (autour ligne 159-174). Avant :
```cpp
case ActiveTool::None:          name = "None"; break;
case ActiveTool::TerrainSculpt: name = "TerrainSculpt"; break;
case ActiveTool::TerrainStamp:  name = "TerrainStamp"; break;
case ActiveTool::SplatPaint:    name = "SplatPaint"; break;
```

Ajouter 2 cases :
```cpp
case ActiveTool::Lake:          name = "Lake"; break;
case ActiveTool::River:         name = "River"; break;
```

d) Ajouter raccourcis `L` et `R` dans `HandleShortcut` (chercher où `'B'`, `'N'`, `'P'` sont gérés) :

```cpp
		case 'L':  // M100.13 — Lake tool
			SetActiveTool(ActiveTool::Lake);
			handled = true;
			break;
		case 'R':  // M100.13 — River tool
			SetActiveTool(ActiveTool::River);
			handled = true;
			break;
```

(Si `HandleShortcut` utilise un autre pattern de dispatch, suivre celui en place.)

- [ ] **Step 3 : Commit**

```bash
git add engine/editor/world/WorldEditorShell.h \
        engine/editor/world/WorldEditorShell.cpp
git commit -m "feat(editor/world): WorldEditorShell wiring Lake/River tools + raccourcis L/R (M100.13 Task 6)"
```

---

## Task 7: ToolPropertiesPanel RenderLake/RiverParams + canvas helper

**Files:**
- Modify: `engine/editor/world/panels/ToolPropertiesPanel.h`
- Modify: `engine/editor/world/panels/ToolPropertiesPanel.cpp`

- [ ] **Step 1 : Ajouter declarations dans `ToolPropertiesPanel.h`**

Après les autres `RenderXxxParams` declarations privées :

```cpp
		void RenderLakeParams(engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::LakeTool& tool);
		void RenderRiverParams(engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::RiverTool& tool);
```

- [ ] **Step 2 : Ajouter cases dans le switch de `Render()` du `.cpp`**

Trouver le switch sur `GetActiveTool()` (autour ligne 213). Ajouter avant le `default` :

```cpp
			else if (m_shell &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::Lake)
			{
				RenderLakeParams(*m_shell, m_shell->MutableLakeTool());
			}
			else if (m_shell &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::River)
			{
				RenderRiverParams(*m_shell, m_shell->MutableRiverTool());
			}
```

(Le pattern exact dépend du code existant — utiliser la même structure if/else if que pour SplatPaint.)

- [ ] **Step 3 : Ajouter le helper canvas top-down 2D dans une anonymous namespace au `.cpp`**

```cpp
namespace
{
	// M100.13 — Helper pour le mini-canvas 2D top-down dans LakeTool/RiverTool.
	struct CanvasState
	{
		float boundsHalfMeters = 50.0f;
		float centerWorldX = 0.0f;
		float centerWorldZ = 0.0f;
	};

	struct CanvasInput
	{
		bool   leftClicked  = false;
		bool   rightClicked = false;
		float  worldX = 0.0f;
		float  worldZ = 0.0f;
	};

	/// Convertit pixel canvas (0..W, 0..H) → coords monde XZ.
	void PixelToWorld(const CanvasState& s, float w, float h, float px, float py,
		float& outX, float& outZ)
	{
		const float fx = (px / w) * 2.0f - 1.0f;
		const float fy = (py / h) * 2.0f - 1.0f;
		outX = s.centerWorldX + fx * s.boundsHalfMeters;
		outZ = s.centerWorldZ - fy * s.boundsHalfMeters;
	}

	/// Convertit coords monde XZ → pixel canvas.
	void WorldToPixel(const CanvasState& s, float w, float h, float worldX, float worldZ,
		float& outPx, float& outPy)
	{
		const float fx = (worldX - s.centerWorldX) / s.boundsHalfMeters;
		const float fy = (s.centerWorldZ - worldZ) / s.boundsHalfMeters;
		outPx = (fx * 0.5f + 0.5f) * w;
		outPy = (fy * 0.5f + 0.5f) * h;
	}

	/// Rend le canvas 2D top-down. Affiche existing scene en gris + currentPolygon
	/// en jaune + currentNodes en cyan. Retourne les events souris.
	CanvasInput RenderTopDownCanvas(
		CanvasState& state,
		const engine::world::water::WaterScene& existingScene,
		const std::vector<engine::math::Vec3>* currentPolygon,
		const std::vector<engine::world::water::RiverNode>* currentNodes)
	{
		CanvasInput input;

#if defined(_WIN32)
		ImGui::SliderFloat("Bounds (m)", &state.boundsHalfMeters, 5.0f, 500.0f, "%.1f");
		ImGui::SameLine();
		if (ImGui::Button("Recenter"))
		{
			state.centerWorldX = 0.0f;
			state.centerWorldZ = 0.0f;
		}

		const ImVec2 canvasSize{ 300.0f, 300.0f };
		ImGui::BeginChild("##waterCanvas", canvasSize, true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const ImVec2 contentSize = ImGui::GetContentRegionAvail();
		const float w = contentSize.x;
		const float h = contentSize.y;

		ImGui::InvisibleButton("##canvasInteract", contentSize);
		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			const ImVec2 mp = ImGui::GetIO().MousePos;
			const float px = mp.x - canvasMin.x;
			const float py = mp.y - canvasMin.y;
			float wx = 0.0f, wz = 0.0f;
			PixelToWorld(state, w, h, px, py, wx, wz);
			input.worldX = wx;
			input.worldZ = wz;
			ImGui::SetTooltip("world: (%.1f, %.1f)", wx, wz);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))  input.leftClicked = true;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) input.rightClicked = true;
		}

		ImDrawList* dl = ImGui::GetWindowDrawList();

		// Croix au centre (origine monde)
		float cx = 0.0f, cy = 0.0f;
		WorldToPixel(state, w, h, 0.0f, 0.0f, cx, cy);
		dl->AddLine(ImVec2(canvasMin.x + cx - 5, canvasMin.y + cy),
		            ImVec2(canvasMin.x + cx + 5, canvasMin.y + cy),
		            IM_COL32(255, 255, 255, 100), 1.0f);
		dl->AddLine(ImVec2(canvasMin.x + cx, canvasMin.y + cy - 5),
		            ImVec2(canvasMin.x + cx, canvasMin.y + cy + 5),
		            IM_COL32(255, 255, 255, 100), 1.0f);

		auto drawPolygonClosed = [&](const std::vector<engine::math::Vec3>& poly, ImU32 color)
		{
			if (poly.size() < 2) return;
			for (size_t i = 0; i < poly.size(); ++i)
			{
				const auto& a = poly[i];
				const auto& b = poly[(i + 1) % poly.size()];
				float ax, ay, bx, by;
				WorldToPixel(state, w, h, a.x, a.z, ax, ay);
				WorldToPixel(state, w, h, b.x, b.z, bx, by);
				dl->AddLine(ImVec2(canvasMin.x + ax, canvasMin.y + ay),
				            ImVec2(canvasMin.x + bx, canvasMin.y + by),
				            color, 1.5f);
			}
			for (const auto& v : poly)
			{
				float px, py;
				WorldToPixel(state, w, h, v.x, v.z, px, py);
				dl->AddCircleFilled(ImVec2(canvasMin.x + px, canvasMin.y + py), 3.0f, color);
			}
		};

		auto drawPolyline = [&](const std::vector<engine::math::Vec3>& nodes, ImU32 color)
		{
			for (size_t i = 0; i + 1 < nodes.size(); ++i)
			{
				const auto& a = nodes[i];
				const auto& b = nodes[i + 1];
				float ax, ay, bx, by;
				WorldToPixel(state, w, h, a.x, a.z, ax, ay);
				WorldToPixel(state, w, h, b.x, b.z, bx, by);
				dl->AddLine(ImVec2(canvasMin.x + ax, canvasMin.y + ay),
				            ImVec2(canvasMin.x + bx, canvasMin.y + by),
				            color, 1.5f);
			}
			for (const auto& v : nodes)
			{
				float px, py;
				WorldToPixel(state, w, h, v.x, v.z, px, py);
				dl->AddCircleFilled(ImVec2(canvasMin.x + px, canvasMin.y + py), 3.0f, color);
			}
		};

		// Existing lakes en gris clair
		for (const auto& lake : existingScene.lakes)
			drawPolygonClosed(lake.polygon, IM_COL32(180, 180, 180, 200));

		// Existing rivers en gris foncé
		for (const auto& river : existingScene.rivers)
		{
			std::vector<engine::math::Vec3> positions;
			positions.reserve(river.nodes.size());
			for (const auto& n : river.nodes) positions.push_back(n.position);
			drawPolyline(positions, IM_COL32(140, 140, 180, 200));
		}

		// Current polygon (lake en cours) en jaune
		if (currentPolygon && !currentPolygon->empty())
			drawPolygonClosed(*currentPolygon, IM_COL32(255, 220, 80, 255));

		// Current river nodes en cyan
		if (currentNodes && !currentNodes->empty())
		{
			std::vector<engine::math::Vec3> positions;
			positions.reserve(currentNodes->size());
			for (const auto& n : *currentNodes) positions.push_back(n.position);
			drawPolyline(positions, IM_COL32(80, 220, 255, 255));
		}

		ImGui::EndChild();
#else
		(void)state; (void)existingScene; (void)currentPolygon; (void)currentNodes;
#endif
		return input;
	}

	// State persistant entre frames pour le canvas (un état partagé Lake/River OK
	// car les deux outils ne sont pas actifs simultanément).
	CanvasState g_canvasState;
}
```

- [ ] **Step 4 : Implémenter `RenderLakeParams` dans le `.cpp`**

```cpp
void ToolPropertiesPanel::RenderLakeParams(
	engine::editor::world::WorldEditorShell& shell,
	engine::editor::world::LakeTool& tool)
{
#if defined(_WIN32)
	ImGui::Text("Lake Tool — M100.13");
	ImGui::Separator();

	ImGui::Text("Default values for next lake :");
	ImGui::SliderFloat("Water Level Y", &tool.MutableWaterLevelY(), -50.0f, 50.0f, "%.3f");
	ImGui::ColorEdit3("Bottom Color", &tool.MutableBottomColor().x);
	ImGui::SliderFloat("Turbidity", &tool.MutableTurbidity(), 0.0f, 1.0f, "%.2f");
	ImGui::Separator();

	ImGui::Text("Current polygon : %zu points", tool.GetPointCount());
	const bool canClose = tool.GetPointCount() >= 3;
	ImGui::BeginDisabled(!canClose);
	if (ImGui::Button("Close polygon (commit lake)"))
		tool.ClosePolygon();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
		tool.Cancel();
	ImGui::Separator();

	ImGui::Text("Top-Down Canvas (LMB add point, RMB cancel)");
	const auto& currentPoints = tool.GetCurrentPoints();
	const auto canvasInput = RenderTopDownCanvas(
		g_canvasState,
		shell.GetWaterDocument().Get(),
		&currentPoints,
		nullptr);
	if (canvasInput.leftClicked)
		tool.AddPoint(canvasInput.worldX, canvasInput.worldZ);
	if (canvasInput.rightClicked)
		tool.Cancel();

	ImGui::Separator();
	ImGui::Text("Existing lakes (%zu) :", shell.GetWaterDocument().Get().lakes.size());
	if (ImGui::BeginTable("##lakes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Pts", ImGuiTableColumnFlags_WidthFixed, 50);
		ImGui::TableSetupColumn("Y-level", ImGuiTableColumnFlags_WidthFixed, 70);
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();
		const auto& lakes = shell.GetWaterDocument().Get().lakes;
		for (size_t i = 0; i < lakes.size(); ++i)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(lakes[i].name.c_str());
			ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", lakes[i].polygon.size());
			ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", lakes[i].waterLevelY);
			ImGui::TableSetColumnIndex(3);
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextDisabled("(no del)");  // Suppression viendra avec edit ticket futur
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
#else
	(void)shell; (void)tool;
#endif
}
```

- [ ] **Step 5 : Implémenter `RenderRiverParams` dans le `.cpp`**

```cpp
void ToolPropertiesPanel::RenderRiverParams(
	engine::editor::world::WorldEditorShell& shell,
	engine::editor::world::RiverTool& tool)
{
#if defined(_WIN32)
	ImGui::Text("River Tool — M100.13");
	ImGui::Separator();

	ImGui::Text("Default values for next node :");
	ImGui::SliderFloat("Width (m)", &tool.MutableDefaultWidth(), 0.5f, 30.0f, "%.2f");
	ImGui::SliderFloat("Depth (m)", &tool.MutableDefaultDepth(), 0.1f, 10.0f, "%.2f");
	ImGui::Separator();

	ImGui::Text("Current river : %zu nodes", tool.GetNodeCount());
	const bool canEnd = tool.GetNodeCount() >= 2;
	ImGui::BeginDisabled(!canEnd);
	if (ImGui::Button("End spline (commit river)"))
		tool.EndSpline();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
		tool.Cancel();
	ImGui::Separator();

	ImGui::Text("Top-Down Canvas (LMB add node, RMB cancel)");
	const auto& currentNodes = tool.GetCurrentNodes();
	const auto canvasInput = RenderTopDownCanvas(
		g_canvasState,
		shell.GetWaterDocument().Get(),
		nullptr,
		&currentNodes);
	if (canvasInput.leftClicked)
		tool.AddNode(canvasInput.worldX, canvasInput.worldZ);
	if (canvasInput.rightClicked)
		tool.Cancel();

	ImGui::Separator();
	ImGui::Text("Existing rivers (%zu) :", shell.GetWaterDocument().Get().rivers.size());
	if (ImGui::BeginTable("##rivers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Nodes", ImGuiTableColumnFlags_WidthFixed, 60);
		ImGui::TableSetupColumn("");
		ImGui::TableHeadersRow();
		const auto& rivers = shell.GetWaterDocument().Get().rivers;
		for (size_t i = 0; i < rivers.size(); ++i)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(rivers[i].name.c_str());
			ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", rivers[i].nodes.size());
			ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("(no del)");
		}
		ImGui::EndTable();
	}
#else
	(void)shell; (void)tool;
#endif
}
```

- [ ] **Step 6 : Ajouter les includes nécessaires en haut du `.cpp`**

```cpp
#include "engine/editor/world/LakeTool.h"
#include "engine/editor/world/RiverTool.h"
```

- [ ] **Step 7 : Commit**

```bash
git add engine/editor/world/panels/ToolPropertiesPanel.h \
        engine/editor/world/panels/ToolPropertiesPanel.cpp
git commit -m "feat(editor/world): ToolPropertiesPanel RenderLake/RiverParams + canvas 2D top-down (M100.13 Task 7)"
```

---

## Task 8: Validation finale + grep gardien serveur + INDEX.md

**Files:**
- (validation only)
- Modify: `tickets/M100/INDEX.md`

- [ ] **Step 1 : Vérifier que le serveur ne référence pas `engine::world::water`**

```bash
grep -rn "engine::world::water\|WaterScene\|LakeInstance\|RiverInstance\|WaterDocument\|LakeTool\|RiverTool" engine/server/ tools/migration_checksum/ tools/load_tester/ tools/hlod_builder/ tools/gen_terrain_placeholders/ 2>&1
```

Expected : aucun résultat. Note : `tools/zone_builder/` peut référencer `WaterScene` via `WriteWater` — c'est attendu (le CLI batch est un tool client-side).

- [ ] **Step 2 : Vérifier que `GeometryPass.cpp` n'a pas été modifié**

```bash
git diff origin/main -- engine/render/GeometryPass.cpp
```

Expected : aucune diff.

- [ ] **Step 3 : Vérifier que `FrameGraph.cpp` n'a pas été modifié**

```bash
git diff origin/main -- engine/render/FrameGraph.cpp
```

Expected : aucune diff.

- [ ] **Step 4 : Mettre à jour `tickets/M100/INDEX.md`**

Dans le tableau, ligne M100.13, changer `Statut` :

```diff
- | M100.13 | Water Surfaces (Lakes & Rivers) | 4 — Hydrologie & Hazards | M100.5, M100.6 | Ready |
+ | M100.13 | Water Surfaces (Lakes & Rivers) | 4 — Hydrologie & Hazards | M100.5, M100.6 | Done (CI pending) |
```

- [ ] **Step 5 : Commit**

```bash
git add tickets/M100/INDEX.md
git commit -m "docs(tickets/M100): marque M100.13 Done (Phase 4a, CI pending)"
```

---

## Récap couverture spec → tasks

| Section spec | Task |
|---|---|
| Architecture (file structure) | T1-T7 (création/modif fichiers exact selon spec) |
| `WaterSurfaces.h` API + format binaire | T1 |
| `SaveWaterBin` / `LoadWaterBin` round-trip | T1 (5 cas) |
| `ComputeFlowDirections` | T2 (test inclus dans water_surfaces_tests) |
| `WaterMeshBuilder` ear clipping + ribbon | T2 (5 cas) |
| `WaterDocument` editor state | T3 |
| `StreamCache::LoadWater` consommation client | T3 |
| `tools/zone_builder/lib WriteWater` | T3 |
| `AddLakeCommand` + `AddRiverCommand` ICommand | T4 |
| `LakeTool` + `RiverTool` state containers | T5 |
| `WorldEditorShell` ActiveTool::Lake/River + raccourcis L/R + wiring | T6 |
| `ToolPropertiesPanel::RenderLake/RiverParams` + canvas 2D | T7 |
| Test grep gardien serveur | T8 |
| GeometryPass non modifié + FrameGraph non modifié | T8 |
| INDEX.md | T8 |
| 2 suites tests TDD (~11 cas) | T1, T2 |

## Self-Review

**1. Spec coverage:** Toutes les sections du spec ont une task qui les implémente. Le chunk-level partitioning, hot-reload, multi-zone, cascades, rendu visuel, hook gameplay nage sont tous explicitement out-of-scope dans le spec — pas de gap.

**2. Placeholder scan:** Aucun "TBD"/"TODO"/"implement later" dans les steps de code. Les commentaires `// M100.13 — ...` dans le code source sont des références ticket, pas des placeholders.

**3. Type consistency:**
- `WaterScene`, `LakeInstance`, `RiverInstance`, `RiverNode`, `WaterMeshCpu`, `WaterVertex`, `LakeTool`, `RiverTool`, `WaterDocument`, `AddLakeCommand`, `AddRiverCommand` : noms stables Tasks 1-7.
- `ICommand` interface utilise `Execute()` / `Undo()` / `GetMemoryFootprint()` — vérifié vs `engine/editor/world/CommandStack.h:23-61`. Tasks 4 utilise les bons noms.
- `engine::world::WorldToGlobalChunkCoord(float, float) → GlobalChunkCoord` : free function, vérifié `engine/world/WorldModel.h:146`.
- `engine::world::ChunkBounds(GlobalChunkCoord) → struct ChunkBounds { minX, minZ, maxX, maxZ }` : free function, vérifié `engine/world/WorldModel.h:152`.
- `engine::world::terrain::TerrainChunk::SampleHeight(float localX, float localZ) const → float` : vérifié `engine/world/terrain/TerrainChunk.h:52`.
- `engine::world::ComputeXxHash64(span<uint8_t>) → uint64_t` : vérifié `engine/world/OutputVersion.h:44`.
- `engine::world::ReadOutputVersionHeader(span, &hdr, &err) → bool` : vérifié `engine/world/OutputVersion.h:57`.
- `kZoneBuilderVersion` / `kZoneEngineVersion` : vérifié `engine/world/OutputVersion.h:27-29`.
- Magic constant `0x52544157u` ("WATR" little-endian) : vérification 'W'(0x57)+'A'(0x41)+'T'(0x54)+'R'(0x52) lus LE = 0x52544157. ✓
- `TerrainDocument::EnsureLoaded(config, chunkX, chunkZ) → shared_ptr<TerrainChunk>` : vérifié `engine/editor/world/TerrainDocument.h:36`.
- `CommandStack::Push(unique_ptr<ICommand>)` : pattern existant via M100.6.

Plan complet.
