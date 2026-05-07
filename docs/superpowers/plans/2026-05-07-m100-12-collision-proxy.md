# M100.12 Collision Proxy System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Livrer la chaîne complète d'authoring de proxies de collision (M100.12 Phase 3b.2) : struct `CollisionProxy` 3 niveaux (Capsule / ConvexHull / TriMesh), format binaire `.collision.bin` round-trip byte-exact, AutoFit heuristique, wireframe edges generator, mini-camera orbitale, et `CollisionEditorPanel` ImGui avec preview 3D wireframe.

**Architecture:** `engine/world/collision/` est pure CPU (pas de Vulkan, pas d'ImGui). `CollisionEditorPanel` consomme via la mini-preview ImGui DrawList (projection 3D→2D côté CPU via `CollisionPreviewCamera`). Pas de modification de FrameGraph ou GeometryPass. Pattern de sérialisation aligné sur `terrain.bin` / `splat.bin` (OutputVersionHeader 24 bytes + payload + xxhash64).

**Tech Stack:** C++20, hand-rolled REQUIRE test framework (pattern repo, pas Catch2), ImGui pour le panel + DrawList pour le wireframe overlay, `engine::math::Vec3` (existant), `OutputVersionHeader` + `ComputeXxHash64` (existants `engine/world/OutputVersion.{h,cpp}`).

**Spec source:** `docs/superpowers/specs/2026-05-07-m100-12-collision-proxy-design.md`.

---

## File Structure

### Création (10 fichiers)

| Fichier | Rôle |
|---|---|
| `engine/world/collision/CollisionProxy.h` | enum `ProxyType`, struct `CollisionProxy`, magic/version |
| `engine/world/collision/CollisionProxy.cpp` | `SaveToFile` / `LoadFromFile` binary serialize via OutputVersionHeader |
| `engine/world/collision/CollisionMeshCpu.h` | struct `{ vertices, indices, isStatic }` |
| `engine/world/collision/AutoFitProxy.h` | `AutoFit(CollisionMeshCpu) → CollisionProxy` |
| `engine/world/collision/AutoFitProxy.cpp` | dispatch heuristique aspect-ratio + vertex count |
| `engine/world/collision/ProxyWireframe.h` | `GenerateWireframeEdges(proxy) → vector<Edge3D>` |
| `engine/world/collision/ProxyWireframe.cpp` | génération arêtes capsule/hull/trimesh |
| `engine/world/collision/tests/CollisionProxyRoundtripTests.cpp` | 5 cas round-trip + bad magic + bad hash |
| `engine/world/collision/tests/AutoFitProxyTests.cpp` | 4 cas dispatch heuristique |
| `engine/world/collision/tests/ProxyWireframeTests.cpp` | 4 cas edge counts |
| `engine/editor/world/CollisionPreviewCamera.h` | mini-orbit camera + Project(worldPos) |
| `engine/editor/world/CollisionPreviewCamera.cpp` | drag/zoom, projection 3D→pixel |
| `engine/editor/world/panels/CollisionEditorPanel.h` | IPanel + Init |
| `engine/editor/world/panels/CollisionEditorPanel.cpp` | UI form + preview ImGui DrawList |

### Modification (2 fichiers)

| Fichier | Modification |
|---|---|
| `engine/editor/world/WorldEditorShell.cpp` | `#include` + `m_panels.emplace_back(make_unique<CollisionEditorPanel>())` après SurfaceTablePanel |
| `CMakeLists.txt` | + 5 sources dans engine_core (CollisionProxy/AutoFitProxy/ProxyWireframe/CollisionPreviewCamera/CollisionEditorPanel) ; + 3 test executables |

---

## Branch & TDD Workflow

Branche active : `claude/m100-phase-3b-collision-proxy` (déjà créée sur origin/main, contient le spec committé `79d2ca9`).

Chaque task : red (test fail) → green (impl minimale) → commit. Build local non disponible (CMake/MSBuild absents) — verification deferred to CI, comme M100.11.

---

## Task 1: CollisionProxy struct + serialization (5 round-trip tests)

**Files:**
- Create: `engine/world/collision/CollisionProxy.h`
- Create: `engine/world/collision/CollisionProxy.cpp`
- Create: `engine/world/collision/tests/CollisionProxyRoundtripTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Écrire le header `engine/world/collision/CollisionProxy.h`**

```cpp
// engine/world/collision/CollisionProxy.h
#pragma once

#include "engine/math/Math.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::world::collision
{
    /// Type discriminé du proxy de collision (M100.12).
    enum class ProxyType : uint32_t
    {
        Capsule    = 0,
        ConvexHull = 1,
        TriMesh    = 2,
    };

    /// Magic "COLL" little-endian. Format binaire `<asset>.collision.bin`.
    constexpr uint32_t kCollisionMagic   = 0x4C4C4F43u;
    constexpr uint32_t kCollisionVersion = 1u;

    /// Proxy de collision pour un asset mesh. Utilise un sous-ensemble des
    /// champs selon `type` :
    ///  - Capsule : capsuleA, capsuleB, capsuleRadius
    ///  - ConvexHull : vertices (4-N points)
    ///  - TriMesh : vertices + indices (3 indices par triangle)
    struct CollisionProxy
    {
        ProxyType type = ProxyType::Capsule;

        // Capsule
        engine::math::Vec3 capsuleA{ 0.0f, -0.5f, 0.0f };
        engine::math::Vec3 capsuleB{ 0.0f,  0.5f, 0.0f };
        float              capsuleRadius = 0.5f;

        // ConvexHull / TriMesh
        std::vector<engine::math::Vec3> vertices;
        std::vector<uint32_t>           indices;     // TriMesh seulement

        /// Désérialise depuis disque. Valide magic, version, contentHash.
        /// \return true si OK ; sinon outError renseigné.
        bool LoadFromFile(const std::filesystem::path& path, std::string& outError);

        /// Sérialise sur disque. Écrit OutputVersionHeader + payload type-dependent.
        /// \return true si OK ; sinon outError renseigné.
        bool SaveToFile(const std::filesystem::path& path, std::string& outError) const;
    };
}
```

- [ ] **Step 2 : Écrire les 5 tests dans `engine/world/collision/tests/CollisionProxyRoundtripTests.cpp`**

```cpp
// engine/world/collision/tests/CollisionProxyRoundtripTests.cpp
#include "engine/world/collision/CollisionProxy.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::collision::CollisionProxy;
	using engine::world::collision::ProxyType;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-5f)
	{
		return std::fabs(a - b) <= eps;
	}

	bool VecEq(const Vec3& a, const Vec3& b, float eps = 1e-5f)
	{
		return ApproxEq(a.x, b.x, eps) && ApproxEq(a.y, b.y, eps) && ApproxEq(a.z, b.z, eps);
	}

	void Test_Roundtrip_Capsule()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_capsule.bin";
		CollisionProxy src;
		src.type = ProxyType::Capsule;
		src.capsuleA = Vec3{ 1.0f, -2.0f, 3.0f };
		src.capsuleB = Vec3{ 4.0f,  5.0f, -6.0f };
		src.capsuleRadius = 0.75f;

		std::string err;
		REQUIRE(src.SaveToFile(path, err));
		REQUIRE(err.empty());

		CollisionProxy dst;
		REQUIRE(dst.LoadFromFile(path, err));
		REQUIRE(dst.type == ProxyType::Capsule);
		REQUIRE(VecEq(dst.capsuleA, src.capsuleA));
		REQUIRE(VecEq(dst.capsuleB, src.capsuleB));
		REQUIRE(ApproxEq(dst.capsuleRadius, src.capsuleRadius));

		std::filesystem::remove(path);
	}

	void Test_Roundtrip_ConvexHull()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_hull.bin";
		CollisionProxy src;
		src.type = ProxyType::ConvexHull;
		// 8 vertices d'un bounding box [-1,1]^3
		src.vertices = {
			{-1, -1, -1}, { 1, -1, -1}, {-1,  1, -1}, { 1,  1, -1},
			{-1, -1,  1}, { 1, -1,  1}, {-1,  1,  1}, { 1,  1,  1},
		};

		std::string err;
		REQUIRE(src.SaveToFile(path, err));

		CollisionProxy dst;
		REQUIRE(dst.LoadFromFile(path, err));
		REQUIRE(dst.type == ProxyType::ConvexHull);
		REQUIRE(dst.vertices.size() == 8);
		REQUIRE(std::memcmp(dst.vertices.data(), src.vertices.data(),
			src.vertices.size() * sizeof(Vec3)) == 0);

		std::filesystem::remove(path);
	}

	void Test_Roundtrip_TriMesh()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_trimesh.bin";
		CollisionProxy src;
		src.type = ProxyType::TriMesh;
		src.vertices = {
			{-1, -1, -1}, { 1, -1, -1}, {-1,  1, -1}, { 1,  1, -1},
		};
		// 12 indices = 4 triangles
		src.indices = { 0, 1, 2,  1, 3, 2,  0, 2, 3,  0, 3, 1 };

		std::string err;
		REQUIRE(src.SaveToFile(path, err));

		CollisionProxy dst;
		REQUIRE(dst.LoadFromFile(path, err));
		REQUIRE(dst.type == ProxyType::TriMesh);
		REQUIRE(dst.vertices.size() == 4);
		REQUIRE(dst.indices.size() == 12);
		REQUIRE(std::memcmp(dst.vertices.data(), src.vertices.data(),
			src.vertices.size() * sizeof(Vec3)) == 0);
		REQUIRE(std::memcmp(dst.indices.data(), src.indices.data(),
			src.indices.size() * sizeof(uint32_t)) == 0);

		std::filesystem::remove(path);
	}

	void Test_Load_BadMagic_Fails()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_bad_magic.bin";
		// Écrit 24 bytes de header avec un magic invalide
		std::ofstream f(path, std::ios::binary);
		const uint32_t badMagic = 0xDEADBEEFu;
		const uint32_t zeroes[5] = { 1, 1, 1, 0, 0 }; // version + builder + engine + hash low + hash high
		f.write(reinterpret_cast<const char*>(&badMagic), sizeof(badMagic));
		f.write(reinterpret_cast<const char*>(zeroes), sizeof(zeroes));
		// 4 bytes de proxyType + 28 bytes de capsule pour padding minimum
		const uint32_t pType = 0;
		f.write(reinterpret_cast<const char*>(&pType), sizeof(pType));
		const float capsuleData[7] = { 0, 0, 0, 0, 1, 0, 0.5f };
		f.write(reinterpret_cast<const char*>(capsuleData), sizeof(capsuleData));
		f.close();

		CollisionProxy dst;
		std::string err;
		REQUIRE(!dst.LoadFromFile(path, err));
		REQUIRE(err.find("magic") != std::string::npos);

		std::filesystem::remove(path);
	}

	void Test_Load_BadContentHash_Fails()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_bad_hash.bin";
		// Save un capsule valide, puis corrompt le payload (1 byte après header)
		CollisionProxy src;
		src.type = ProxyType::Capsule;
		src.capsuleA = Vec3{ 0, 0, 0 };
		src.capsuleB = Vec3{ 0, 1, 0 };
		src.capsuleRadius = 0.5f;
		std::string err;
		REQUIRE(src.SaveToFile(path, err));

		// Lis tout, corrompt 1 byte du payload (offset 24+8 = byte 32, milieu de capsuleA.x), réécrit
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		const std::streamsize size = in.tellg();
		in.seekg(0);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		in.read(reinterpret_cast<char*>(bytes.data()), size);
		in.close();
		bytes[32] ^= 0xFFu;  // flip un byte du payload

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), size);
		out.close();

		CollisionProxy dst;
		REQUIRE(!dst.LoadFromFile(path, err));
		REQUIRE(err.find("contentHash") != std::string::npos);

		std::filesystem::remove(path);
	}
}

