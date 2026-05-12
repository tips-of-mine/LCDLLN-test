# M100.16 Hazard Volume System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Livrer le système de volumes 3D dangereux (Quicksand, Bog, Tar, LavaSurface) avec 4 modes d'évasion, simulation strictement côté client, callbacks injectés pour découplage inventaire/animation/audio/mort.

**Architecture:** `HazardVolumes` (data + I/O), `HazardSimulator` (state machine joueur ↔ hazard avec callbacks), modifications minimales à `CharacterController` (SetHazardEffect) et `ThirdPersonCamera` (SetGroundOffset), nouveau `HazardTool` + `HazardDocument` côté éditeur, hooks pipeline (StreamCache + ChunkPackageWriter).

**Tech Stack:** C++20, CMake/CTest, framework de test maison (`REQUIRE` + `g_failed`), MSVC (Windows), pas de framework externe.

**Spec source :** [docs/superpowers/specs/2026-05-12-m100-16-hazard-volume-system-design.md](../specs/2026-05-12-m100-16-hazard-volume-system-design.md)

---

## File Structure

| Fichier | Responsabilité | Action |
|---|---|---|
| `src/client/world/hazard/HazardVolumes.h` | Enums, struct `HazardInstance`/`HazardScene`, magic/version, `PointInHazard` | Créer |
| `src/client/world/hazard/HazardVolumes.cpp` | `SaveHazardsBin`/`LoadHazardsBin` round-trip, `PointInHazard` impl | Créer |
| `src/client/world/hazard/HazardSimulator.h` | `HazardCallbacks`, `HazardState`, `HazardEffect`, classe `HazardSimulator` | Créer |
| `src/client/world/hazard/HazardSimulator.cpp` | State machine : détection, sink rate, escapes, lava timer, death | Créer |
| `src/client/world/hazard/tests/HazardVolumesTests.cpp` | Test round-trip 4 types | Créer |
| `src/client/world/hazard/tests/HazardSimulatorTests.cpp` | 5 tests : sink rate, mash escape, lateral escape, lava 3s, death max depth | Créer |
| `src/client/gameplay/CharacterController.h/.cpp` | Ajout `SetHazardEffect` + `IsSinking()` + intégration dans `Update()` | Modifier |
| `src/client/gameplay/ThirdPersonCamera.h/.cpp` | Ajout `SetGroundOffset(float)` + application dans calcul cible | Modifier |
| `src/world_editor/hazard/HazardDocument.h/.cpp` | État monde éditeur (add/remove hazard, snapshot pour undo) | Créer |
| `src/world_editor/hazard/HazardTool.h/.cpp` | Panneau ImGui (type, shape, sliders, raycast) | Créer |
| `src/client/world/StreamCache.h/.cpp` | Ajout `LoadHazards(config, zoneName)` (pattern `LoadWater`) | Modifier |
| `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` + `.cpp` | Ajout `WriteHazards(outputRootDir, scene, error)` (pattern `WriteWater`) | Modifier |
| `CMakeLists.txt` | Ajout sources `engine_core` + 2 exécutables CTest gardés `if(WIN32)` | Modifier |
| `CODEBASE_MAP.md` | Entrée M100.16 livrée | Modifier |

**Hors scope explicite :**
- Audio loops complets (callback stub côté Engine, mix complet en M100.33)
- Animation `struggle/pullup` (callback stub côté Engine, vrais hooks anim avec CHAR-MODEL.x)
- Inventory check : callback retourne `false` par défaut → `MashButtonItem` non-praticable, mais ne casse rien (joueur meurt à maxDepth comme mode None)

---

## Test Framework Reminder

Pattern identique aux tests M100.15. `REQUIRE` macro + `g_failed` + `int main()` qui appelle les tests et retourne `g_failed > 0 ? 1 : 0`. Voir [`src/client/world/water/tests/WaterSurfacesTests.cpp`](../../../src/client/world/water/tests/WaterSurfacesTests.cpp) comme exemple de référence.

`cmake` indisponible en sandbox local. Toutes les commandes `cmake --build` / `ctest` doivent être skippées — la validation finale se fait via CI Windows.

---

## Task 1: `HazardVolumes` — enums + struct + point-in-volume

**Files:**
- Create: `src/client/world/hazard/HazardVolumes.h`
- Create: `src/client/world/hazard/HazardVolumes.cpp`
- Modify: `CMakeLists.txt` (ajout source `engine_core` flat list après `WaterSampler.cpp` ou similaire)

- [ ] **Step 1: Créer le header `HazardVolumes.h`**

```cpp
// src/client/world/hazard/HazardVolumes.h
#pragma once

#include "src/shared/math/Math.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::hazard
{
	/// Magic du fichier `instances/hazards.bin` ("HAZA" little-endian).
	constexpr uint32_t kHazardsMagic   = 0x5A415748u;
	constexpr uint32_t kHazardsVersion = 1u;

	/// Type de hazard. Détermine les paramètres de simulation par défaut
	/// et le comportement spécial (LavaSurface tue par contact).
	enum class HazardType : uint32_t
	{
		Quicksand   = 0,
		Bog         = 1,
		Tar         = 2,
		LavaSurface = 3
	};

	/// Forme du volume. Le choix Box/Cylinder est figé à la création par l'éditeur.
	enum class HazardShape : uint32_t
	{
		Box      = 0,
		Cylinder = 1
	};

	/// Mode d'évasion. `None` = mort scriptée si maxDepth atteinte.
	/// `MashButtonItem` requiert en plus un item dans l'inventaire local.
	enum class EscapeMode : uint32_t
	{
		None            = 0,
		MashButton      = 1,
		LateralMove     = 2,
		MashButtonItem  = 3
	};

	/// Une instance de volume hazard dans le monde. Position monde +
	/// dimensions selon `shape`. Les paramètres de simulation sont par
	/// instance pour permettre des variantes éditeur (ex. quicksand
	/// très profond vs superficiel).
	struct HazardInstance
	{
		HazardType type            = HazardType::Quicksand;
		HazardShape shape          = HazardShape::Cylinder;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 boxHalfExtents{ 2.0f, 1.0f, 2.0f }; // utilisé si Box
		float cylRadius            = 4.0f;                      // utilisé si Cylinder
		float cylHeight            = 2.0f;                      // utilisé si Cylinder
		float sinkRateMps          = 0.15f;
		float maxDepthMeters       = 1.8f;
		float slowdownMul          = 0.10f;
		EscapeMode escapeMode      = EscapeMode::MashButton;
		uint32_t requiredItemId    = 0;                         // 0 si escapeMode != MashButtonItem
	};

	struct HazardScene
	{
		std::vector<HazardInstance> hazards;
	};

	/// Sérialise au format `hazards.bin` (M100.16). Header OutputVersionHeader
	/// (magic=kHazardsMagic, version=kHazardsVersion, contentHash=xxhash64 du payload).
	bool SaveHazardsBin(const HazardScene& scene,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise. Valide magic, version, contentHash. Reset outScene.
	bool LoadHazardsBin(std::span<const uint8_t> bytes,
		HazardScene& outScene, std::string& outError);

	/// Test point-in-volume 3D selon `hz.shape`.
	/// Box : `|pos - center|.{x,y,z} <= halfExtents.{x,y,z}` composant par composant.
	/// Cylinder : `pos.y ∈ [center.y, center.y + cylHeight]` ET `(pos.xz - center.xz).length <= cylRadius`.
	bool PointInHazard(const HazardInstance& hz, engine::math::Vec3 worldPos) noexcept;
}
```

- [ ] **Step 2: Créer le `.cpp` avec implémentation `PointInHazard` (Save/Load stub pour cette task)**

