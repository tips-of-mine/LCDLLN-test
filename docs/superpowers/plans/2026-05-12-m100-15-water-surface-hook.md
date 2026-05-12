# M100.15 Water Surface Hook — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Brancher les volumes d'eau de M100.13 dans `SurfaceQueryService` (override `Shallow/DeepWater`) et fournir un `IWorldCollider` concret qui pilote le `MovementMode::Water` du `CharacterController` avec hystérésis 1.0 m / 0.7 m et callbacks d'entrée/sortie pour le splash audio.

**Architecture:** Trois pièces neuves (`WaterSampler` + `WorldColliderImpl` + extensions CC) reliées par référence sans dépendance audio dans le `CharacterController`. Le sampler est stateless et lit le `WaterScene` existant. L'hystérésis est dérivée de l'état `m_mode` actuel du CC (aucun membre dédié). Toutes les transitions remontent vers l'Engine via deux callbacks `std::function<void()>`.

**Tech Stack:** C++20, CMake/CTest, framework de test maison (`REQUIRE` macro + `g_failed`), MSVC (Windows), pas de framework externe.

**Spec source :** [docs/superpowers/specs/2026-05-12-m100-15-water-surface-hook-design.md](../specs/2026-05-12-m100-15-water-surface-hook-design.md)

---

## File Structure

| Fichier | Responsabilité | Action |
|---|---|---|
| `src/client/world/water/WaterSurfaces.h` | Données scène + nouvelle struct `WaterSample` | Modifier (ajout struct) |
| `src/client/world/water/WaterSampler.h` | Interface du sampler (pure géométrie eau) | Créer |
| `src/client/world/water/WaterSampler.cpp` | PIP lac + projection segment rivière + multi-overlap | Créer |
| `src/client/world/water/tests/WaterSamplerTests.cpp` | 6 tests géométrie sampler | Créer |
| `src/client/world/surface/SurfaceQueryService.h` | Setter + membre `m_waterSampler` | Modifier |
| `src/client/world/surface/SurfaceQueryService.cpp` | Override final dans `Query()` | Modifier |
| `src/client/world/surface/tests/WaterHookTests.cpp` | 3 tests intégration SurfaceQueryService ↔ Sampler | Créer |
| `src/client/gameplay/WorldColliderImpl.h` | Première impl `IWorldCollider` | Créer |
| `src/client/gameplay/WorldColliderImpl.cpp` | `QueryWater` câblé sampler, `SweepCapsule` stub | Créer |
| `src/client/gameplay/CharacterController.h` | Callbacks setter + 2 membres | Modifier |
| `src/client/gameplay/CharacterController.cpp` | Logique hystérésis + émission callbacks | Modifier |
| `src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp` | 6 tests hystérésis + callbacks | Créer |
| `CMakeLists.txt` (racine) | Ajout 2 sources `engine_core` + 3 exécutables CTest | Modifier |

**Hors scope explicite :** implémentation complète de `SweepCapsule` (heightmap + collision proxies M100.12), animation nage, noyade, courants rivière, intégration runtime in-game du `CharacterController` (chaîne CHAR-MODEL).

---

## Test Framework Reminder

Tous les tests suivent le pattern existant (voir [WaterSurfacesTests.cpp:11-18](../../../src/client/world/water/tests/WaterSurfacesTests.cpp)) :

```cpp
namespace {
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)
}

int main() {
    Test_X();
    Test_Y();
    if (g_failed == 0) std::printf("[OK] N tests passed\n");
    return g_failed == 0 ? 0 : 1;
}
```

Build : `cmake --build build/vs2022-x64 --config Debug --target <test_name>` puis `ctest --test-dir build/vs2022-x64 -C Debug -R <test_name> --output-on-failure`.

---

## Task 1: Ajouter la struct `WaterSample`

**Files:**
- Modify: `src/client/world/water/WaterSurfaces.h:1-56` (ajout après `RiverInstance`, avant `WaterScene`)

- [ ] **Step 1: Ajouter la struct `WaterSample`**

Dans [WaterSurfaces.h](../../../src/client/world/water/WaterSurfaces.h), insérer entre la déclaration `RiverInstance` (l.36-40) et `WaterScene` (l.42-46) :

```cpp
	/// Résultat d'un sampling (M100.15). `surfaceY` est la hauteur monde de
	/// la surface d'eau au point sondé. `depthMeters = surfaceY - feetY`,
	/// toujours strictement positif quand le sample est retourné (les hits
	/// avec pieds au-dessus de la surface ne sont pas remontés).
	struct WaterSample
	{
		float surfaceY = 0.0f;
		float depthMeters = 0.0f;
	};
```

- [ ] **Step 2: Vérifier que le projet compile encore**

