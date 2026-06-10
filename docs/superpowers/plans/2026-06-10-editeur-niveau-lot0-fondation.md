# Éditeur de niveau — Lot 0 : Fondation d'édition — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre l'éditeur de niveau utilisable en câblant la fondation d'édition — sélection (clic + marquee multi), suppression réversible, et calques — sur le modèle vivant `layoutInstances`.

**Architecture:** Approche A (cf. spec) : on opère sur `WorldMapEditDocument::layoutInstances` (+ MeshInsert / DungeonPortal), pas sur les orphelins `PropInstance`. Cœur pur et testable (sélection multi, encode/décode `EntityId`, ops de suppression, clé de calque) compilé dans `engine_core` et testé sous ctest Linux ; UI ImGui (panneaux, toolbar) gardée `_WIN32` ; intégration des entrées dans `Engine.cpp`.

**Tech Stack:** C++17, ImGui (gardé `_WIN32`), CMake, tests « maison » (macro `REQUIRE` + `main()`), réutilisation de la géométrie pure `SelectInRect`/`SelectInLasso` et du raycast `RaycastTerrainFromCamera`.

**Spec de référence :** `docs/superpowers/specs/2026-06-10-editeur-niveau-lot0-fondation-design.md`
**Branche :** `editeur-lot0-fondation-spec` (le plan continue dessus).
**Déploiement :** ✅ client/éditeur uniquement — aucun redéploiement serveur.

---

## Structure des fichiers

**Créés (cœur pur — compilés dans `engine_core`, testés Linux) :**
- `src/world_editor/scene/EntityKey.h` / `.cpp` — clé stable `uint64` par entité (FNV-1a 64).
- `src/world_editor/scene/SelectionQuery.h` / `.cpp` — encode/décode `EntityId↔uint32`, pick nearest, wrappers rect/lasso.
- `src/world_editor/scene/DeleteEntitiesOps.h` — templates pures remove/restore par index (header-only).
- `src/world_editor/scene/DeleteEntitiesCommand.h` / `.cpp` — `ICommand` réversible (callbacks remove/restore).
- `src/world_editor/panels/LayersPanel.h` / `.cpp` — panneau ImGui des 16 calques.

**Créés (tests) :**
- `src/world_editor/tests/EntityKeyTests.cpp`
- `src/world_editor/tests/SelectionQueryTests.cpp`
- `src/world_editor/tests/DeleteEntitiesOpsTests.cpp`
- `src/world_editor/tests/DeleteEntitiesCommandTests.cpp`

**Modifiés :**
- `src/world_editor/scene/EditorSelection.h` / `.cpp` — set multi + API.
- `src/world_editor/tests/EditorSelectionTests.cpp` — tests multi.
- `src/world_editor/core/WorldEditorShell.h` / `.cpp` — `ActiveTool::Select`, `LayersDocument` membre + accesseurs, résolveur de clé d'entité, enregistrement `LayersPanel`.
- `src/world_editor/ui/EditorToolbar.cpp` — `Select` dans `m_orderedTools`.
- `src/world_editor/ui/ToolbarIconAtlas.cpp` — style icône `Select`.
- `src/world_editor/panels/OutlinerPanel.h` / `.cpp` — Ctrl+clic multi + toggles calque + pastille couleur.
- `src/world_editor/panels/ToolPropertiesPanel.cpp` — bloc `Select`.
- `src/world_editor/volumes/MeshInsertDocument.h`, `src/world_editor/volumes/dungeons/DungeonPortalDocument.h` — accesseur `Mutable()`.
- `src/shared/platform/Input.h` — `Key::Delete`.
- `src/world_editor/ui/WorldEditorImGui.h` / `.cpp` — masque de visibilité des marqueurs `layoutInstances`.
- `src/client/app/Engine.cpp` — routage souris/clavier `Select`, build des candidats, push `DeleteEntitiesCommand`, install résolveurs, masque visibilité.
- `CMakeLists.txt` (racine) — nouvelles sources `engine_core` + 4 cibles de test.

---

## Phase 1 — Cœur pur (testable sous ctest Linux)

### Task 1 : Multi-sélection dans `EditorSelection`

**Files:**
- Modify: `src/world_editor/scene/EditorSelection.h`
- Modify: `src/world_editor/scene/EditorSelection.cpp`
- Test: `src/world_editor/tests/EditorSelectionTests.cpp`

Contexte : `EditorSelection` est mono-sélection (`m_current`). On ajoute un **set** ordonné + un **primaire** (= `m_current`, inchangé) pour ne pas casser Outliner/Inspector.

- [ ] **Step 1 : Écrire les tests qui échouent** — ajouter dans `EditorSelectionTests.cpp`, avant `int main()`, une nouvelle fonction de test et l'appeler depuis `main()` :

```cpp
	void Test_MultiSelection()
	{
		EditorSelection sel;
		int calls = 0;
		sel.SetOnChanged([&](EntityId) { ++calls; });

		const EntityId a{EntityKind::LayoutInstance, 1};
		const EntityId b{EntityKind::LayoutInstance, 2};
		const EntityId c{EntityKind::MeshInsert, 0};

		// SelectMany : set = {a,b,c}, primaire = premier (a).
		sel.SelectMany({a, b, c});
		REQUIRE(calls == 1);
		REQUIRE(sel.SelectedSet().size() == 3);
		REQUIRE(sel.Current() == a);
		REQUIRE(sel.IsSelected(b));
		REQUIRE(sel.IsSelected(c));

		// Select mono : set = {b}, primaire = b.
		sel.Select(b);
		REQUIRE(sel.SelectedSet().size() == 1);
		REQUIRE(sel.Current() == b);
		REQUIRE(!sel.IsSelected(a));

		// Toggle : ajoute a -> {b,a}.
		sel.ToggleInSelection(a);
		REQUIRE(sel.SelectedSet().size() == 2);
		REQUIRE(sel.IsSelected(a));
		// Toggle : retire b ; primaire se reporte sur un restant (a).
		sel.ToggleInSelection(b);
		REQUIRE(sel.SelectedSet().size() == 1);
		REQUIRE(!sel.IsSelected(b));
		REQUIRE(sel.Current() == a);

		// Clear vide le set + le primaire.
		sel.Clear();
		REQUIRE(sel.SelectedSet().empty());
		REQUIRE(!sel.HasSelection());

		// SelectMany vide = Clear sémantique (set vide, primaire None).
		sel.SelectMany({a});
		sel.SelectMany({});
		REQUIRE(sel.SelectedSet().empty());
		REQUIRE(sel.Current().kind == EntityKind::None);
	}
```

Et dans `main()` ajouter `Test_MultiSelection();` après `Test_SelectionNotifiesOnChange();`.

- [ ] **Step 2 : Lancer le test, vérifier l'échec de compilation/exécution**