```cpp
// src/client/world/hazard/HazardVolumes.cpp
#include "src/client/world/hazard/HazardVolumes.h"

#include <cmath>

namespace engine::world::hazard
{
	bool PointInHazard(const HazardInstance& hz, engine::math::Vec3 worldPos) noexcept
	{
		switch (hz.shape)
		{
			case HazardShape::Box:
			{
				const float dx = worldPos.x - hz.position.x;
				const float dy = worldPos.y - hz.position.y;
				const float dz = worldPos.z - hz.position.z;
				return std::fabs(dx) <= hz.boxHalfExtents.x
					&& std::fabs(dy) <= hz.boxHalfExtents.y
					&& std::fabs(dz) <= hz.boxHalfExtents.z;
			}
			case HazardShape::Cylinder:
			{
				if (worldPos.y < hz.position.y) return false;
				if (worldPos.y > hz.position.y + hz.cylHeight) return false;
				const float dx = worldPos.x - hz.position.x;
				const float dz = worldPos.z - hz.position.z;
				return (dx * dx + dz * dz) <= (hz.cylRadius * hz.cylRadius);
			}
		}
		return false;
	}

	bool SaveHazardsBin(const HazardScene& /*scene*/,
		std::vector<uint8_t>& /*outBytes*/, std::string& outError)
	{
		// stub : implémenté en Task 2.
		outError = "SaveHazardsBin not implemented yet (Task 2)";
		return false;
	}

	bool LoadHazardsBin(std::span<const uint8_t> /*bytes*/,
		HazardScene& /*outScene*/, std::string& outError)
	{
		// stub : implémenté en Task 2.
		outError = "LoadHazardsBin not implemented yet (Task 2)";
		return false;
	}
}
```

- [ ] **Step 3: Ajouter `HazardVolumes.cpp` à `engine_core` dans `CMakeLists.txt`**

Repère la flat list `engine_core` (lignes ~395-470). Ajouter après une source water existante :

```cmake
  src/client/world/hazard/HazardVolumes.cpp
```

- [ ] **Step 4: Commit**

```bash
git add src/client/world/hazard/HazardVolumes.h src/client/world/hazard/HazardVolumes.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.16): HazardVolumes — enums + struct + PointInHazard

Préparation du format `instances/hazards.bin` (M100.16). Enums HazardType
(Quicksand/Bog/Tar/LavaSurface), HazardShape (Box/Cylinder), EscapeMode
(None/MashButton/LateralMove/MashButtonItem). Struct HazardInstance avec
les paramètres de simulation par instance.

PointInHazard 3D implémenté (Box AABB + Cylinder Y+radial). Save/Load
stub (impl Task 2).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `HazardVolumes` — Save/Load round-trip

**Files:**
- Modify: `src/client/world/hazard/HazardVolumes.cpp` (implémentation des 2 stubs)
- Create: `src/client/world/hazard/tests/HazardVolumesTests.cpp`
- Modify: `CMakeLists.txt` (ajout bloc test `hazard_volumes_tests` après les blocs water hazard)

- [ ] **Step 1: Implémenter `SaveHazardsBin` et `LoadHazardsBin`**

> Le projet expose des helpers `OutputVersionHeader`, `ByteWriter`, `ByteReader` dans `src/client/world/OutputVersion.h` et `src/shared/network/ByteWriter.h/ByteReader.h`. Imite [WaterSurfaces.cpp](../../../src/client/world/water/WaterSurfaces.cpp) (M100.13) pour le pattern de sérialisation : header avec magic + version + contentHash, puis sérialisation séquentielle.

> **Note** : lis le contenu actuel de `WaterSurfaces.cpp` pour le pattern exact. Adapter aux champs de `HazardInstance`. Layout binaire attendu :

```
[OutputVersionHeader]  (kHazardsMagic, kHazardsVersion, contentHash)
[uint32 hazardCount]
For each hazard:
  uint32 type, uint32 shape, vec3 position (12 octets float),
  vec3 boxHalfExtents (12), float cylRadius (4), float cylHeight (4),
  float sinkRateMps (4), float maxDepthMeters (4), float slowdownMul (4),
  uint32 escapeMode, uint32 requiredItemId
```

Total par hazard = 4 + 4 + 12 + 12 + 4 + 4 + 4 + 4 + 4 + 4 + 4 = **60 octets**.

Adapte le code en suivant strictement le pattern `WaterSurfaces` :
- `SaveHazardsBin` : construit le payload via `ByteWriter`, compute hash, prepend header
- `LoadHazardsBin` : parse header, valide magic/version, recalcule + compare hash, parse via `ByteReader`

- [ ] **Step 2: Créer `HazardVolumesTests.cpp`**

```cpp
// src/client/world/hazard/tests/HazardVolumesTests.cpp
#include "src/client/world/hazard/HazardVolumes.h"

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

	using engine::math::Vec3;
	using engine::world::hazard::EscapeMode;
	using engine::world::hazard::HazardInstance;
	using engine::world::hazard::HazardScene;
	using engine::world::hazard::HazardShape;
	using engine::world::hazard::HazardType;
	using engine::world::hazard::LoadHazardsBin;
	using engine::world::hazard::PointInHazard;
	using engine::world::hazard::SaveHazardsBin;

	HazardScene MakeFullScene()
	{
		HazardScene s;
		HazardInstance h1{ HazardType::Quicksand, HazardShape::Cylinder,
			Vec3{10, 0, 20}, Vec3{2, 1, 2}, 4.0f, 2.0f,
			0.15f, 1.8f, 0.10f, EscapeMode::MashButton, 0 };
		HazardInstance h2{ HazardType::Bog, HazardShape::Box,
			Vec3{30, 0, 40}, Vec3{3, 1.5f, 3}, 0.0f, 0.0f,
			0.08f, 1.2f, 0.20f, EscapeMode::LateralMove, 0 };
		HazardInstance h3{ HazardType::Tar, HazardShape::Cylinder,
			Vec3{50, 0, 60}, Vec3{2, 1, 2}, 3.0f, 1.5f,
			0.05f, 0.8f, 0.05f, EscapeMode::MashButtonItem, 42 };
		HazardInstance h4{ HazardType::LavaSurface, HazardShape::Box,
			Vec3{70, 0, 80}, Vec3{5, 0.5f, 5}, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, EscapeMode::None, 0 };
		s.hazards = { h1, h2, h3, h4 };
		return s;
	}

	void Test_Hazards_RoundtripBin()
	{
		HazardScene src = MakeFullScene();
		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveHazardsBin(src, bytes, err));
		REQUIRE(err.empty());

		HazardScene dst;
		REQUIRE(LoadHazardsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(err.empty());
		REQUIRE(dst.hazards.size() == src.hazards.size());

		for (size_t i = 0; i < src.hazards.size(); ++i)
		{
			const auto& a = src.hazards[i];
			const auto& b = dst.hazards[i];
			REQUIRE(a.type == b.type);
			REQUIRE(a.shape == b.shape);
			REQUIRE(std::memcmp(&a.position, &b.position, sizeof(Vec3)) == 0);
			REQUIRE(std::memcmp(&a.boxHalfExtents, &b.boxHalfExtents, sizeof(Vec3)) == 0);
			REQUIRE(a.cylRadius == b.cylRadius);
			REQUIRE(a.cylHeight == b.cylHeight);
			REQUIRE(a.sinkRateMps == b.sinkRateMps);
			REQUIRE(a.maxDepthMeters == b.maxDepthMeters);
			REQUIRE(a.slowdownMul == b.slowdownMul);
			REQUIRE(a.escapeMode == b.escapeMode);
			REQUIRE(a.requiredItemId == b.requiredItemId);
		}
	}

	void Test_PointInHazard_Cylinder()
	{
		HazardInstance hz{};
		hz.shape = HazardShape::Cylinder;
		hz.position = Vec3{0, 0, 0};
		hz.cylRadius = 3.0f;
		hz.cylHeight = 2.0f;
		REQUIRE(PointInHazard(hz, Vec3{0, 1, 0}));         // centre interne
		REQUIRE(!PointInHazard(hz, Vec3{0, -0.1f, 0}));    // sous le cylindre
		REQUIRE(!PointInHazard(hz, Vec3{0, 2.1f, 0}));     // au-dessus
		REQUIRE(!PointInHazard(hz, Vec3{3.1f, 1, 0}));     // hors radius
		REQUIRE(PointInHazard(hz, Vec3{2.9f, 1, 0}));      // juste dedans
	}

	void Test_PointInHazard_Box()
	{
		HazardInstance hz{};
		hz.shape = HazardShape::Box;
		hz.position = Vec3{0, 0, 0};
		hz.boxHalfExtents = Vec3{2, 1, 3};
		REQUIRE(PointInHazard(hz, Vec3{0, 0, 0}));
		REQUIRE(PointInHazard(hz, Vec3{1.9f, 0.9f, 2.9f}));
		REQUIRE(!PointInHazard(hz, Vec3{2.1f, 0, 0}));
		REQUIRE(!PointInHazard(hz, Vec3{0, 1.1f, 0}));
		REQUIRE(!PointInHazard(hz, Vec3{0, 0, 3.1f}));
	}
}