Run: `cmake --build build/vs2022-x64 --config Debug --target engine_core`
Expected: build success, aucun warning (la struct est non utilisée pour l'instant).

- [ ] **Step 3: Commit**

```bash
git add src/client/world/water/WaterSurfaces.h
git commit -m "$(cat <<'EOF'
feat(M100.15): ajout struct WaterSample (sampler API contract)

Préparation de l'API WaterSampler::Sample() qui retournera optional<WaterSample>.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `WaterSampler` — lac (PIP)

**Files:**
- Create: `src/client/world/water/WaterSampler.h`
- Create: `src/client/world/water/WaterSampler.cpp`
- Create: `src/client/world/water/tests/WaterSamplerTests.cpp`
- Modify: `CMakeLists.txt` (ajout source `engine_core` ligne ~418 + bloc test ligne ~1162)

- [ ] **Step 1: Créer le header `WaterSampler.h`**

```cpp
// src/client/world/water/WaterSampler.h
#pragma once

#include "src/client/world/water/WaterSurfaces.h"
#include "src/shared/math/Math.h"

#include <optional>

namespace engine::world::water
{
	/// Sampler géométrique stateless qui interroge un `WaterScene` (lacs + rivières)
	/// pour répondre à : « ce point monde est-il dans un volume d'eau, et si oui à
	/// quelle profondeur ? ». Aucun état runtime. Thread-safe pour les lectures
	/// concurrentes (lecture seule sur la scène référencée).
	///
	/// Algorithme :
	/// - Lac : point-in-polygon 2D dans XZ.
	/// - Rivière : projection orthogonale de `worldPos.xz` sur chaque segment,
	///   hit si distance latérale <= widthLocal/2 (interpolation linéaire de la
	///   largeur entre les deux nodes).
	/// - Multi-overlap : retourne le hit le plus profond (plus grand `depthMeters`).
	/// - Filtre `depth > 0` : pas de hit si pieds au-dessus de la surface.
	class WaterSampler
	{
	public:
		/// Mémorise la référence vers la scène. Aucune copie. La scène doit
		/// survivre au sampler.
		bool Init(const WaterScene& scene) noexcept;

		/// Retourne `{surfaceY, depth}` ou `nullopt` si hors eau.
		/// `worldPos.y` = position monde des pieds du joueur.
		std::optional<WaterSample> Sample(engine::math::Vec3 worldPos) const noexcept;

	private:
		const WaterScene* m_scene = nullptr;
	};
}
```

- [ ] **Step 2: Créer la stub `WaterSampler.cpp`** (juste assez pour compiler)

```cpp
// src/client/world/water/WaterSampler.cpp
#include "src/client/world/water/WaterSampler.h"

namespace engine::world::water
{
	bool WaterSampler::Init(const WaterScene& scene) noexcept
	{
		m_scene = &scene;
		return true;
	}

	std::optional<WaterSample> WaterSampler::Sample(engine::math::Vec3 /*worldPos*/) const noexcept
	{
		// stub : sera implémenté étape par étape (lac → rivière → multi-overlap).
		return std::nullopt;
	}
}
```

- [ ] **Step 3: Ajouter `WaterSampler.cpp` à `engine_core` dans `CMakeLists.txt`**

Dans [CMakeLists.txt](../../../CMakeLists.txt), juste après la ligne `src/client/world/water/WaterSurfaces.cpp` (l.418), ajouter :

```cmake
  src/client/world/water/WaterSampler.cpp
```

- [ ] **Step 4: Créer le fichier de tests avec 2 tests lac**

Fichier `src/client/world/water/tests/WaterSamplerTests.cpp` :

```cpp
// src/client/world/water/tests/WaterSamplerTests.cpp
#include "src/client/world/water/WaterSampler.h"
#include "src/client/world/water/WaterSurfaces.h"

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
	using engine::world::water::LakeInstance;
	using engine::world::water::WaterSampler;
	using engine::world::water::WaterScene;

	// Carré de 10×10 centré sur (5, *, 5), surface à Y=10.
	WaterScene MakeSquareLake()
	{
		WaterScene s;
		LakeInstance lake;
		lake.name = "square_lake";
		lake.polygon = { {0, 10, 0}, {10, 10, 0}, {10, 10, 10}, {0, 10, 10} };
		lake.waterLevelY = 10.0f;
		s.lakes.push_back(std::move(lake));
		return s;
	}

	void Test_Lake_PointInside_ReturnsSurfaceY()
	{
		WaterScene scene = MakeSquareLake();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Pieds à Y=8, donc 2 m sous la surface au centre du lac.
		auto hit = sampler.Sample(Vec3{ 5.0f, 8.0f, 5.0f });
		REQUIRE(hit.has_value());
		REQUIRE(std::fabs(hit->surfaceY - 10.0f) < 1e-5f);
		REQUIRE(std::fabs(hit->depthMeters - 2.0f) < 1e-5f);
	}

	void Test_Lake_PointOutside_ReturnsNullopt()
	{
		WaterScene scene = MakeSquareLake();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Hors polygone (x=15).
		auto hit = sampler.Sample(Vec3{ 15.0f, 8.0f, 5.0f });
		REQUIRE(!hit.has_value());
	}
}

