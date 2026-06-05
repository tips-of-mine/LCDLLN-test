# Terrain Phase 0 — Rendu exclusif (anti z-fighting) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Supprimer le z-fighting (triangles scintillants au sol) en n'affichant le terrain heightmap legacy que lorsque les chunks ne couvrent pas la scène.

**Architecture:** La décision « dessiner le terrain legacy ? » est extraite dans une fonction pure header-only (`ShouldDrawLegacyTerrain`), unit-testée. `TerrainChunkRenderer` compte les chunks réellement dessinés lors de son dernier `RenderVisibleChunks`. `Engine` mémorise ce compte par frame (`m_lastFrameChunkDrawCount`, remis à zéro chaque frame) et l'injecte dans la fonction de décision au moment du gating. Latence assumée d'une frame (le gating lit le compte de la frame précédente).

**Tech Stack:** C++17, Vulkan, tests C++ autonomes (macro `REQUIRE` maison + `main()` retournant le nombre d'échecs), enregistrés via `add_executable`/`add_test` dans `CMakeLists.txt`, exécutés par `ctest` en CI Linux. **Pas de build local** : compilation et tests validés via CI ; le rendu est validé manuellement en jeu.

**Référence spec :** `docs/superpowers/specs/2026-06-05-unification-terrain-design.md` (Phase 0).

**Portée :** client uniquement — **pas de redéploiement serveur**. Aucune modification de `frontFace`/`cullMode`/winding (convention stricte `CLAUDE.md`).

---

## Décomposition

Ce plan couvre **uniquement la Phase 0** du spec (livrable indépendant, mergeable seul). Les Phases 1 à 3 (réconciliation taille de chunk, `IHeightField`, retrait du legacy) feront l'objet de plans dédiés après validation en jeu de la Phase 0.

## File Structure

- **Create** `src/client/render/terrain/TerrainRenderSelection.h` — fonction pure `ShouldDrawLegacyTerrain` (header-only, sans dépendance Vulkan).
- **Create** `src/client/render/tests/TerrainRenderSelectionTests.cpp` — test unitaire de la fonction pure.
- **Modify** `CMakeLists.txt` — enregistrer le nouvel exécutable de test.
- **Modify** `src/client/render/terrain_chunk/TerrainChunkRenderer.h` — getter `GetLastDrawnChunkCount()` + membre compteur.
- **Modify** `src/client/render/terrain_chunk/TerrainChunkRenderer.cpp` — reset + incrément du compteur dans `RenderVisibleChunks`.
- **Modify** `src/client/app/Engine.h` — membre `m_lastFrameChunkDrawCount`.
- **Modify** `src/client/app/Engine.cpp` — include + gating via la fonction pure + mise à jour du compteur par frame.

---

### Task 1: Fonction pure de décision `ShouldDrawLegacyTerrain`

**Files:**
- Create: `src/client/render/terrain/TerrainRenderSelection.h`
- Test: `src/client/render/tests/TerrainRenderSelectionTests.cpp`
- Modify: `CMakeLists.txt` (section des `add_test`, ~après la ligne 1038 `add_test(NAME entity_influence_tests ...)`)

- [ ] **Step 1: Écrire le test qui échoue**

Create `src/client/render/tests/TerrainRenderSelectionTests.cpp` :

```cpp
// src/client/render/tests/TerrainRenderSelectionTests.cpp
#include "src/client/render/terrain/TerrainRenderSelection.h"

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

	using engine::render::ShouldDrawLegacyTerrain;

	// Carte heightmap-only : aucun chunk dessiné -> on DOIT dessiner le legacy.
	void Test_NoChunks_DrawsLegacy()
	{
		REQUIRE(ShouldDrawLegacyTerrain(/*legacyValid=*/true, /*hasLoadPass=*/true, /*chunkCount=*/0u) == true);
	}

	// Carte chunkée : des chunks dessinés -> on N'AFFICHE PAS le legacy (anti z-fight).
	void Test_ChunksCover_HidesLegacy()
	{
		REQUIRE(ShouldDrawLegacyTerrain(true, true, 5u) == false);
	}

	// Borne : un seul chunk suffit à masquer le legacy.
	void Test_OneChunk_HidesLegacy()
	{
		REQUIRE(ShouldDrawLegacyTerrain(true, true, 1u) == false);
	}

	// Legacy invalide -> jamais dessiné, peu importe le reste.
	void Test_LegacyInvalid_NeverDraws()
	{
		REQUIRE(ShouldDrawLegacyTerrain(false, true, 0u) == false);
	}

	// Pas de LOAD pass disponible -> on ne dessine pas le legacy ici.
	void Test_NoLoadPass_NeverDraws()
	{
		REQUIRE(ShouldDrawLegacyTerrain(true, false, 0u) == false);
	}
}

int main()
{
	Test_NoChunks_DrawsLegacy();
	Test_ChunksCover_HidesLegacy();
	Test_OneChunk_HidesLegacy();
	Test_LegacyInvalid_NeverDraws();
	Test_NoLoadPass_NeverDraws();

	if (g_failed == 0)
		std::printf("[OK] TerrainRenderSelectionTests: tous les cas passent\n");
	return g_failed;
}
```

- [ ] **Step 2: Vérifier que le test échoue (header absent)**

Le test ne compile pas tant que `TerrainRenderSelection.h` n'existe pas. En CI, l'étape de build du test échouera avec « fichier introuvable » / `ShouldDrawLegacyTerrain` non déclaré. C'est l'échec attendu.

Localement (pas de build) : impossible — on s'appuie sur la CI. Ne pas committer avant le Step 3 (le repo doit rester compilable).

- [ ] **Step 3: Écrire l'implémentation minimale**

Create `src/client/render/terrain/TerrainRenderSelection.h` :

```cpp
#pragma once

// src/client/render/terrain/TerrainRenderSelection.h
//
// Phase 0 du chantier C (unification terrain). Décision pure : faut-il
// dessiner le terrain heightmap legacy (`TerrainRenderer`) cette frame ?
//
// Contexte : deux terrains coexistent (heightmap legacy + chunks data-driven).
// Les dessiner ensemble crée du z-fighting (triangles scintillants au sol).
// On rend donc le legacy EXCLUSIF : il n'est dessiné que si les chunks ne
// couvrent pas la scène (carte heightmap-only, ou chunks pas encore streamés).
//
// Fonction pure, sans état ni dépendance Vulkan -> unit-testable.

#include <cstdint>

namespace engine::render
{
	/// Retourne true si le terrain heightmap legacy doit être dessiné cette frame.
	///
	/// \param legacyTerrainValid  `TerrainRenderer::IsValid()` — une heightmap est chargée.
	/// \param geometryHasLoadPass `GeometryPass::HasLoadPass()` — la passe LOAD est dispo.
	/// \param lastFrameChunkDrawCount Nombre de chunks réellement dessinés à la frame
	///        précédente (0 si le renderer chunk est invalide ou n'a rien dessiné).
	/// \return true si on dessine le legacy ; false si les chunks couvrent la scène
	///         (>0 chunk dessiné), pour éviter le z-fighting / l'overdraw.
	inline bool ShouldDrawLegacyTerrain(bool legacyTerrainValid,
		bool geometryHasLoadPass,
		std::uint32_t lastFrameChunkDrawCount)
	{
		const bool chunksCoveredScene = lastFrameChunkDrawCount > 0u;
		return legacyTerrainValid && geometryHasLoadPass && !chunksCoveredScene;
	}
}
```

- [ ] **Step 4: Enregistrer le test dans CMake**

Dans `CMakeLists.txt`, juste après le bloc `entity_influence_tests` (la ligne `add_test(NAME entity_influence_tests COMMAND entity_influence_tests)`, ~ligne 1038), ajouter :

```cmake
add_executable(terrain_render_selection_tests src/client/render/tests/TerrainRenderSelectionTests.cpp)
add_test(NAME terrain_render_selection_tests COMMAND terrain_render_selection_tests)
```

(Header-only sans dépendance : aucune `target_link_libraries` nécessaire, comme les autres tests purs du repo. Le chemin d'include `src/...` est résolu depuis la racine du projet, conformément aux tests existants p.ex. `WaterMeshGpuTests.cpp`.)

- [ ] **Step 5: Vérifier que le test passe (CI)**

Pousser la branche et laisser la CI Linux compiler + exécuter ctest.
Attendu : `terrain_render_selection_tests` PASS (sortie `[OK] TerrainRenderSelectionTests: tous les cas passent`, code retour 0).

- [ ] **Step 6: Commit**

```bash
git add src/client/render/terrain/TerrainRenderSelection.h \
        src/client/render/tests/TerrainRenderSelectionTests.cpp \
        CMakeLists.txt
git commit -m "test(terrain): fonction pure ShouldDrawLegacyTerrain (Phase 0)"
```

---

### Task 2: Compteur de chunks dessinés dans `TerrainChunkRenderer`

**Files:**
- Modify: `src/client/render/terrain_chunk/TerrainChunkRenderer.h` (ajout getter + membre)
- Modify: `src/client/render/terrain_chunk/TerrainChunkRenderer.cpp:1038-1133` (reset + incrément dans `RenderVisibleChunks`)

> Pas de test unitaire : `RenderVisibleChunks` est couplé à Vulkan (device, descriptor pool, command buffer). Vérification = compilation CI + validation en jeu (Task 4). La logique décisionnelle, elle, est couverte par le test pur de la Task 1.

- [ ] **Step 1: Ajouter le getter et le membre dans le header**

Dans `src/client/render/terrain_chunk/TerrainChunkRenderer.h`, juste après la méthode `IsValid()` (actuellement lignes 208-211, fin du commentaire + `bool IsValid() const { return m_pipeline.IsValid(); }`), ajouter :

```cpp
		/// Nombre de chunks réellement dessinés lors du dernier appel à
		/// `RenderVisibleChunks` (hors chunks skippés faute de terrain.bin/
		/// splat.bin/descriptor). Remis à zéro au début de chaque appel.
		/// Utilisé par l'Engine pour décider du rendu exclusif terrain
		/// legacy vs chunks (anti z-fighting, Phase 0 chantier C).
		std::uint32_t GetLastDrawnChunkCount() const { return m_lastDrawnChunkCount; }
```

Puis, dans la section `private:` (après le membre `m_slotToCoord` à la ligne 231, ou à côté des autres compteurs/états), ajouter :

```cpp
		// Compteur de chunks dessinés au dernier RenderVisibleChunks (Phase 0).
		std::uint32_t m_lastDrawnChunkCount = 0u;
```

(`<cstdint>` est déjà inclus ligne 35.)

- [ ] **Step 2: Reset du compteur en tête de `RenderVisibleChunks`**

Dans `src/client/render/terrain_chunk/TerrainChunkRenderer.cpp`, au tout début de `RenderVisibleChunks` (juste après l'ouverture `{` ligne 1042, AVANT les gardes `if (!IsValid() ...) return;`), ajouter :

```cpp
	// Phase 0 : remis à zéro à chaque appel pour que tout retour anticipé
	// (renderer invalide, PBR non chargé) laisse un compte de 0.
	m_lastDrawnChunkCount = 0u;
```

- [ ] **Step 3: Incrémenter après chaque draw émis**

Toujours dans `RenderVisibleChunks`, juste après l'émission du draw `m_pipeline.RecordChunkDraw(...)` (ligne 1131), avant la fermeture `}` de la boucle `for` (ligne 1132), ajouter :

```cpp
			++m_lastDrawnChunkCount;
```

(Placé après `RecordChunkDraw` : tous les `continue` au-dessus — fichier absent, mesh/splat/descriptor échoués — sortent de l'itération sans incrémenter, donc seul un draw réellement émis compte.)

- [ ] **Step 4: Vérifier la compilation (CI)**

Pousser ; la CI compile `engine_app` + `world_editor_app`. Attendu : build OK (changement additif, pas de site d'appel cassé — le getter est nouveau et le comportement existant inchangé tant que personne ne le lit).

- [ ] **Step 5: Commit**

```bash
git add src/client/render/terrain_chunk/TerrainChunkRenderer.h \
        src/client/render/terrain_chunk/TerrainChunkRenderer.cpp
git commit -m "feat(terrain): TerrainChunkRenderer expose GetLastDrawnChunkCount (Phase 0)"
```

---

### Task 3: Câbler le gating exclusif dans `Engine`

**Files:**
- Modify: `src/client/app/Engine.h` (membre `m_lastFrameChunkDrawCount`)
- Modify: `src/client/app/Engine.cpp` (include + gating ligne 5070-5071 + mise à jour du compteur après le bloc chunk ~5305)

> Pas de test unitaire (couplé Engine/Vulkan). Vérification = compilation CI + validation en jeu (Task 4).

- [ ] **Step 1: Ajouter le membre dans `Engine.h`**

Dans `src/client/app/Engine.h`, à côté de la déclaration `m_terrainChunkRenderer` (ligne 983), ajouter :

```cpp
		// Phase 0 (chantier C) : nombre de chunks dessinés à la frame précédente.
		// Lu par le gating terrain legacy (rendu exclusif anti z-fighting).
		// Remis à jour chaque frame à la fin du bloc de dessin des chunks.
		std::uint32_t m_lastFrameChunkDrawCount = 0u;
```

(`<cstdint>` : vérifier qu'il est inclus dans `Engine.h` ; sinon les `uint32_t` déjà présents dans le fichier confirment sa disponibilité transitive — ne rien ajouter si d'autres `uint32_t` membres existent déjà.)

- [ ] **Step 2: Inclure le header de décision dans `Engine.cpp`**

Dans `src/client/app/Engine.cpp`, dans le bloc d'includes en tête de fichier (à côté des autres `#include "src/client/render/..."`), ajouter :

```cpp
#include "src/client/render/terrain/TerrainRenderSelection.h"
```

- [ ] **Step 3: Remplacer le gating du terrain legacy**

Dans `src/client/app/Engine.cpp`, remplacer les lignes 5070-5071 :

```cpp
												const bool terrainBeforeGeometry = m_terrain.IsValid()
													&& m_pipeline->GetGeometryPass().HasLoadPass();
```

par :

```cpp
												// Phase 0 (chantier C) : rendu exclusif. On ne dessine le terrain
												// heightmap legacy que si les chunks n'ont pas couvert la scène à la
												// frame précédente -> supprime le z-fighting du double terrain.
												const bool terrainBeforeGeometry = engine::render::ShouldDrawLegacyTerrain(
													m_terrain.IsValid(),
													m_pipeline->GetGeometryPass().HasLoadPass(),
													m_lastFrameChunkDrawCount);
```

- [ ] **Step 4: Mettre à jour le compteur après le bloc de dessin des chunks**

Dans `src/client/app/Engine.cpp`, le bloc de dessin des chunks va de la ligne 5285 (`if (m_terrainChunkRenderer && m_terrainChunkRenderer->IsValid())`) à 5305 (`}` fermant ce `if`). Remplacer ce bloc :

```cpp
												if (m_terrainChunkRenderer && m_terrainChunkRenderer->IsValid())
												{
													UpdateTerrainChunkCameraUbo(rs.viewProjMatrix.m);
													const std::vector<engine::world::GlobalChunkCoord> visibleChunks =
														m_world.GetActiveAndVisibleChunks();
													if (!visibleChunks.empty())
													{
														m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
															m_vkDeviceContext.GetDevice(), cmd, reg,
															m_vkSwapchain.GetExtent(),
															m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
															m_fgGBufferVelocityId, m_fgDepthId,
															[this, &visibleChunks](VkCommandBuffer innerCmd) {
																m_terrainChunkRenderer->RenderVisibleChunks(
																	innerCmd,
																	m_terrainChunkCameraSet,
																	m_world,
																	visibleChunks);
															});
													}
												}
```

par (ajout de la capture du compte en fin de bloc) :

```cpp
												std::uint32_t chunksDrawnThisFrame = 0u;
												if (m_terrainChunkRenderer && m_terrainChunkRenderer->IsValid())
												{
													UpdateTerrainChunkCameraUbo(rs.viewProjMatrix.m);
													const std::vector<engine::world::GlobalChunkCoord> visibleChunks =
														m_world.GetActiveAndVisibleChunks();
													if (!visibleChunks.empty())
													{
														m_pipeline->GetGeometryPass().RecordTerrainChunkBatch(
															m_vkDeviceContext.GetDevice(), cmd, reg,
															m_vkSwapchain.GetExtent(),
															m_fgGBufferAId, m_fgGBufferBId, m_fgGBufferCId,
															m_fgGBufferVelocityId, m_fgDepthId,
															[this, &visibleChunks](VkCommandBuffer innerCmd) {
																m_terrainChunkRenderer->RenderVisibleChunks(
																	innerCmd,
																	m_terrainChunkCameraSet,
																	m_world,
																	visibleChunks);
															});
														chunksDrawnThisFrame = m_terrainChunkRenderer->GetLastDrawnChunkCount();
													}
												}
												// Phase 0 : mémorise pour le gating legacy de la frame SUIVANTE.
												// Remis à 0 chaque frame (carte heightmap-only / chunks non visibles
												// -> le legacy redevient visible sans rester "collé" éteint).
												m_lastFrameChunkDrawCount = chunksDrawnThisFrame;
```

- [ ] **Step 5: Vérifier la compilation (CI)**

Pousser ; la CI compile `engine_app` + `world_editor_app` et exécute ctest (dont `terrain_render_selection_tests` de la Task 1).
Attendu : build OK + tous les tests verts.

- [ ] **Step 6: Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(terrain): rendu exclusif legacy/chunks via gating (Phase 0, chantier C)"
```

---

### Task 4: Validation en jeu

**Files:** aucun (validation manuelle).

> Le rendu ne peut pas être validé par un test automatisé. Cette tâche est manuelle, à faire par l'utilisateur (ou en session de test sur build CI).

- [ ] **Step 1: Lancer le jeu sur une carte chunkée**

Lancer le client sur une zone disposant de chunks `terrain.bin` + `splat.bin`. Déplacer le personnage (pas à gauche/droite).
Attendu : **plus de triangles scintillants au sol** ; le terrain ne « clignote » plus quand la caméra bouge.

- [ ] **Step 2: Vérifier la non-régression heightmap-only**

Lancer sur une carte sans chunks (heightmap legacy uniquement).
Attendu : le terrain reste visible et correct (le legacy n'est PAS éteint, car `m_lastFrameChunkDrawCount == 0`).

- [ ] **Step 3: Vérifier la transition zone chunkée -> zone heightmap**

Faire traverser au personnage une frontière entre zone chunkée et zone heightmap-only (si une telle carte existe).
Attendu : pas de disparition durable du terrain (le compteur est remis à 0 quand aucun chunk n'est dessiné -> le legacy réapparaît).

---

## Self-Review

**Couverture spec (Phase 0) :**
- « Exposer le nombre de chunks réellement dessinés » → Task 2. ✅
- « Éteindre le legacy quand des chunks sont dessinés (gating `Engine.cpp:5070`) » → Task 3. ✅
- « Vérifier que `terrainBeforeGeometry=false` n'empêche pas l'ouverture du render-pass LOAD » → géré : `terrainBeforeGeometry` reste un `bool` passé à `RecordIndirect`/`Record` (lignes 5117/5130) exactement comme avant ; seule sa **valeur** change. Le comportement d'ouverture du LOAD pass par `Record`/`RecordIndirect` quand le flag est faux est inchangé. ✅ (À confirmer en jeu, Task 4.)
- Comportements carte chunkée / heightmap-only / mixte → couverts par les tests purs (Task 1) + validation (Task 4). ✅

**Scan placeholders :** aucun TBD/TODO ; tout le code est fourni intégralement. ✅

**Cohérence des types :** la fonction `ShouldDrawLegacyTerrain(bool, bool, std::uint32_t)` a la même signature en Task 1 (définition + test) et Task 3 (appel). Le getter `GetLastDrawnChunkCount()` (Task 2) retourne `std::uint32_t`, consommé en `std::uint32_t` (Task 3). Le membre `m_lastFrameChunkDrawCount` (Engine) et `m_lastDrawnChunkCount` (renderer) sont distincts et nommés sans ambiguïté. ✅

**Latence d'une frame :** assumée et documentée — au pire, un frame de double-dessin lors de la toute première apparition des chunks, invisible à l'œil. Le reset par frame empêche tout état « collé ». ✅