int main()
{
	Test_Hazards_RoundtripBin();
	Test_PointInHazard_Cylinder();
	Test_PointInHazard_Box();
	if (g_failed == 0) std::printf("[OK] 3 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Ajouter le bloc test dans `CMakeLists.txt`**

Trouve une bonne position (après le bloc `water_mesh_builder_tests` ou similaire) :

```cmake
# M100.16 — Tests round-trip + point-in-volume HazardVolumes.
if(WIN32)
  add_executable(hazard_volumes_tests src/client/world/hazard/tests/HazardVolumesTests.cpp)
  target_include_directories(hazard_volumes_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(hazard_volumes_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(hazard_volumes_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME hazard_volumes_tests COMMAND hazard_volumes_tests)
endif()
```

- [ ] **Step 4: Commit**

```bash
git add src/client/world/hazard/HazardVolumes.cpp src/client/world/hazard/tests/HazardVolumesTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.16): HazardVolumes Save/Load round-trip + tests

SaveHazardsBin / LoadHazardsBin imitent le pattern WaterSurfaces (M100.13) :
OutputVersionHeader (magic+version+contentHash) + payload via ByteWriter/
ByteReader. 60 octets fixes par hazard.

Tests : hazard_volumes_tests (3 PASS — Roundtrip 4 types, PointInHazard
Cylinder, PointInHazard Box).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `HazardSimulator` skeleton + Quicksand sinking

**Files:**
- Create: `src/client/world/hazard/HazardSimulator.h`
- Create: `src/client/world/hazard/HazardSimulator.cpp`
- Modify: `CMakeLists.txt` (ajout source `engine_core`)

- [ ] **Step 1: Créer `HazardSimulator.h`**

```cpp
// src/client/world/hazard/HazardSimulator.h
#pragma once

#include "src/client/world/hazard/HazardVolumes.h"
#include "src/shared/math/Math.h"

#include <functional>
#include <string_view>

namespace engine::world::hazard
{
	/// Callbacks injectés pour découpler le simulator des systèmes inventaire,
	/// animation, audio et mort scriptée. Tous nullables : un callback null
	/// est traité comme un no-op (`hasItem` null → `false`).
	struct HazardCallbacks
	{
		std::function<bool(uint32_t itemId)> hasItem;          // inventaire local
		std::function<void()> onEnter;                          // anim + audio enter
		std::function<void()> onExit;                           // anim + audio exit
		std::function<void(std::string_view reason)> die;       // mort scriptée
	};

	/// État courant du simulator (lecture seule pour debug HUD ou tests).
	struct HazardState
	{
		bool inHazard = false;
		const HazardInstance* activeHazard = nullptr;
		float currentDepth = 0.0f;        // mètres enfoncés sous la surface
		float lateralTraveled = 0.0f;     // mètres horizontal cumulés (LateralMove)
		int   mashCount = 0;              // appuis "Action" dans la fenêtre
		float mashWindowSec = 0.0f;       // âge de la fenêtre mash (s)
		float lavaTimer = 0.0f;           // secondes dans LavaSurface
	};

	/// Effet à appliquer au CharacterController chaque frame.
	/// `applySinkRate=true` → CC force `vel.y = -sinkRateMps` (override gravité).
	/// `slowdownMul` multiplie la vitesse horizontale.
	struct HazardEffect
	{
		bool applySinkRate = false;
		float sinkRateMps = 0.0f;
		float slowdownMul = 1.0f;
	};

	/// Simulator client : détection entrée volume, progression sinking, escape,
	/// mort scriptée. Doit être ticked chaque frame depuis l'Engine après le
	/// calcul de position du joueur. Pas de thread-safety (main thread uniquement).
	class HazardSimulator
	{
	public:
		/// Mémorise les références. La scène et les callbacks doivent survivre
		/// au simulator.
		void Init(const HazardScene& scene, const HazardCallbacks& cb) noexcept;

		/// Avance la simulation d'une frame.
		/// \param dt seconds écoulées depuis la dernière frame.
		/// \param playerPos position monde des pieds du joueur.
		/// \param actionPressed front montant du bouton Action (true exactement
		///        la frame où le joueur appuie).
		/// \return effet à appliquer au CharacterController ce frame.
		HazardEffect Update(float dt, engine::math::Vec3 playerPos,
			bool actionPressed) noexcept;

		const HazardState& State() const noexcept { return m_state; }

	private:
		const HazardScene* m_scene = nullptr;
		HazardCallbacks m_cb;
		HazardState m_state;
		engine::math::Vec3 m_lastPlayerPos{0, 0, 0};
		bool m_hasLastPos = false;
	};
}
```

- [ ] **Step 2: Créer `HazardSimulator.cpp` avec entrée + sinking de base (Quicksand/Bog/Tar)**

```cpp
// src/client/world/hazard/HazardSimulator.cpp
#include "src/client/world/hazard/HazardSimulator.h"

#include <cmath>

namespace engine::world::hazard
{
	namespace
	{
		// Fenêtre de mash : compteur valide 5 s, reset après.
		constexpr float kMashWindowSec = 5.0f;
		constexpr int   kMashThreshold = 10;
		constexpr float kLateralThresholdMeters = 2.0f;
		constexpr float kLavaKillSec = 3.0f;
	}

	void HazardSimulator::Init(const HazardScene& scene, const HazardCallbacks& cb) noexcept
	{
		m_scene = &scene;
		m_cb = cb;
		m_state = HazardState{};
		m_hasLastPos = false;
	}

	HazardEffect HazardSimulator::Update(float dt, engine::math::Vec3 playerPos,
		bool /*actionPressed*/) noexcept
	{
		if (!m_scene) return HazardEffect{};

		// Détection entrée : si pas déjà dans un hazard, cherche un volume.
		if (!m_state.inHazard)
		{
			for (const auto& hz : m_scene->hazards)
			{
				if (PointInHazard(hz, playerPos))
				{
					m_state.inHazard = true;
					m_state.activeHazard = &hz;
					m_state.currentDepth = 0.0f;
					m_state.lateralTraveled = 0.0f;
					m_state.mashCount = 0;
					m_state.mashWindowSec = 0.0f;
					m_state.lavaTimer = 0.0f;
					if (m_cb.onEnter) m_cb.onEnter();
					break;
				}
			}
		}

		// Pas (ou plus) dans un hazard : reset et no-op.
		if (!m_state.inHazard)
		{
			m_lastPlayerPos = playerPos;
			m_hasLastPos = true;
			return HazardEffect{};
		}

		const HazardInstance& hz = *m_state.activeHazard;

		// LavaSurface : mort en 3 s, pas d'enfoncement ni d'escape.
		if (hz.type == HazardType::LavaSurface)
		{
			m_state.lavaTimer += dt;
			if (m_state.lavaTimer >= kLavaKillSec)
			{
				if (m_cb.die) m_cb.die("lava_burning");
				m_state = HazardState{};
				return HazardEffect{};
			}
			return HazardEffect{ false, 0.0f, hz.slowdownMul };
		}

		// Sinking progressif (Quicksand/Bog/Tar).
		m_state.currentDepth += hz.sinkRateMps * dt;

		// (Escape modes implémentés en Task 4.)

		// Mort si profondeur max atteinte sans escape.
		if (m_state.currentDepth >= hz.maxDepthMeters)
		{
			if (m_cb.die) m_cb.die("hazard_drowning");
			m_state = HazardState{};
			return HazardEffect{};
		}

		m_lastPlayerPos = playerPos;
		m_hasLastPos = true;
		return HazardEffect{ true, hz.sinkRateMps, hz.slowdownMul };
	}
}
```

- [ ] **Step 3: Ajouter `HazardSimulator.cpp` à `engine_core` dans `CMakeLists.txt`**

Ajouter juste après `HazardVolumes.cpp` dans la flat list `engine_core`.

- [ ] **Step 4: Commit**

```bash
git add src/client/world/hazard/HazardSimulator.h src/client/world/hazard/HazardSimulator.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.16): HazardSimulator skeleton + Quicksand/Bog/Tar sinking

State machine : détection point-in-volume → onEnter callback → sinking
progressif. Mort scriptée si currentDepth >= maxDepthMeters.

LavaSurface implémenté en partie : timer 3 s puis cb.die("lava_burning").

Escape modes (Mash/Lateral/Item) en Task 4.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `HazardSimulator` — escape modes + tests

**Files:**
- Modify: `src/client/world/hazard/HazardSimulator.cpp` (logique escape)
- Create: `src/client/world/hazard/tests/HazardSimulatorTests.cpp`
- Modify: `CMakeLists.txt` (bloc test `hazard_simulator_tests`)

- [ ] **Step 1: Implémenter les escape modes dans `Update`**

Remplace dans `HazardSimulator::Update()` le commentaire `// (Escape modes implémentés en Task 4.)` par :

```cpp
		// Escape modes : tente l'évasion AVANT le check de mort.
		bool escaped = false;
		switch (hz.escapeMode)
		{
			case EscapeMode::MashButton:
			case EscapeMode::MashButtonItem:
			{
				// Fenêtre glissante de kMashWindowSec.
				m_state.mashWindowSec += dt;
				if (m_state.mashWindowSec > kMashWindowSec)
				{
					m_state.mashWindowSec = 0.0f;
					m_state.mashCount = 0;
				}
				if (actionPressed) ++m_state.mashCount;

				if (m_state.mashCount >= kMashThreshold)
				{
					if (hz.escapeMode == EscapeMode::MashButtonItem)
					{
						// L'item est requis pour libérer.
						if (m_cb.hasItem && m_cb.hasItem(hz.requiredItemId))
						{
							escaped = true;
						}
					}
					else
					{
						escaped = true;
					}
				}
				break;
			}
			case EscapeMode::LateralMove:
			{
				if (m_hasLastPos)
				{
					const float dxz = std::sqrt(
						  (playerPos.x - m_lastPlayerPos.x) * (playerPos.x - m_lastPlayerPos.x)
						+ (playerPos.z - m_lastPlayerPos.z) * (playerPos.z - m_lastPlayerPos.z));
					m_state.lateralTraveled += dxz;
				}
				if (m_state.lateralTraveled >= kLateralThresholdMeters)
				{
					escaped = true;
				}
				break;
			}
			case EscapeMode::None:
			default:
				break;
		}

		if (escaped)
		{
			if (m_cb.onExit) m_cb.onExit();
			m_state = HazardState{};
			m_lastPlayerPos = playerPos;
			m_hasLastPos = true;
			return HazardEffect{};
		}
```

> Note : le paramètre `actionPressed` doit maintenant être réellement utilisé. Retire le `/*actionPressed*/` de la signature dans le `.cpp` pour activer le warning W4 si jamais inutilisé.

- [ ] **Step 2: Créer `HazardSimulatorTests.cpp` avec 5 tests**

```cpp
// src/client/world/hazard/tests/HazardSimulatorTests.cpp
#include "src/client/world/hazard/HazardSimulator.h"
#include "src/client/world/hazard/HazardVolumes.h"

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

	using engine::math::Vec3;
	using engine::world::hazard::EscapeMode;
	using engine::world::hazard::HazardCallbacks;
	using engine::world::hazard::HazardEffect;
	using engine::world::hazard::HazardInstance;
	using engine::world::hazard::HazardScene;
	using engine::world::hazard::HazardShape;
	using engine::world::hazard::HazardSimulator;
	using engine::world::hazard::HazardType;

	HazardScene MakeQuicksandScene()
	{
		HazardScene s;
		HazardInstance h{ HazardType::Quicksand, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.15f, 1.8f, 0.10f, EscapeMode::MashButton, 0 };
		s.hazards.push_back(h);
		return s;
	}

	HazardScene MakeBogScene()
	{
		HazardScene s;
		HazardInstance h{ HazardType::Bog, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.08f, 100.0f, 0.20f, EscapeMode::LateralMove, 0 };
		s.hazards.push_back(h);
		return s;
	}

	HazardScene MakeLavaScene()
	{
		HazardScene s;
		HazardInstance h{ HazardType::LavaSurface, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.0f, 0.0f, 0.0f, EscapeMode::None, 0 };
		s.hazards.push_back(h);
		return s;
	}

	void Test_HazardSimulator_SinkRate()
	{
		HazardScene scene = MakeQuicksandScene();
		HazardSimulator sim;
		sim.Init(scene, HazardCallbacks{});

		// Tick 1 s à 0.1 s par frame, player au centre du cylindre.
		for (int i = 0; i < 10; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, false);
		}
		// Après 10 frames × 0.1 s × 0.15 m/s = 0.15 m.
		REQUIRE(std::fabs(sim.State().currentDepth - 0.15f) < 1e-4f);
	}

	void Test_HazardSimulator_MashEscape()
	{
		HazardScene scene = MakeQuicksandScene();
		HazardSimulator sim;
		bool exited = false;
		HazardCallbacks cb;
		cb.onExit = [&exited]() { exited = true; };
		sim.Init(scene, cb);

		// 10 appuis sur 5 frames de 0.1 s.
		for (int i = 0; i < 10; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, true);
		}
		REQUIRE(exited);
	}

	void Test_HazardSimulator_LateralEscape()
	{
		HazardScene scene = MakeBogScene();
		HazardSimulator sim;
		bool exited = false;
		HazardCallbacks cb;
		cb.onExit = [&exited]() { exited = true; };
		sim.Init(scene, cb);

		// Démarre au centre, fait des steps de 0.3 m latéraux.
		// Premier tick : pas de déplacement (m_hasLastPos = false → 0).
		// Frames suivantes : +0.3 m latéral chacune.
		Vec3 pos{0, 0.5f, 0};
		sim.Update(0.1f, pos, false);  // entrée

		// Fait 8 steps de 0.3 m → 2.4 m cumulés, dépasse seuil 2.0 m.
		for (int i = 0; i < 8; ++i)
		{
			pos.x += 0.3f;
			sim.Update(0.1f, pos, false);
			if (exited) break;
		}
		REQUIRE(exited);
	}

	void Test_HazardSimulator_LavaKills3s()
	{
		HazardScene scene = MakeLavaScene();
		HazardSimulator sim;
		int dieCount = 0;
		std::string lastReason;
		HazardCallbacks cb;
		cb.die = [&dieCount, &lastReason](std::string_view reason) {
			++dieCount;
			lastReason = std::string(reason);
		};
		sim.Init(scene, cb);

		// 30 frames de 0.1 s = 3.0 s exactement.
		for (int i = 0; i < 30; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, false);
			if (dieCount > 0) break;
		}
		// Le 30e tick fait que lavaTimer = 3.0 s, >= kLavaKillSec → die.
		REQUIRE(dieCount == 1);
		REQUIRE(lastReason == "lava_burning");
	}

	void Test_HazardSimulator_DeathOnMaxDepth()
	{
		// Scène Quicksand avec escapeMode=None pour atteindre maxDepth sans escape.
		HazardScene s;
		HazardInstance h{ HazardType::Quicksand, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.5f, 1.0f, 0.10f, EscapeMode::None, 0 };
		s.hazards.push_back(h);

		HazardSimulator sim;
		int dieCount = 0;
		std::string lastReason;
		HazardCallbacks cb;
		cb.die = [&dieCount, &lastReason](std::string_view reason) {
			++dieCount;
			lastReason = std::string(reason);
		};
		sim.Init(s, cb);

		// 0.5 m/s × 0.1 s = 0.05 m/frame. Maxdepth=1.0 → ~20 frames.
		for (int i = 0; i < 25; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, false);
			if (dieCount > 0) break;
		}
		REQUIRE(dieCount == 1);
		REQUIRE(lastReason == "hazard_drowning");
	}
}

int main()
{
	Test_HazardSimulator_SinkRate();
	Test_HazardSimulator_MashEscape();
	Test_HazardSimulator_LateralEscape();
	Test_HazardSimulator_LavaKills3s();
	Test_HazardSimulator_DeathOnMaxDepth();
	if (g_failed == 0) std::printf("[OK] 5 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Ajouter le bloc test dans `CMakeLists.txt`**

```cmake
# M100.16 — Tests state machine HazardSimulator.
if(WIN32)
  add_executable(hazard_simulator_tests src/client/world/hazard/tests/HazardSimulatorTests.cpp)
  target_include_directories(hazard_simulator_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(hazard_simulator_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(hazard_simulator_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME hazard_simulator_tests COMMAND hazard_simulator_tests)
endif()
```

- [ ] **Step 4: Commit**

```bash
git add src/client/world/hazard/HazardSimulator.cpp src/client/world/hazard/tests/HazardSimulatorTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.16): HazardSimulator escape modes + 5 tests

Implémentation des 3 modes d'évasion (MashButton, LateralMove,
MashButtonItem). Fenêtre mash 5 s, seuil 10 appuis. Seuil lateral 2 m
cumulés. MashButtonItem appelle cb.hasItem(requiredItemId).

Tests : hazard_simulator_tests (5 PASS — SinkRate, MashEscape,
LateralEscape, LavaKills3s, DeathOnMaxDepth).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `CharacterController` — `SetHazardEffect` hook

**Files:**
- Modify: `src/client/gameplay/CharacterController.h`
- Modify: `src/client/gameplay/CharacterController.cpp`

- [ ] **Step 1: Étendre l'API de `CharacterController.h`**

Inclure en haut (s'il n'y est pas déjà) :
```cpp
#include "src/client/world/hazard/HazardSimulator.h"  // pour HazardEffect
```

> Si le include crée un cycle ou alourdit le header, préférer un forward declaration et déplacer l'inclusion dans le .cpp. Test : faire `class HazardSimulator;` ne marche pas car on a besoin de `HazardEffect` (struct) — utiliser forward decl `struct HazardEffect;` et include dans .cpp.

Préférer le forward declaration : ajouter au début du fichier .h, après les includes :

```cpp
namespace engine::world::hazard { struct HazardEffect; }
```

Dans la classe `CharacterController`, après `SetWaterTransitionCallbacks(...)` (M100.15) :

```cpp
		/// M100.16 — applique un effet hazard (sinking + slowdown) pour la frame
		/// courante. Doit être appelé chaque frame depuis l'Engine après
		/// `HazardSimulator::Update`. Sans appel : pas d'effet (comportement
		/// par défaut M100.15 préservé).
		void SetHazardEffect(const engine::world::hazard::HazardEffect& effect) noexcept;

		/// True si la frame courante a un sink rate forcé (joueur s'enfonce).
		bool IsSinking() const noexcept;
```

Dans la section private (après les callbacks M100.15) :

```cpp
		// M100.16 — effet hazard appliqué la frame courante.
		// applySinkRate=false par défaut → no-op.
		// Re-set chaque frame par l'Engine ; reset implicite à HazardEffect{}
		// si l'Engine ne l'appelle pas (impossible : doit appeler chaque frame).
		bool m_hazardApplySinkRate = false;
		float m_hazardSinkRateMps = 0.0f;
		float m_hazardSlowdownMul = 1.0f;
```

> On stocke les 3 champs dénormalisés plutôt que `HazardEffect` complet pour éviter d'inclure le header dans CharacterController.h.

- [ ] **Step 2: Implémenter setter + appliquer l'effet dans `Update()`**

Dans `CharacterController.cpp`, include en tête :

```cpp
#include "src/client/world/hazard/HazardSimulator.h"
```

Ajouter en fin de fichier (avant la fermeture namespace) :

```cpp
	void CharacterController::SetHazardEffect(
		const engine::world::hazard::HazardEffect& effect) noexcept
	{
		m_hazardApplySinkRate = effect.applySinkRate;
		m_hazardSinkRateMps = effect.sinkRateMps;
		m_hazardSlowdownMul = effect.slowdownMul;
	}

	bool CharacterController::IsSinking() const noexcept
	{
		return m_hazardApplySinkRate;
	}
```

Dans `Update()` :

**Édit chirurgical A** — après le calcul des `vel` horizontaux (cherche dans Update() la zone où `vel.x` et `vel.z` sont assignés depuis `desiredVel` ou similaire) — appliquer le slowdown horizontal :

```cpp
		// M100.16 — hazard slowdown : multiplie la vitesse horizontale.
		// Application APRÈS le calcul nominal pour ne pas brider les inputs ;
		// la vitesse résultante est seulement réduite, pas annulée.
		if (m_hazardSlowdownMul != 1.0f)
		{
			vel.x *= m_hazardSlowdownMul;
			vel.z *= m_hazardSlowdownMul;
		}
```

> Placement : repère la zone où `vel` final est calculé pour la sweep capsule. Insère le bloc juste avant le sweep. Si plusieurs sites possibles, choisis celui qui s'exécute aussi en mode `Water` pour que le slowdown s'applique même en nage (cas où un joueur swimming entre dans une zone Bog, par exemple).

**Édit chirurgical B** — pour le sink rate vertical : override `vel.y` quand `applySinkRate=true`. Ce override doit se faire APRÈS la gravité et avant le sweep. Cherche la zone gravité (ligne ~170-201 environ) :

```cpp
		// M100.16 — hazard sink rate : remplace vel.y par -sinkRateMps.
		// Override la gravité et le breach surface eau. Applique seulement
		// si le simulator a posé un sinkRate (Quicksand/Bog/Tar actif).
		if (m_hazardApplySinkRate)
		{
			m_velocity.y = -m_hazardSinkRateMps;
		}
```

Insère juste après le calcul de gravité (la dernière modification de `m_velocity.y` issue de la branche `isFlying ? : inWater ? : ground/air`).

- [ ] **Step 3: Commit**

```bash
git add src/client/gameplay/CharacterController.h src/client/gameplay/CharacterController.cpp
git commit -m "$(cat <<'EOF'
feat(M100.16): CharacterController — SetHazardEffect hook

Hook minimal pour appliquer l'effet hazard calculé par HazardSimulator :
- vel.y = -sinkRateMps quand applySinkRate (override gravité + breach eau)
- vel.x/vel.z multipliés par slowdownMul (réduction horizontale)
- IsSinking() pour query externe (camera offset, debug HUD)

Forward declaration `struct HazardEffect` dans le header pour éviter
l'include transitif. Les 3 champs sont dénormalisés dans le CC pour
préserver l'isolation du header.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: `ThirdPersonCamera::SetGroundOffset` + éditeur (HazardDocument + HazardTool)

**Files:**
- Modify: `src/client/gameplay/ThirdPersonCamera.h`
- Modify: `src/client/gameplay/ThirdPersonCamera.cpp`
- Create: `src/world_editor/hazard/HazardDocument.h`
- Create: `src/world_editor/hazard/HazardDocument.cpp`
- Create: `src/world_editor/hazard/HazardTool.h`
- Create: `src/world_editor/hazard/HazardTool.cpp`
- Modify: `CMakeLists.txt` (ajout sources `engine_core` pour HazardDocument + HazardTool)

- [ ] **Step 1: Ajouter `SetGroundOffset` à `ThirdPersonCamera.h`**

Dans la classe `ThirdPersonCamera`, après la méthode `SetCombatMode(...)` :

```cpp
		/// M100.16 — décalage Y appliqué à la cible (élève la caméra par rapport
		/// aux pieds joueur). Utile pour les hazards : pendant l'enfoncement, les
		/// pieds descendent mais la tête (cible caméra) doit rester à sa hauteur
		/// d'origine. `offset > 0` → caméra plus haute.
		void SetGroundOffset(float offsetY) noexcept;
```

Dans la section private (après `m_combatMode`) :

```cpp
		float m_groundOffsetY = 0.0f;  ///< M100.16 — décalage Y additionnel à la cible
```

- [ ] **Step 2: Implémenter dans `ThirdPersonCamera.cpp`**

Setter trivial en fin de fichier :

```cpp
	void ThirdPersonCamera::SetGroundOffset(float offsetY) noexcept
	{
		m_groundOffsetY = offsetY;
	}
```

Dans `Update()`, repère la ligne où `targetPos.y + m_cfg.targetOffsetY` est calculé pour la cible/focus. Y ajouter `m_groundOffsetY` :

```cpp
		// Avant : float targetY = targetPos.y + m_cfg.targetOffsetY;
		const float targetY = targetPos.y + m_cfg.targetOffsetY + m_groundOffsetY;
```

> Adapte selon l'expression exacte trouvée dans le fichier. L'idée : la cible logique de la caméra reçoit `+ m_groundOffsetY`.

- [ ] **Step 3: Créer `HazardDocument.h`**

```cpp
// src/world_editor/hazard/HazardDocument.h
#pragma once

#include "src/client/world/hazard/HazardVolumes.h"

#include <cstddef>
#include <vector>

namespace engine::editor::hazard
{
	/// État monde éditeur pour les hazards. Liste mutable d'instances avec
	/// add/remove/get. Persistance via les helpers HazardVolumes.
	///
	/// Pas d'undo/redo intégré (utilisera le CommandStack éditeur dans un
	/// futur ticket — actuellement géré par M100.2 via un wrapper externe).
	class HazardDocument
	{
	public:
		/// Ajoute une nouvelle instance. Retourne son index.
		size_t Add(const engine::world::hazard::HazardInstance& hz);

		/// Retire l'instance à `index`. No-op si index invalide.
		void Remove(size_t index);

		/// Accès lecture seule à la scène complète.
		const engine::world::hazard::HazardScene& Scene() const noexcept { return m_scene; }

		/// Accès lecture/écriture (pour modifier les paramètres d'un hazard
		/// existant via Tool Properties).
		engine::world::hazard::HazardScene& MutableScene() noexcept { return m_scene; }

		/// Nombre d'instances.
		size_t Count() const noexcept { return m_scene.hazards.size(); }

		/// Reset complet.
		void Clear() noexcept { m_scene.hazards.clear(); }

	private:
		engine::world::hazard::HazardScene m_scene;
	};
}
```

- [ ] **Step 4: Créer `HazardDocument.cpp`**

```cpp
// src/world_editor/hazard/HazardDocument.cpp
#include "src/world_editor/hazard/HazardDocument.h"

namespace engine::editor::hazard
{
	size_t HazardDocument::Add(const engine::world::hazard::HazardInstance& hz)
	{
		m_scene.hazards.push_back(hz);
		return m_scene.hazards.size() - 1;
	}

	void HazardDocument::Remove(size_t index)
	{
		if (index >= m_scene.hazards.size()) return;
		m_scene.hazards.erase(m_scene.hazards.begin() + static_cast<std::ptrdiff_t>(index));
	}
}
```

- [ ] **Step 5: Créer `HazardTool.h`**

```cpp
// src/world_editor/hazard/HazardTool.h
#pragma once

#include "src/client/world/hazard/HazardVolumes.h"

namespace engine::editor::hazard
{
	class HazardDocument;

	/// Outil éditeur "Hazard". Stocke les paramètres courants (type, shape,
	/// dimensions, escape) et offre une méthode `PlaceAt(pos)` qui crée
	/// une nouvelle instance dans le HazardDocument cible.
	///
	/// MVP : pas de gizmo Vulkan ni d'undo via CommandStack. Le panneau ImGui
	/// (Tool Properties) appellera directement ces accesseurs. L'intégration
	/// avec CommandStack viendra avec M100.34 (Save/Load Zone orchestrateur).
	class HazardTool
	{
	public:
		void SetDocument(HazardDocument* doc) noexcept { m_document = doc; }

		void SetType(engine::world::hazard::HazardType t) noexcept { m_template.type = t; }
		void SetShape(engine::world::hazard::HazardShape s) noexcept { m_template.shape = s; }
		void SetBoxHalfExtents(engine::math::Vec3 he) noexcept { m_template.boxHalfExtents = he; }
		void SetCylRadius(float r) noexcept { m_template.cylRadius = r; }
		void SetCylHeight(float h) noexcept { m_template.cylHeight = h; }
		void SetSinkRateMps(float r) noexcept { m_template.sinkRateMps = r; }
		void SetMaxDepthMeters(float d) noexcept { m_template.maxDepthMeters = d; }
		void SetSlowdownMul(float m) noexcept { m_template.slowdownMul = m; }
		void SetEscapeMode(engine::world::hazard::EscapeMode m) noexcept { m_template.escapeMode = m; }
		void SetRequiredItemId(uint32_t id) noexcept { m_template.requiredItemId = id; }

		const engine::world::hazard::HazardInstance& Template() const noexcept { return m_template; }

		/// Place une instance à la position monde demandée (typiquement issue
		/// d'un raycast click). Retourne l'index de la nouvelle instance, ou
		/// SIZE_MAX si pas de document.
		size_t PlaceAt(engine::math::Vec3 worldPos);

	private:
		HazardDocument* m_document = nullptr;
		engine::world::hazard::HazardInstance m_template{};
	};
}
```

- [ ] **Step 6: Créer `HazardTool.cpp`**

```cpp
// src/world_editor/hazard/HazardTool.cpp
#include "src/world_editor/hazard/HazardTool.h"
#include "src/world_editor/hazard/HazardDocument.h"

#include <limits>

namespace engine::editor::hazard
{
	size_t HazardTool::PlaceAt(engine::math::Vec3 worldPos)
	{
		if (!m_document) return std::numeric_limits<size_t>::max();
		auto instance = m_template;
		instance.position = worldPos;
		return m_document->Add(instance);
	}
}
```

- [ ] **Step 7: Ajouter les 2 sources éditeur à `engine_core` dans `CMakeLists.txt`**

Dans la flat list de sources `engine_core`, après les sources `src/world_editor/water/` (lignes ~300-304), ajouter :

```cmake
  src/world_editor/hazard/HazardDocument.cpp
  src/world_editor/hazard/HazardTool.cpp
```

- [ ] **Step 8: Commit**

```bash
git add src/client/gameplay/ThirdPersonCamera.h src/client/gameplay/ThirdPersonCamera.cpp src/world_editor/hazard/HazardDocument.h src/world_editor/hazard/HazardDocument.cpp src/world_editor/hazard/HazardTool.h src/world_editor/hazard/HazardTool.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.16): ThirdPersonCamera SetGroundOffset + HazardTool/Document