int main()
{
	Test_Lake_PointInside_ReturnsSurfaceY();
	Test_Lake_PointOutside_ReturnsNullopt();
	if (g_failed == 0) std::printf("[OK] 2 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 5: Ajouter l'exécutable test dans `CMakeLists.txt`**

Dans [CMakeLists.txt](../../../CMakeLists.txt), après le bloc `water_surfaces_tests` (l.1153-1162), insérer :

```cmake
# M100.15 — Tests WaterSampler (PIP lac + projection rivière).
if(WIN32)
  add_executable(water_sampler_tests src/client/world/water/tests/WaterSamplerTests.cpp)
  target_include_directories(water_sampler_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_sampler_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_sampler_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_sampler_tests COMMAND water_sampler_tests)
endif()
```

- [ ] **Step 6: Build + run pour vérifier que les 2 tests ÉCHOUENT** (TDD red phase)

Run :
```
cmake --build build/vs2022-x64 --config Debug --target water_sampler_tests
ctest --test-dir build/vs2022-x64 -C Debug -R water_sampler_tests --output-on-failure
```
Expected: FAIL (les tests REQUIRE échouent car `Sample()` retourne toujours `nullopt`).

- [ ] **Step 7: Implémenter le PIP lac dans `Sample()`**

Remplacer le contenu de `WaterSampler.cpp` :

```cpp
// src/client/world/water/WaterSampler.cpp
#include "src/client/world/water/WaterSampler.h"

namespace engine::world::water
{
	namespace
	{
		// Point-in-polygon 2D (algorithme du nombre d'intersections, XZ).
		// Polygone CCW garanti par M100.13.
		bool PointInPolygonXZ(float px, float pz,
			const std::vector<engine::math::Vec3>& poly) noexcept
		{
			const size_t n = poly.size();
			if (n < 3) return false;
			bool inside = false;
			for (size_t i = 0, j = n - 1; i < n; j = i++)
			{
				const float xi = poly[i].x, zi = poly[i].z;
				const float xj = poly[j].x, zj = poly[j].z;
				const bool intersect = ((zi > pz) != (zj > pz)) &&
					(px < (xj - xi) * (pz - zi) / (zj - zi + 1e-30f) + xi);
				if (intersect) inside = !inside;
			}
			return inside;
		}
	}

	bool WaterSampler::Init(const WaterScene& scene) noexcept
	{
		m_scene = &scene;
		return true;
	}

	std::optional<WaterSample> WaterSampler::Sample(engine::math::Vec3 worldPos) const noexcept
	{
		if (!m_scene) return std::nullopt;

		for (const auto& lake : m_scene->lakes)
		{
			if (PointInPolygonXZ(worldPos.x, worldPos.z, lake.polygon))
			{
				const float depth = lake.waterLevelY - worldPos.y;
				if (depth > 0.0f)
				{
					return WaterSample{ lake.waterLevelY, depth };
				}
			}
		}
		return std::nullopt;
	}
}
```

- [ ] **Step 8: Build + run pour vérifier que les 2 tests PASSENT** (TDD green phase)

Run :
```
cmake --build build/vs2022-x64 --config Debug --target water_sampler_tests
ctest --test-dir build/vs2022-x64 -C Debug -R water_sampler_tests --output-on-failure
```
Expected: PASS, `[OK] 2 tests passed`.

- [ ] **Step 9: Commit**

```bash
git add src/client/world/water/WaterSampler.h src/client/world/water/WaterSampler.cpp src/client/world/water/tests/WaterSamplerTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.15): WaterSampler — lac PIP (Test_Lake_PointInside/Outside)

Première moitié du sampler géométrique. Point-in-polygon 2D dans XZ via
l'algorithme du nombre d'intersections impaires. Polygone CCW garanti
par M100.13 — pas de test de winding.

Tests: water_sampler_tests (2 PASS).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `WaterSampler` — rivière (projection segment)

**Files:**
- Modify: `src/client/world/water/WaterSampler.cpp`
- Modify: `src/client/world/water/tests/WaterSamplerTests.cpp`

- [ ] **Step 1: Ajouter 2 tests rivière (qui échoueront)**

Dans `WaterSamplerTests.cpp`, ajouter avant `int main()` :

```cpp
	using engine::world::water::RiverInstance;
	using engine::world::water::RiverNode;

	// Rivière en ligne droite de (0,5,0) à (20,5,0), largeur 4 m partout.
	WaterScene MakeStraightRiver()
	{
		WaterScene s;
		RiverInstance r;
		r.name = "straight_river";
		r.nodes = {
			RiverNode{ Vec3{  0, 5, 0 }, 4.0f, 1.0f },
			RiverNode{ Vec3{ 20, 5, 0 }, 4.0f, 1.0f },
		};
		s.rivers.push_back(std::move(r));
		return s;
	}

	void Test_River_ProjectionOnSegment()
	{
		WaterScene scene = MakeStraightRiver();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Pieds à (10, 3, 1) : projection sur (10, 5, 0), distance latérale = 1 m
		// < width/2 = 2 m → hit avec surfaceY=5, depth=2.
		auto hit = sampler.Sample(Vec3{ 10.0f, 3.0f, 1.0f });
		REQUIRE(hit.has_value());
		REQUIRE(std::fabs(hit->surfaceY - 5.0f) < 1e-5f);
		REQUIRE(std::fabs(hit->depthMeters - 2.0f) < 1e-5f);
	}

	void Test_River_PointBeyondWidth_Misses()
	{
		WaterScene scene = MakeStraightRiver();
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Distance latérale = 3 m > width/2 = 2 m → miss.
		auto hit = sampler.Sample(Vec3{ 10.0f, 3.0f, 3.0f });
		REQUIRE(!hit.has_value());
	}
```

Étendre `int main()` :

```cpp
int main()
{
	Test_Lake_PointInside_ReturnsSurfaceY();
	Test_Lake_PointOutside_ReturnsNullopt();
	Test_River_ProjectionOnSegment();
	Test_River_PointBeyondWidth_Misses();
	if (g_failed == 0) std::printf("[OK] 4 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Build + run pour vérifier les nouveaux tests ÉCHOUENT**

Run :
```
cmake --build build/vs2022-x64 --config Debug --target water_sampler_tests
ctest --test-dir build/vs2022-x64 -C Debug -R water_sampler_tests --output-on-failure
```
Expected: 2 PASS, 2 FAIL (les tests rivière échouent car aucune itération sur `m_scene->rivers`).

- [ ] **Step 3: Implémenter la projection sur segments rivière**

Dans `WaterSampler.cpp`, ajouter dans l'anonymous namespace avant `PointInPolygonXZ` :

```cpp
		// Projection orthogonale 2D (XZ) de `p` sur le segment [a, b].
		// Retourne `t` clampé dans [0, 1] et la distance latérale.
		struct SegmentProjection
		{
			float t = 0.0f;
			float distXZ = 0.0f;
		};

		SegmentProjection ProjectOnSegmentXZ(const engine::math::Vec3& p,
			const engine::math::Vec3& a, const engine::math::Vec3& b) noexcept
		{
			const float dx = b.x - a.x;
			const float dz = b.z - a.z;
			const float lenSq = dx * dx + dz * dz;
			if (lenSq < 1e-12f) {
				const float ddx = p.x - a.x;
				const float ddz = p.z - a.z;
				return { 0.0f, std::sqrt(ddx * ddx + ddz * ddz) };
			}
			float t = ((p.x - a.x) * dx + (p.z - a.z) * dz) / lenSq;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			const float projX = a.x + t * dx;
			const float projZ = a.z + t * dz;
			const float ddx = p.x - projX;
			const float ddz = p.z - projZ;
			return { t, std::sqrt(ddx * ddx + ddz * ddz) };
		}
```

Ajouter `#include <cmath>` en tête du `.cpp` si pas déjà présent.

Étendre `Sample()` — après la boucle lacs, **avant** le `return std::nullopt;` final :

```cpp
		for (const auto& river : m_scene->rivers)
		{
			for (size_t i = 0; i + 1 < river.nodes.size(); ++i)
			{
				const auto& na = river.nodes[i];
				const auto& nb = river.nodes[i + 1];
				const auto proj = ProjectOnSegmentXZ(worldPos, na.position, nb.position);
				const float widthLocal = na.widthMeters * (1.0f - proj.t) + nb.widthMeters * proj.t;
				if (proj.distXZ <= widthLocal * 0.5f)
				{
					const float surfaceY = na.position.y * (1.0f - proj.t) + nb.position.y * proj.t;
					const float depth = surfaceY - worldPos.y;
					if (depth > 0.0f)
					{
						return WaterSample{ surfaceY, depth };
					}
				}
			}
		}
```

- [ ] **Step 4: Build + run pour vérifier que les 4 tests PASSENT**

Run :
```
cmake --build build/vs2022-x64 --config Debug --target water_sampler_tests
ctest --test-dir build/vs2022-x64 -C Debug -R water_sampler_tests --output-on-failure
```
Expected: PASS, `[OK] 4 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/client/world/water/WaterSampler.cpp src/client/world/water/tests/WaterSamplerTests.cpp
git commit -m "$(cat <<'EOF'
feat(M100.15): WaterSampler — rivière (projection segment)

Projection orthogonale 2D (XZ) sur chaque segment de la spline rivière,
hit si distance latérale <= widthLocal/2. Interpolation linéaire de la
largeur et de la surfaceY entre les deux nodes.

Tests: water_sampler_tests (4 PASS).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `WaterSampler` — multi-overlap et filtres

**Files:**
- Modify: `src/client/world/water/WaterSampler.cpp`
- Modify: `src/client/world/water/tests/WaterSamplerTests.cpp`

- [ ] **Step 1: Ajouter 2 tests (multi-overlap + pieds au-dessus de la surface)**

Dans `WaterSamplerTests.cpp`, ajouter :

```cpp
	void Test_MultiOverlap_ReturnsDeepest()
	{
		// Lac à Y=10 (depth=5 si pieds à 5) ET rivière à Y=7 (depth=2 si pieds à 5)
		// au même point monde. Doit retourner le hit le plus profond (lac).
		WaterScene s;
		LakeInstance lake;
		lake.polygon = { {0, 10, 0}, {10, 10, 0}, {10, 10, 10}, {0, 10, 10} };
		lake.waterLevelY = 10.0f;
		s.lakes.push_back(lake);

		RiverInstance r;
		r.nodes = {
			RiverNode{ Vec3{  0, 7, 5 }, 6.0f, 1.0f },
			RiverNode{ Vec3{ 20, 7, 5 }, 6.0f, 1.0f },
		};
		s.rivers.push_back(r);

		WaterSampler sampler;
		REQUIRE(sampler.Init(s));

		// Pieds à (5, 5, 5). Lac : depth=5. Rivière : depth=2.
		auto hit = sampler.Sample(Vec3{ 5.0f, 5.0f, 5.0f });
		REQUIRE(hit.has_value());
		REQUIRE(std::fabs(hit->surfaceY - 10.0f) < 1e-5f);  // surface lac
		REQUIRE(std::fabs(hit->depthMeters - 5.0f) < 1e-5f); // profondeur lac
	}

	void Test_FeetAboveSurface_ReturnsNullopt()
	{
		WaterScene scene = MakeSquareLake();  // surface Y=10
		WaterSampler sampler;
		REQUIRE(sampler.Init(scene));

		// Pieds à Y=12, au-dessus de la surface (saut/vol au-dessus du lac).
		auto hit = sampler.Sample(Vec3{ 5.0f, 12.0f, 5.0f });
		REQUIRE(!hit.has_value());
	}
```

Étendre `int main()` :

```cpp
int main()
{
	Test_Lake_PointInside_ReturnsSurfaceY();
	Test_Lake_PointOutside_ReturnsNullopt();
	Test_River_ProjectionOnSegment();
	Test_River_PointBeyondWidth_Misses();
	Test_MultiOverlap_ReturnsDeepest();
	Test_FeetAboveSurface_ReturnsNullopt();
	if (g_failed == 0) std::printf("[OK] 6 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Build + run pour voir l'état**

Run :
```
cmake --build build/vs2022-x64 --config Debug --target water_sampler_tests
ctest --test-dir build/vs2022-x64 -C Debug -R water_sampler_tests --output-on-failure
```
Expected: `Test_FeetAboveSurface_ReturnsNullopt` passe déjà (le filtre `depth > 0` existe). `Test_MultiOverlap_ReturnsDeepest` ÉCHOUE (le code retourne dès le premier hit, sans comparer).

- [ ] **Step 3: Modifier `Sample()` pour le tri par profondeur maximale**

Refactor de `WaterSampler::Sample` :

```cpp
	std::optional<WaterSample> WaterSampler::Sample(engine::math::Vec3 worldPos) const noexcept
	{
		if (!m_scene) return std::nullopt;

		std::optional<WaterSample> best;

		auto consider = [&](float surfaceY) {
			const float depth = surfaceY - worldPos.y;
			if (depth <= 0.0f) return;
			if (!best || depth > best->depthMeters) {
				best = WaterSample{ surfaceY, depth };
			}
		};

		for (const auto& lake : m_scene->lakes)
		{
			if (PointInPolygonXZ(worldPos.x, worldPos.z, lake.polygon))
			{
				consider(lake.waterLevelY);
			}
		}

		for (const auto& river : m_scene->rivers)
		{
			for (size_t i = 0; i + 1 < river.nodes.size(); ++i)
			{
				const auto& na = river.nodes[i];
				const auto& nb = river.nodes[i + 1];
				const auto proj = ProjectOnSegmentXZ(worldPos, na.position, nb.position);
				const float widthLocal = na.widthMeters * (1.0f - proj.t) + nb.widthMeters * proj.t;
				if (proj.distXZ <= widthLocal * 0.5f)
				{
					const float surfaceY = na.position.y * (1.0f - proj.t) + nb.position.y * proj.t;
					consider(surfaceY);
				}
			}
		}

		return best;
	}
```

- [ ] **Step 4: Build + run pour vérifier 6 PASS**

Run :
```
cmake --build build/vs2022-x64 --config Debug --target water_sampler_tests
ctest --test-dir build/vs2022-x64 -C Debug -R water_sampler_tests --output-on-failure
```
Expected: `[OK] 6 tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/client/world/water/WaterSampler.cpp src/client/world/water/tests/WaterSamplerTests.cpp
git commit -m "$(cat <<'EOF'
feat(M100.15): WaterSampler — multi-overlap (deepest wins) + filtre

Refactor de Sample() pour itérer sur tous les hits potentiels et retenir
celui de profondeur maximale. Confluence rivière → lac à l'embouchure :
le lac (plus profond) gagne, pas de saut artificiel.

Tests: water_sampler_tests (6 PASS).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `SurfaceQueryService` — override `Shallow/DeepWater`

**Files:**
- Modify: `src/client/world/surface/SurfaceQueryService.h`
- Modify: `src/client/world/surface/SurfaceQueryService.cpp`
- Create: `src/client/world/surface/tests/WaterHookTests.cpp`
- Modify: `CMakeLists.txt` (ajout bloc test après `water_sampler_tests`)

- [ ] **Step 1: Étendre l'API de `SurfaceQueryService.h`**

Dans [SurfaceQueryService.h](../../../src/client/world/surface/SurfaceQueryService.h), ajouter un forward declaration en haut du namespace :

```cpp
namespace engine::world::water { class WaterSampler; }
```

Et ajouter dans la classe (après `Query()`) :

```cpp
        /// Branche optionnellement un `WaterSampler` (M100.15) pour overrider
        /// le résultat splat-map vers `ShallowWater` (depth < 1 m) ou
        /// `DeepWater` (depth >= 1 m) si le point est dans un volume d'eau.
        /// Passer `nullptr` désactive l'override (comportement M100.11 pur).
        void SetWaterSampler(const engine::world::water::WaterSampler* sampler) noexcept;
```

Et dans la section private :

```cpp
        const engine::world::water::WaterSampler*        m_waterSampler = nullptr;
```

- [ ] **Step 2a: Ajouter un helper privé dans `SurfaceQueryService.h`**

Le `Query()` actuel a **plusieurs** `return` (l.30, 40, 62, 85). Pour appliquer l'override eau de façon uniforme sans rewriter toute la fonction, on introduit un helper privé. Dans la section `private:` de la classe `SurfaceQueryService`, juste après le membre `m_waterSampler`, ajouter :

```cpp
        /// M100.15 — applique l'override Shallow/DeepWater au résultat splat
        /// si un sampler est branché ET que le point est dans l'eau. Sinon
        /// retourne `r` tel quel.
        SurfaceQueryResult ApplyWaterOverride(SurfaceQueryResult r,
                                              engine::math::Vec3 worldPos) const noexcept;
```

- [ ] **Step 2b: Implémenter le setter + le helper dans `SurfaceQueryService.cpp`**

Dans [SurfaceQueryService.cpp](../../../src/client/world/surface/SurfaceQueryService.cpp), ajouter en tête du fichier :

```cpp
#include "src/client/world/water/WaterSampler.h"
```

Ajouter, après la fonction `Init` (vers l.26, avant `Query`) :

```cpp
    void SurfaceQueryService::SetWaterSampler(
        const engine::world::water::WaterSampler* sampler) noexcept
    {
        m_waterSampler = sampler;
    }

    SurfaceQueryResult SurfaceQueryService::ApplyWaterOverride(
        SurfaceQueryResult r, engine::math::Vec3 worldPos) const noexcept
    {
        if (m_waterSampler)
        {
            if (auto sample = m_waterSampler->Sample(worldPos))
            {
                r.base = (sample->depthMeters >= 1.0f)
                    ? SurfaceType::DeepWater
                    : SurfaceType::ShallowWater;
            }
        }
        return r;
    }
```

- [ ] **Step 2c: Modifier les `return` de `Query()` (l.62 et l.85)**

Dans `Query()`, deux substitutions :

Ligne 62 actuelle (splat absent → fallback) :

```cpp
            return fallback;
```

Remplacer par :

```cpp
            return ApplyWaterOverride(fallback, worldPos);
```

Ligne 85 actuelle (chemin normal) :

```cpp
        return r;
```

Remplacer par :

```cpp
        return ApplyWaterOverride(r, worldPos);
```

> **Ne pas toucher** les `return fallback` des lignes 30 (null-guard `m_table`/`m_cache`/`m_cfg`/`m_palette`) et 40 (chunk bounds invalides). Dans ces cas, le service est inutilisable ou la requête est dégénérée — la sortie n'a pas vocation à exposer un état eau. Cohérence garantie : le harness de test appelle `Init()` avec des objets valides (m_table/m_cache/m_cfg/m_palette non null), donc on ne hit jamais l.30 ; le splat absent renvoie via l.62 où l'override fire.

- [ ] **Step 3: Build engine_core pour vérifier que tout compile**

Run: `cmake --build build/vs2022-x64 --config Debug --target engine_core`
Expected: build success.

- [ ] **Step 4: Créer le fichier de tests `WaterHookTests.cpp`**

Note importante : ce test n'a pas besoin d'une `StreamCache` ou d'une `SplatMap` réelles. La spec dit « si pas de sampler → fallback splat-map M100.11 ». On teste uniquement le **branchement water**, en réutilisant la sortie splat actuelle quelle qu'elle soit (`base.base` initialisée à `Dirt` par défaut).

```cpp
// src/client/world/surface/tests/WaterHookTests.cpp
#include "src/client/world/surface/SurfaceQueryService.h"
#include "src/client/world/surface/SurfaceTable.h"
#include "src/client/world/surface/SurfaceType.h"
#include "src/client/world/water/WaterSampler.h"
#include "src/client/world/water/WaterSurfaces.h"
#include "src/client/world/StreamCache.h"
#include "src/client/world/terrain/LayerPalette.h"
#include "src/shared/core/Config.h"

#include <cstdio>
#include <cmath>

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
	using engine::world::water::LakeInstance;
	using engine::world::water::WaterSampler;
	using engine::world::water::WaterScene;
	using engine::world::surface::SurfaceQueryService;
	using engine::world::surface::SurfaceTable;
	using engine::world::surface::SurfaceType;

	// Lac 10×10 à Y=10. Pieds à Y=variable selon test.
	WaterScene MakeSquareLake(float surfaceY)
	{
		WaterScene s;
		LakeInstance lake;
		lake.polygon = { {0, surfaceY, 0}, {10, surfaceY, 0}, {10, surfaceY, 10}, {0, surfaceY, 10} };
		lake.waterLevelY = surfaceY;
		s.lakes.push_back(std::move(lake));
		return s;
	}

	struct Harness
	{
		WaterScene scene;
		WaterSampler sampler;
		SurfaceTable table;
		engine::world::StreamCache cache;
		engine::core::Config cfg;
		engine::world::terrain::LayerPalette palette;
		SurfaceQueryService service;

		void Setup(float surfaceY)
		{
			scene = MakeSquareLake(surfaceY);
			sampler.Init(scene);
			service.Init(table, cache, cfg, palette);
			service.SetWaterSampler(&sampler);
		}
	};

	void Test_DepthBelow1m_IsShallowWater()
	{
		Harness h;
		h.Setup(10.0f);

		// Pieds à Y=9.7 → depth=0.3 → ShallowWater.
		auto r = h.service.Query(Vec3{ 5.0f, 9.7f, 5.0f });
		REQUIRE(r.base == SurfaceType::ShallowWater);
	}

	void Test_DepthAtOrAbove1m_IsDeepWater()
	{
		Harness h;
		h.Setup(10.0f);

		// Pieds à Y=9.0 → depth=1.0 (frontière inclusive) → DeepWater.
		auto r1 = h.service.Query(Vec3{ 5.0f, 9.0f, 5.0f });
		REQUIRE(r1.base == SurfaceType::DeepWater);

		// Pieds à Y=8.8 → depth=1.2 → DeepWater.
		auto r2 = h.service.Query(Vec3{ 5.0f, 8.8f, 5.0f });
		REQUIRE(r2.base == SurfaceType::DeepWater);
	}

	void Test_NoSampler_FallsBackToSplat()
	{
		// Service sans SetWaterSampler : pas d'override. La sortie ne dépend
		// que de la splat (fallback Dirt avec setup minimal). On vérifie
		// l'absence de Shallow/Deep, ce qui prouve le non-branchement.
		Harness h;
		h.Setup(10.0f);
		h.service.SetWaterSampler(nullptr);

		auto r = h.service.Query(Vec3{ 5.0f, 9.0f, 5.0f });
		REQUIRE(r.base != SurfaceType::ShallowWater);
		REQUIRE(r.base != SurfaceType::DeepWater);
	}
}

int main()
{
	Test_DepthBelow1m_IsShallowWater();
	Test_DepthAtOrAbove1m_IsDeepWater();
	Test_NoSampler_FallsBackToSplat();
	if (g_failed == 0) std::printf("[OK] 3 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 5: Ajouter le bloc test dans `CMakeLists.txt`**

Juste après le bloc `water_sampler_tests` ajouté en Task 2, insérer :

```cmake
# M100.15 — Tests intégration SurfaceQueryService ↔ WaterSampler.
if(WIN32)
  add_executable(water_hook_tests src/client/world/surface/tests/WaterHookTests.cpp)
  target_include_directories(water_hook_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(water_hook_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(water_hook_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME water_hook_tests COMMAND water_hook_tests)
endif()
```

- [ ] **Step 6: Build + run pour vérifier 3 PASS**

Run :
```
cmake --build build/vs2022-x64 --config Debug --target water_hook_tests
ctest --test-dir build/vs2022-x64 -C Debug -R water_hook_tests --output-on-failure
```
Expected: `[OK] 3 tests passed`.

> **Si `Test_NoSampler_FallsBackToSplat` échoue** parce que le `Query()` sans cache valide renvoie un `result.base` qui se trouve être `ShallowWater` ou `DeepWater` (improbable mais possible) : c'est qu'il faut adapter le harness pour forcer un fallback `Dirt` clair. Lire `SurfaceQueryService.cpp` ligne 29 (`SurfaceQueryResult fallback{ SurfaceType::Dirt, {} };`) confirme que sans `m_table`/`m_cache`/`m_cfg`/`m_palette` valides, le fallback est `Dirt`. Si nécessaire, modifier le test pour vérifier `r.base == SurfaceType::Dirt`.

- [ ] **Step 7: Commit**

```bash
git add src/client/world/surface/SurfaceQueryService.h src/client/world/surface/SurfaceQueryService.cpp src/client/world/surface/tests/WaterHookTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.15): SurfaceQueryService override Shallow/DeepWater via sampler

Ajout SetWaterSampler() + override final dans Query() : si le point est
dans un volume d'eau et que depth >= 1.0 m → DeepWater, sinon ShallowWater.
Comportement inchangé sans sampler (rétrocompat M100.11).

Tests: water_hook_tests (3 PASS).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: `WorldColliderImpl` — première implémentation `IWorldCollider`

**Files:**
- Create: `src/client/gameplay/WorldColliderImpl.h`
- Create: `src/client/gameplay/WorldColliderImpl.cpp`
- Modify: `CMakeLists.txt` (ajout source `engine_core` ligne ~399-403)

> **Note** : pas de tests dédiés `WorldColliderImpl` ici — la validation se fait via les tests `cc_water_hysteresis_tests` de la Task 7 qui consomment le collider. Cette task crée juste l'infrastructure.

- [ ] **Step 1: Créer le header `WorldColliderImpl.h`**

```cpp
// src/client/gameplay/WorldColliderImpl.h
#pragma once

#include "src/client/gameplay/CharacterController.h"

namespace engine::world::water { class WaterSampler; }

namespace engine::gameplay
{
	/// Première implémentation concrète d'`IWorldCollider`. Pour M100.15 :
	///   - `QueryWater` consulte un `WaterSampler` injecté (nullable).
	///   - `SweepCapsule` est un stub MVP qui retourne `hit=false`. La version
	///     complète (heightmap + collision proxies M100.12) viendra avec la
	///     chaîne CHAR-MODEL.
	class WorldColliderImpl : public IWorldCollider
	{
	public:
		/// Branche un sampler. `nullptr` = pas d'eau (QueryWater retourne false).
		void SetWaterSampler(const engine::world::water::WaterSampler* sampler) noexcept;

		bool SweepCapsule(const Capsule& capsule,
			const engine::math::Vec3& startCenter,
			const engine::math::Vec3& endCenter,
			SweepHit& outHit) const override;

		bool QueryWater(const engine::math::Vec3& worldCenter,
			WaterQuery& out) const override;

	private:
		const engine::world::water::WaterSampler* m_waterSampler = nullptr;
	};
}
```

- [ ] **Step 2: Créer l'implémentation `WorldColliderImpl.cpp`**

```cpp
// src/client/gameplay/WorldColliderImpl.cpp
#include "src/client/gameplay/WorldColliderImpl.h"
#include "src/client/world/water/WaterSampler.h"

namespace engine::gameplay
{
	void WorldColliderImpl::SetWaterSampler(
		const engine::world::water::WaterSampler* sampler) noexcept
	{
		m_waterSampler = sampler;
	}

	bool WorldColliderImpl::SweepCapsule(const Capsule& /*capsule*/,
		const engine::math::Vec3& /*startCenter*/,
		const engine::math::Vec3& /*endCenter*/,
		SweepHit& outHit) const
	{
		// Stub MVP : aucun obstacle détecté. La version complète viendra
		// avec la chaîne CHAR-MODEL (heightmap + collision proxies M100.12).
		outHit = SweepHit{};
		return false;
	}

	bool WorldColliderImpl::QueryWater(const engine::math::Vec3& worldCenter,
		WaterQuery& out) const
	{
		out = WaterQuery{};
		if (!m_waterSampler) return false;

		// Le sampler attend la position des PIEDS. Le `CharacterController`
		// nous passe la position du CENTRE de la capsule. On descend d'une
		// demi-hauteur de capsule en supposant pieds = center - height/2.
		// Comme le collider connaît la capsule via Update(), mais ici on
		// reçoit juste worldCenter, on suppose une capsule standard 1.8 m :
		// pieds = center.y - 0.9 m.
		// NOTE : si la hauteur capsule devient configurable côté Engine,
		// passer la valeur via SetCapsuleHeight() — pas nécessaire pour M100.15.
		constexpr float kAssumedHalfHeight = 0.9f;
		const engine::math::Vec3 feet{
			worldCenter.x, worldCenter.y - kAssumedHalfHeight, worldCenter.z
		};

		auto sample = m_waterSampler->Sample(feet);
		if (!sample) return false;

		out.inWater = true;
		out.surfaceY = sample->surfaceY;
		out.depth = sample->depthMeters;
		return true;
	}
}
```

- [ ] **Step 3: Ajouter `WorldColliderImpl.cpp` à `engine_core`**

Dans [CMakeLists.txt](../../../CMakeLists.txt), juste après `src/client/gameplay/CharacterController.cpp` (l.399), insérer :

```cmake
  src/client/gameplay/WorldColliderImpl.cpp
```

- [ ] **Step 4: Build pour vérifier compilation**

Run: `cmake --build build/vs2022-x64 --config Debug --target engine_core`
Expected: build success.

- [ ] **Step 5: Vérifier que le serveur (Linux) NE compile PAS ces fichiers**

Run: `grep -n "WaterSampler.cpp\|WorldColliderImpl.cpp" CMakeLists.txt`
Expected: lignes uniquement dans la section `engine_core` (cible client). Ne PAS apparaître dans `engine_core_server` ni dans des cibles serveur.

- [ ] **Step 6: Commit**

```bash
git add src/client/gameplay/WorldColliderImpl.h src/client/gameplay/WorldColliderImpl.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.15): WorldColliderImpl — première impl IWorldCollider

Première classe concrète d'IWorldCollider. QueryWater() consulte un
WaterSampler injecté, ajustant le worldCenter passé par CharacterController
en position pieds (center.y - 0.9 m capsule standard).

SweepCapsule reste un stub MVP (hors scope M100.15 — vient avec CHAR-MODEL).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: `CharacterController` — hystérésis + callbacks

**Files:**
- Modify: `src/client/gameplay/CharacterController.h`
- Modify: `src/client/gameplay/CharacterController.cpp`
- Create: `src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp`
- Modify: `CMakeLists.txt` (ajout bloc test après `water_hook_tests`)

- [ ] **Step 1: Étendre l'API de `CharacterController.h`**

Dans [CharacterController.h](../../../src/client/gameplay/CharacterController.h), ajouter `#include <functional>` au top des includes.

Dans la classe, après la méthode `GetCapsule()` (vers l.121), ajouter :

```cpp
		/// Callback à appeler quand le CC bascule en `MovementMode::Water`.
		/// Consommé par l'Engine pour jouer `splash_water_enter`. Pas de
		/// dépendance audio dans le CC.
		using WaterTransitionCallback = std::function<void()>;
		void SetWaterTransitionCallbacks(
			WaterTransitionCallback onEnter,
			WaterTransitionCallback onExit);
```

Dans la section private (après `m_timeSinceJumpPressedSec`), ajouter :

```cpp
		// M100.15 — callbacks audio transitions eau (consommés par l'Engine).
		WaterTransitionCallback m_onEnterWater;
		WaterTransitionCallback m_onExitWater;
```

- [ ] **Step 2a: Ajouter le setter `SetWaterTransitionCallbacks` dans `CharacterController.cpp`**

Dans [CharacterController.cpp](../../../src/client/gameplay/CharacterController.cpp), ajouter en fin de fichier (avant la fermeture du namespace `engine::gameplay`) :

```cpp
	void CharacterController::SetWaterTransitionCallbacks(
		WaterTransitionCallback onEnter,
		WaterTransitionCallback onExit)
	{
		m_onEnterWater = std::move(onEnter);
		m_onExitWater = std::move(onExit);
	}
```

- [ ] **Step 2b: Édit chirurgical n°1 — calcul `desiredMode` avec hystérésis**

Dans `CharacterController::Update`, lignes 83-84 actuelles :

```cpp
		const MovementMode desiredMode =
			isFlying ? MovementMode::Fly : (inWater ? MovementMode::Water : (m_isGrounded ? MovementMode::Ground : MovementMode::Air));
```

Remplacer par :

```cpp
		// M100.15 — hystérésis : entrée swim à >= 1.0 m, sortie à < 0.7 m.
		// L'état précédent est lu depuis m_mode (pas de membre dédié).
		constexpr float kSwimEnterDepth = 1.0f;
		constexpr float kSwimExitDepth  = 0.7f;
		const bool wasInWaterMode = (m_mode == MovementMode::Water);
		const bool depthSaysSwim =
			inWater && (wq.depth >= kSwimEnterDepth ||
			            (wasInWaterMode && wq.depth >= kSwimExitDepth));

		const MovementMode desiredMode =
			isFlying ? MovementMode::Fly
			         : (depthSaysSwim ? MovementMode::Water
			         : (m_isGrounded ? MovementMode::Ground : MovementMode::Air));
```

- [ ] **Step 2c: Édit chirurgical n°2 — émission callbacks dans le bloc transition**

Lignes 86-97 actuelles :

```cpp
		if (desiredMode != m_mode)
		{
			m_mode = desiredMode;
			if (m_mode == MovementMode::Water)
				LOG_INFO(Core, "[CharacterController] Mode -> Water (depth={})", wq.depth);
			else if (m_mode == MovementMode::Fly)
				LOG_INFO(Core, "[CharacterController] Mode -> Fly (staminaSec={})", m_staminaSec);
			else if (m_mode == MovementMode::Ground)
				LOG_INFO(Core, "[CharacterController] Mode -> Ground");
			else
				LOG_INFO(Core, "[CharacterController] Mode -> Air");
		}
```

Remplacer par :

```cpp
		if (desiredMode != m_mode)
		{
			// M100.15 — callbacks audio (Engine consomme) AVANT mutation de m_mode.
			if (desiredMode == MovementMode::Water && m_onEnterWater)
				m_onEnterWater();
			else if (m_mode == MovementMode::Water
			         && desiredMode != MovementMode::Water
			         && m_onExitWater)
				m_onExitWater();

			m_mode = desiredMode;
			if (m_mode == MovementMode::Water)
				LOG_INFO(Core, "[CharacterController] Mode -> Water (depth={})", wq.depth);
			else if (m_mode == MovementMode::Fly)
				LOG_INFO(Core, "[CharacterController] Mode -> Fly (staminaSec={})", m_staminaSec);
			else if (m_mode == MovementMode::Ground)
				LOG_INFO(Core, "[CharacterController] Mode -> Ground");
			else
				LOG_INFO(Core, "[CharacterController] Mode -> Air");
		}
```

> **Important — invariant à préserver** : tous les autres usages de `inWater` plus bas dans `Update()` (gravité réduite, surface breaching, jump gates, grounding gates — lignes ~100, 139, 170, 173, 201, 222, 266, 280, 303, 314) doivent rester inchangés. La nouvelle variable `depthSaysSwim` n'est utilisée que pour le calcul de `desiredMode`. La flottabilité physique reste autoritaire sur `WaterQuery.inWater` (cohérent avec l'audit du 6 mai).

- [ ] **Step 3: Build pour vérifier compilation**

Run: `cmake --build build/vs2022-x64 --config Debug --target engine_core`
Expected: build success, pas de warning W4.

- [ ] **Step 4: Créer le fichier de tests d'hystérésis**

```cpp
// src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp
#include "src/client/gameplay/CharacterController.h"
#include "src/client/gameplay/WorldColliderImpl.h"
#include "src/client/world/water/WaterSampler.h"
#include "src/client/world/water/WaterSurfaces.h"

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
	using engine::gameplay::CharacterController;
	using engine::gameplay::MoveInput;
	using engine::gameplay::WorldColliderImpl;
	using engine::world::water::LakeInstance;
	using engine::world::water::WaterSampler;
	using engine::world::water::WaterScene;

	// Mini-IWorldCollider qui retourne directement la profondeur configurée
	// (évite la conversion center→feet, donne contrôle parfait sur depth).
	class FakeCollider : public engine::gameplay::IWorldCollider
	{
	public:
		float currentDepth = 0.0f;  // <=0 → out of water

		bool SweepCapsule(const Capsule&, const engine::math::Vec3&,
			const engine::math::Vec3&, SweepHit& outHit) const override
		{
			outHit = SweepHit{};
			return false;
		}

		bool QueryWater(const engine::math::Vec3&, WaterQuery& out) const override
		{
			if (currentDepth > 0.0f)
			{
				out.inWater = true;
				out.surfaceY = 10.0f;
				out.depth = currentDepth;
				return true;
			}
			out = WaterQuery{};
			return false;
		}
	};

	// Helper : avancer une frame avec input neutre.
	void StepFrame(CharacterController& cc, const FakeCollider& world)
	{
		MoveInput in{};
		cc.Update(1.0f / 60.0f, in, world);
	}

	void Test_EntersWaterAt1m()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		world.currentDepth = 0.5f;  // shallow → ne déclenche pas Water
		StepFrame(cc, world);
		// (on ne peut pas inspecter m_mode directement, mais on vérifiera via
		//  les callbacks au test suivant)

		// La condition d'entrée est depth >= 1.0 — par callback.
		bool entered = false;
		cc.SetWaterTransitionCallbacks(
			[&entered]() { entered = true; },
			nullptr);

		world.currentDepth = 1.1f;
		StepFrame(cc, world);
		REQUIRE(entered);
	}

	void Test_ExitsWaterAt0p7m()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		bool entered = false, exited = false;
		cc.SetWaterTransitionCallbacks(
			[&entered]() { entered = true; },
			[&exited]()  { exited  = true; });

		world.currentDepth = 1.2f;
		StepFrame(cc, world);
		REQUIRE(entered);

		// Profondeur descend à 0.8 → reste en Water (au-dessus du seuil de
		// sortie 0.7).
		world.currentDepth = 0.8f;
		StepFrame(cc, world);
		REQUIRE(!exited);

		// Profondeur descend à 0.6 → sort.
		world.currentDepth = 0.6f;
		StepFrame(cc, world);
		REQUIRE(exited);
	}

	void Test_HysteresisDoesNotFlicker()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		int enterCount = 0, exitCount = 0;
		cc.SetWaterTransitionCallbacks(
			[&enterCount]() { ++enterCount; },
			[&exitCount]()  { ++exitCount;  });

		// Démarre hors eau.
		world.currentDepth = 0.0f;
		StepFrame(cc, world);

		// Oscille 10 frames entre 0.95 et 1.05 (autour du seuil 1.0).
		for (int i = 0; i < 10; ++i)
		{
			world.currentDepth = (i % 2 == 0) ? 1.05f : 0.95f;
			StepFrame(cc, world);
		}
		// Une seule entrée doit être déclenchée (au premier passage >= 1.0),
		// puis 0.95 (depth >= 0.7) reste en Water, 1.05 reste en Water.
		REQUIRE(enterCount == 1);
		REQUIRE(exitCount == 0);
	}

	void Test_OnEnterCallbackFiresOnce()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		int enterCount = 0;
		cc.SetWaterTransitionCallbacks([&enterCount]() { ++enterCount; }, nullptr);

		world.currentDepth = 1.2f;
		StepFrame(cc, world);
		StepFrame(cc, world);  // 2e frame en Water → pas de 2e enter.
		StepFrame(cc, world);

		REQUIRE(enterCount == 1);
	}

	void Test_OnExitCallbackFiresOnce()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		int exitCount = 0;
		cc.SetWaterTransitionCallbacks(nullptr, [&exitCount]() { ++exitCount; });

		world.currentDepth = 1.2f;
		StepFrame(cc, world);  // entrée

		world.currentDepth = 0.0f;
		StepFrame(cc, world);  // sortie
		StepFrame(cc, world);  // 2e frame hors eau → pas de 2e exit.

		REQUIRE(exitCount == 1);
	}

	void Test_NoCallbacksSetIsNoCrash()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;
		// Pas de SetWaterTransitionCallbacks() → callbacks restent vides.

		world.currentDepth = 1.2f;
		StepFrame(cc, world);  // doit transitionner sans crash
		world.currentDepth = 0.0f;
		StepFrame(cc, world);  // idem

		// Si on est arrivé ici sans crash, c'est passé.
		REQUIRE(true);
	}
}

int main()
{
	Test_EntersWaterAt1m();
	Test_ExitsWaterAt0p7m();
	Test_HysteresisDoesNotFlicker();
	Test_OnEnterCallbackFiresOnce();
	Test_OnExitCallbackFiresOnce();
	Test_NoCallbacksSetIsNoCrash();
	if (g_failed == 0) std::printf("[OK] 6 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 5: Ajouter le bloc test dans `CMakeLists.txt`**

Juste après le bloc `water_hook_tests` (ajouté en Task 5), insérer :

```cmake
# M100.15 — Tests CharacterController hystérésis eau + callbacks.
if(WIN32)
  add_executable(cc_water_hysteresis_tests
    src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp)
  target_include_directories(cc_water_hysteresis_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(cc_water_hysteresis_tests PRIVATE engine_core)
  if(MSVC)
    target_compile_options(cc_water_hysteresis_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
  endif()
  add_test(NAME cc_water_hysteresis_tests COMMAND cc_water_hysteresis_tests)
endif()
```

- [ ] **Step 6: Build + run pour vérifier 6 PASS**

Run :
```
cmake --build build/vs2022-x64 --config Debug --target cc_water_hysteresis_tests
ctest --test-dir build/vs2022-x64 -C Debug -R cc_water_hysteresis_tests --output-on-failure
```
Expected: `[OK] 6 tests passed`.

- [ ] **Step 7: Commit**

```bash
git add src/client/gameplay/CharacterController.h src/client/gameplay/CharacterController.cpp src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(M100.15): CharacterController hystérésis 1.0/0.7 m + callbacks audio

Logique de mode enrichie : entrée swim à depth >= 1.0 m, sortie à
depth < 0.7 m (hystérésis bidirectionnelle, lecture du m_mode précédent).
Callbacks onEnterWater/onExitWater émis aux transitions ; Engine les
consomme pour splash_water_enter/exit.

La flottabilité reste pilotée par WaterQuery (inchangé).

Tests: cc_water_hysteresis_tests (6 PASS).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Vérification finale

**Files:** aucun changement, juste validation.

- [ ] **Step 1: Build complet `engine_core`**

Run: `cmake --build build/vs2022-x64 --config Debug --target engine_core`
Expected: build success.

- [ ] **Step 2: Run l'intégralité des 3 nouvelles suites de tests**

Run :
```
ctest --test-dir build/vs2022-x64 -C Debug -R "water_sampler_tests|water_hook_tests|cc_water_hysteresis_tests" --output-on-failure
```
Expected :
- `water_sampler_tests` : `[OK] 6 tests passed`
- `water_hook_tests` : `[OK] 3 tests passed`
- `cc_water_hysteresis_tests` : `[OK] 6 tests passed`

Total : **15 tests PASS**.

- [ ] **Step 3: Vérifier aucune régression sur les suites existantes liées**

Run :
```
ctest --test-dir build/vs2022-x64 -C Debug -R "water_surfaces_tests|water_mesh_builder_tests|surface_query_service_tests|client_prediction_surface_multiplier_tests" --output-on-failure
```
Expected: tous PASS (M100.11, M100.13 inchangés).

- [ ] **Step 4: Vérifier l'isolation serveur**

Run :
```
grep -rE "WaterSampler\.|WorldColliderImpl\." CMakeLists.txt | grep -iE "server|masterd|shardd"
```
Expected: **aucune sortie** (les deux nouveaux fichiers ne sont liés que dans la cible `engine_core` client).

- [ ] **Step 5: Vérifier critères d'acceptation ticket**

Cocher mentalement :
- [x] Depth 0.3 m → `ShallowWater` (test `Test_DepthBelow1m_IsShallowWater`)
- [x] Depth 1.2 m → `DeepWater` (test `Test_DepthAtOrAbove1m_IsDeepWater`)
- [x] CC bascule en `MovementMode::Water` à depth >= 1.0 m (test `Test_EntersWaterAt1m`)
- [x] Sortie swim à depth < 0.7 m (test `Test_ExitsWaterAt0p7m`)
- [x] Hystérésis (test `Test_HysteresisDoesNotFlicker`)
- [x] Callbacks (4 tests dédiés)
- [x] Sampling lac PIP + rivière projection (4 tests dédiés)
- [x] `src/masterd/` ne compile pas les fichiers M100.15 (Step 4)

- [ ] **Step 6: Mettre à jour `CODEBASE_MAP.md` (entrée brève)**

Lire `CODEBASE_MAP.md` autour du début pour identifier la convention de mention « Phase X / Wave Y ». Ajouter une ligne dans le bloc de header daté (et/ou dans la section appropriée) :

```
**M100.15 — Water Surface Hook** livré : WaterSampler (PIP lac + projection rivière, multi-overlap deepest wins) + WorldColliderImpl (première impl IWorldCollider, QueryWater câblé sampler, SweepCapsule stub) + SurfaceQueryService override Shallow/DeepWater + CharacterController hystérésis 1.0/0.7 m + callbacks onEnterWater/onExitWater (consommés par Engine pour splash audio). 15 tests CTest répartis sur water_sampler_tests / water_hook_tests / cc_water_hysteresis_tests. Aucun redéploiement serveur.
```

> Si l'entrée date du jour existe déjà, ajouter juste la mention M100.15 ; sinon créer une entrée datée `2026-05-12`.

- [ ] **Step 7: Commit final (CODEBASE_MAP)**

```bash
git add CODEBASE_MAP.md
git commit -m "$(cat <<'EOF'
docs(M100.15): mise à jour CODEBASE_MAP — Water Surface Hook livré

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Vérification git globale**

Run: `git log --oneline -10`
Expected: voir les 8 commits de la chaîne M100.15 (Task 1-7 + docs final) + le commit du spec déjà mergé en amont.

Run: `git status`
Expected: `nothing to commit, working tree clean`.

---

## Synthèse déploiement

> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur. Aucun opcode, aucune migration DB, aucun changement de format binaire. Les nouveaux fichiers `WaterSampler.cpp` et `WorldColliderImpl.cpp` sont compilés uniquement dans `engine_core` (cible client). Aucun changement à `engine_core_server` ni aux exécutables `lcdlln-master` / `lcdlln-shard`.