Run: `cmake --build build --target editor_selection_tests` (ou compilation CI)
Expected: échec de compilation (`SelectMany`, `SelectedSet`, `IsSelected`, `ToggleInSelection` n'existent pas).

- [ ] **Step 3 : Implémenter dans `EditorSelection.h`** — ajouter `#include <vector>` en tête, puis dans la classe (après `Clear()`), déclarer :

```cpp
		/// Remplace la sélection par `ids` (ordre préservé). Le primaire devient
		/// le premier élément (ou None si `ids` est vide). Notifie si changement.
		void SelectMany(const std::vector<EntityId>& ids);

		/// Ajoute `id` au set s'il est absent, l'en retire sinon. Ajuste le
		/// primaire (reste valide ; se reporte sur un élément restant après un
		/// retrait du primaire ; None si le set devient vide). Notifie.
		void ToggleInSelection(EntityId id);

		/// Set courant des entités sélectionnées (le primaire en fait partie).
		const std::vector<EntityId>& SelectedSet() const { return m_set; }

		/// true si `id` appartient au set.
		bool IsSelected(EntityId id) const;
```

Et dans la section privée, après `EntityId m_current{};` :

```cpp
		std::vector<EntityId> m_set; ///< Set de sélection (primaire = m_current).
```

- [ ] **Step 4 : Implémenter dans `EditorSelection.cpp`** — modifier `Select`/`Clear` pour maintenir `m_set`, et ajouter les nouvelles méthodes :

```cpp
#include "src/world_editor/scene/EditorSelection.h"

#include <algorithm>

namespace engine::editor::scene
{
	void EditorSelection::Select(EntityId id)
	{
		if (m_set.size() == 1 && m_current == id) return; // déjà la seule sélection
		m_current = id;
		m_set.clear();
		if (id.kind != EntityKind::None) m_set.push_back(id);
		if (m_onChanged) m_onChanged(m_current);
	}

	void EditorSelection::Clear()
	{
		if (m_current.kind == EntityKind::None && m_set.empty()) return; // déjà vide
		m_current = EntityId{};
		m_set.clear();
		if (m_onChanged) m_onChanged(m_current);
	}

	void EditorSelection::SelectMany(const std::vector<EntityId>& ids)
	{
		m_set = ids;
		m_current = ids.empty() ? EntityId{} : ids.front();
		if (m_onChanged) m_onChanged(m_current);
	}

	void EditorSelection::ToggleInSelection(EntityId id)
	{
		auto it = std::find(m_set.begin(), m_set.end(), id);
		if (it != m_set.end())
			m_set.erase(it);
		else
			m_set.push_back(id);
		m_current = m_set.empty() ? EntityId{} : m_set.front();
		if (m_onChanged) m_onChanged(m_current);
	}

	bool EditorSelection::IsSelected(EntityId id) const
	{
		return std::find(m_set.begin(), m_set.end(), id) != m_set.end();
	}
}
```

- [ ] **Step 5 : Lancer le test, vérifier le succès**

Run: build + exécuter `editor_selection_tests`
Expected: `[PASS] EditorSelectionTests`

- [ ] **Step 6 : Commit**

```bash
git add src/world_editor/scene/EditorSelection.h src/world_editor/scene/EditorSelection.cpp src/world_editor/tests/EditorSelectionTests.cpp
git commit -m "feat(world_editor): EditorSelection multi-sélection (set + primaire)"
```

---

### Task 2 : Clé stable d'entité (`EntityKey`)

**Files:**
- Create: `src/world_editor/scene/EntityKey.h`
- Create: `src/world_editor/scene/EntityKey.cpp`
- Test: `src/world_editor/tests/EntityKeyTests.cpp`
- Modify: `CMakeLists.txt`

Contexte : `LayersDocument` indexe par `uint64 entityKey`. L'`EntityId.index` n'est pas stable au `Rebuild` ; on dérive une clé du `guid` (layout = string, mesh/dungeon = uint64), préfixée par le `kind` pour éviter les collisions inter-types. FNV-1a 64 bits (concern distinct du hash 32 bits `assetIdHash` existant — on n'y touche pas).

- [ ] **Step 1 : Écrire le test qui échoue** — `src/world_editor/tests/EntityKeyTests.cpp` :

```cpp
/// Tests CPU pour EntityKey (clé stable de calque). Pur, ctest Linux.
#include "src/world_editor/scene/EntityKey.h"
#include <cstdio>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;

	void Test_KeyStableAndDistinct()
	{
		// Déterministe : même entrée -> même clé.
		REQUIRE(MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_001")
			== MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_001"));
		// Guid différent -> clé différente.
		REQUIRE(MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_001")
			!= MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_002"));
		// Même guid, kind différent -> clé différente (préfixe kind).
		REQUIRE(MakeEntityKeyFromString(EntityKind::LayoutInstance, "x")
			!= MakeEntityKeyFromString(EntityKind::MeshInsert, "x"));
		// Variante numérique (mesh/dungeon) : déterministe + séparée par kind.
		REQUIRE(MakeEntityKeyFromGuid(EntityKind::MeshInsert, 42)
			== MakeEntityKeyFromGuid(EntityKind::MeshInsert, 42));
		REQUIRE(MakeEntityKeyFromGuid(EntityKind::MeshInsert, 42)
			!= MakeEntityKeyFromGuid(EntityKind::DungeonPortal, 42));
		// Clé non nulle (0 = "non assigné" côté LayersDocument).
		REQUIRE(MakeEntityKeyFromGuid(EntityKind::MeshInsert, 0) != 0u);
	}
}

int main()
{
	Test_KeyStableAndDistinct();
	if (g_failed == 0) { std::printf("[PASS] EntityKeyTests\n"); return 0; }
	std::printf("[FAIL] EntityKeyTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 2 : Vérifier l'échec** — compilation impossible (`EntityKey.h` absent). Expected: FAIL.

- [ ] **Step 3 : Créer `src/world_editor/scene/EntityKey.h`** :

```cpp
#pragma once

#include "src/world_editor/scene/EditorSelection.h" // EntityKind

#include <cstdint>
#include <string_view>

namespace engine::editor::scene
{
	/// Clé stable `uint64` d'une entité pour `LayersDocument` (assignement par
	/// calque survivant aux `Rebuild`, contrairement à `EntityId.index`).
	/// Dérive un FNV-1a 64 bits du `guid`, mélangé au `kind` pour éviter les
	/// collisions inter-types. Concern distinct du hash 32 bits `assetIdHash`.

	/// Clé pour une entité à guid textuel (LayoutInstance). Jamais nulle.
	uint64_t MakeEntityKeyFromString(EntityKind kind, std::string_view guid);

	/// Clé pour une entité à guid numérique (MeshInsert / DungeonPortal). Jamais nulle.
	uint64_t MakeEntityKeyFromGuid(EntityKind kind, uint64_t guid);
}
```

- [ ] **Step 4 : Créer `src/world_editor/scene/EntityKey.cpp`** :

```cpp
#include "src/world_editor/scene/EntityKey.h"

namespace engine::editor::scene
{
	namespace
	{
		constexpr uint64_t kFnvOffset = 1469598103934665603ull;
		constexpr uint64_t kFnvPrime  = 1099511628211ull;

		uint64_t Fnv1a64(uint64_t seed, const unsigned char* data, size_t n)
		{
			uint64_t h = seed;
			for (size_t i = 0; i < n; ++i) { h ^= data[i]; h *= kFnvPrime; }
			return h;
		}
	}

	uint64_t MakeEntityKeyFromString(EntityKind kind, std::string_view guid)
	{
		uint64_t h = kFnvOffset;
		const unsigned char k = static_cast<unsigned char>(kind);
		h = Fnv1a64(h, &k, 1);
		h = Fnv1a64(h, reinterpret_cast<const unsigned char*>(guid.data()), guid.size());
		return h | 1ull; // jamais 0 (0 = non assigné côté LayersDocument)
	}

	uint64_t MakeEntityKeyFromGuid(EntityKind kind, uint64_t guid)
	{
		unsigned char buf[9];
		buf[0] = static_cast<unsigned char>(kind);
		for (int i = 0; i < 8; ++i) buf[1 + i] = static_cast<unsigned char>((guid >> (i * 8)) & 0xFF);
		return Fnv1a64(kFnvOffset, buf, sizeof(buf)) | 1ull;
	}
}
```

- [ ] **Step 5 : Enregistrer dans `CMakeLists.txt`** — (a) ajouter la source à `engine_core` juste après la ligne `src/world_editor/scene/EditorSceneModel.cpp` (≈ ligne 381) :

```cmake
  src/world_editor/scene/EntityKey.cpp
```

(b) ajouter une cible de test après le bloc `editor_scene_model_tests` (≈ ligne 1373) :

```cmake
# Lot 0 — Tests CPU pour EntityKey (clé stable de calque). Pur, ctest Linux.
add_executable(entity_key_tests src/world_editor/tests/EntityKeyTests.cpp)
target_include_directories(entity_key_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(entity_key_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(entity_key_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME entity_key_tests COMMAND entity_key_tests)
```

- [ ] **Step 6 : Lancer le test, vérifier le succès**

Run: build + `entity_key_tests`
Expected: `[PASS] EntityKeyTests`

- [ ] **Step 7 : Commit**

```bash
git add src/world_editor/scene/EntityKey.h src/world_editor/scene/EntityKey.cpp src/world_editor/tests/EntityKeyTests.cpp CMakeLists.txt
git commit -m "feat(world_editor): EntityKey — clé stable de calque (FNV-1a 64)"
```

---

### Task 3 : `SelectionQuery` (encode/décode + pick + rect/lasso)

**Files:**
- Create: `src/world_editor/scene/SelectionQuery.h`
- Create: `src/world_editor/scene/SelectionQuery.cpp`
- Test: `src/world_editor/tests/SelectionQueryTests.cpp`
- Modify: `CMakeLists.txt`

Contexte : transite les `EntityId` à travers l'API `uint32` de `SelectionTool` (rect/lasso) et fournit le pick « plus proche en X/Z sous un rayon » (clic simple). `EntityKind` max = 5 → 8 bits suffisent ; index sur 24 bits.

- [ ] **Step 1 : Écrire le test qui échoue** — `src/world_editor/tests/SelectionQueryTests.cpp` :

```cpp
/// Tests CPU pour SelectionQuery (encode/décode, pick nearest, rect/lasso).
#include "src/world_editor/scene/SelectionQuery.h"
#include <cstdio>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;
	using namespace engine::editor::world;

	void Test_EncodeRoundTrip()
	{
		const EntityId id{EntityKind::MeshInsert, 1234567};
		const uint32_t enc = EncodeEntityId(id);
		const EntityId dec = DecodeEntityId(enc);
		REQUIRE(dec.kind == EntityKind::MeshInsert);
		REQUIRE(dec.index == 1234567u);
	}

	void Test_PickNearest()
	{
		std::vector<SelectableEntity> pts = {
			{ {EntityKind::LayoutInstance, 0}, 0.f, 0.f },
			{ {EntityKind::LayoutInstance, 1}, 10.f, 0.f },
			{ {EntityKind::LayoutInstance, 2}, 0.f, 10.f },
		};
		// Clic près de (9,0) avec rayon 3 -> index 1.
		auto hit = PickNearest(pts, 9.f, 0.f, 3.f);
		REQUIRE(hit.has_value());
		REQUIRE(hit->index == 1u);
		// Clic loin de tout (rayon 1) -> rien.
		REQUIRE(!PickNearest(pts, 50.f, 50.f, 1.f).has_value());
	}

	void Test_RectAndLasso()
	{
		std::vector<SelectableEntity> pts = {
			{ {EntityKind::LayoutInstance, 0}, 1.f, 1.f },
			{ {EntityKind::LayoutInstance, 1}, 5.f, 5.f },
			{ {EntityKind::LayoutInstance, 2}, 20.f, 20.f },
		};
		SelectionRect rect; rect.minX = 0.f; rect.minZ = 0.f; rect.maxX = 10.f; rect.maxZ = 10.f;
		auto inRect = PickInRect(pts, rect);
		REQUIRE(inRect.size() == 2); // 0 et 1
		// Lasso = même carré 0..10 -> mêmes 2 entités.
		std::vector<std::pair<float, float>> poly = {{0,0},{10,0},{10,10},{0,10}};
		auto inLasso = PickInLasso(pts, poly);
		REQUIRE(inLasso.size() == 2);
	}
}

int main()
{
	Test_EncodeRoundTrip();
	Test_PickNearest();
	Test_RectAndLasso();
	if (g_failed == 0) { std::printf("[PASS] SelectionQueryTests\n"); return 0; }
	std::printf("[FAIL] SelectionQueryTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 2 : Vérifier l'échec** — `SelectionQuery.h` absent. Expected: FAIL compilation.

- [ ] **Step 3 : Créer `src/world_editor/scene/SelectionQuery.h`** :

```cpp
#pragma once

#include "src/world_editor/SelectionTool.h"           // SelectionRect, SelectablePoint
#include "src/world_editor/scene/EditorSelection.h"    // EntityId, EntityKind

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace engine::editor::scene
{
	/// Candidat sélectionnable projeté au sol (X/Z monde).
	struct SelectableEntity
	{
		EntityId id{};
		float    x = 0.0f;
		float    z = 0.0f;
	};

	/// Encode `(kind, index)` sur 32 bits (8 bits kind << 24 | index 24 bits)
	/// pour transiter par l'API uint32 de SelectionTool.
	uint32_t EncodeEntityId(EntityId id);
	EntityId DecodeEntityId(uint32_t encoded);

	/// Entité la plus proche de (x,z) dont la distance ≤ `radius` (monde).
	/// std::nullopt si aucune dans le rayon.
	std::optional<EntityId> PickNearest(const std::vector<SelectableEntity>& pts,
		float x, float z, float radius);

	/// Entités dont la projection X/Z tombe dans `rect` (réutilise SelectInRect).
	std::vector<EntityId> PickInRect(const std::vector<SelectableEntity>& pts,
		engine::editor::world::SelectionRect rect);

	/// Entités dont la projection X/Z tombe dans le polygone lasso (SelectInLasso).
	std::vector<EntityId> PickInLasso(const std::vector<SelectableEntity>& pts,
		const std::vector<std::pair<float, float>>& polygon);
}
```

- [ ] **Step 4 : Créer `src/world_editor/scene/SelectionQuery.cpp`** :

```cpp
#include "src/world_editor/scene/SelectionQuery.h"

namespace engine::editor::scene
{
	using engine::editor::world::SelectablePoint;
	using engine::editor::world::SelectionRect;

	uint32_t EncodeEntityId(EntityId id)
	{
		return (static_cast<uint32_t>(id.kind) << 24) | (id.index & 0x00FFFFFFu);
	}

	EntityId DecodeEntityId(uint32_t encoded)
	{
		EntityId id;
		id.kind  = static_cast<EntityKind>((encoded >> 24) & 0xFFu);
		id.index = encoded & 0x00FFFFFFu;
		return id;
	}

	std::optional<EntityId> PickNearest(const std::vector<SelectableEntity>& pts,
		float x, float z, float radius)
	{
		const float r2 = radius * radius;
		std::optional<EntityId> best;
		float bestD2 = r2;
		for (const auto& p : pts)
		{
			const float dx = p.x - x;
			const float dz = p.z - z;
			const float d2 = dx * dx + dz * dz;
			if (d2 <= bestD2)
			{
				bestD2 = d2;
				best = p.id;
			}
		}
		return best;
	}

	std::vector<EntityId> PickInRect(const std::vector<SelectableEntity>& pts,
		SelectionRect rect)
	{
		std::vector<SelectablePoint> raw;
		raw.reserve(pts.size());
		for (const auto& p : pts)
			raw.push_back(SelectablePoint{ EncodeEntityId(p.id), p.x, p.z });

		std::vector<uint32_t> ids = engine::editor::world::SelectInRect(raw, rect);
		std::vector<EntityId> out;
		out.reserve(ids.size());
		for (uint32_t e : ids) out.push_back(DecodeEntityId(e));
		return out;
	}

	std::vector<EntityId> PickInLasso(const std::vector<SelectableEntity>& pts,
		const std::vector<std::pair<float, float>>& polygon)
	{
		std::vector<SelectablePoint> raw;
		raw.reserve(pts.size());
		for (const auto& p : pts)
			raw.push_back(SelectablePoint{ EncodeEntityId(p.id), p.x, p.z });

		std::vector<uint32_t> ids = engine::editor::world::SelectInLasso(raw, polygon);
		std::vector<EntityId> out;
		out.reserve(ids.size());
		for (uint32_t e : ids) out.push_back(DecodeEntityId(e));
		return out;
	}
}
```

- [ ] **Step 5 : Enregistrer dans `CMakeLists.txt`** — (a) source `engine_core` après `EntityKey.cpp` :

```cmake
  src/world_editor/scene/SelectionQuery.cpp
```

(b) cible de test après `entity_key_tests` :

```cmake
# Lot 0 — Tests CPU pour SelectionQuery (encode/décode, pick, rect/lasso). ctest Linux.
add_executable(selection_query_tests src/world_editor/tests/SelectionQueryTests.cpp)
target_include_directories(selection_query_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(selection_query_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(selection_query_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME selection_query_tests COMMAND selection_query_tests)
```

> Note : `SelectionTool.cpp` est déjà compilé dans `engine_core` (orphelin mais bâti). Vérifier sa présence dans la liste de sources ; si absent, l'ajouter à côté de `SelectionQuery.cpp`.

- [ ] **Step 6 : Lancer le test, vérifier le succès** — `selection_query_tests` → `[PASS]`.

- [ ] **Step 7 : Commit**

```bash
git add src/world_editor/scene/SelectionQuery.h src/world_editor/scene/SelectionQuery.cpp src/world_editor/tests/SelectionQueryTests.cpp CMakeLists.txt
git commit -m "feat(world_editor): SelectionQuery — encode/décode EntityId + pick + rect/lasso"
```

---

### Task 4 : Ops pures de suppression/restauration par index

**Files:**
- Create: `src/world_editor/scene/DeleteEntitiesOps.h` (header-only, templates)
- Test: `src/world_editor/tests/DeleteEntitiesOpsTests.cpp`
- Modify: `CMakeLists.txt`

Contexte : logique générique d'undo exact — supprimer par index (décroissant) en capturant `(index, copie)`, réinsérer (croissant). Réutilisée par les 3 conteneurs (layout/mesh/dungeon).

- [ ] **Step 1 : Écrire le test qui échoue** — `src/world_editor/tests/DeleteEntitiesOpsTests.cpp` :

```cpp
/// Tests CPU pour DeleteEntitiesOps (remove/restore par index, undo exact).
#include "src/world_editor/scene/DeleteEntitiesOps.h"
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;

	void Test_RemoveRestoreExact()
	{
		std::vector<std::string> v = {"a", "b", "c", "d", "e"};
		// Supprime indices {1,3} (b,d). Ordre d'entrée volontairement non trié.
		auto removed = RemoveByIndexDescending(v, {3, 1});
		REQUIRE(v.size() == 3);
		REQUIRE(v[0] == "a"); REQUIRE(v[1] == "c"); REQUIRE(v[2] == "e");
		// Snapshot trié croissant : (1,b),(3,d).
		REQUIRE(removed.size() == 2);
		REQUIRE(removed[0].first == 1u); REQUIRE(removed[0].second == "b");
		REQUIRE(removed[1].first == 3u); REQUIRE(removed[1].second == "d");

		// Restore -> état initial exact.
		RestoreByIndexAscending(v, removed);
		REQUIRE(v.size() == 5);
		REQUIRE(v[0] == "a"); REQUIRE(v[1] == "b"); REQUIRE(v[2] == "c");
		REQUIRE(v[3] == "d"); REQUIRE(v[4] == "e");
	}

	void Test_OutOfRangeIgnored()
	{
		std::vector<int> v = {10, 20};
		auto removed = RemoveByIndexDescending(v, {5, 0}); // 5 hors borne -> ignoré
		REQUIRE(v.size() == 1);
		REQUIRE(v[0] == 20);
		REQUIRE(removed.size() == 1);
		REQUIRE(removed[0].first == 0u);
		REQUIRE(removed[0].second == 10);
	}
}

int main()
{
	Test_RemoveRestoreExact();
	Test_OutOfRangeIgnored();
	if (g_failed == 0) { std::printf("[PASS] DeleteEntitiesOpsTests\n"); return 0; }
	std::printf("[FAIL] DeleteEntitiesOpsTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 2 : Vérifier l'échec** — header absent. Expected: FAIL.

- [ ] **Step 3 : Créer `src/world_editor/scene/DeleteEntitiesOps.h`** :

```cpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace engine::editor::scene
{
	/// Supprime de `vec` les éléments aux `indices` donnés (ordre quelconque,
	/// doublons tolérés). Retire en ordre décroissant pour ne pas invalider les
	/// index restants. Retourne les `(index, copie)` retirés, **triés par index
	/// croissant** (prêt pour `RestoreByIndexAscending`). Index hors borne ignorés.
	template <class T>
	std::vector<std::pair<uint32_t, T>> RemoveByIndexDescending(
		std::vector<T>& vec, std::vector<uint32_t> indices)
	{
		std::sort(indices.begin(), indices.end());
		indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

		std::vector<std::pair<uint32_t, T>> removed;
		removed.reserve(indices.size());
		// Croissant pour le snapshot…
		for (uint32_t idx : indices)
			if (idx < vec.size())
				removed.emplace_back(idx, vec[idx]);
		// …mais on supprime en décroissant.
		for (auto it = indices.rbegin(); it != indices.rend(); ++it)
			if (*it < vec.size())
				vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(*it));
		return removed;
	}

	/// Réinsère les `(index, copie)` (supposés triés croissants) à leur position
	/// d'origine. Reconstruit l'état d'avant `RemoveByIndexDescending` exactement.
	template <class T>
	void RestoreByIndexAscending(std::vector<T>& vec,
		const std::vector<std::pair<uint32_t, T>>& removed)
	{
		for (const auto& [idx, item] : removed)
		{
			const size_t clamped = idx <= vec.size() ? idx : vec.size();
			vec.insert(vec.begin() + static_cast<std::ptrdiff_t>(clamped), item);
		}
	}
}
```

- [ ] **Step 4 : Enregistrer la cible de test dans `CMakeLists.txt`** (header-only ⇒ pas de source `engine_core`) après `selection_query_tests` :

```cmake
# Lot 0 — Tests CPU pour DeleteEntitiesOps (remove/restore par index). ctest Linux.
add_executable(delete_entities_ops_tests src/world_editor/tests/DeleteEntitiesOpsTests.cpp)
target_include_directories(delete_entities_ops_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(delete_entities_ops_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(delete_entities_ops_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME delete_entities_ops_tests COMMAND delete_entities_ops_tests)
```

- [ ] **Step 5 : Lancer le test, vérifier le succès** — `delete_entities_ops_tests` → `[PASS]`.

- [ ] **Step 6 : Commit**

```bash
git add src/world_editor/scene/DeleteEntitiesOps.h src/world_editor/tests/DeleteEntitiesOpsTests.cpp CMakeLists.txt
git commit -m "feat(world_editor): DeleteEntitiesOps — remove/restore par index (undo exact)"
```

---

### Task 5 : `DeleteEntitiesCommand` (ICommand réversible)

**Files:**
- Create: `src/world_editor/scene/DeleteEntitiesCommand.h`
- Create: `src/world_editor/scene/DeleteEntitiesCommand.cpp`
- Test: `src/world_editor/tests/DeleteEntitiesCommandTests.cpp`
- Modify: `CMakeLists.txt`

Contexte : commande générique via 2 callbacks (`RemoveFn`/`RestoreFn`). Le snapshot `DeletedEntities` porte les 3 vecteurs `(index, copie)`. L'Engine fournira les callbacks (qui appellent les ops de la Task 4 sur les docs concrets). Le test fournit des lambdas triviales sur un vecteur local.

- [ ] **Step 1 : Écrire le test qui échoue** — `src/world_editor/tests/DeleteEntitiesCommandTests.cpp` :

```cpp
/// Tests CPU pour DeleteEntitiesCommand (Execute/Undo/Redo via callbacks).
#include "src/world_editor/scene/DeleteEntitiesCommand.h"
#include "src/world_editor/scene/DeleteEntitiesOps.h"
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;

	void Test_ExecuteUndoRedo()
	{
		// Modèle de test : un vecteur de strings simulant layoutInstances.
		std::vector<std::string> model = {"a", "b", "c"};

		std::vector<EntityId> ids = {
			{EntityKind::LayoutInstance, 0}, {EntityKind::LayoutInstance, 2} };

		// Snapshot custom du test : on réutilise le champ générique via lambdas.
		std::vector<std::pair<uint32_t, std::string>> backup;

		auto remove = [&](const std::vector<EntityId>& sel) -> DeletedEntities
		{
			std::vector<uint32_t> idx;
			for (const EntityId& e : sel) idx.push_back(e.index);
			backup = RemoveByIndexDescending(model, idx);
			return DeletedEntities{}; // l'état réel est dans `backup` (capturé)
		};
		auto restore = [&](const DeletedEntities&)
		{
			RestoreByIndexAscending(model, backup);
		};

		DeleteEntitiesCommand cmd(ids, remove, restore);
		REQUIRE(std::string(cmd.GetLabel()) != "");

		cmd.Execute();
		REQUIRE(model.size() == 1);
		REQUIRE(model[0] == "b");

		cmd.Undo();
		REQUIRE(model.size() == 3);
		REQUIRE(model[0] == "a"); REQUIRE(model[1] == "b"); REQUIRE(model[2] == "c");

		// Redo = Execute à nouveau (indices valides car Undo a tout restitué).
		cmd.Execute();
		REQUIRE(model.size() == 1);
		REQUIRE(model[0] == "b");
	}
}

int main()
{
	Test_ExecuteUndoRedo();
	if (g_failed == 0) { std::printf("[PASS] DeleteEntitiesCommandTests\n"); return 0; }
	std::printf("[FAIL] DeleteEntitiesCommandTests: %d failure(s)\n", g_failed);
	return 1;
}
```

- [ ] **Step 2 : Vérifier l'échec** — header absent. Expected: FAIL.

- [ ] **Step 3 : Créer `src/world_editor/scene/DeleteEntitiesCommand.h`** :

```cpp
#pragma once

#include "src/world_editor/core/CommandStack.h"        // ICommand
#include "src/world_editor/scene/EditorSelection.h"     // EntityId
#include "src/world_editor/ui/WorldMapEditDocument.h"   // WorldMapEditLayoutInstance
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace engine::editor::scene
{
	/// Snapshot des entités retirées (index d'origine + copie) par type, pour
	/// l'undo exact. Rempli par le `RemoveFn`, consommé par le `RestoreFn`.
	struct DeletedEntities
	{
		std::vector<std::pair<uint32_t, engine::editor::WorldMapEditLayoutInstance>>     layout;
		std::vector<std::pair<uint32_t, engine::editor::world::volumes::MeshInsertInstance>>          mesh;
		std::vector<std::pair<uint32_t, engine::editor::world::volumes::dungeons::DungeonPortalInstance>> dungeon;
	};

	/// Commande réversible de suppression d'un ensemble d'entités sélectionnées
	/// (modèle vivant : layoutInstances + mesh inserts + dungeon portals).
	///
	/// Découplée des documents concrets via deux callbacks fournis par l'Engine :
	/// `RemoveFn` retire les entités et renvoie le snapshot ; `RestoreFn` les
	/// réinsère depuis le snapshot. Implémente le contrat ICommand undo/redo.
	///
	/// Limite assumée (cf. spec §4.4) : `EntityId.index` n'est pas stable après
	/// édition structurelle ; la commande suppose un undo/redo linéaire immédiat
	/// (même compromis que SetEntityTransformCommand).
	class DeleteEntitiesCommand : public engine::editor::world::ICommand
	{
	public:
		using RemoveFn  = std::function<DeletedEntities(const std::vector<EntityId>&)>;
		using RestoreFn = std::function<void(const DeletedEntities&)>;

		DeleteEntitiesCommand(std::vector<EntityId> ids, RemoveFn remove, RestoreFn restore)
			: m_ids(std::move(ids)), m_remove(std::move(remove)), m_restore(std::move(restore)) {}

		const char* GetLabel() const override { return "Supprimer la sélection"; }
		size_t GetMemoryFootprint() const override;
		void Execute() override;
		void Undo() override;

	private:
		std::vector<EntityId> m_ids;
		RemoveFn  m_remove;
		RestoreFn m_restore;
		DeletedEntities m_snapshot;
	};
}
```

- [ ] **Step 4 : Créer `src/world_editor/scene/DeleteEntitiesCommand.cpp`** :

```cpp
#include "src/world_editor/scene/DeleteEntitiesCommand.h"

namespace engine::editor::scene
{
	void DeleteEntitiesCommand::Execute()
	{
		if (m_remove) m_snapshot = m_remove(m_ids);
	}

	void DeleteEntitiesCommand::Undo()
	{
		if (m_restore) m_restore(m_snapshot);
	}

	size_t DeleteEntitiesCommand::GetMemoryFootprint() const
	{
		return sizeof(DeleteEntitiesCommand)
			+ m_ids.capacity() * sizeof(EntityId)
			+ m_snapshot.layout.capacity()  * sizeof(m_snapshot.layout[0])
			+ m_snapshot.mesh.capacity()    * sizeof(m_snapshot.mesh[0])
			+ m_snapshot.dungeon.capacity() * sizeof(m_snapshot.dungeon[0]);
	}
}
```

> Si `sizeof(m_snapshot.layout[0])` ne compile pas sur un vecteur vide (il s'agit du type d'élément, pas d'un déréférencement — valide en C++), remplacer par `sizeof(std::pair<uint32_t, engine::editor::WorldMapEditLayoutInstance>)` etc.

- [ ] **Step 5 : Enregistrer dans `CMakeLists.txt`** — (a) source `engine_core` après `SelectionQuery.cpp` :

```cmake
  src/world_editor/scene/DeleteEntitiesCommand.cpp
```

(b) cible de test après `delete_entities_ops_tests` :

```cmake
# Lot 0 — Tests CPU pour DeleteEntitiesCommand (Execute/Undo/Redo). ctest Linux.
add_executable(delete_entities_command_tests src/world_editor/tests/DeleteEntitiesCommandTests.cpp)
target_include_directories(delete_entities_command_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(delete_entities_command_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(delete_entities_command_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME delete_entities_command_tests COMMAND delete_entities_command_tests)
```

- [ ] **Step 6 : Lancer le test, vérifier le succès** — `delete_entities_command_tests` → `[PASS]`.

- [ ] **Step 7 : Commit**

```bash
git add src/world_editor/scene/DeleteEntitiesCommand.h src/world_editor/scene/DeleteEntitiesCommand.cpp src/world_editor/tests/DeleteEntitiesCommandTests.cpp CMakeLists.txt
git commit -m "feat(world_editor): DeleteEntitiesCommand — suppression réversible (callbacks)"
```

---

## Phase 2 — Shell & UI (gardée `_WIN32`, vérifiée au build)

### Task 6 : `ActiveTool::Select` + toolbar + icône

**Files:**
- Modify: `src/world_editor/core/WorldEditorShell.h:42-60`
- Modify: `src/world_editor/ui/EditorToolbar.cpp:14-30`
- Modify: `src/world_editor/ui/ToolbarIconAtlas.cpp`

- [ ] **Step 1 : Ajouter l'entrée enum** — dans `WorldEditorShell.h`, à la fin de l'enum `ActiveTool` (après `DungeonPortal = 15,`), ajouter :

```cpp
		Select              = 16,  // Lot 0 — sélection (clic + marquee), raccourci Q
```

- [ ] **Step 2 : Ajouter à la toolbar** — dans `EditorToolbar.cpp`, à la fin de l'initialisation de `m_orderedTools` (après `ActiveTool::DungeonPortal,`), ajouter :

```cpp
			ActiveTool::Select,             // Lot 0 — sélection / suppression
```

- [ ] **Step 3 : Ajouter le style d'icône** — dans `ToolbarIconAtlas.cpp`, repérer le `switch (tool)` de `Get(...)` et ajouter un `case` (avant le `default`) :

```cpp
		case ActiveTool::Select:
			return ToolIconStyle{ 0xFF3A6EA5u, "S", "Sélection (clic / rectangle) — Suppr pour supprimer", true };
```

> Adapter l'ordre des champs à la définition de `ToolIconStyle` (`bgColorArgb`, `letter`, `tooltipFr`, `enabled`). Vérifier la forme exacte des `case` voisins (certains utilisent l'agrégat `{...}`, d'autres l'affectation champ par champ) et **suivre le style existant**.

- [ ] **Step 4 : Build, vérifier la compilation** — `cmake --build build --target world_editor_app` ; pas d'erreur ; l'outil « S » apparaît dans la toolbar.

- [ ] **Step 5 : Commit**

```bash
git add src/world_editor/core/WorldEditorShell.h src/world_editor/ui/EditorToolbar.cpp src/world_editor/ui/ToolbarIconAtlas.cpp
git commit -m "feat(world_editor): outil Select (enum + toolbar + icône)"
```

---

### Task 7 : `LayersDocument` + résolveur de clé sur le shell

**Files:**
- Modify: `src/world_editor/core/WorldEditorShell.h`
- Modify: `src/world_editor/core/WorldEditorShell.cpp`

Contexte : le shell héberge le `LayersDocument` (source d'autorité runtime des calques) et un résolveur `EntityId → entityKey` installé par l'Engine (qui seul connaît les guids des docs).

- [ ] **Step 1 : Inclure + déclarer les membres** — dans `WorldEditorShell.h`, ajouter en tête (avec les autres includes éditeur) :

```cpp
#include "src/world_editor/LayersDocument.h"
#include "src/world_editor/scene/EditorSelection.h"
```

(le second est déjà tiré indirectement ; sans danger). Puis dans la section publique, après les accesseurs de sélection (`GetSelection()`), ajouter :

```cpp
		/// Lot 0 — Document des 16 calques (visibilité / verrou / couleur +
		/// assignement par entityKey). Source d'autorité runtime ; non persisté
		/// (cf. spec : persistance d'un layerIndex différée).
		LayersDocument&       MutableLayersDocument()       { return m_layersDoc; }
		const LayersDocument& GetLayersDocument()     const { return m_layersDoc; }

		/// Lot 0 — Résolveur EntityId -> entityKey (clé stable de calque),
		/// installé par l'Engine (capture les documents concrets pour lire le
		/// guid). Retourne 0 si non installé ou entité inconnue.
		using EntityKeyResolver = std::function<uint64_t(engine::editor::scene::EntityId)>;
		void SetEntityKeyResolver(EntityKeyResolver r) { m_entityKeyResolver = std::move(r); }
		uint64_t EntityKeyFor(engine::editor::scene::EntityId id) const
		{
			return m_entityKeyResolver ? m_entityKeyResolver(id) : 0ull;
		}
```

Et en section privée, près des autres documents :

```cpp
		LayersDocument m_layersDoc;            // Lot 0
		EntityKeyResolver m_entityKeyResolver; // Lot 0
```

- [ ] **Step 2 : Build, vérifier la compilation** — `world_editor_app` compile (les accesseurs ne sont pas encore consommés).

- [ ] **Step 3 : Commit**

```bash
git add src/world_editor/core/WorldEditorShell.h src/world_editor/core/WorldEditorShell.cpp
git commit -m "feat(world_editor): shell héberge LayersDocument + résolveur de clé d'entité"
```

---

### Task 8 : Panneau `LayersPanel`

**Files:**
- Create: `src/world_editor/panels/LayersPanel.h`
- Create: `src/world_editor/panels/LayersPanel.cpp`
- Modify: `src/world_editor/core/WorldEditorShell.cpp` (Init : enregistrement + SetShell)
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Créer `src/world_editor/panels/LayersPanel.h`** :

```cpp
#pragma once
#include "src/world_editor/core/IPanel.h"

namespace engine::editor::world
{
	class WorldEditorShell;
}

namespace engine::editor::world::panels
{
	/// Panneau des 16 calques (Lot 0). Renommage, visibilité, verrou, couleur,
	/// et « assigner la sélection au calque ». Lit/écrit le `LayersDocument` et
	/// la sélection via le shell injecté. Rendu ImGui gardé `_WIN32`.
	class LayersPanel final : public IPanel
	{
	public:
		const char* GetName() const override { return "Layers"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Injecte le shell (durée de vie garantie : le shell possède le panneau).
		void SetShell(WorldEditorShell* shell) { m_shell = shell; }

	private:
		bool m_visible = true;
		WorldEditorShell* m_shell = nullptr;
		int m_assignTargetLayer = 0; ///< Calque cible du bouton « assigner ».
	};
}
```

- [ ] **Step 2 : Créer `src/world_editor/panels/LayersPanel.cpp`** :

```cpp
#include "src/world_editor/panels/LayersPanel.h"

#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/LayersDocument.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	void LayersPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Layers", &m_visible))
		{
			if (m_shell == nullptr)
			{
				ImGui::TextDisabled("Layers — shell non lié.");
				ImGui::End();
				return;
			}

			LayersDocument& doc = m_shell->MutableLayersDocument();

			// Bouton d'assignement de la sélection au calque cible.
			ImGui::SliderInt("Calque cible", &m_assignTargetLayer, 0, kLayerCount - 1);
			if (ImGui::Button("Assigner la sélection à ce calque"))
			{
				const auto& sel = m_shell->GetSelection().SelectedSet();
				for (const engine::editor::scene::EntityId& id : sel)
				{
					const uint64_t key = m_shell->EntityKeyFor(id);
					if (key != 0ull)
						doc.AssignEntity(key, static_cast<uint8_t>(m_assignTargetLayer));
				}
			}
			ImGui::Separator();

			// Liste des 16 calques : nom, visibilité, verrou, couleur.
			for (uint8_t i = 0; i < kLayerCount; ++i)
			{
				Layer& layer = doc.LayerAt(i);
				ImGui::PushID(static_cast<int>(i));

				bool visible = layer.visible;
				if (ImGui::Checkbox("##vis", &visible)) doc.SetVisible(i, visible);
				ImGui::SameLine();

				bool locked = layer.locked;
				if (ImGui::Checkbox("##lock", &locked)) doc.SetLocked(i, locked);
				ImGui::SameLine();

				char buf[64];
				std::snprintf(buf, sizeof(buf), "%s", layer.name.c_str());
				if (ImGui::InputText("##name", buf, sizeof(buf)))
					doc.SetLayerName(i, buf);

				ImGui::SameLine();
				float col[4] = {
					((layer.overlayColorRgba >> 24) & 0xFF) / 255.0f,
					((layer.overlayColorRgba >> 16) & 0xFF) / 255.0f,
					((layer.overlayColorRgba >>  8) & 0xFF) / 255.0f,
					( layer.overlayColorRgba        & 0xFF) / 255.0f };
				if (ImGui::ColorEdit4("##col", col, ImGuiColorEditFlags_NoInputs))
				{
					layer.overlayColorRgba =
						(static_cast<uint32_t>(col[0] * 255.0f) << 24) |
						(static_cast<uint32_t>(col[1] * 255.0f) << 16) |
						(static_cast<uint32_t>(col[2] * 255.0f) <<  8) |
						(static_cast<uint32_t>(col[3] * 255.0f));
				}

				ImGui::PopID();
			}
		}
		ImGui::End();
#endif
	}
}
```

> `#include <cstdio>` est tiré via les en-têtes ImGui ; si `std::snprintf` manque, ajouter `#include <cstdio>` en tête.

- [ ] **Step 3 : Enregistrer le panneau dans `WorldEditorShell.cpp::Init`** — après l'`emplace_back` du `ToolPropertiesPanel` (ligne 94) ou en fin de liste, ajouter (et inclure `#include "src/world_editor/panels/LayersPanel.h"` en tête du `.cpp`) :

```cpp
		auto layersPanel = std::make_unique<panels::LayersPanel>();
		layersPanel->SetShell(this);
		m_panels.emplace_back(std::move(layersPanel));
```

> Ne PAS réutiliser l'indexation fixe `m_panels[5]` ; on capture le pointeur localement avant le `move`. Le `SetShell(this)` du `ToolPropertiesPanel` existant (ligne 279) reste inchangé.

- [ ] **Step 4 : Enregistrer la source dans `CMakeLists.txt`** — dans `engine_core`, près des autres panneaux (`src/world_editor/panels/...`) :

```cmake
  src/world_editor/panels/LayersPanel.cpp
```

- [ ] **Step 5 : Build + vérification visuelle** — lancer `lcdlln_world_editor.exe`, vérifier qu'un panneau « Layers » s'affiche avec 16 lignes (cases visibilité/verrou, nom éditable, couleur).

- [ ] **Step 6 : Commit**

```bash
git add src/world_editor/panels/LayersPanel.h src/world_editor/panels/LayersPanel.cpp src/world_editor/core/WorldEditorShell.cpp CMakeLists.txt
git commit -m "feat(world_editor): panneau Layers (16 calques + assignement sélection)"
```

---

### Task 9 : Outliner — Ctrl+clic multi-sélection

**Files:**
- Modify: `src/world_editor/panels/OutlinerPanel.h`
- Modify: `src/world_editor/panels/OutlinerPanel.cpp`

Contexte : l'Outliner sélectionne en mono (`m_selection->Select`). On ajoute le Ctrl+clic (toggle) et on surligne toute la multi-sélection (`IsSelected`). *(Périmètre : les toggles visibilité/verrou par calque vivent dans le `LayersPanel` — Task 8 ; la pastille de couleur de calque par ligne d'Outliner est différée pour garder la ligne simple. Le shell n'est donc PAS nécessaire ici.)*

- [ ] **Step 1 : (aucun changement d'en-tête requis)** — `OutlinerPanel.h` reste inchangé : `m_selection` (déjà présent) suffit pour `IsSelected`/`ToggleInSelection`. Passer directement au Step 2.

- [ ] **Step 2 : Gérer Ctrl+clic dans `RenderKindGroup`** — dans `OutlinerPanel.cpp`, remplacer le bloc `if (ImGui::Selectable(...))` par :

```cpp
				const bool selected = (m_selection != nullptr) && m_selection->IsSelected(e.id);
				ImGui::PushID(static_cast<int>(e.id.index)
					| (static_cast<int>(kind) << 24));
				if (ImGui::Selectable(e.label.c_str(), selected))
				{
					if (m_selection != nullptr)
					{
						if (ImGui::GetIO().KeyCtrl)
							m_selection->ToggleInSelection(e.id);
						else
							m_selection->Select(e.id);
					}
				}
				ImGui::PopID();
```

> Note : on remplace `m_selection->Current() == e.id` par `IsSelected(e.id)` pour surligner toute la multi-sélection.

- [ ] **Step 3 : Build + vérification** — lancer l'éditeur ; Ctrl+clic sur plusieurs entités de l'Outliner les surligne toutes ; clic simple en sélectionne une seule.

- [ ] **Step 4 : Commit**

```bash
git add src/world_editor/panels/OutlinerPanel.cpp
git commit -m "feat(world_editor): Outliner — Ctrl+clic multi-sélection"
```

---

### Task 10 : `ToolPropertiesPanel` — bloc `Select`

**Files:**
- Modify: `src/world_editor/panels/ToolPropertiesPanel.cpp`

Contexte : quand `ActiveTool::Select`, afficher le rayon de pick, le mode, et le nombre d'entités sélectionnées. Le rayon de pick est stocké comme un état simple ; on le porte sur le shell pour que l'Engine le lise (Task 12). Pour le Lot 0, on l'expose via une variable membre du shell.

- [ ] **Step 1 : Ajouter un champ « rayon de pick » au shell** — dans `WorldEditorShell.h`, section publique :

```cpp
		/// Lot 0 — Rayon de pick de l'outil Select (mètres monde), lu par l'Engine.
		float  GetSelectPickRadiusMeters() const { return m_selectPickRadiusM; }
		void   SetSelectPickRadiusMeters(float r) { m_selectPickRadiusM = r; }
```

section privée :

```cpp
		float m_selectPickRadiusM = 2.0f; // Lot 0
```

- [ ] **Step 2 : Ajouter le bloc UI** — dans `ToolPropertiesPanel.cpp`, repérer dans `Render()` la chaîne de `if (tool == ActiveTool::...)`. Ajouter un bloc (avant le placeholder par défaut) :

```cpp
			else if (tool == ActiveTool::Select)
			{
				ImGui::TextUnformatted("Outil : Sélection");
				ImGui::Separator();
				float r = m_shell->GetSelectPickRadiusMeters();
				if (ImGui::SliderFloat("Rayon de pick (m)", &r, 0.25f, 20.0f, "%.2f"))
					m_shell->SetSelectPickRadiusMeters(r);
				ImGui::TextDisabled("Clic : sélectionne la plus proche.");
				ImGui::TextDisabled("Glisser : rectangle (multi). Suppr : supprime.");
				ImGui::Text("Sélection : %d entité(s)",
					static_cast<int>(m_shell->GetSelection().SelectedSet().size()));
			}
```

> Adapter `m_shell` / `tool` au code réel : vérifier comment `Render()` obtient `tool` (probablement `m_shell->GetActiveTool()`) et l'accès `m_shell`. S'aligner sur les blocs voisins (ex. `RenderCaveParams`).

- [ ] **Step 3 : Build + vérification** — sélectionner l'outil « S » ; le panneau Tool Properties affiche le slider rayon + le compteur de sélection.

- [ ] **Step 4 : Commit**

```bash
git add src/world_editor/core/WorldEditorShell.h src/world_editor/panels/ToolPropertiesPanel.cpp
git commit -m "feat(world_editor): Tool Properties — bloc outil Select (rayon + compteur)"
```

---

## Phase 3 — Intégration Engine (vérifiée en application)

### Task 11 : `Key::Delete` dans l'input

**Files:**
- Modify: `src/shared/platform/Input.h:45-62`

- [ ] **Step 1 : Ajouter la touche** — dans l'enum `Key`, ajouter (à côté de `Escape = 0x1B`) :

```cpp
		Delete = 0x2E, ///< VK_DELETE — suppression de la sélection (éditeur).
```

- [ ] **Step 2 : Build, vérifier la compilation** — `m_down`/`m_pressed` sont indexés par le code VK (≤ 256) ; `WasPressed(Key::Delete)` fonctionne sans autre changement.

- [ ] **Step 3 : Commit**

```bash
git add src/shared/platform/Input.h
git commit -m "feat(platform): touche Delete (VK_DELETE) dans l'enum Key"
```

---

### Task 12 : Engine — candidats sélectionnables + routage clic/marquee `Select`

**Files:**
- Modify: `src/client/app/Engine.cpp` (bloc outils ≈ 9904-9965 ; bloc SceneModel/writers ≈ 8906-8963)

Contexte : on installe le résolveur de clé (comme `SetTransformWriter`), on construit la liste des candidats (positions X/Z, filtrés calques cachés/verrouillés), et on route les clics/drag vers `EditorSelection`. Variables locales disponibles dans le bloc outils : `out.camera`, `vw`, `vh`, `pickX`, `pickZ`, `terrainPick`, `freeClick`, `m_input`.

- [ ] **Step 1 : Installer le résolveur de clé d'entité** — dans le bloc `if (m_worldEditorSession)` (≈ 8906), après le `SetTransformWriter(...)`, ajouter :

```cpp
				m_worldEditorShell->SetEntityKeyResolver(
					[this](engine::editor::scene::EntityId id) -> uint64_t
					{
						using K = engine::editor::scene::EntityKind;
						if (id.kind == K::LayoutInstance && m_worldEditorSession)
						{
							const auto& insts = m_worldEditorSession->Doc().layoutInstances;
							if (id.index < insts.size())
								return engine::editor::scene::MakeEntityKeyFromString(
									K::LayoutInstance, insts[id.index].guid);
						}
						else if (id.kind == K::MeshInsert && m_worldEditorShell)
						{
							const auto& all = m_worldEditorShell->GetMeshInsertDocument().All();
							if (id.index < all.size())
								return engine::editor::scene::MakeEntityKeyFromGuid(
									K::MeshInsert, all[id.index].guid);
						}
						else if (id.kind == K::DungeonPortal && m_worldEditorShell)
						{
							const auto& all = m_worldEditorShell->GetDungeonPortalDocument().All();
							if (id.index < all.size())
								return engine::editor::scene::MakeEntityKeyFromGuid(
									K::DungeonPortal, all[id.index].guid);
						}
						return 0ull;
					});
```

Ajouter en tête de `Engine.cpp` (avec les autres includes éditeur) :

```cpp
#include "src/world_editor/scene/EntityKey.h"
#include "src/world_editor/scene/SelectionQuery.h"
```

> Vérifier le nom exact du champ guid : `MeshInsertInstance::guid` et `DungeonPortalInstance::guid` (uint64). Ajuster si le membre porte un autre nom (`guid`/`instanceGuid`).

- [ ] **Step 2 : Construire un helper de candidats** — dans la même TU, ajouter une lambda/fonction locale (juste avant le bloc outils ≈ 9904) qui bâtit les candidats filtrés par calque :

```cpp
			// Lot 0 — candidats sélectionnables (X/Z), calques cachés/verrouillés exclus.
			auto buildSelectables = [this]() -> std::vector<engine::editor::scene::SelectableEntity>
			{
				using K = engine::editor::scene::EntityKind;
				std::vector<engine::editor::scene::SelectableEntity> out;
				if (!m_worldEditorSession || !m_worldEditorShell) return out;
				const auto& layers = m_worldEditorShell->GetLayersDocument();
				auto pushIf = [&](engine::editor::scene::EntityId id, float x, float z)
				{
					const uint64_t key = m_worldEditorShell->EntityKeyFor(id);
					if (key != 0ull && (!layers.IsEntityVisible(key) || layers.IsEntityLocked(key)))
						return; // caché ou verrouillé -> non sélectionnable
					out.push_back({ id, x, z });
				};
				const auto& insts = m_worldEditorSession->Doc().layoutInstances;
				for (uint32_t i = 0; i < insts.size(); ++i)
					pushIf({ K::LayoutInstance, i },
						static_cast<float>(insts[i].worldX), static_cast<float>(insts[i].worldZ));
				const auto& mesh = m_worldEditorShell->GetMeshInsertDocument().All();
				for (uint32_t i = 0; i < mesh.size(); ++i)
					pushIf({ K::MeshInsert, i }, mesh[i].worldPosition.x, mesh[i].worldPosition.z);
				const auto& dung = m_worldEditorShell->GetDungeonPortalDocument().All();
				for (uint32_t i = 0; i < dung.size(); ++i)
					pushIf({ K::DungeonPortal, i }, dung[i].worldPosition.x, dung[i].worldPosition.z);
				return out;
			};
```

> Vérifier les noms : `MeshInsertInstance::worldPosition` (Vec3) et `DungeonPortalInstance::worldPosition` — confirmés par `SetTransformWriter` (lignes 8944/8957). `layoutInstances[i].worldX/worldZ` sont des `double`.

- [ ] **Step 3 : Router le clic simple** — dans la chaîne d'outils, après la branche `ValleyChain` (≈ ligne 9964), ajouter :

```cpp
				else if (tool == engine::editor::world::ActiveTool::Select)
				{
					modernEditActive = true;
					if (freeClick && terrainPick
						&& m_input.WasMousePressed(engine::platform::MouseButton::Left))
					{
						const auto cands = buildSelectables();
						const float radius = m_worldEditorShell->GetSelectPickRadiusMeters();
						auto hit = engine::editor::scene::PickNearest(cands, pickX, pickZ, radius);
						if (hit.has_value())
							m_worldEditorShell->MutableSelection().Select(*hit);
						else
							m_worldEditorShell->MutableSelection().Clear();
					}
				}
```

- [ ] **Step 4 : Build + vérification** — sélectionner l'outil S ; cliquer près d'un prop posé → il devient sélectionné (surligné dans l'Outliner + compteur Tool Properties = 1) ; cliquer dans le vide → désélection.

- [ ] **Step 5 : Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(world_editor): Engine — résolveur de clé + pick simple outil Select"
```

---

### Task 13 : Engine — marquee rectangle (multi-sélection au drag)

**Files:**
- Modify: `src/client/app/Engine.cpp` (branche `ActiveTool::Select`)

Contexte : on capte le début du drag (point sol au press) et la fin (point sol au release), on construit un `SelectionRect` monde, on appelle `PickInRect`. État de drag stocké en membres `Engine` (ajoutés). Shift = additif au set.

- [ ] **Step 1 : Ajouter l'état de drag à `Engine.h`** — dans la classe `Engine`, près des autres membres éditeur :

```cpp
	bool  m_editorMarqueeActive = false;
	float m_editorMarqueeStartX = 0.0f;
	float m_editorMarqueeStartZ = 0.0f;
```

- [ ] **Step 2 : Étendre la branche `Select`** — remplacer le corps de la branche `ActiveTool::Select` (Task 12 Step 3) par la version gérant clic ET marquee :

```cpp
				else if (tool == engine::editor::world::ActiveTool::Select)
				{
					modernEditActive = true;
					if (freeClick && terrainPick)
					{
						const bool shift = m_input.IsDown(engine::platform::Key::Shift);
						if (m_input.WasMousePressed(engine::platform::MouseButton::Left))
						{
							m_editorMarqueeActive = true;
							m_editorMarqueeStartX = pickX;
							m_editorMarqueeStartZ = pickZ;
						}
						if (m_editorMarqueeActive
							&& m_input.WasMouseReleased(engine::platform::MouseButton::Left))
						{
							m_editorMarqueeActive = false;
							const float dx = pickX - m_editorMarqueeStartX;
							const float dz = pickZ - m_editorMarqueeStartZ;
							const auto cands = buildSelectables();
							const float radius = m_worldEditorShell->GetSelectPickRadiusMeters();
							// Drag négligeable -> clic simple ; sinon -> rectangle.
							if (dx * dx + dz * dz < radius * radius)
							{
								auto hit = engine::editor::scene::PickNearest(cands, pickX, pickZ, radius);
								if (hit.has_value()) m_worldEditorShell->MutableSelection().Select(*hit);
								else if (!shift) m_worldEditorShell->MutableSelection().Clear();
							}
							else
							{
								engine::editor::world::SelectionRect rect;
								rect.minX = m_editorMarqueeStartX; rect.maxX = pickX;
								rect.minZ = m_editorMarqueeStartZ; rect.maxZ = pickZ;
								auto ids = engine::editor::scene::PickInRect(cands, rect);
								if (shift)
									for (const auto& id : ids) m_worldEditorShell->MutableSelection().ToggleInSelection(id);
								else
									m_worldEditorShell->MutableSelection().SelectMany(ids);
							}
						}
					}
				}
```

> `SelectionRect::Normalize()` est appelé par `SelectInRect` ; on n'a donc pas à trier min/max ici.

- [ ] **Step 3 : Build + vérification** — outil S ; glisser un rectangle au sol par-dessus plusieurs props → tous sélectionnés (compteur > 1) ; Shift+glisser → ajoute/retire au set existant.

- [ ] **Step 4 : Commit**

```bash
git add src/client/app/Engine.cpp src/client/app/Engine.h
git commit -m "feat(world_editor): Engine — marquee rectangle multi-sélection (outil Select)"
```

---

### Task 14 : Engine — suppression réversible (touche Suppr) + `Mutable()` docs

**Files:**
- Modify: `src/world_editor/volumes/MeshInsertDocument.h`
- Modify: `src/world_editor/volumes/dungeons/DungeonPortalDocument.h`
- Modify: `src/client/app/Engine.cpp` (forward Suppr + build/push commande)

Contexte : la touche Suppr (forwardée comme les autres raccourcis ≈ Engine.cpp:7720) construit une `DeleteEntitiesCommand` dont les callbacks utilisent `RemoveByIndexDescending`/`RestoreByIndexAscending` sur les 3 conteneurs. Les docs mesh/dungeon exposent un accès mutable au vecteur (comme `PlacementDocument::Mutable`).

- [ ] **Step 1 : Ajouter `Mutable()` aux deux docs** — dans `MeshInsertDocument.h`, après `All()` :

```cpp
		/// Lot 0 — Accès mutable au vecteur (suppression réversible par index).
		/// Le caller est responsable de `MarkDirty()` après mutation.
		std::vector<MeshInsertInstance>& Mutable() { return m_instances; }
```

Dans `DungeonPortalDocument.h`, après `All()` :

```cpp
		/// Lot 0 — Accès mutable au vecteur (suppression réversible par index).
		std::vector<DungeonPortalInstance>& Mutable() { return m_instances; }
		void MarkDirty() noexcept { m_dirty = true; }
```

(MeshInsertDocument a déjà `MarkDirty()`.)

- [ ] **Step 2 : Forwarder la touche Suppr** — dans `Engine.cpp` (≈ 7720, bloc `if (m_worldEditorShell && IsInitialized())` qui gère Z/Y), ajouter, mais **uniquement** quand l'outil Select est actif et qu'il y a une sélection, construire et pousser la commande. Préférable : un bloc dédié juste après le dispatch Z/Y :

```cpp
				if (m_input.WasPressed(engine::platform::Key::Delete)
					&& m_worldEditorShell->GetActiveTool() == engine::editor::world::ActiveTool::Select
					&& m_worldEditorSession)
				{
					const auto& set = m_worldEditorShell->GetSelection().SelectedSet();
					if (!set.empty())
					{
						std::vector<engine::editor::scene::EntityId> ids(set.begin(), set.end());

						auto remove = [this](const std::vector<engine::editor::scene::EntityId>& sel)
							-> engine::editor::scene::DeletedEntities
						{
							using K = engine::editor::scene::EntityKind;
							std::vector<uint32_t> layoutIdx, meshIdx, dungIdx;
							const auto& layers = m_worldEditorShell->GetLayersDocument();
							for (const auto& id : sel)
							{
								const uint64_t key = m_worldEditorShell->EntityKeyFor(id);
								if (key != 0ull && layers.IsEntityLocked(key)) continue; // calque verrouillé
								if (id.kind == K::LayoutInstance) layoutIdx.push_back(id.index);
								else if (id.kind == K::MeshInsert)  meshIdx.push_back(id.index);
								else if (id.kind == K::DungeonPortal) dungIdx.push_back(id.index);
							}
							engine::editor::scene::DeletedEntities snap;
							snap.layout = engine::editor::scene::RemoveByIndexDescending(
								m_worldEditorSession->MutableDoc().layoutInstances, layoutIdx);
							snap.mesh = engine::editor::scene::RemoveByIndexDescending(
								m_worldEditorShell->MutableMeshInsertDocument().Mutable(), meshIdx);
							snap.dungeon = engine::editor::scene::RemoveByIndexDescending(
								m_worldEditorShell->MutableDungeonPortalDocument().Mutable(), dungIdx);
							m_worldEditorShell->MutableMeshInsertDocument().MarkDirty();
							m_worldEditorShell->MutableDungeonPortalDocument().MarkDirty();
							return snap;
						};
						auto restore = [this](const engine::editor::scene::DeletedEntities& snap)
						{
							engine::editor::scene::RestoreByIndexAscending(
								m_worldEditorSession->MutableDoc().layoutInstances, snap.layout);
							engine::editor::scene::RestoreByIndexAscending(
								m_worldEditorShell->MutableMeshInsertDocument().Mutable(), snap.mesh);
							engine::editor::scene::RestoreByIndexAscending(
								m_worldEditorShell->MutableDungeonPortalDocument().Mutable(), snap.dungeon);
							m_worldEditorShell->MutableMeshInsertDocument().MarkDirty();
							m_worldEditorShell->MutableDungeonPortalDocument().MarkDirty();
						};

						m_worldEditorShell->MutableCommandStack().Push(
							std::make_unique<engine::editor::scene::DeleteEntitiesCommand>(
								std::move(ids), std::move(remove), std::move(restore)));
						m_worldEditorShell->MutableSelection().Clear();
						m_worldEditorShell->MarkDirty("delete selection");
					}
				}
```

Ajouter l'include en tête de `Engine.cpp` :

```cpp
#include "src/world_editor/scene/DeleteEntitiesCommand.h"
#include "src/world_editor/scene/DeleteEntitiesOps.h"
```

> Lifetime : la commande capture `[this]` (Engine), comme `SetTransformWriter` ; pas de référence directe aux vecteurs ⇒ pas de dangling si la session est recréée tant que la pile est vidée à chaque nouvelle carte. **Vérifier** que `CommandStack::Clear()` est appelé au chargement/à la création d'une carte ; sinon ajouter l'appel (hors périmètre si déjà présent).

- [ ] **Step 3 : Build + vérification** — outil S ; sélectionner un/plusieurs props ; Suppr → disparaissent (Outliner se met à jour) ; Ctrl+Z → réapparaissent à leur place exacte ; Ctrl+Y → re-supprimés. Une entité sur un calque verrouillé n'est pas supprimée.

- [ ] **Step 4 : Commit**

```bash
git add src/world_editor/volumes/MeshInsertDocument.h src/world_editor/volumes/dungeons/DungeonPortalDocument.h src/client/app/Engine.cpp
git commit -m "feat(world_editor): Engine — suppression réversible de la sélection (Suppr)"
```

---

### Task 15 : Gating de visibilité — marqueurs `layoutInstances` cachés

**Files:**
- Modify: `src/world_editor/ui/WorldEditorImGui.h` (struct `WorldEditorViewportOverlayDesc`)
- Modify: `src/world_editor/ui/WorldEditorImGui.cpp:341-360`
- Modify: `src/client/app/Engine.cpp` (remplir le masque)

Contexte : l'overlay dessine un **marqueur** (cercle) par instance, indexé par `ii` (WorldEditorImGui.cpp:345). On ajoute un set d'indices cachés ; le marqueur est sauté si l'index y figure. *(Le masquage du mesh 3D complet par calque est hors Lot 0 — cf. spec : marqueur + sélectabilité seulement.)*

- [ ] **Step 1 : Ajouter le champ masque à la struct** — dans `WorldEditorImGui.h`, struct `WorldEditorViewportOverlayDesc`, après `selectedLayoutInstanceOverlay` :

```cpp
		/// Lot 0 — Indices d'instances de layout à NE PAS dessiner (calque caché).
		/// Pointeur non possédé ; nullptr = tout visible.
		const std::vector<uint8_t>* layoutInstanceHiddenMask = nullptr;
```

> On utilise un masque `vector<uint8_t>` aligné sur l'index `ii` (1 = caché). Plus simple/rapide qu'un set pour une itération indexée.

- [ ] **Step 2 : Sauter les marqueurs cachés** — dans `WorldEditorImGui.cpp`, dans la boucle (ligne 345), juste après `const ... inst = (*d.layoutInstancesOverlay)[ii];` :

```cpp
						if (d.layoutInstanceHiddenMask != nullptr
							&& ii < d.layoutInstanceHiddenMask->size()
							&& (*d.layoutInstanceHiddenMask)[ii] != 0)
							continue;
```

- [ ] **Step 3 : Remplir le masque côté Engine** — là où `overlay.layoutInstancesOverlay` est assigné (Engine.cpp:9855), construire le masque dans un membre `Engine` (durée de vie ≥ la frame) :

Ajouter à `Engine.h` : `std::vector<uint8_t> m_editorLayoutHiddenMask;`

Puis après l'assignation de `overlay.layoutInstancesOverlay` :

```cpp
				// Lot 0 — masque de visibilité par calque (marqueurs).
				{
					const auto& insts = m_worldEditorSession->Doc().layoutInstances;
					const auto& layers = m_worldEditorShell->GetLayersDocument();
					m_editorLayoutHiddenMask.assign(insts.size(), 0u);
					for (uint32_t i = 0; i < insts.size(); ++i)
					{
						const uint64_t key = m_worldEditorShell->EntityKeyFor(
							{ engine::editor::scene::EntityKind::LayoutInstance, i });
						if (key != 0ull && !layers.IsEntityVisible(key))
							m_editorLayoutHiddenMask[i] = 1u;
					}
					overlay.layoutInstanceHiddenMask = &m_editorLayoutHiddenMask;
				}
```

- [ ] **Step 4 : Build + vérification** — assigner des props à un calque, le rendre invisible dans le panneau Layers → leurs marqueurs disparaissent du viewport ; les rendre visibles → réapparaissent. (Le mesh 3D reste, c'est attendu pour le Lot 0.)

- [ ] **Step 5 : Commit**

```bash
git add src/world_editor/ui/WorldEditorImGui.h src/world_editor/ui/WorldEditorImGui.cpp src/client/app/Engine.cpp src/client/app/Engine.h
git commit -m "feat(world_editor): gating de visibilité des marqueurs layoutInstances par calque"
```

---

## Vérification finale (avant PR)

- [ ] **Build complet Windows** (`world_editor_app` + jeu) sans erreur/warning bloquant (`/W4`).
- [ ] **Build Linux + ctest** : les 5 nouvelles/étendues cibles passent :
  `editor_selection_tests`, `entity_key_tests`, `selection_query_tests`, `delete_entities_ops_tests`, `delete_entities_command_tests`.
- [ ] **Parcours manuel éditeur** : pick simple, marquee, Ctrl+clic Outliner, Suppr + Undo/Redo exact, assignement calque, visibilité/verrou (marqueur + sélectabilité).
- [ ] **Mention déploiement dans la PR** : ✅ client/éditeur uniquement, pas de redéploiement serveur.

---

## Notes pour l'implémenteur (pièges connus)

1. **Ne pas toucher** aux pipelines de rasterization (`frontFace`/`cullMode`) : aucun rapport, et c'est une garde anti-régression (CLAUDE.md).
2. **Noms de champs à confirmer au câblage** : `MeshInsertInstance::worldPosition` / `::guid`, `DungeonPortalInstance::worldPosition` / `::guid` — vérifiés via `SetTransformWriter` (Engine.cpp:8937-8960) mais re-confirmer en ouvrant les headers `MeshInsertInstance.h` / `DungeonPortalInstance.h`.
3. **Tests Linux non gatés `_WIN32`** : les 5 cibles de test doivent compiler sans ImGui (le cœur pur n'en dépend pas). Ne jamais inclure `imgui.h` dans les fichiers `scene/`.
4. **Hors périmètre, ne pas dériver** : pas de persistance de calque, pas de masquage 3D des meshes, pas de réveil des orphelins `PropInstance`, pas d'unification des deux modèles. Ce sont des tickets futurs (cf. spec §8).
5. **`CommandStack::Clear()` au changement de carte** : vérifier l'existant pour éviter des commandes de suppression pointant vers une session recréée. Ajouter l'appel si manquant (petit ; sinon documenter).