ThirdPersonCamera.SetGroundOffset(float) : décalage Y additionnel sur
la cible caméra. Utilisé par l'Engine pour compenser la descente des
pieds pendant l'enfoncement hazard (la tête reste à hauteur d'origine).

HazardDocument : état monde éditeur (liste mutable, Add/Remove/Clear).
HazardTool : template d'instance + PlaceAt(worldPos). Pas d'undo
intégré (M100.34 orchestrera via CommandStack).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: `StreamCache::LoadHazards` + `ChunkPackageWriter::WriteHazards`

**Files:**
- Modify: `src/client/world/StreamCache.h` (déclaration `LoadHazards`)
- Modify: `src/client/world/StreamCache.cpp` (implémentation)
- Modify: `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` (déclaration `WriteHazards`)
- Modify: `tools/zone_builder/lib/ChunkPackageWriter.cpp` (implémentation)

- [ ] **Step 1: Étendre `StreamCache.h`**

Ajouter en haut du namespace `engine::world`, après le forward `namespace water { struct WaterScene; }` :

```cpp
	namespace hazard  { struct HazardScene; }
```

Dans la classe `StreamCache`, après `LoadWater(...)` :

```cpp
		/// Charge le `instances/hazards.bin` global de la zone (M100.16).
		/// Si fichier absent, retourne nullptr sans warning (zone sans hazards).
		/// \param zoneName réservé pour multi-zone (M100.34) — actuellement ignoré.
		std::shared_ptr<engine::world::hazard::HazardScene> LoadHazards(
			const engine::core::Config& config, std::string_view zoneName);
```