int main()
{
	Test_Roundtrip_Capsule();
	Test_Roundtrip_ConvexHull();
	Test_Roundtrip_TriMesh();
	Test_Load_BadMagic_Fails();
	Test_Load_BadContentHash_Fails();
	return g_failed;
}
```

- [ ] **Step 3 : Ajouter test exécutable au CMakeLists.txt**

Dans `CMakeLists.txt`, après le bloc `client_prediction_surface_multiplier_tests` (M100.11 Task 6), ajouter :

```cmake
# M100.12 — Tests round-trip CollisionProxy (Phase 3b.2).
if(WIN32)
  add_executable(collision_proxy_roundtrip_tests engine/world/collision/tests/CollisionProxyRoundtripTests.cpp)
  target_include_directories(collision_proxy_roundtrip_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(collision_proxy_roundtrip_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(collision_proxy_roundtrip_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME collision_proxy_roundtrip_tests COMMAND collision_proxy_roundtrip_tests)
endif()
```

- [ ] **Step 4 : Créer `engine/world/collision/CollisionProxy.cpp` (impl complète)**

```cpp
// engine/world/collision/CollisionProxy.cpp
#include "engine/world/collision/CollisionProxy.h"

#include "engine/world/OutputVersion.h"

#include <cstring>
#include <fstream>

namespace engine::world::collision
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

		size_t ComputePayloadSize(const CollisionProxy& proxy)
		{
			// 4 bytes pour proxyType
			size_t s = 4u;
			switch (proxy.type)
			{
				case ProxyType::Capsule:
					s += 12u + 12u + 4u;  // a + b + radius
					break;
				case ProxyType::ConvexHull:
					s += 4u + proxy.vertices.size() * 12u;  // vertexCount + vertices
					break;
				case ProxyType::TriMesh:
					s += 4u + 4u + proxy.vertices.size() * 12u + proxy.indices.size() * 4u;
					break;
			}
			return s;
		}
	}

	bool CollisionProxy::SaveToFile(const std::filesystem::path& path, std::string& outError) const
	{
		const size_t headerSize = 24u;
		const size_t payloadSize = ComputePayloadSize(*this);
		const size_t totalSize = headerSize + payloadSize;

		std::vector<uint8_t> bytes(totalSize, 0u);

		// Payload (post-header)
		uint8_t* p = bytes.data() + headerSize;
		p = Write32(p, static_cast<uint32_t>(type));

		switch (type)
		{
			case ProxyType::Capsule:
				p = WriteVec3(p, capsuleA);
				p = WriteVec3(p, capsuleB);
				std::memcpy(p, &capsuleRadius, 4);
				p += 4;
				break;
			case ProxyType::ConvexHull:
			{
				const uint32_t vertCount = static_cast<uint32_t>(vertices.size());
				p = Write32(p, vertCount);
				for (const auto& v : vertices)
					p = WriteVec3(p, v);
				break;
			}
			case ProxyType::TriMesh:
			{
				const uint32_t vertCount = static_cast<uint32_t>(vertices.size());
				const uint32_t idxCount  = static_cast<uint32_t>(indices.size());
				p = Write32(p, vertCount);
				p = Write32(p, idxCount);
				for (const auto& v : vertices)
					p = WriteVec3(p, v);
				if (idxCount > 0)
				{
					std::memcpy(p, indices.data(), idxCount * 4u);
					p += idxCount * 4u;
				}
				break;
			}
		}

		// ContentHash xxhash64 sur payload post-header
		std::span<const uint8_t> payload(bytes.data() + headerSize, totalSize - headerSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		// Header (24 bytes, OutputVersionHeader layout)
		uint8_t* h = bytes.data();
		h = Write32(h, kCollisionMagic);
		h = Write32(h, kCollisionVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "CollisionProxy: cannot open " + path.string() + " for write";
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "CollisionProxy: write failed for " + path.string();
			return false;
		}
		return true;
	}

	bool CollisionProxy::LoadFromFile(const std::filesystem::path& path, std::string& outError)
	{
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			outError = "CollisionProxy: cannot open " + path.string();
			return false;
		}
		const std::streamsize size = f.tellg();
		f.seekg(0);
		if (size < 24 + 4)
		{
			outError = "CollisionProxy: file too small";
			return false;
		}
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);
		if (!f.good() && !f.eof())
		{
			outError = "CollisionProxy: read failed";
			return false;
		}

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError))
			return false;

		if (hdr.magic != kCollisionMagic)
		{
			outError = "CollisionProxy: bad magic (expected COLL)";
			return false;
		}
		if (hdr.formatVersion != kCollisionVersion)
		{
			outError = "CollisionProxy: bad version";
			return false;
		}

		std::span<const uint8_t> payload(bytes.data() + 24, bytes.size() - 24);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{
			outError = "CollisionProxy: contentHash mismatch (file corrupted)";
			return false;
		}

		const uint8_t* p = bytes.data() + 24;
		const uint8_t* end = bytes.data() + bytes.size();
		uint32_t pTypeRaw = 0;
		std::memcpy(&pTypeRaw, p, 4); p += 4;
		if (pTypeRaw > 2u)
		{
			outError = "CollisionProxy: invalid proxyType " + std::to_string(pTypeRaw);
			return false;
		}
		type = static_cast<ProxyType>(pTypeRaw);

		// Reset des champs avant désérialisation
		vertices.clear();
		indices.clear();

		switch (type)
		{
			case ProxyType::Capsule:
				if (end - p < 28)
				{
					outError = "CollisionProxy: capsule payload truncated";
					return false;
				}
				std::memcpy(&capsuleA.x, p,      4);
				std::memcpy(&capsuleA.y, p + 4,  4);
				std::memcpy(&capsuleA.z, p + 8,  4);
				std::memcpy(&capsuleB.x, p + 12, 4);
				std::memcpy(&capsuleB.y, p + 16, 4);
				std::memcpy(&capsuleB.z, p + 20, 4);
				std::memcpy(&capsuleRadius, p + 24, 4);
				break;
			case ProxyType::ConvexHull:
			{
				if (end - p < 4)
				{
					outError = "CollisionProxy: hull header truncated";
					return false;
				}
				uint32_t vertCount = 0;
				std::memcpy(&vertCount, p, 4); p += 4;
				if (static_cast<size_t>(end - p) < vertCount * 12u)
				{
					outError = "CollisionProxy: hull vertices truncated";
					return false;
				}
				vertices.resize(vertCount);
				for (uint32_t i = 0; i < vertCount; ++i)
				{
					std::memcpy(&vertices[i].x, p,     4);
					std::memcpy(&vertices[i].y, p + 4, 4);
					std::memcpy(&vertices[i].z, p + 8, 4);
					p += 12;
				}
				break;
			}
			case ProxyType::TriMesh:
			{
				if (end - p < 8)
				{
					outError = "CollisionProxy: trimesh header truncated";
					return false;
				}
				uint32_t vertCount = 0, idxCount = 0;
				std::memcpy(&vertCount, p,     4);
				std::memcpy(&idxCount,  p + 4, 4);
				p += 8;
				if (static_cast<size_t>(end - p) < vertCount * 12u + idxCount * 4u)
				{
					outError = "CollisionProxy: trimesh payload truncated";
					return false;
				}
				vertices.resize(vertCount);
				for (uint32_t i = 0; i < vertCount; ++i)
				{
					std::memcpy(&vertices[i].x, p,     4);
					std::memcpy(&vertices[i].y, p + 4, 4);
					std::memcpy(&vertices[i].z, p + 8, 4);
					p += 12;
				}
				indices.resize(idxCount);
				if (idxCount > 0)
					std::memcpy(indices.data(), p, idxCount * 4u);
				break;
			}
		}
		return true;
	}
}
```

- [ ] **Step 5 : Ajouter `CollisionProxy.cpp` à engine_core dans CMakeLists.txt**

Dans la liste des sources `engine_core`, près des autres sources `engine/world/*` (autour ligne 367-369 où SurfaceType.cpp / SurfaceTable.cpp / SurfaceQueryService.cpp sont listés), ajouter :

```cmake
  engine/world/collision/CollisionProxy.cpp
```

- [ ] **Step 6 : Commit**

```bash
git add engine/world/collision/CollisionProxy.h \
        engine/world/collision/CollisionProxy.cpp \
        engine/world/collision/tests/CollisionProxyRoundtripTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/collision): CollisionProxy + binary serialize 3 types (M100.12 Task 1)"
```

---

## Task 2: AutoFitProxy heuristique (4 tests)

**Files:**
- Create: `engine/world/collision/CollisionMeshCpu.h`
- Create: `engine/world/collision/AutoFitProxy.h`
- Create: `engine/world/collision/AutoFitProxy.cpp`
- Create: `engine/world/collision/tests/AutoFitProxyTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `engine/world/collision/CollisionMeshCpu.h`**

```cpp
// engine/world/collision/CollisionMeshCpu.h
#pragma once

#include "engine/math/Math.h"

#include <cstdint>
#include <vector>

namespace engine::world::collision
{
    /// Représentation CPU minimale d'un mesh consommée par AutoFit (M100.12).
    /// Pas de matériaux, UVs, normales — juste géométrie.
    /// Le caller fournit les données (pas de loader .obj/.gltf en M100.12).
    struct CollisionMeshCpu
    {
        std::vector<engine::math::Vec3> vertices;
        std::vector<uint32_t>           indices;
        bool                            isStatic = false; // hint pour le dispatch
    };
}
```

- [ ] **Step 2 : Créer `engine/world/collision/AutoFitProxy.h`**

```cpp
// engine/world/collision/AutoFitProxy.h
#pragma once

#include "engine/world/collision/CollisionMeshCpu.h"
#include "engine/world/collision/CollisionProxy.h"

namespace engine::world::collision
{
    /// Choisit automatiquement un proxy à partir d'un mesh CPU (M100.12) :
    ///  - Capsule si height/widthMax > 3 (mesh très vertical, ex. tronc d'arbre)
    ///  - TriMesh si vertices.size() > 500 OU mesh.isStatic == true
    ///  - ConvexHull (= bounding box 8 vertices) sinon
    ///
    /// Note : ConvexHull est un placeholder bounding box. Un vrai quickhull
    /// viendra dans un follow-up si nécessaire pour le gameplay. Le ticket
    /// M100.12 dit explicitement "single pass" — le dispatch lui-même est
    /// le pass unique.
    ///
    /// \param mesh Mesh CPU. Si vide, retourne une capsule par défaut.
    CollisionProxy AutoFit(const CollisionMeshCpu& mesh);
}
```

- [ ] **Step 3 : Écrire les 4 tests dans `engine/world/collision/tests/AutoFitProxyTests.cpp`**

```cpp
// engine/world/collision/tests/AutoFitProxyTests.cpp
#include "engine/world/collision/AutoFitProxy.h"

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

	using engine::world::collision::AutoFit;
	using engine::world::collision::CollisionMeshCpu;
	using engine::world::collision::CollisionProxy;
	using engine::world::collision::ProxyType;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

	/// height = 4.0, widthMax = 0.6 → ratio 6.7 → Capsule.
	CollisionMeshCpu MakeTallCylinderMesh()
	{
		CollisionMeshCpu m;
		// 8 vertices d'un cylindre maigre (cube fin)
		m.vertices = {
			{ -0.3f, -2.0f, -0.3f }, {  0.3f, -2.0f, -0.3f },
			{ -0.3f,  2.0f, -0.3f }, {  0.3f,  2.0f, -0.3f },
			{ -0.3f, -2.0f,  0.3f }, {  0.3f, -2.0f,  0.3f },
			{ -0.3f,  2.0f,  0.3f }, {  0.3f,  2.0f,  0.3f },
		};
		return m;
	}

	/// height = widthMax = 1.0 → ratio 1.0 → ConvexHull.
	CollisionMeshCpu MakeCompactCubeMesh()
	{
		CollisionMeshCpu m;
		m.vertices = {
			{ -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
			{ -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f },
			{ -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f },
			{ -0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f },
		};
		return m;
	}

	/// 800 vertices + isStatic=true → TriMesh.
	CollisionMeshCpu MakeStaticBuildingMesh()
	{
		CollisionMeshCpu m;
		m.isStatic = true;
		m.vertices.reserve(800);
		for (int i = 0; i < 800; ++i)
		{
			const float fi = static_cast<float>(i);
			m.vertices.push_back({ fi * 0.01f, std::sin(fi), fi * 0.005f });
		}
		// Indices triviaux : 798 triangles (i, i+1, i+2)
		for (uint32_t i = 0; i + 2 < 800; ++i)
		{
			m.indices.push_back(i);
			m.indices.push_back(i + 1);
			m.indices.push_back(i + 2);
		}
		return m;
	}

	/// height = 0.01, widthMax = 10 → ratio 0.001 → ConvexHull (pas Capsule).
	CollisionMeshCpu MakeFlatPlaneMesh()
	{
		CollisionMeshCpu m;
		m.vertices = {
			{ -5.0f, 0.0f, -5.0f }, {  5.0f, 0.0f, -5.0f },
			{ -5.0f, 0.01f,  5.0f }, {  5.0f, 0.01f,  5.0f },
		};
		return m;
	}

	void Test_AutoFit_TallSlim_PicksCapsule()
	{
		CollisionProxy p = AutoFit(MakeTallCylinderMesh());
		REQUIRE(p.type == ProxyType::Capsule);
		// La capsule est verticale : capsuleA.y < capsuleB.y, x/z presque alignés
		REQUIRE(p.capsuleA.y < p.capsuleB.y);
		REQUIRE(ApproxEq(p.capsuleA.x, p.capsuleB.x, 1e-3f));
		REQUIRE(ApproxEq(p.capsuleA.z, p.capsuleB.z, 1e-3f));
		// Radius ≈ widthMax / 2 = 0.3
		REQUIRE(ApproxEq(p.capsuleRadius, 0.3f, 1e-3f));
	}

	void Test_AutoFit_Compact_PicksConvexHull()
	{
		CollisionProxy p = AutoFit(MakeCompactCubeMesh());
		REQUIRE(p.type == ProxyType::ConvexHull);
		REQUIRE(p.vertices.size() == 8);  // bounding box 8 verts
	}

	void Test_AutoFit_StaticComplex_PicksTriMesh()
	{
		CollisionProxy p = AutoFit(MakeStaticBuildingMesh());
		REQUIRE(p.type == ProxyType::TriMesh);
		REQUIRE(p.vertices.size() == 800);
		REQUIRE(p.indices.size() > 0);
	}

	void Test_AutoFit_FlatPlane_PicksConvexHull()
	{
		CollisionProxy p = AutoFit(MakeFlatPlaneMesh());
		REQUIRE(p.type == ProxyType::ConvexHull);
		REQUIRE(p.vertices.size() == 8);
	}
}

int main()
{
	Test_AutoFit_TallSlim_PicksCapsule();
	Test_AutoFit_Compact_PicksConvexHull();
	Test_AutoFit_StaticComplex_PicksTriMesh();
	Test_AutoFit_FlatPlane_PicksConvexHull();
	return g_failed;
}
```

- [ ] **Step 4 : Ajouter test exécutable au CMakeLists.txt**

Après le bloc `collision_proxy_roundtrip_tests` :

```cmake
# M100.12 — Tests AutoFit dispatch heuristique.
if(WIN32)
  add_executable(auto_fit_proxy_tests engine/world/collision/tests/AutoFitProxyTests.cpp)
  target_include_directories(auto_fit_proxy_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(auto_fit_proxy_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(auto_fit_proxy_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME auto_fit_proxy_tests COMMAND auto_fit_proxy_tests)
endif()
```

- [ ] **Step 5 : Créer `engine/world/collision/AutoFitProxy.cpp`**

```cpp
// engine/world/collision/AutoFitProxy.cpp
#include "engine/world/collision/AutoFitProxy.h"

#include <algorithm>
#include <cfloat>

namespace engine::world::collision
{
	CollisionProxy AutoFit(const CollisionMeshCpu& mesh)
	{
		CollisionProxy out;

		if (mesh.vertices.empty())
		{
			// Fallback : capsule par défaut. Le caller a donné un mesh vide.
			return out;
		}

		// 1. Bounding box AABB
		engine::math::Vec3 bmin{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
		engine::math::Vec3 bmax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (const auto& v : mesh.vertices)
		{
			bmin.x = std::min(bmin.x, v.x); bmax.x = std::max(bmax.x, v.x);
			bmin.y = std::min(bmin.y, v.y); bmax.y = std::max(bmax.y, v.y);
			bmin.z = std::min(bmin.z, v.z); bmax.z = std::max(bmax.z, v.z);
		}

		const float height   = bmax.y - bmin.y;
		const float widthX   = bmax.x - bmin.x;
		const float widthZ   = bmax.z - bmin.z;
		const float widthMax = std::max(widthX, widthZ);

		// 2. Dispatch
		if (mesh.isStatic || mesh.vertices.size() > 500u)
		{
			out.type = ProxyType::TriMesh;
			out.vertices = mesh.vertices;
			out.indices  = mesh.indices;
		}
		else if (widthMax > 0.0f && (height / widthMax) > 3.0f)
		{
			out.type = ProxyType::Capsule;
			const float r = widthMax * 0.5f;
			const engine::math::Vec3 center{
				(bmin.x + bmax.x) * 0.5f, 0.0f, (bmin.z + bmax.z) * 0.5f };
			out.capsuleA      = engine::math::Vec3{ center.x, bmin.y + r, center.z };
			out.capsuleB      = engine::math::Vec3{ center.x, bmax.y - r, center.z };
			out.capsuleRadius = r;
		}
		else
		{
			out.type = ProxyType::ConvexHull;
			// 8 vertices du bounding box (placeholder pour vrai quickhull)
			out.vertices = {
				{ bmin.x, bmin.y, bmin.z }, { bmax.x, bmin.y, bmin.z },
				{ bmin.x, bmax.y, bmin.z }, { bmax.x, bmax.y, bmin.z },
				{ bmin.x, bmin.y, bmax.z }, { bmax.x, bmin.y, bmax.z },
				{ bmin.x, bmax.y, bmax.z }, { bmax.x, bmax.y, bmax.z },
			};
		}

		return out;
	}
}
```

- [ ] **Step 6 : Ajouter `AutoFitProxy.cpp` à engine_core**

À côté de `CollisionProxy.cpp` :

```cmake
  engine/world/collision/AutoFitProxy.cpp
```

- [ ] **Step 7 : Commit**

```bash
git add engine/world/collision/CollisionMeshCpu.h \
        engine/world/collision/AutoFitProxy.h \
        engine/world/collision/AutoFitProxy.cpp \
        engine/world/collision/tests/AutoFitProxyTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/collision): AutoFit heuristique dispatch capsule/hull/trimesh (M100.12 Task 2)"
```

---

## Task 3: ProxyWireframe edge generator (4 tests)

**Files:**
- Create: `engine/world/collision/ProxyWireframe.h`
- Create: `engine/world/collision/ProxyWireframe.cpp`
- Create: `engine/world/collision/tests/ProxyWireframeTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `engine/world/collision/ProxyWireframe.h`**

```cpp
// engine/world/collision/ProxyWireframe.h
#pragma once

#include "engine/math/Math.h"
#include "engine/world/collision/CollisionProxy.h"

#include <utility>
#include <vector>

namespace engine::world::collision
{
    using Edge3D = std::pair<engine::math::Vec3, engine::math::Vec3>;

    /// Génère les arêtes 3D du wireframe d'un proxy (M100.12). Pour :
    ///  - Capsule : 2 cap rings (16 segments chacun) + 4 longitudinal lines = 36 edges
    ///  - ConvexHull : 12 edges du bounding box (assume 8 vertices structuraux)
    ///  - TriMesh : 3 edges par triangle (peut être beaucoup, sans dédup MVP)
    ///
    /// Pure function, aucune allocation Vulkan, aucune dépendance externe.
    /// Consommé par `CollisionEditorPanel` pour le mini-preview ImGui DrawList.
    std::vector<Edge3D> GenerateWireframeEdges(const CollisionProxy& proxy);
}
```

- [ ] **Step 2 : Écrire les 4 tests dans `engine/world/collision/tests/ProxyWireframeTests.cpp`**

```cpp
// engine/world/collision/tests/ProxyWireframeTests.cpp
#include "engine/world/collision/ProxyWireframe.h"

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

	using engine::world::collision::CollisionProxy;
	using engine::world::collision::ProxyType;
	using engine::world::collision::GenerateWireframeEdges;
	using engine::math::Vec3;

	void Test_Wireframe_Capsule_EdgeCount()
	{
		CollisionProxy p;
		p.type = ProxyType::Capsule;
		p.capsuleA = Vec3{ 0.0f, -1.0f, 0.0f };
		p.capsuleB = Vec3{ 0.0f,  1.0f, 0.0f };
		p.capsuleRadius = 0.3f;

		auto edges = GenerateWireframeEdges(p);
		// 2 cap rings × 16 segments + 4 longitudinal = 36
		REQUIRE(edges.size() == 36);
	}

	void Test_Wireframe_ConvexHull_BoundingBox_12Edges()
	{
		CollisionProxy p;
		p.type = ProxyType::ConvexHull;
		p.vertices = {
			{-1, -1, -1}, { 1, -1, -1}, {-1,  1, -1}, { 1,  1, -1},
			{-1, -1,  1}, { 1, -1,  1}, {-1,  1,  1}, { 1,  1,  1},
		};

		auto edges = GenerateWireframeEdges(p);
		REQUIRE(edges.size() == 12);  // 12 arêtes d'un cube
	}

	void Test_Wireframe_TriMesh_3EdgesPerTriangle()
	{
		CollisionProxy p;
		p.type = ProxyType::TriMesh;
		p.vertices = {
			{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
		};
		// 4 triangles (12 indices)
		p.indices = { 0, 1, 2,  1, 3, 2,  0, 2, 3,  0, 3, 1 };

		auto edges = GenerateWireframeEdges(p);
		REQUIRE(edges.size() == 12);  // 4 tris × 3 edges (sans dédup)
	}

	void Test_Wireframe_EdgesNotEmpty()
	{
		CollisionProxy capsule;
		capsule.type = ProxyType::Capsule;
		REQUIRE(!GenerateWireframeEdges(capsule).empty());

		CollisionProxy hull;
		hull.type = ProxyType::ConvexHull;
		hull.vertices = {
			{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0},
			{0,0,1}, {1,0,1}, {0,1,1}, {1,1,1},
		};
		REQUIRE(!GenerateWireframeEdges(hull).empty());

		CollisionProxy trimesh;
		trimesh.type = ProxyType::TriMesh;
		trimesh.vertices = { {0,0,0}, {1,0,0}, {0,1,0} };
		trimesh.indices  = { 0, 1, 2 };
		REQUIRE(!GenerateWireframeEdges(trimesh).empty());
	}
}

int main()
{
	Test_Wireframe_Capsule_EdgeCount();
	Test_Wireframe_ConvexHull_BoundingBox_12Edges();
	Test_Wireframe_TriMesh_3EdgesPerTriangle();
	Test_Wireframe_EdgesNotEmpty();
	return g_failed;
}
```

- [ ] **Step 3 : Ajouter test exécutable au CMakeLists.txt**

```cmake
# M100.12 — Tests ProxyWireframe edge generator.
if(WIN32)
  add_executable(proxy_wireframe_tests engine/world/collision/tests/ProxyWireframeTests.cpp)
  target_include_directories(proxy_wireframe_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(proxy_wireframe_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(proxy_wireframe_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME proxy_wireframe_tests COMMAND proxy_wireframe_tests)
endif()
```

- [ ] **Step 4 : Créer `engine/world/collision/ProxyWireframe.cpp`**

```cpp
// engine/world/collision/ProxyWireframe.cpp
#include "engine/world/collision/ProxyWireframe.h"

#include <cmath>

namespace engine::world::collision
{
	namespace
	{
		constexpr int kCapsuleRingSegments = 16;

		void AddCapsuleEdges(const CollisionProxy& p, std::vector<Edge3D>& out)
		{
			const auto& a = p.capsuleA;
			const auto& b = p.capsuleB;
			const float r = p.capsuleRadius;

			// Direction de la capsule (normalisée)
			engine::math::Vec3 axis = b - a;
			const float axisLen = axis.Length();
			if (axisLen <= 0.0f)
			{
				// Capsule dégénérée → juste 2 cercles à la même position
				axis = engine::math::Vec3{ 0.0f, 1.0f, 0.0f };
			}
			else
			{
				axis = axis.Normalized();
			}

			// Construire un repère orthonormé (axis, u, v)
			engine::math::Vec3 u;
			if (std::fabs(axis.y) < 0.9f)
				u = engine::math::Vec3{ axis.z, 0.0f, -axis.x }.Normalized();
			else
				u = engine::math::Vec3{ 1.0f, 0.0f, 0.0f };
			engine::math::Vec3 v{
				axis.y * u.z - axis.z * u.y,
				axis.z * u.x - axis.x * u.z,
				axis.x * u.y - axis.y * u.x,
			};

			// 2 cap rings : 1 centré sur a, 1 centré sur b. 16 segments chacun.
			for (int cap = 0; cap < 2; ++cap)
			{
				const auto& center = (cap == 0) ? a : b;
				engine::math::Vec3 prev{
					center.x + u.x * r,
					center.y + u.y * r,
					center.z + u.z * r,
				};
				for (int i = 1; i <= kCapsuleRingSegments; ++i)
				{
					const float t = static_cast<float>(i) / kCapsuleRingSegments * 6.2831853f;
					const float c = std::cos(t);
					const float s = std::sin(t);
					engine::math::Vec3 cur{
						center.x + (u.x * c + v.x * s) * r,
						center.y + (u.y * c + v.y * s) * r,
						center.z + (u.z * c + v.z * s) * r,
					};
					out.emplace_back(prev, cur);
					prev = cur;
				}
			}

			// 4 lignes longitudinales : 0°, 90°, 180°, 270°
			for (int i = 0; i < 4; ++i)
			{
				const float t = static_cast<float>(i) * 1.5707963f;
				const float c = std::cos(t);
				const float s = std::sin(t);
				engine::math::Vec3 offset{
					(u.x * c + v.x * s) * r,
					(u.y * c + v.y * s) * r,
					(u.z * c + v.z * s) * r,
				};
				engine::math::Vec3 pa{ a.x + offset.x, a.y + offset.y, a.z + offset.z };
				engine::math::Vec3 pb{ b.x + offset.x, b.y + offset.y, b.z + offset.z };
				out.emplace_back(pa, pb);
			}
		}

		/// Pour un ConvexHull avec 8 vertices structurés en bounding box (ordre :
		/// bmin/bmax XYZ), génère les 12 arêtes du cube.
		void AddBoundingBoxEdges(const std::vector<engine::math::Vec3>& v,
			std::vector<Edge3D>& out)
		{
			// Convention vertex order (bmin/bmax encoding) :
			//  0: (xMin, yMin, zMin)   1: (xMax, yMin, zMin)
			//  2: (xMin, yMax, zMin)   3: (xMax, yMax, zMin)
			//  4: (xMin, yMin, zMax)   5: (xMax, yMin, zMax)
			//  6: (xMin, yMax, zMax)   7: (xMax, yMax, zMax)
			static constexpr int edges[12][2] = {
				{0, 1}, {1, 3}, {3, 2}, {2, 0},  // bottom (z = zMin) — wait this is wrong
				{4, 5}, {5, 7}, {7, 6}, {6, 4},  // top    (z = zMax)
				{0, 4}, {1, 5}, {2, 6}, {3, 7},  // verticals
			};
			// Correction : 4 arêtes face zMin, 4 face zMax, 4 connectives Y.
			// (Indices ci-dessus sont les bonnes 12 arêtes.)
			for (int i = 0; i < 12; ++i)
				out.emplace_back(v[edges[i][0]], v[edges[i][1]]);
		}

		void AddHullEdges(const CollisionProxy& p, std::vector<Edge3D>& out)
		{
			if (p.vertices.size() == 8)
			{
				AddBoundingBoxEdges(p.vertices, out);
			}
			else
			{
				// Fallback générique pour hulls non-bbox : connecte chaque
				// vertex au suivant (M100.12 ne génère pas ce cas, mais le
				// support défensif évite des arêtes manquantes en preview).
				for (size_t i = 0; i + 1 < p.vertices.size(); ++i)
					out.emplace_back(p.vertices[i], p.vertices[i + 1]);
			}
		}

		void AddTriMeshEdges(const CollisionProxy& p, std::vector<Edge3D>& out)
		{
			for (size_t i = 0; i + 2 < p.indices.size(); i += 3)
			{
				const auto& a = p.vertices[p.indices[i]];
				const auto& b = p.vertices[p.indices[i + 1]];
				const auto& c = p.vertices[p.indices[i + 2]];
				out.emplace_back(a, b);
				out.emplace_back(b, c);
				out.emplace_back(c, a);
			}
		}
	}

	std::vector<Edge3D> GenerateWireframeEdges(const CollisionProxy& proxy)
	{
		std::vector<Edge3D> edges;
		switch (proxy.type)
		{
			case ProxyType::Capsule:    AddCapsuleEdges(proxy, edges); break;
			case ProxyType::ConvexHull: AddHullEdges(proxy, edges);    break;
			case ProxyType::TriMesh:    AddTriMeshEdges(proxy, edges); break;
		}
		return edges;
	}
}
```

- [ ] **Step 5 : Ajouter `ProxyWireframe.cpp` à engine_core**

```cmake
  engine/world/collision/ProxyWireframe.cpp
```

- [ ] **Step 6 : Commit**

```bash
git add engine/world/collision/ProxyWireframe.h \
        engine/world/collision/ProxyWireframe.cpp \
        engine/world/collision/tests/ProxyWireframeTests.cpp \
        CMakeLists.txt
git commit -m "feat(world/collision): ProxyWireframe edge generator capsule/hull/trimesh (M100.12 Task 3)"
```

---

## Task 4: CollisionPreviewCamera (orbit + project, no tests)

**Files:**
- Create: `engine/editor/world/CollisionPreviewCamera.h`
- Create: `engine/editor/world/CollisionPreviewCamera.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `engine/editor/world/CollisionPreviewCamera.h`**

```cpp
// engine/editor/world/CollisionPreviewCamera.h
#pragma once

#include "engine/math/Math.h"

namespace engine::editor::world
{
    /// Mini-camera orbitale pour le preview 3D du CollisionEditorPanel (M100.12).
    /// La caméra orbite autour de l'origine (0,0,0). Drag souris ajuste yaw/pitch ;
    /// molette ajuste distance. Pas d'état Vulkan ou ImGui — pure math.
    class CollisionPreviewCamera
    {
    public:
        /// Ajuste yaw/pitch en radians (sensibilité fixée).
        /// Pitch est clampé [-π/2 + 0.05, π/2 - 0.05] pour éviter gimbal lock.
        void HandleDrag(float deltaX, float deltaY) noexcept;

        /// Ajuste distance (zoom). deltaWheel positif = zoom in (plus proche).
        /// Distance clampée [0.5, 20.0].
        void HandleZoom(float deltaWheel) noexcept;

        /// Reset à valeurs par défaut (yaw=0.7, pitch=0.4, distance=3.0).
        void Reset() noexcept;

        /// Projette un point 3D world-space vers les coordonnées pixel
        /// (top-left origin) dans une zone de viewport `viewportW × viewportH`.
        /// \return true si le point est devant la caméra et dans le viewport.
        bool Project(engine::math::Vec3 worldPos,
            float viewportW, float viewportH,
            float& outScreenX, float& outScreenY) const;

        // Accesseurs pour HUD du panel.
        float GetYawDegrees() const noexcept;
        float GetPitchDegrees() const noexcept;
        float GetDistance() const noexcept { return m_distance; }

    private:
        float m_yaw      = 0.7f;  // radians
        float m_pitch    = 0.4f;  // radians
        float m_distance = 3.0f;  // meters from origin
    };
}
```

- [ ] **Step 2 : Créer `engine/editor/world/CollisionPreviewCamera.cpp`**

```cpp
// engine/editor/world/CollisionPreviewCamera.cpp
#include "engine/editor/world/CollisionPreviewCamera.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		constexpr float kDragSensitivity = 0.01f;
		constexpr float kZoomFactor      = 0.9f;
		constexpr float kMinDistance     = 0.5f;
		constexpr float kMaxDistance     = 20.0f;
		constexpr float kPitchClamp      = 1.5208f; // π/2 - 0.05
		constexpr float kFovYRad         = 1.0472f; // 60° in radians
		constexpr float kNearZ           = 0.1f;
		constexpr float kFarZ            = 100.0f;
	}

	void CollisionPreviewCamera::HandleDrag(float deltaX, float deltaY) noexcept
	{
		m_yaw   -= deltaX * kDragSensitivity;
		m_pitch -= deltaY * kDragSensitivity;
		m_pitch  = std::clamp(m_pitch, -kPitchClamp, kPitchClamp);
	}

	void CollisionPreviewCamera::HandleZoom(float deltaWheel) noexcept
	{
		if (deltaWheel > 0.0f) m_distance *= kZoomFactor;
		else if (deltaWheel < 0.0f) m_distance /= kZoomFactor;
		m_distance = std::clamp(m_distance, kMinDistance, kMaxDistance);
	}

	void CollisionPreviewCamera::Reset() noexcept
	{
		m_yaw      = 0.7f;
		m_pitch    = 0.4f;
		m_distance = 3.0f;
	}

	float CollisionPreviewCamera::GetYawDegrees() const noexcept
	{
		return m_yaw * 57.29578f;  // 180/π
	}

	float CollisionPreviewCamera::GetPitchDegrees() const noexcept
	{
		return m_pitch * 57.29578f;
	}

	bool CollisionPreviewCamera::Project(engine::math::Vec3 worldPos,
		float viewportW, float viewportH,
		float& outScreenX, float& outScreenY) const
	{
		// Camera orbits the origin :
		//   eye = (sin(yaw)*cos(pitch), sin(pitch), cos(yaw)*cos(pitch)) * distance
		//   target = (0,0,0), up = (0,1,0)
		const float cy = std::cos(m_yaw);
		const float sy = std::sin(m_yaw);
		const float cp = std::cos(m_pitch);
		const float sp = std::sin(m_pitch);

		// View-space transform : on translate par -eye puis on tourne dans le repère caméra.
		// Repère caméra : forward (vers origine), right, up.
		const float fx = -sy * cp;
		const float fy = -sp;
		const float fz = -cy * cp;
		// right = (fz, 0, -fx) après normalisation (hors composante up)
		const float rxLen = std::sqrt(fz * fz + fx * fx);
		const float rx = (rxLen > 0.0f) ? ( fz / rxLen) : 1.0f;
		const float rz = (rxLen > 0.0f) ? (-fx / rxLen) : 0.0f;
		// up = right × forward
		const float ux = -rz * fy;
		const float uy = rx * fz - rz * fx;
		const float uz = rx * fy;

		// View-space position : world - eye, then dot avec right/up/forward
		const float ex = sy * cp * m_distance;
		const float ey = sp * m_distance;
		const float ez = cy * cp * m_distance;
		const float dx = worldPos.x - ex;
		const float dy = worldPos.y - ey;
		const float dz = worldPos.z - ez;

		const float vx = dx * rx + dz * rz;       // right
		const float vy = dx * 0.0f + dy * uy + dz * 0.0f;
		const float vyAlt = dx * (-rz * fy) + dy * (rx * fz - rz * fx) + dz * (rx * fy);
		(void)vyAlt;
		const float vyy = dx * ux + dy * uy + dz * uz;
		const float vz = dx * fx + dy * fy + dz * fz;  // forward

		if (vz <= kNearZ) return false;  // derrière ou trop proche du near plane

		// Perspective projection : x/z * (1/tan(fov/2)) / aspect
		const float aspect = (viewportH > 0.0f) ? (viewportW / viewportH) : 1.0f;
		const float t = 1.0f / std::tan(kFovYRad * 0.5f);
		const float ndcX = (vx * t / aspect) / vz;
		const float ndcY = (vyy * t) / vz;

		// NDC [-1, 1] → pixel space top-left origin
		outScreenX = (ndcX * 0.5f + 0.5f) * viewportW;
		outScreenY = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportH;

		return outScreenX >= 0.0f && outScreenX <= viewportW
		    && outScreenY >= 0.0f && outScreenY <= viewportH
		    && vz < kFarZ;
	}
}
```

- [ ] **Step 3 : Ajouter `CollisionPreviewCamera.cpp` à engine_core**

Dans la liste des sources, près des autres fichiers `engine/editor/world/*.cpp` (autour ligne 277 où `EditorCameraController.cpp` est listé) :

```cmake
  engine/editor/world/CollisionPreviewCamera.cpp
```

- [ ] **Step 4 : Commit**

```bash
git add engine/editor/world/CollisionPreviewCamera.h \
        engine/editor/world/CollisionPreviewCamera.cpp \
        CMakeLists.txt
git commit -m "feat(editor/world): CollisionPreviewCamera orbit + project 3D->pixel (M100.12 Task 4)"
```

---

## Task 5: CollisionEditorPanel UI (form fields + preview + WorldEditorShell wiring)

**Files:**
- Create: `engine/editor/world/panels/CollisionEditorPanel.h`
- Create: `engine/editor/world/panels/CollisionEditorPanel.cpp`
- Modify: `engine/editor/world/WorldEditorShell.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `engine/editor/world/panels/CollisionEditorPanel.h`**

```cpp
// engine/editor/world/panels/CollisionEditorPanel.h
#pragma once

#include "engine/editor/world/CollisionPreviewCamera.h"
#include "engine/editor/world/IPanel.h"
#include "engine/world/collision/CollisionProxy.h"

#include <filesystem>
#include <string>

namespace engine::editor::world::panels
{
    /// Panel ImGui d'authoring de proxies de collision (M100.12).
    /// Workflow : Open .collision.bin OU New (Capsule/Hull/TriMesh) → switch type →
    /// edit fields (sliders capsule, read-only hull/trimesh) → Save .collision.bin.
    /// Mini-preview 3D avec wireframe vert overlay sur mesh test synthétique.
    class CollisionEditorPanel final : public engine::editor::world::IPanel
    {
    public:
        const char* GetName() const override { return "Collision Editor"; }
        void Render() override;
        bool IsVisible() const override { return m_visible; }
        void SetVisible(bool v) override { m_visible = v; }

        /// Initialise le panel avec un contentRoot (typiquement "game/data").
        /// Ne charge pas un proxy automatique.
        void Init(const std::filesystem::path& contentRoot);

    private:
        bool m_visible = false;
        std::filesystem::path m_contentRoot;
        std::filesystem::path m_currentPath;
        engine::world::collision::CollisionProxy m_proxy;
        engine::editor::world::CollisionPreviewCamera m_camera;
        int m_testMeshIndex = 0;  // 0=Cube, 1=Cylinder, 2=Sphere, 3=Slab
        std::string m_status;
        int m_statusFramesLeft = 0;
        char m_pathInputBuf[260] = {};
    };
}
```

- [ ] **Step 2 : Créer `engine/editor/world/panels/CollisionEditorPanel.cpp`**

```cpp
// engine/editor/world/panels/CollisionEditorPanel.cpp
#include "engine/editor/world/panels/CollisionEditorPanel.h"
#include "engine/world/collision/ProxyWireframe.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <cmath>
#include <cstring>
#include <vector>

namespace engine::editor::world::panels
{
	namespace
	{
		using engine::math::Vec3;
		using engine::world::collision::CollisionProxy;
		using engine::world::collision::ProxyType;
		using engine::world::collision::Edge3D;

		/// Mesh test : cube unitaire centré sur origine (8 verts, 12 arêtes).
		std::vector<Edge3D> MakeCubeEdges()
		{
			Vec3 v[8] = {
				{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f},
				{-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
				{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f},
				{-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f},
			};
			static constexpr int e[12][2] = {
				{0,1},{1,3},{3,2},{2,0},
				{4,5},{5,7},{7,6},{6,4},
				{0,4},{1,5},{2,6},{3,7},
			};
			std::vector<Edge3D> out;
			out.reserve(12);
			for (int i = 0; i < 12; ++i)
				out.emplace_back(v[e[i][0]], v[e[i][1]]);
			return out;
		}

		/// Mesh test : cylindre vertical centré, 16 segments × 2 caps.
		std::vector<Edge3D> MakeCylinderEdges()
		{
			std::vector<Edge3D> out;
			constexpr int segs = 16;
			const float r = 0.4f;
			const float h = 1.0f;
			for (int cap = 0; cap < 2; ++cap)
			{
				const float y = (cap == 0) ? -h * 0.5f : h * 0.5f;
				for (int i = 0; i < segs; ++i)
				{
					const float t0 = static_cast<float>(i)     / segs * 6.2831853f;
					const float t1 = static_cast<float>(i + 1) / segs * 6.2831853f;
					out.emplace_back(
						Vec3{ std::cos(t0) * r, y, std::sin(t0) * r },
						Vec3{ std::cos(t1) * r, y, std::sin(t1) * r });
				}
			}
			// 4 lignes longitudinales
			for (int i = 0; i < 4; ++i)
			{
				const float t = static_cast<float>(i) * 1.5707963f;
				const float x = std::cos(t) * r;
				const float z = std::sin(t) * r;
				out.emplace_back(Vec3{ x, -h * 0.5f, z }, Vec3{ x, h * 0.5f, z });
			}
			return out;
		}

		/// Mesh test : icosphère grossière 20 tris (12 verts).
		std::vector<Edge3D> MakeSphereEdges()
		{
			const float t = (1.0f + std::sqrt(5.0f)) * 0.5f * 0.5f; // golden ratio scaled
			const float s = 0.5f;
			Vec3 v[12] = {
				{-s,  t*s, 0}, { s,  t*s, 0}, {-s, -t*s, 0}, { s, -t*s, 0},
				{ 0, -s,  t*s}, { 0,  s,  t*s}, { 0, -s, -t*s}, { 0,  s, -t*s},
				{ t*s, 0, -s}, { t*s, 0,  s}, {-t*s, 0, -s}, {-t*s, 0,  s},
			};
			static constexpr int idx[20][3] = {
				{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
				{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
				{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
				{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1},
			};
			std::vector<Edge3D> out;
			out.reserve(60);
			for (int i = 0; i < 20; ++i)
			{
				out.emplace_back(v[idx[i][0]], v[idx[i][1]]);
				out.emplace_back(v[idx[i][1]], v[idx[i][2]]);
				out.emplace_back(v[idx[i][2]], v[idx[i][0]]);
			}
			return out;
		}

		/// Mesh test : slab plat 1×0.05×1.
		std::vector<Edge3D> MakeSlabEdges()
		{
			Vec3 v[8] = {
				{-0.5f,-0.025f,-0.5f}, { 0.5f,-0.025f,-0.5f},
				{-0.5f, 0.025f,-0.5f}, { 0.5f, 0.025f,-0.5f},
				{-0.5f,-0.025f, 0.5f}, { 0.5f,-0.025f, 0.5f},
				{-0.5f, 0.025f, 0.5f}, { 0.5f, 0.025f, 0.5f},
			};
			static constexpr int e[12][2] = {
				{0,1},{1,3},{3,2},{2,0},
				{4,5},{5,7},{7,6},{6,4},
				{0,4},{1,5},{2,6},{3,7},
			};
			std::vector<Edge3D> out;
			out.reserve(12);
			for (int i = 0; i < 12; ++i)
				out.emplace_back(v[e[i][0]], v[e[i][1]]);
			return out;
		}

		std::vector<Edge3D> GetTestMeshEdges(int index)
		{
			switch (index)
			{
				case 1:  return MakeCylinderEdges();
				case 2:  return MakeSphereEdges();
				case 3:  return MakeSlabEdges();
				default: return MakeCubeEdges();
			}
		}
	}

	void CollisionEditorPanel::Init(const std::filesystem::path& contentRoot)
	{
		m_contentRoot = contentRoot;
	}

	void CollisionEditorPanel::Render()
	{
#if defined(_WIN32)
		if (!ImGui::Begin(GetName(), &m_visible))
		{
			ImGui::End();
			return;
		}

		// ── Toolbar ────────────────────────────────────────────────────
		ImGui::InputText("##path", m_pathInputBuf, sizeof(m_pathInputBuf));
		ImGui::SameLine();
		if (ImGui::Button("Open"))
		{
			std::filesystem::path p = m_contentRoot / m_pathInputBuf;
			std::string err;
			if (m_proxy.LoadFromFile(p, err))
			{
				m_currentPath = p;
				m_status = "Loaded \xE2\x9C\x93";
				m_statusFramesLeft = 60;
			}
			else
			{
				m_status = "Load error: " + err;
				m_statusFramesLeft = 180;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("New Capsule"))
		{
			m_proxy = engine::world::collision::CollisionProxy{};
			m_proxy.type = ProxyType::Capsule;
		}
		ImGui::SameLine();
		if (ImGui::Button("New ConvexHull"))
		{
			m_proxy = engine::world::collision::CollisionProxy{};
			m_proxy.type = ProxyType::ConvexHull;
		}
		ImGui::SameLine();
		if (ImGui::Button("New TriMesh"))
		{
			m_proxy = engine::world::collision::CollisionProxy{};
			m_proxy.type = ProxyType::TriMesh;
		}

		ImGui::Text("Source: %s", m_currentPath.empty() ? "<empty>" : m_currentPath.string().c_str());
		ImGui::Separator();

		// ── Type radios ─────────────────────────────────────────────────
		int t = static_cast<int>(m_proxy.type);
		ImGui::RadioButton("Capsule",    &t, 0); ImGui::SameLine();
		ImGui::RadioButton("ConvexHull", &t, 1); ImGui::SameLine();
		ImGui::RadioButton("TriMesh",    &t, 2);
		m_proxy.type = static_cast<ProxyType>(t);
		ImGui::Separator();

		// ── Type-specific fields ────────────────────────────────────────
		if (m_proxy.type == ProxyType::Capsule)
		{
			ImGui::SliderFloat3("A",      &m_proxy.capsuleA.x,    -5.0f, 5.0f, "%.3f");
			ImGui::SliderFloat3("B",      &m_proxy.capsuleB.x,    -5.0f, 5.0f, "%.3f");
			ImGui::SliderFloat ("Radius", &m_proxy.capsuleRadius,  0.05f, 2.0f, "%.3f");
		}
		else if (m_proxy.type == ProxyType::ConvexHull)
		{
			ImGui::Text("Vertex count: %zu", m_proxy.vertices.size());
			ImGui::BeginDisabled(true);
			ImGui::Button("Re-run AutoFit");
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Requires mesh CPU data — wired with mesh import (out-of-scope M100.12)");
		}
		else // TriMesh
		{
			ImGui::Text("Vertex count: %zu", m_proxy.vertices.size());
			ImGui::Text("Tri count: %zu",     m_proxy.indices.size() / 3);
		}
		ImGui::Separator();

		// ── Preview 3D ──────────────────────────────────────────────────
		ImGui::Text("Preview");
		const char* meshNames[4] = { "Cube", "Cylinder", "Sphere", "Slab" };
		ImGui::Combo("Test mesh", &m_testMeshIndex, meshNames, 4);
		ImGui::SameLine();
		if (ImGui::Button("Reset Camera")) m_camera.Reset();

		const ImVec2 previewSize{ 300.0f, 200.0f };
		ImGui::BeginChild("##preview", previewSize, true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		const ImVec2 previewMin = ImGui::GetCursorScreenPos();
		const ImVec2 contentSize = ImGui::GetContentRegionAvail();
		const float w = contentSize.x;
		const float h = contentSize.y;

		// Drag / wheel
		ImGui::InvisibleButton("##previewCanvas", contentSize);
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const ImVec2 d = ImGui::GetIO().MouseDelta;
			m_camera.HandleDrag(d.x, d.y);
		}
		if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
			m_camera.HandleZoom(ImGui::GetIO().MouseWheel);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		auto drawEdge = [&](const Vec3& a, const Vec3& b, ImU32 color)
		{
			float ax, ay, bx, by;
			const bool aOk = m_camera.Project(a, w, h, ax, ay);
			const bool bOk = m_camera.Project(b, w, h, bx, by);
			if (!aOk || !bOk) return;
			dl->AddLine(ImVec2(previewMin.x + ax, previewMin.y + ay),
			            ImVec2(previewMin.x + bx, previewMin.y + by),
			            color, 1.5f);
		};

		// Test mesh en gris
		const auto testEdges = GetTestMeshEdges(m_testMeshIndex);
		for (const auto& e : testEdges)
			drawEdge(e.first, e.second, IM_COL32(140, 140, 140, 200));

		// Proxy wireframe en vert
		const auto proxyEdges = engine::world::collision::GenerateWireframeEdges(m_proxy);
		for (const auto& e : proxyEdges)
			drawEdge(e.first, e.second, IM_COL32(80, 255, 80, 255));

		ImGui::EndChild();

		ImGui::Text("Yaw: %.0f deg   Pitch: %.0f deg   Distance: %.1f m",
			m_camera.GetYawDegrees(), m_camera.GetPitchDegrees(), m_camera.GetDistance());
		ImGui::Separator();

		// ── Save ────────────────────────────────────────────────────────
		if (ImGui::Button("Save .collision.bin"))
		{
			std::filesystem::path p = m_currentPath.empty()
				? (m_contentRoot / m_pathInputBuf)
				: m_currentPath;
			std::string err;
			if (m_proxy.SaveToFile(p, err))
			{
				m_currentPath = p;
				m_status = "Saved \xE2\x9C\x93";
				m_statusFramesLeft = 60;
			}
			else
			{
				m_status = "Save error: " + err;
				m_statusFramesLeft = 180;
			}
		}

		// ── Status ──────────────────────────────────────────────────────
		if (m_statusFramesLeft > 0)
		{
			--m_statusFramesLeft;
			if (m_status.find("error") != std::string::npos)
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: %s", m_status.c_str());
			else
				ImGui::Text("Status: %s", m_status.c_str());
		}

		ImGui::End();
#endif
	}
}
```

- [ ] **Step 3 : Wirer dans `engine/editor/world/WorldEditorShell.cpp`**

Ajouter l'include en haut du fichier (avec les autres includes panels) :

```cpp
#include "engine/editor/world/panels/CollisionEditorPanel.h"
```

Dans `Init()`, **après** l'emplace du `SurfaceTablePanel` (M100.11), ajouter :

```cpp
		// M100.12 — Panel d'authoring de collision proxies. Hidden par défaut,
		// toggle via View > Collision Editor.
		auto collisionPanel = std::make_unique<panels::CollisionEditorPanel>();
		collisionPanel->Init(
			std::filesystem::path(cfg.GetString("paths.content", "game/data")));
		m_panels.emplace_back(std::move(collisionPanel));
```

Mettre à jour le commentaire Doxygen du `Init()` pour refléter 9 panels :

```cpp
	/// Initialise la coquille : lit `editor.world.layout_path`, instancie les
	/// 9 panneaux dans l'ordre stable, charge le fichier .ini de layout s'il
	/// existe, sinon réinitialise un layout par défaut. L'ordre des panneaux
	/// est figé : 0=Scene, 1=Inspector, 2=AssetBrowser, 3=Outliner, 4=Console,
	/// 5=ToolProperties, 6=History (M100.2), 7=SurfaceTable (M100.11),
	/// 8=CollisionEditor (M100.12) — référencé par les tests M100.1.
```

- [ ] **Step 4 : Ajouter `CollisionEditorPanel.cpp` à engine_core dans CMakeLists.txt**

À côté de `SurfaceTablePanel.cpp` (M100.11), dans la liste des sources `engine/editor/world/panels/*.cpp` :

```cmake
  engine/editor/world/panels/CollisionEditorPanel.cpp
```

- [ ] **Step 5 : Commit**

```bash
git add engine/editor/world/panels/CollisionEditorPanel.h \
        engine/editor/world/panels/CollisionEditorPanel.cpp \
        engine/editor/world/WorldEditorShell.cpp \
        CMakeLists.txt
git commit -m "feat(editor/world): CollisionEditorPanel ImGui + mini-preview 3D (M100.12 Task 5)"
```

---

## Task 6: Validation finale + grep gardien serveur + INDEX.md

**Files:**
- (validation only)
- Modify: `tickets/M100/INDEX.md`

- [ ] **Step 1 : Vérifier que le serveur ne référence pas `engine::world::collision`**

```bash
grep -rn "engine::world::collision\|CollisionProxy\|AutoFit" engine/server/ tools/zone_builder/ tools/migration_checksum/ tools/load_tester/ tools/hlod_builder/ tools/gen_terrain_placeholders/ 2>&1
```

Expected : aucun résultat (le serveur ne touche pas à collision).

- [ ] **Step 2 : Vérifier que `GeometryPass.cpp` n'a pas été modifié**

```bash
git diff origin/main -- engine/render/GeometryPass.cpp
```

Expected : aucune diff (le ticket M100.12 exige que GeometryPass reste inchangé).

- [ ] **Step 3 : Mettre à jour `tickets/M100/INDEX.md`**

Dans le tableau des tickets, ligne M100.12, changer la colonne `Statut` :

```diff
- | M100.12 | Collision Proxy System | 3 — Splat / Surfaces / Collision | M100.1 | Ready |
+ | M100.12 | Collision Proxy System | 3 — Splat / Surfaces / Collision | M100.1 | Done (CI pending) |
```

- [ ] **Step 4 : Commit**

```bash
git add tickets/M100/INDEX.md
git commit -m "docs(tickets/M100): marque M100.12 Done (Phase 3b.2, CI pending)"
```

---

## Récap couverture spec → tasks

| Section spec | Task |
|---|---|
| Architecture (file structure) | T1-T5 (création/modif fichiers exact selon spec) |
| `CollisionProxy.h` API + format binaire | T1 |
| `SaveToFile` / `LoadFromFile` round-trip | T1 (5 tests) |
| `CollisionMeshCpu.h` | T2 |
| `AutoFitProxy` heuristique | T2 (4 tests) |
| `ProxyWireframe` edge generator | T3 (4 tests) |
| `CollisionPreviewCamera` orbit + project | T4 |
| `CollisionEditorPanel` UI complet (toolbar, type radios, sliders, preview, save) | T5 |
| WorldEditorShell wiring | T5 |
| Test grep gardien serveur | T6 |
| GeometryPass non modifié | T6 |
| INDEX.md | T6 |
| 3 suites tests TDD (~13 cas) | T1, T2, T3 |

## Self-Review

**1. Spec coverage:** Toutes les sections du spec ont une task qui les implémente. Le `View → Show Collision Proxies (V)` global toggle stub est non couvert : c'est cohérent avec le spec qui le déclare out-of-scope (dépend de M100.34). Aucun gap réel.

**2. Placeholder scan:** Aucun "TBD"/"TODO"/"implement later" dans les steps de code. Les commentaires dans le code source qui mentionnent M100.12/M100.34 sont des références ticket, pas des placeholders.

**3. Type consistency:**
- `CollisionProxy`, `ProxyType`, `Edge3D`, `CollisionMeshCpu` : noms stables Tasks 1-5.
- `AutoFit(CollisionMeshCpu) → CollisionProxy` : signature identique entre AutoFitProxy.h (T2 step 2) et l'usage dans tests (T2 step 3).
- `GenerateWireframeEdges(proxy)` : signature stable T3.
- `CollisionPreviewCamera::Project(Vec3, w, h, &x, &y) → bool` : signature stable T4 step 1, consommée T5 step 2.
- Magic `kCollisionMagic = 0x4C4C4F43u` ("COLL" little-endian) : confirmé par 'C'(0x43)+'O'(0x4F)+'L'(0x4C)+'L'(0x4C) lus en little-endian = 0x4C4C4F43.
- `OutputVersionHeader` 24 bytes layout : magic(4) + formatVersion(4) + builderVersion(4) + engineVersion(4) + contentHash(8) = 24 ✓ (vérifié `engine/world/OutputVersion.h:23`).
- `kZoneBuilderVersion` / `kZoneEngineVersion` : utilisés dans Task 1 step 4, déclarés `engine/world/OutputVersion.h:27-29`.
- `ComputeXxHash64(span<uint8_t>) → uint64_t` : utilisé Task 1 step 4, déclaré `engine/world/OutputVersion.h:44`.
- `ReadOutputVersionHeader(span, &hdr, &err) → bool` : utilisé Task 1 step 4, déclaré `engine/world/OutputVersion.h:57`.

Plan complet.
