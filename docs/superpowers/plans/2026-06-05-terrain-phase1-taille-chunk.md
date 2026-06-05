# Terrain Phase 1 — Réconcilier la taille de chunk (256 vs 500) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Router le chemin de rendu terrain du client de jeu sur la grille de chunks 256 m (au lieu de 500 m), pour qu'il charge et place correctement les `terrain.bin` authorés par l'éditeur (jointifs, sans trou de 244 m, alignés avec la heightmap).

**Architecture:** On introduit une constante terrain dédiée `kTerrainChunkSizeMeters = 256` et deux helpers purs (`WorldToTerrainChunkCoord`, `ComputeVisibleTerrainChunks`) miroir de la logique existante mais sur 256. On bascule le placement du renderer et le site Engine du chemin terrain sur ces helpers. La grille d'instances/zone/streaming reste sur `kChunkSize = 500` (usage légitime). Le `World` (grille 500) continue d'être passé à `RenderVisibleChunks` pour le ring LRU d'éviction uniquement — ring approximatif toléré et documenté (n'affecte jamais l'exactitude du rendu).

**Tech Stack:** C++17, Vulkan. Tests : exécutables C++ autonomes (macro `REQUIRE` maison + `main()` retournant le nombre d'échecs), enregistrés via `add_executable`/`add_test` dans `CMakeLists.txt`, exécutés par `ctest` en CI Linux. **Pas de build local** : compile + tests validés en CI ; effet rendu validé manuellement en éditeur/jeu.

**Référence spec :** `docs/superpowers/specs/2026-06-05-unification-terrain-design.md` (Phase 1).

**Portée :** client uniquement — **pas de redéploiement serveur** (le serveur n'utilise jamais `kChunkSize`/`GlobalChunkCoord` ; il partitionne sur `kZoneSize`/`kSpatialCellSizeMeters`). Aucune modification `frontFace`/`cullMode`/winding.

---

## Décomposition & périmètre

Ce plan couvre le **chemin de RENDU terrain** (l'objectif : afficher correctement les chunks). Sont **hors périmètre, différés** :
- `SurfaceQueryService` (splat sampling) : actuellement seulement test-wired / consommé par `ClientPrediction` (mort). Correctif `TerrainChunkBounds`/256 à traiter dans une PR séparée quand il sera câblé.
- `StreamingScheduler` priorité (ordonnancement, pas de chemin de rendu).
- Phases 2 (`IHeightField` + collision) et 3 (retrait legacy) : plans dédiés ultérieurs.

## File Structure

- **Modify** `src/client/world/WorldModel.h` — déclarer `kTerrainChunkSizeMeters` + les 2 helpers libres.
- **Modify** `src/client/world/WorldModel.cpp` — définir les 2 helpers.
- **Create** `src/client/world/tests/TerrainChunkGridTests.cpp` — tests unitaires des helpers + cohérence de la constante.
- **Modify** `CMakeLists.txt` — enregistrer l'exécutable de test (lié à `engine_core`, comme les autres tests `src/client/world/`).
- **Modify** `src/client/render/terrain_chunk/TerrainChunkRenderer.cpp` — placement origine sur 256 + commentaire ring.
- **Modify** `src/client/app/Engine.cpp` — site terrain : produire les coords visibles sur la grille 256.

---

### Task 1: Constante terrain 256 + helpers purs + tests

**Files:**
- Modify: `src/client/world/WorldModel.h`
- Modify: `src/client/world/WorldModel.cpp`
- Create: `src/client/world/tests/TerrainChunkGridTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Écrire le test qui échoue**

Create `src/client/world/tests/TerrainChunkGridTests.cpp` :

```cpp
// src/client/world/tests/TerrainChunkGridTests.cpp
#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/TerrainChunk.h"

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

	using engine::world::GlobalChunkCoord;
	using engine::world::WorldToTerrainChunkCoord;
	using engine::world::ComputeVisibleTerrainChunks;

	// La constante terrain vaut bien 256 et correspond au span réel du mesh.
	void Test_Constant_Is256()
	{
		REQUIRE(engine::world::kTerrainChunkSizeMeters == 256);
		REQUIRE(engine::world::kTerrainChunkSizeMeters
			== static_cast<int>((engine::world::terrain::kTerrainResolution - 1)
				* engine::world::terrain::kTerrainCellSizeMeters));
	}

	// floor(world/256) — origine, frontières, négatifs.
	void Test_WorldToTerrainChunkCoord()
	{
		REQUIRE(WorldToTerrainChunkCoord(0.0f, 0.0f).x == 0);
		REQUIRE(WorldToTerrainChunkCoord(0.0f, 0.0f).z == 0);
		REQUIRE(WorldToTerrainChunkCoord(255.9f, 0.0f).x == 0);
		REQUIRE(WorldToTerrainChunkCoord(256.0f, 0.0f).x == 1);
		REQUIRE(WorldToTerrainChunkCoord(0.0f, 768.0f).z == 3);
		REQUIRE(WorldToTerrainChunkCoord(-1.0f, 0.0f).x == -1);
		REQUIRE(WorldToTerrainChunkCoord(-256.0f, 0.0f).x == -1);
		REQUIRE(WorldToTerrainChunkCoord(-257.0f, 0.0f).x == -2);
	}

	// 7x7 = 49 coords, centrées sur le chunk de la caméra, span [-3,+3].
	void Test_ComputeVisibleTerrainChunks_7x7()
	{
		const auto chunks = ComputeVisibleTerrainChunks(300.0f, 300.0f); // centre = (1,1)
		REQUIRE(chunks.size() == 49u);

		bool hasCenter = false, hasCorner = false, hasOutOfRange = false;
		for (const auto& c : chunks)
		{
			if (c.x == 1 && c.z == 1) hasCenter = true;       // centre
			if (c.x == -2 && c.z == -2) hasCorner = true;     // 1-3 = -2 (coin)
			if (c.x > 4 || c.x < -2 || c.z > 4 || c.z < -2) hasOutOfRange = true;
		}
		REQUIRE(hasCenter);
		REQUIRE(hasCorner);
		REQUIRE(!hasOutOfRange);
	}

	// Non-régression du trou de 244 m : deux chunks voisins sont jointifs à 256 m.
	void Test_NeighbourChunks_AreContiguous()
	{
		// origine d'un chunk = coord * kTerrainChunkSizeMeters (cf. placement renderer)
		const int o0 = 0 * engine::world::kTerrainChunkSizeMeters;          // 0
		const int o1 = 1 * engine::world::kTerrainChunkSizeMeters;          // 256
		const int span = engine::world::kTerrainChunkSizeMeters;            // 256
		REQUIRE(o0 + span == o1); // bord droit du chunk 0 == bord gauche du chunk 1 -> pas de trou
	}
}

int main()
{
	Test_Constant_Is256();
	Test_WorldToTerrainChunkCoord();
	Test_ComputeVisibleTerrainChunks_7x7();
	Test_NeighbourChunks_AreContiguous();

	if (g_failed == 0)
		std::printf("[OK] TerrainChunkGridTests: tous les cas passent\n");
	return g_failed;
}
```

- [ ] **Step 2: Vérifier que le test échoue (symboles absents)**

Le test ne compile pas tant que `kTerrainChunkSizeMeters`, `WorldToTerrainChunkCoord`, `ComputeVisibleTerrainChunks` n'existent pas. Échec attendu en CI : symboles non déclarés. Pas de build local — ne pas committer avant le Step 4 (repo doit rester compilable).

- [ ] **Step 3: Déclarer la constante + les helpers dans `WorldModel.h`**

Dans `src/client/world/WorldModel.h`, juste après le bloc de constantes existant (après `kSpatialCellSizeMeters` et ses `static_assert`, ~ligne 27), ajouter :

```cpp
	// --- Grille TERRAIN (chantier C, Phase 1) ---------------------------------
	// Côté du mesh d'un chunk terrain, en mètres = (terrain::kTerrainResolution - 1)
	// * terrain::kTerrainCellSizeMeters = 256 m. C'est une grille DISTINCTE de
	// kChunkSize (500 m, instances/zone/streaming). Les terrain.bin sont indexés
	// et placés sur cette grille 256 (cohérent avec l'éditeur). Ne PAS lier par
	// static_assert à kZoneSize (10000 % 256 != 0) : le terrain est indexé en
	// coordonnées monde globales absolues, sans alignement zone. Voir TerrainChunk.h.
	constexpr int kTerrainChunkSizeMeters = 256;
```

Puis, à côté des autres free functions de `WorldModel.h` (ex. près de la déclaration de `WorldToGlobalChunkCoord` ou en fin de namespace `engine::world`, là où les fonctions libres sont déclarées), ajouter :

```cpp
	/// Convertit une position monde (x,z) en coord de chunk TERRAIN (grille
	/// kTerrainChunkSizeMeters = 256 m). Miroir de WorldToGlobalChunkCoord mais
	/// sur la grille terrain. Pur, sans état.
	GlobalChunkCoord WorldToTerrainChunkCoord(float worldX, float worldZ);

	/// Retourne les chunks TERRAIN (grille 256 m) du carré 7×7 autour de la
	/// position monde donnée. Miroir de World::GetActiveAndVisibleChunks mais sur
	/// la grille terrain — utilisé par le chemin de RENDU terrain (Phase 1).
	/// Pur, sans état : unit-testable.
	std::vector<GlobalChunkCoord> ComputeVisibleTerrainChunks(float worldX, float worldZ);
```

Vérifier par lecture que `<vector>` est déjà inclus dans `WorldModel.h` (le type `std::vector` est déjà utilisé par `GetActiveAndVisibleChunks`/`GetPendingChunkRequests`). Sinon, ajouter `#include <vector>`.

- [ ] **Step 4: Définir les helpers dans `WorldModel.cpp`**

Dans `src/client/world/WorldModel.cpp`, ajouter (après la définition de `WorldToGlobalChunkCoord`, et en réutilisant le `kVisibleRadius` du namespace anonyme du fichier — vérifier par lecture qu'il vaut bien 3) :

```cpp
GlobalChunkCoord WorldToTerrainChunkCoord(float worldX, float worldZ)
{
	const int32_t cx = static_cast<int32_t>(std::floor(worldX / static_cast<float>(kTerrainChunkSizeMeters)));
	const int32_t cz = static_cast<int32_t>(std::floor(worldZ / static_cast<float>(kTerrainChunkSizeMeters)));
	return GlobalChunkCoord{ cx, cz };
}

std::vector<GlobalChunkCoord> ComputeVisibleTerrainChunks(float worldX, float worldZ)
{
	const GlobalChunkCoord center = WorldToTerrainChunkCoord(worldX, worldZ);
	std::vector<GlobalChunkCoord> result;
	result.reserve(static_cast<size_t>((2 * kVisibleRadius + 1) * (2 * kVisibleRadius + 1)));
	for (int dz = -kVisibleRadius; dz <= kVisibleRadius; ++dz)
		for (int dx = -kVisibleRadius; dx <= kVisibleRadius; ++dx)
			result.push_back(GlobalChunkCoord{ center.x + dx, center.z + dz });
	return result;
}
```

Vérifier par lecture que `<cmath>` (pour `std::floor`) et `<cstdint>` sont déjà inclus dans `WorldModel.cpp` (ils le sont, car `WorldToGlobalChunkCoord` les utilise). Si `kVisibleRadius` n'est pas accessible (autre nom/valeur), le lire et adapter pour produire un 7×7 (rayon 3).

- [ ] **Step 5: Enregistrer le test dans `CMakeLists.txt`**

Mirror du pattern d'un test existant qui exerce un symbole de `src/client/world/` — par ex. `foliage_tests` (`add_executable` + `target_link_libraries(... engine_core)` + `add_test`). Repérer ce bloc dans `CMakeLists.txt`, et ajouter juste à côté (même style, même lib) :

```cmake
add_executable(terrain_chunk_grid_tests src/client/world/tests/TerrainChunkGridTests.cpp)
target_link_libraries(terrain_chunk_grid_tests PRIVATE engine_core)
add_test(NAME terrain_chunk_grid_tests COMMAND terrain_chunk_grid_tests)
```

> Important : ce test, contrairement au test header-only de la Phase 0, dépend de symboles **définis dans `WorldModel.cpp`** (donc dans `engine_core`). Il DOIT linker `engine_core` (qui exporte aussi le répertoire racine en PUBLIC, ce qui résout les `#include "src/..."`). Calquer exactement la lib utilisée par `foliage_tests`/`wind_tests`.

- [ ] **Step 6: Vérifier que le test passe (CI)**

Pousser et laisser la CI compiler + exécuter ctest.
Attendu : `terrain_chunk_grid_tests` PASS (`[OK] TerrainChunkGridTests: tous les cas passent`, code 0).

- [ ] **Step 7: Commit**

```bash
git add src/client/world/WorldModel.h src/client/world/WorldModel.cpp \
        src/client/world/tests/TerrainChunkGridTests.cpp CMakeLists.txt
git commit -m "feat(terrain): grille terrain 256m dédiée + helpers (Phase 1, chantier C)"
```

---

### Task 2: Placement du renderer sur la grille 256

**Files:**
- Modify: `src/client/render/terrain_chunk/TerrainChunkRenderer.cpp` (placement ~ligne 1133-1134 ; commentaire ring ~ligne 1080)

> Pas de test unitaire (chemin Vulkan). La logique « origine = 256 × coord » est couverte indirectement par `Test_NeighbourChunks_AreContiguous` (Task 1). Vérification = compilation CI + jeu/éditeur (Task 4).

- [ ] **Step 1: Basculer l'origine de placement sur 256**

Dans `RenderVisibleChunks`, localiser le calcul de l'origine monde du chunk (recherche `kChunkSize) * coord`) :

```cpp
			const float originX = static_cast<float>(engine::world::kChunkSize) * coord.x;
			const float originZ = static_cast<float>(engine::world::kChunkSize) * coord.z;
```

et remplacer par :

```cpp
			// Phase 1 (chantier C) : le mesh d'un chunk terrain couvre 256 m
			// (= kTerrainChunkSizeMeters), pas 500 m. Placer sur la grille terrain
			// rend les chunks jointifs (plus de trou de 244 m) et aligne le jeu
			// sur l'éditeur. kChunkSize (500) reste réservé aux instances/zone.
			const float originX = static_cast<float>(engine::world::kTerrainChunkSizeMeters) * coord.x;
			const float originZ = static_cast<float>(engine::world::kTerrainChunkSizeMeters) * coord.z;
```

- [ ] **Step 2: Documenter le ring approximatif**

Localiser l'appel (recherche `world.GetRingForChunk`) :

```cpp
			m_runtime.UpdateRing(coord, world.GetRingForChunk(coord));
```

et ajouter juste au-dessus le commentaire :

```cpp
			// Phase 1 : `coord` est sur la grille terrain 256, alors que `world`
			// (et donc GetRingForChunk) raisonne sur la grille 500. Le ring obtenu
			// est donc approximatif. C'est TOLÉRÉ : le ring ne pilote QUE la
			// politique d'éviction LRU (CollectEvictionsForBudget), jamais
			// l'exactitude du rendu. Un ring dédié 256 pourra être ajouté si la
            // pression budgétaire GPU le justifie (hors périmètre Phase 1).
			m_runtime.UpdateRing(coord, world.GetRingForChunk(coord));
```

- [ ] **Step 3: Vérifier la compilation (CI)**

Pousser ; CI compile `engine_app` + `world_editor_app`. Attendu : build OK.

- [ ] **Step 4: Commit**

```bash
git add src/client/render/terrain_chunk/TerrainChunkRenderer.cpp
git commit -m "feat(terrain): place les chunks sur la grille 256m (Phase 1)"
```

---

### Task 3: Site Engine — coords visibles sur la grille 256

**Files:**
- Modify: `src/client/app/Engine.cpp` (site du dessin des chunks, déclaration de `visibleChunks`)

> Pas de test unitaire (intégration Engine/Vulkan). La logique sous-jacente (`ComputeVisibleTerrainChunks`) est couverte par Task 1. Vérification = compilation CI + jeu/éditeur (Task 4).

- [ ] **Step 1: Remplacer la source des chunks visibles du chemin terrain**

Dans `src/client/app/Engine.cpp`, dans le lambda de la passe "Geometry", localiser (recherche `GetActiveAndVisibleChunks`) :

```cpp
													const std::vector<engine::world::GlobalChunkCoord> visibleChunks =
														m_world.GetActiveAndVisibleChunks();
```

et remplacer par :

```cpp
													// Phase 1 (chantier C) : les chunks terrain sont sur la grille 256 m,
													// pas sur la grille 500 m de m_world (instances/zone). On dérive donc
													// l'ensemble visible directement de la position caméra sur la grille
													// terrain, pour charger/placer les bons terrain.bin (alignés éditeur).
													const std::vector<engine::world::GlobalChunkCoord> visibleChunks =
														engine::world::ComputeVisibleTerrainChunks(
															rs.camera.position.x, rs.camera.position.z);
```

> `rs` (le `RenderState` lu) est en portée à ce site : il est déjà utilisé juste au-dessus (ex. `UpdateTerrainChunkCameraUbo(rs.viewProjMatrix.m)`) et `rs.camera.position` est consommé ailleurs dans le même lambda. Vérifier par lecture que `rs` est bien la variable du `RenderState` courant à cet endroit.

- [ ] **Step 2: Vérifier la compilation (CI)**

Pousser ; CI compile `engine_app` + `world_editor_app` et exécute ctest (dont `terrain_chunk_grid_tests`). Attendu : build OK + tests verts.

- [ ] **Step 3: Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(terrain): chemin de rendu utilise la grille terrain 256 (Phase 1)"
```

---

### Task 4: Validation en éditeur/jeu

**Files:** aucun (validation manuelle, utilisateur).

> Aucun `terrain.bin` n'est livré : produire d'abord du contenu chunké via l'éditeur.

- [ ] **Step 1: Produire une zone chunkée avec relief**

Ouvrir `lcdlln_world_editor.exe --world-editor`, sculpter du relief sur ≥ 4 chunks voisins, **Sauvegarder** (écrit `game/data/chunks/chunk_X_Z/terrain.bin` + `splat.bin`).

- [ ] **Step 2: Vérifier la contiguïté (plus de trou de 244 m)**

Charger en jeu (ou playtest). Attendu : les chunks terrain sont **jointifs** (plus de bandes de 244 m manquantes entre chunks), et le terrain chunké est **aligné** avec l'ancienne heightmap / l'éditeur (même position monde).

- [ ] **Step 3: Vérifier la combinaison avec la Phase 0**

Attendu : avec Phase 0 + Phase 1, sur une zone chunkée avec relief, **pas de z-fighting** (Phase 0 éteint le legacy) ET **pas de trou** (Phase 1 place sur 256). Déplacer le personnage : sol continu et stable.

- [ ] **Step 4: Non-régression heightmap-only**

Charger une carte sans `terrain.bin` (ex. `demo_plains`). Attendu : terrain heightmap toujours rendu normalement (le chemin terrain 256 ne dessine rien faute de chunks, le legacy reste affiché — comportement Phase 0).

---

## Self-Review

**Couverture spec (Phase 1) :**
- « Introduire `kTerrainChunkSizeMeters` sans static_assert vs zone » → Task 1 Step 3. ✅
- « Router le placement renderer sur 256 » → Task 2. ✅
- « Router le chemin terrain (coords visibles) sur 256 » → Task 3. ✅
- « Laisser instances/zone/streaming sur 500 » → respecté (aucune modif de `kChunkSize`, `GlobalToZoneAndLocal`, zone_builder, HLOD). ✅
- « SurfaceQueryService → 256 » → **explicitement différé** (code mort/test-only), documenté dans Décomposition. ⚠️ (écart assumé vs spec, justifié)
- « static_assert ajusté » → aucun static_assert ajouté liant terrain↔zone (conforme : 10000 % 256 ≠ 0). ✅

**Scan placeholders :** aucun TBD/TODO ; tout le code est fourni. Les seules instructions « vérifier par lecture » portent sur des ancres/includes existants, pas sur du code à inventer. ✅

**Cohérence des types :** `kTerrainChunkSizeMeters` (int) ; `WorldToTerrainChunkCoord(float,float) -> GlobalChunkCoord` ; `ComputeVisibleTerrainChunks(float,float) -> std::vector<GlobalChunkCoord>`. Signatures identiques entre déclaration (Task 1 Step 3), définition (Step 4), test (Step 1) et appel Engine (Task 3). `rs.camera.position.x/z` sont des `float`. ✅

**Risque ring (Task 2 Step 2) :** documenté et borné — n'affecte que l'éviction LRU, jamais le rendu. Un 7×7 sur 256 m (49 chunks) reste très en-dessous du budget GPU, donc aucune éviction ne se déclenche en pratique. ✅

**Couverture visible :** un 7×7 sur 256 m = rayon ~1792 m (vs ~3500 m sur 500). Plus serré, mais cohérent avec l'intention « chunks jointifs ». À ajuster (rayon) seulement si une zone chunkée réelle dépasse cette portée — noté pour la validation. ✅