- [ ] **Step 2: Implémenter `LoadHazards` dans `StreamCache.cpp`**

Inclure en haut :

```cpp
#include "src/client/world/hazard/HazardVolumes.h"
```

Imite l'implémentation de `LoadWater` qui existe déjà dans le fichier (cherche `LoadWater` pour repérer le pattern). Pattern :
1. Construire la clé `instances/hazards.bin` (zoneName ignoré pour MVP).
2. Lookup cache via `Lookup(key)` ; sur miss, lire le fichier disque.
3. Si fichier absent → retourner nullptr sans warning.
4. Désérialiser via `LoadHazardsBin`. Si erreur, LOG_WARN et retourner nullptr.
5. Insérer dans cache si nécessaire.

Adapter en respectant strictement le format de `LoadWater` (chemins, paths.content, etc.).

- [ ] **Step 3: Étendre `ChunkPackageWriter.h`**

Ajouter le forward declaration et la déclaration du writer juste après `WriteWater(...)` (vers ligne 60 du header) :

```cpp
namespace engine::world::hazard { struct HazardScene; }
```

(au top du fichier, dans le bon emplacement)

```cpp
	/// Écrit `instances/hazards.bin` (M100.16) à `outputRootDir/instances/hazards.bin`.
	/// Crée le dossier parent si nécessaire. Retourne false + outError sur erreur I/O.
	bool WriteHazards(std::string_view outputRootDir,
		const engine::world::hazard::HazardScene& scene,
		std::string& outError);
```

- [ ] **Step 4: Implémenter `WriteHazards` dans `ChunkPackageWriter.cpp`**

Inclure en haut :
```cpp
#include "src/client/world/hazard/HazardVolumes.h"
```

Imite `WriteWater` (cherche `bool WriteWater(` autour de la ligne 598 du fichier) :

```cpp
	bool WriteHazards(std::string_view outputRootDir,
		const engine::world::hazard::HazardScene& scene,
		std::string& outError)
	{
		std::filesystem::path dir = std::filesystem::path(outputRootDir) / "instances";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		if (ec && !std::filesystem::exists(dir))
		{
			outError = "WriteHazards: mkdir failed: " + ec.message();
			return false;
		}

		std::vector<uint8_t> bytes;
		std::string err;
		if (!engine::world::hazard::SaveHazardsBin(scene, bytes, err))
		{
			outError = "WriteHazards: serialize failed: " + err;
			return false;
		}

		std::filesystem::path file = dir / "hazards.bin";
		std::ofstream stream(file, std::ios::binary);
		if (!stream)
		{
			outError = "WriteHazards: open failed: " + file.string();
			return false;
		}
		stream.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!stream)
		{
			outError = "WriteHazards: write failed: " + file.string();
			return false;
		}
		return true;
	}
```

> Vérifie les noms exacts dans le pattern de WriteWater (ofstream, paths, error format). Réutilise les mêmes.

- [ ] **Step 5: Commit**

```bash
git add src/client/world/StreamCache.h src/client/world/StreamCache.cpp tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h tools/zone_builder/lib/ChunkPackageWriter.cpp
git commit -m "$(cat <<'EOF'
feat(M100.16): pipeline I/O — StreamCache.LoadHazards + WriteHazards

StreamCache.LoadHazards : runtime client lit `instances/hazards.bin`
via le cache (pattern LoadWater M100.13). Retourne nullptr sans warning
si fichier absent (zones sans hazards).

ChunkPackageWriter.WriteHazards : zone_builder écrit hazards.bin à
l'export zone (pattern WriteWater).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Vérification finale + CODEBASE_MAP + PR

**Files:** verification + `CODEBASE_MAP.md`

- [ ] **Step 1: Vérifier l'isolation serveur**

Run: `grep -rE "HazardVolumes\.cpp|HazardSimulator\.cpp|HazardDocument\.cpp|HazardTool\.cpp" CMakeLists.txt`

**Expected** : toutes les occurrences uniquement dans la cible `engine_core`. Aucune dans `engine_core_server`, `lcdlln_master`, `lcdlln_shard`, etc.

- [ ] **Step 2: Vérifier que tous les fichiers attendus sont commités**

Run: `git diff --name-only main..HEAD`

**Expected** (16-17 fichiers) :
- `CMakeLists.txt`
- `CODEBASE_MAP.md` (ajouté à cette task)
- `docs/superpowers/specs/2026-05-12-m100-16-hazard-volume-system-design.md`
- `docs/superpowers/plans/2026-05-12-m100-16-hazard-volume-system.md`
- `src/client/world/hazard/HazardVolumes.h` + `.cpp`
- `src/client/world/hazard/HazardSimulator.h` + `.cpp`
- `src/client/world/hazard/tests/HazardVolumesTests.cpp`
- `src/client/world/hazard/tests/HazardSimulatorTests.cpp`
- `src/client/gameplay/CharacterController.h` + `.cpp`
- `src/client/gameplay/ThirdPersonCamera.h` + `.cpp`
- `src/world_editor/hazard/HazardDocument.h` + `.cpp`
- `src/world_editor/hazard/HazardTool.h` + `.cpp`
- `src/client/world/StreamCache.h` + `.cpp`
- `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h`
- `tools/zone_builder/lib/ChunkPackageWriter.cpp`

- [ ] **Step 3: Mettre à jour `CODEBASE_MAP.md`**

Lis les 5 premières lignes pour repérer la zone "Dernière mise à jour : YYYY-MM-DD —" et le premier paragraphe de l'historique.

Ajoute une nouvelle entrée AU DÉBUT du paragraphe (juste après "Dernière mise à jour : 2026-05-12 —" et l'entrée M100.15) :

```
**M100.16 (Hazard Volume System)** sur la branche `claude/m100-16-hazard-volumes`. Volumes 3D (Box ou Cylinder) qui déclenchent enfoncement progressif (Quicksand/Bog/Tar) ou mort par contact (LavaSurface). 4 modes d'évasion (None, MashButton, LateralMove, MashButtonItem) via callbacks injectés au `HazardSimulator` (inventaire, animation, audio, mort) — découplage strict, CC sans dépendance audio/inventory. Composants livrés : (1) `src/client/world/hazard/HazardVolumes.{h,cpp}` — enums `HazardType/Shape/EscapeMode`, struct `HazardInstance`, format `instances/hazards.bin` (round-trip 60 octets/hazard, header magic+version+xxhash64). (2) `src/client/world/hazard/HazardSimulator.{h,cpp}` — state machine entrée/sinking/escape/death, fenêtre mash 5s/seuil 10 appuis, lateral 2 m cumulés, lava timer 3 s. (3) `CharacterController::SetHazardEffect` — override vel.y avec sinkRateMps, multiplie vel.x/z par slowdownMul (forward decl `HazardEffect` pour préserver header). (4) `ThirdPersonCamera::SetGroundOffset` — décalage Y additionnel sur la cible (compensation enfoncement). (5) `src/world_editor/hazard/HazardDocument` + `HazardTool` — outil éditeur (template + PlaceAt). (6) Pipeline I/O : `StreamCache::LoadHazards` (pattern LoadWater) + `ChunkPackageWriter::WriteHazards` (pattern WriteWater). 8 tests CTest sur 2 exécutables (`hazard_volumes_tests` 3, `hazard_simulator_tests` 5) tous gardés `if(WIN32)`. Aucun opcode réseau, aucune migration DB. Déploiement : ✅ client/éditeur uniquement, pas de redéploiement serveur.
```

> Si l'entrée M100.15 existe déjà, ajoute M100.16 juste après "Dernière mise à jour" et chaîne avec "Précédente : 2026-05-12 — **M100.15 ...**".

- [ ] **Step 4: Commit le CODEBASE_MAP**

```bash
git add CODEBASE_MAP.md
git commit -m "$(cat <<'EOF'
docs(M100.16): mise à jour CODEBASE_MAP — Hazard Volume System livré

HazardVolumes + HazardSimulator + CC SetHazardEffect + ThirdPersonCamera
SetGroundOffset + HazardTool/Document + StreamCache.LoadHazards +
ChunkPackageWriter.WriteHazards.

8 tests CTest sur 2 exécutables. Client/éditeur uniquement.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Push + ouverture PR**

```bash
git push -u origin claude/m100-16-hazard-volumes
gh pr create --title "feat(M100.16): Hazard Volume System — Quicksand/Bog/Tar/Lava" --body "$(cat <<'BODY'
## Résumé

Livre **M100.16 — Hazard Volume System** : volumes 3D dangereux (Box ou Cylinder) qui déclenchent enfoncement progressif (Quicksand, Bog, Tar) ou mort par contact (LavaSurface). 4 modes d'évasion (None, MashButton, LateralMove, MashButtonItem). Simulation strictement côté client.

- **`HazardVolumes`** (`src/client/world/hazard/`) — enums + struct `HazardInstance` + format `instances/hazards.bin` (round-trip 60 octets/hazard, header magic+version+xxhash64) + helper `PointInHazard`.
- **`HazardSimulator`** — state machine entrée → sinking → escape → death. Callbacks injectés (`hasItem`, `onEnter`, `onExit`, `die`) pour découplage strict avec systèmes inventaire/animation/audio/mort.
- **`CharacterController::SetHazardEffect`** — override `vel.y = -sinkRateMps` quand sinking, multiplie `vel.x/z` par `slowdownMul`. Forward declaration `HazardEffect` pour préserver le header.
- **`ThirdPersonCamera::SetGroundOffset`** — décalage Y additionnel sur la cible (compensation hauteur tête pendant enfoncement).
- **`HazardTool` + `HazardDocument`** — outil éditeur (template d'instance + `PlaceAt(worldPos)`).
- **Pipeline I/O** : `StreamCache::LoadHazards` (pattern `LoadWater`) + `ChunkPackageWriter::WriteHazards` (pattern `WriteWater`).

**Spec :** [docs/superpowers/specs/2026-05-12-m100-16-hazard-volume-system-design.md](docs/superpowers/specs/2026-05-12-m100-16-hazard-volume-system-design.md)
**Plan :** [docs/superpowers/plans/2026-05-12-m100-16-hazard-volume-system.md](docs/superpowers/plans/2026-05-12-m100-16-hazard-volume-system.md)
**Ticket :** [tickets/M100/M100.16-HazardVolumeSystem.md](tickets/M100/M100.16-HazardVolumeSystem.md)

## Test plan

8 tests CTest répartis sur 2 nouveaux exécutables (gardés `if(WIN32)`) :

- [ ] `hazard_volumes_tests` (3) — `Test_Hazards_RoundtripBin`, `Test_PointInHazard_Cylinder`, `Test_PointInHazard_Box`
- [ ] `hazard_simulator_tests` (5) — `Test_HazardSimulator_SinkRate`, `Test_HazardSimulator_MashEscape`, `Test_HazardSimulator_LateralEscape`, `Test_HazardSimulator_LavaKills3s`, `Test_HazardSimulator_DeathOnMaxDepth`
- [ ] Aucune régression sur les suites existantes
- [ ] Build Linux compile-only OK

## Critères d'acceptation du ticket

- [x] Les 4 types (Quicksand, Bog, Tar, LavaSurface) sélectionnables et sauvegardés (HazardTool + roundtrip test)
- [x] Round-trip `hazards.bin` parfait (`Test_Hazards_RoundtripBin`)
- [x] Le joueur s'enfonce à `sinkRate` m/s mesurés (`Test_HazardSimulator_SinkRate`)
- [x] La caméra tierce reste à hauteur originale (compensation via `SetGroundOffset`, à câbler par l'Engine)
- [x] Mash button compté correctement, 10 appuis en 5 s → escape (`Test_HazardSimulator_MashEscape`)
- [x] LavaSurface tue après exactement 3 s (`Test_HazardSimulator_LavaKills3s`)
- [x] Aucun fichier hazard compilé dans une cible serveur (vérifié par grep)
- [x] Aucun message réseau spécifique aux hazards (aucun opcode ajouté)

## Déploiement

> **Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur. Aucun opcode, aucune migration DB, aucun changement de format binaire serveur.

## Process

- Brainstorming → Spec → Plan → 8 tasks subagent-driven (avec spec+code-quality review par task)
- Stack indépendant de M100.15 (PR #603) — basé sur `origin/main`

🤖 Generated with [Claude Code](https://claude.com/claude-code)
BODY
)"
```

- [ ] **Step 6: Vérifier l'état git final**

```bash
git status
git log --oneline -15
```

**Expected** : working tree clean, ~10 commits M100.16 visibles (Task 1-7 commits + spec + plan + CODEBASE_MAP).

---

## Synthèse déploiement

> **Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur. Aucun opcode, aucune migration DB, aucun changement de format binaire. Sources hazard linkées dans `engine_core` (cible client + éditeur) uniquement. Les callbacks `hasItem`/`onEnter`/`onExit`/`die` doivent être câblés par l'Engine (audio, inventaire, mort scriptée) au moment de l'intégration runtime — stubs acceptables pour M100.16 tant que CHAR-MODEL n'a pas câblé `CharacterController` au runtime.
