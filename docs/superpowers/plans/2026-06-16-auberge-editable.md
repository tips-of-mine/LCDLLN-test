# Auberge éditable — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Permettre de composer une auberge depuis l'éditeur de carte à partir des props modulaires existants, de la sauver comme preset réutilisable, et de l'exporter vers `config.json > world.scenery` (+ ancre respawn) pour la rendre visible dans la carte démo du client.

**Architecture :** La logique pure (scan d'assets, parsing/instanciation de preset, aplatissement vers `world.scenery`, sérialisation `props.bin`) vit dans des unités testables headless liées à `engine_core` ; les panneaux ImGui (Windows-only, `#if defined(_WIN32)`) ne sont qu'une fine couche de présentation vérifiée manuellement. Le client de jeu ne change pas : il rend déjà `world.scenery` via `LoadScenery()`.

**Tech Stack :** C++20, CMake + CTest (CI `build-linux`), parseur JSON hand-rolled (pattern repo, pas de lib), ImGui (éditeur, Windows), sérialisation binaire header-only partagée `engine_core`/`zone_builder_lib`.

---

## Environnement de build / test (IMPORTANT — lire avant de commencer)

- **Pas de toolchain de build local** (cmake/MSVC/vcpkg absents des shells). On **ne peut pas** exécuter `ctest` localement.
- **Vérification = CI.** Le push de la branche déclenche GitHub Actions ; `build-linux.yml` compile et lance `ctest`. C'est là qu'on observe rouge/vert.
- **Conséquence sur le rythme TDD :** on écrit le test (rouge attendu) puis l'implémentation, et on **pousse au niveau de la tâche** (pas à chaque micro-step) pour limiter le nombre de runs CI. Les commandes `ctest -R ...` ci-dessous décrivent ce que la CI exécute et le résultat attendu — elles ne sont pas lançables en local.
- **Les panneaux ImGui ne compilent/testent pas sur Linux** (`#if defined(_WIN32)`). Toute logique qui doit être testée par `ctest` doit être **hors ImGui**, dans une unité pure. La couche ImGui est vérifiée **manuellement** dans le binaire éditeur Windows.
- **Convention repo :** commentaires en français, nouveau code/fichiers/dossiers en **PascalCase**, docs en kebab-case. Ne pas employer le terme « CMANGOS ».

## Référence : fichiers existants à connaître

- Modèle de données props : `src/client/world/instances/PropInstances.h` (struct `PropInstance`, `SavePropsBin`/`LoadPropsBin`, helpers `detail::PutU32/PutF32/GetU32/...`, `kPropsVersion`).
- Document de placement éditeur : `src/world_editor/PlacementDocument.h`.
- Hash chemin → assetId : `HashAssetPath()` utilisé dans `src/world_editor/PlacementTool.cpp:37` (déclaré dans `src/world_editor/PlacementTool.h`).
- Parseur JSON de référence : `src/world_editor/presets/ToolPresetIo.cpp` (helpers `SkipWs/ReadString/ReadNumber/SeekKey/MatchObjectEnd/MatchArrayEnd`).
- Test de référence (style + macro `REQUIRE`) : `src/world_editor/tests/PlacementTests.cpp`.
- Panneaux : `AssetBrowserPanel.{h,cpp}` (placeholder), `InspectorPanel.cpp` (transform), `OutlinerPanel.{h,cpp}` (liste), `src/world_editor/DeleteCommand.h` (suppression réversible existante).
- Scène éditeur : `src/world_editor/scene/EditorSceneModel.h` (`SceneEntity`, `EntityTransform`), `EditorSelection.h` (`EntityId`, `EntityKind`).
- Client : `Engine::LoadScenery()` (`src/client/app/Engine.cpp:12978`), `Engine::LoadRespawnMarkers()` (`:12817`).
- Format cible : `config.json` objet `world.scenery` (`"count"` + clés `"0"..N"`, champs `mesh/x/z/yaw_deg/scale/collision_radius/solid`) ; bloc auberge actuel `:552-566`. `game/data/respawn/respawn_points.txt` ligne `0 inn 88.0 1.5 100.0`.
- Enregistrement build : `CMakeLists.txt` (racine) — sources `engine_core` près de `:340` (bloc M100.17 placement), tests près de `:1026` (`placement_tests`). Helper `lcdlln_add_simple_test` dans `cmake/LCDLLNHelpers.cmake`.

## Structure de fichiers (vue d'ensemble)

**Créés :**
- `src/world_editor/assets/AssetCatalog.h` / `.cpp` — scan disque `game/data/meshes/props/`, catégorisation par préfixe de nom. (T1, pur)
- `src/world_editor/assets/tests/AssetCatalogTests.cpp` — tests scan/catégorie. (T1)
- `src/world_editor/structures/BuildingPreset.h` — structs `BuildingPresetElement`, `BuildingPreset`. (T2, header-only)
- `src/world_editor/structures/BuildingPresetIo.h` / `.cpp` — parse/sérialise le preset JSON. (T2, pur)
- `src/world_editor/structures/BuildingInstantiate.h` / `.cpp` — preset + pivot/yaw → `PropInstance[]` + ancre spawn monde. (T2, pur)
- `src/world_editor/structures/MoveGroupCommand.h` — commande déplacement de groupe réversible. (T2, header-only)
- `src/world_editor/structures/SceneryExport.h` / `.cpp` — entries `world.scenery`, splice config.json, ligne respawn. (T4, pur + I/O)
- `src/world_editor/structures/tests/BuildingStructuresTests.cpp` — tests preset IO, instanciation, groupId, move group, export. (T2+T4)
- `game/data/assets/structures/presets/auberge_demo.json` — contenu auberge de référence. (T5)

**Modifiés :**
- `src/client/world/instances/PropInstances.h` — champ `groupId`, `kPropsVersion = 2u`, lecture rétro-compatible. (T2)
- `src/world_editor/PlacementDocument.h` — `AllocGroupId()`, `RemoveByGroup()`. (T2)
- `src/world_editor/panels/AssetBrowserPanel.{h,cpp}` — liste fonctionnelle + callback sélection. (T1)
- `src/world_editor/panels/InspectorPanel.cpp` — rotation Euler XYZ (au lieu de Y seul). (T3)
- `src/world_editor/panels/OutlinerPanel.{h,cpp}` — bouton « Supprimer ». (T3)
- `CMakeLists.txt` (racine) — sources `engine_core` + cibles de test. (T1, T2, T4)
- `config.json` — sentinelle `_comment_auberge_end` puis bloc auberge régénéré. (T4, T5)
- `game/data/respawn/respawn_points.txt` — ligne `inn` régénérée. (T5)

---

## Task 1 : AssetCatalog — scan testable des props (T1, logique pure)

**Files:**
- Create: `src/world_editor/assets/AssetCatalog.h`
- Create: `src/world_editor/assets/AssetCatalog.cpp`
- Test: `src/world_editor/assets/tests/AssetCatalogTests.cpp`
- Modify: `CMakeLists.txt` (racine) — sources `engine_core` + cible `asset_catalog_tests`

- [ ] **Step 1 : Écrire l'en-tête `AssetCatalog.h`**

```cpp
#pragma once

// Auberge éditable (T1) — Catalogue d'assets : scan disque des meshes props
// pour alimenter l'AssetBrowserPanel. Logique pure (std::filesystem), testable
// headless ; la couche ImGui consomme ScanPropAssets() en lecture seule.

#include <string>
#include <vector>

namespace engine::editor::world::assets
{
	/// Catégorie d'asset déduite du préfixe du nom de fichier (regroupe la liste
	/// dans l'UI). Ordre stable (switch UI). Unknown = préfixe non reconnu.
	enum class AssetCategory
	{
		Wall, Door, Window, Roof, Floor, Corner, Overhang, Balcony, Stairs,
		Furniture, Lighting, Container, Decoration, Unknown
	};

	/// Une entrée de catalogue : un fichier mesh .gltf trouvé sous le dossier
	/// scanné. `relativePath` est relatif à la racine contenu (ex.
	/// "meshes/props/Wall_Plaster_Straight.gltf") — directement utilisable comme
	/// chemin de mesh world.scenery / PlacementParams.assetPath.
	struct AssetEntry
	{
		std::string fileName;      ///< "Wall_Plaster_Straight.gltf"
		std::string relativePath;  ///< "meshes/props/Wall_Plaster_Straight.gltf"
		AssetCategory category = AssetCategory::Unknown;
	};

	/// Déduit la catégorie depuis un nom de fichier (insensible au reste du
	/// chemin). Basé sur le préfixe avant le premier '_'. \return Unknown si non
	/// reconnu.
	AssetCategory CategorizeAsset(const std::string& fileName);

	/// Libellé français court d'une catégorie (pour en-têtes UI).
	const char* CategoryLabel(AssetCategory c);

	/// Scanne `absoluteDir` (non récursif) pour les fichiers *.gltf et retourne
	/// les entrées triées par (catégorie, nom). `relativePrefix` est préfixé à
	/// chaque `relativePath` (ex. "meshes/props/"). Dossier absent / vide →
	/// liste vide (pas d'erreur).
	/// Effet de bord : lecture disque seule.
	std::vector<AssetEntry> ScanPropAssets(const std::string& absoluteDir,
		const std::string& relativePrefix);
}
```

- [ ] **Step 2 : Écrire le test (rouge attendu) `AssetCatalogTests.cpp`**

```cpp
// Auberge éditable (T1) — Tests AssetCatalog : catégorisation + scan disque.
// Headless. Lié à engine_core.

#include "src/world_editor/assets/AssetCatalog.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace engine::editor::world::assets;

namespace
{
	int g_failed = 0;
#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	void Touch(const std::filesystem::path& p)
	{
		std::ofstream f(p); f << "{}";
	}

	void Test_Categorize()
	{
		REQUIRE(CategorizeAsset("Wall_Plaster_Straight.gltf") == AssetCategory::Wall);
		REQUIRE(CategorizeAsset("Door_2_Round.gltf") == AssetCategory::Door);
		REQUIRE(CategorizeAsset("Roof_RoundTiles_6x8.gltf") == AssetCategory::Roof);
		REQUIRE(CategorizeAsset("Table_Large.gltf") == AssetCategory::Furniture);
		REQUIRE(CategorizeAsset("Barrel.gltf") == AssetCategory::Container);
		REQUIRE(CategorizeAsset("Lantern_Wall.gltf") == AssetCategory::Lighting);
		REQUIRE(CategorizeAsset("Zorglub_X.gltf") == AssetCategory::Unknown);
	}

	void Test_Scan()
	{
		namespace fs = std::filesystem;
		const fs::path dir = fs::temp_directory_path() / "lcdlln_asset_scan_test";
		fs::remove_all(dir);
		fs::create_directories(dir);
		Touch(dir / "Wall_Plaster_Straight.gltf");
		Touch(dir / "Door_2_Round.gltf");
		Touch(dir / "Wall_Plaster_Straight.bin"); // ignoré (pas .gltf)
		Touch(dir / "notes.txt");                 // ignoré

		auto entries = ScanPropAssets(dir.string(), "meshes/props/");
		REQUIRE(entries.size() == 2);
		// Tri par (catégorie, nom) : Wall(0) avant Door(1) ? Non — ordre enum :
		// Wall=0, Door=1 → Wall d'abord.
		REQUIRE(entries[0].category == AssetCategory::Wall);
		REQUIRE(entries[0].relativePath == "meshes/props/Wall_Plaster_Straight.gltf");
		REQUIRE(entries[1].category == AssetCategory::Door);
		fs::remove_all(dir);
	}

	void Test_Scan_MissingDir()
	{
		auto entries = ScanPropAssets("/nonexistent/dir/xyz", "meshes/props/");
		REQUIRE(entries.empty());
	}
}

int main()
{
	Test_Categorize();
	Test_Scan();
	Test_Scan_MissingDir();
	if (g_failed == 0) std::fprintf(stderr, "[OK] AssetCatalogTests\n");
	else std::fprintf(stderr, "[FAIL] AssetCatalogTests: %d\n", g_failed);
	return g_failed;
}
```

- [ ] **Step 3 : Écrire l'implémentation `AssetCatalog.cpp`**

```cpp
#include "src/world_editor/assets/AssetCatalog.h"

#include <algorithm>
#include <filesystem>

namespace engine::editor::world::assets
{
	namespace
	{
		/// Préfixe (avant le premier '_', ou nom sans extension si pas de '_').
		std::string Prefix(const std::string& fileName)
		{
			const size_t us = fileName.find('_');
			if (us != std::string::npos) return fileName.substr(0, us);
			const size_t dot = fileName.find('.');
			return fileName.substr(0, dot == std::string::npos ? fileName.size() : dot);
		}
	}

	AssetCategory CategorizeAsset(const std::string& fileName)
	{
		const std::string p = Prefix(fileName);
		if (p == "Wall")        return AssetCategory::Wall;
		if (p == "Door" || p == "DoorFrame") return AssetCategory::Door;
		if (p == "Window" || p == "WindowShutters") return AssetCategory::Window;
		if (p == "Roof")        return AssetCategory::Roof;
		if (p == "Floor")       return AssetCategory::Floor;
		if (p == "Corner")      return AssetCategory::Corner;
		if (p == "Overhang")    return AssetCategory::Overhang;
		if (p == "Balcony")     return AssetCategory::Balcony;
		if (p == "Stairs" || p == "Stair") return AssetCategory::Stairs;
		if (p == "Table" || p == "Chair" || p == "Bench" || p == "Stool" ||
		    p == "Bed" || p == "Shelf" || p == "Bookcase" || p == "Cabinet" ||
		    p == "Nightstand" || p == "Workbench")
			return AssetCategory::Furniture;
		if (p == "Lantern" || p == "Torch" || p == "Chandelier" ||
		    p == "CandleStick")
			return AssetCategory::Lighting;
		if (p == "Barrel" || p == "Crate" || p == "FarmCrate" || p == "Chest" ||
		    p == "Bucket" || p == "Pouch")
			return AssetCategory::Container;
		if (p == "Banner" || p == "Vase" || p == "Pot" || p == "Mug" ||
		    p == "Bottle" || p == "SmallBottle" || p == "SmallBottles" ||
		    p == "Chalice" || p == "Coin" || p == "Cauldron" || p == "Key")
			return AssetCategory::Decoration;
		return AssetCategory::Unknown;
	}

	const char* CategoryLabel(AssetCategory c)
	{
		switch (c)
		{
			case AssetCategory::Wall:       return "Murs";
			case AssetCategory::Door:       return "Portes";
			case AssetCategory::Window:     return "Fenetres";
			case AssetCategory::Roof:       return "Toits";
			case AssetCategory::Floor:      return "Planchers";
			case AssetCategory::Corner:     return "Coins";
			case AssetCategory::Overhang:   return "Surplombs";
			case AssetCategory::Balcony:    return "Balcons";
			case AssetCategory::Stairs:     return "Escaliers";
			case AssetCategory::Furniture:  return "Mobilier";
			case AssetCategory::Lighting:   return "Eclairage";
			case AssetCategory::Container:  return "Conteneurs";
			case AssetCategory::Decoration: return "Decoration";
			case AssetCategory::Unknown:    return "Autres";
		}
		return "Autres";
	}

	std::vector<AssetEntry> ScanPropAssets(const std::string& absoluteDir,
		const std::string& relativePrefix)
	{
		namespace fs = std::filesystem;
		std::vector<AssetEntry> out;
		std::error_code ec;
		if (!fs::is_directory(absoluteDir, ec)) return out;
		for (const auto& e : fs::directory_iterator(absoluteDir, ec))
		{
			if (ec) break;
			if (!e.is_regular_file()) continue;
			const std::string name = e.path().filename().string();
			if (e.path().extension() != ".gltf") continue;
			AssetEntry a;
			a.fileName = name;
			a.relativePath = relativePrefix + name;
			a.category = CategorizeAsset(name);
			out.push_back(std::move(a));
		}
		std::sort(out.begin(), out.end(), [](const AssetEntry& x, const AssetEntry& y)
		{
			if (x.category != y.category)
				return static_cast<int>(x.category) < static_cast<int>(y.category);
			return x.fileName < y.fileName;
		});
		return out;
	}
}
```

- [ ] **Step 4 : Enregistrer dans `CMakeLists.txt` (racine)**

Dans la liste des sources `engine_core`, après le bloc M100.17 (`src/world_editor/PlacementTool.cpp`, ~ligne 342), ajouter :

```cmake
  # Auberge editable (T1) — Catalogue d'assets (scan disque props).
  src/world_editor/assets/AssetCatalog.cpp
```

Après la cible `placement_tests` (~ligne 1028), ajouter :

```cmake
# Auberge editable (T1) — Tests AssetCatalog (categorisation + scan disque).
add_executable(asset_catalog_tests src/world_editor/assets/tests/AssetCatalogTests.cpp)
target_link_libraries(asset_catalog_tests PRIVATE engine_core)
add_test(NAME asset_catalog_tests COMMAND asset_catalog_tests)
```

- [ ] **Step 5 : Pousser et vérifier en CI**

Run (en CI `build-linux`) : `ctest -R asset_catalog_tests --output-on-failure`
Expected : PASS (`[OK] AssetCatalogTests`).

- [ ] **Step 6 : Commit**

```bash
git add src/world_editor/assets CMakeLists.txt
git commit -m "feat(editor): AssetCatalog — scan testable des props (auberge T1)"
```

## Task 2 : AssetBrowserPanel fonctionnel (T1, UI — vérif. manuelle)

**Files:**
- Modify: `src/world_editor/panels/AssetBrowserPanel.h`
- Modify: `src/world_editor/panels/AssetBrowserPanel.cpp`

- [ ] **Step 1 : Étendre l'en-tête du panneau**

Remplacer le corps de la classe dans `AssetBrowserPanel.h` par :

```cpp
#pragma once
#include "src/world_editor/core/IPanel.h"
#include "src/world_editor/assets/AssetCatalog.h"

#include <functional>
#include <string>
#include <vector>

namespace engine::editor::world::panels
{
	/// Panneau Asset Browser : liste les meshes props scannés sur disque,
	/// groupés par catégorie, et notifie l'asset sélectionné (chemin relatif)
	/// via un callback (branché par le shell sur PlacementParams.assetPath).
	class AssetBrowserPanel final : public IPanel
	{
	public:
		using OnAssetPicked = std::function<void(const std::string&)>;

		const char* GetName() const override { return "Asset Browser"; }
		void Render() override;
		bool IsVisible() const override { return m_visible; }
		void SetVisible(bool visible) override { m_visible = visible; }

		/// Charge/rafraîchit la liste depuis le disque. \param absoluteDir
		/// dossier props absolu. \param relativePrefix préfixe chemin relatif.
		/// Effet de bord : lecture disque (à appeler hors frame critique).
		void Refresh(const std::string& absoluteDir, const std::string& relativePrefix);

		/// Installe l'observateur de sélection (remplace le précédent).
		void SetOnAssetPicked(OnAssetPicked cb) { m_onPicked = std::move(cb); }

	private:
		bool m_visible = true;
		std::vector<assets::AssetEntry> m_entries;
		std::string m_selectedPath;
		OnAssetPicked m_onPicked;
	};
}
```

- [ ] **Step 2 : Implémenter le rendu `AssetBrowserPanel.cpp`**

```cpp
#include "src/world_editor/panels/AssetBrowserPanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	void AssetBrowserPanel::Refresh(const std::string& absoluteDir,
		const std::string& relativePrefix)
	{
		m_entries = assets::ScanPropAssets(absoluteDir, relativePrefix);
	}

	void AssetBrowserPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Asset Browser", &m_visible))
		{
			if (m_entries.empty())
			{
				ImGui::TextDisabled("Aucun asset. (Refresh non appele ou dossier vide.)");
			}
			else
			{
				ImGui::Text("%d asset(s)", static_cast<int>(m_entries.size()));
				ImGui::Separator();
				assets::AssetCategory current = assets::AssetCategory::Unknown;
				bool headerOpen = false;
				for (const assets::AssetEntry& e : m_entries)
				{
					if (e.category != current)
					{
						current = e.category;
						headerOpen = ImGui::CollapsingHeader(
							assets::CategoryLabel(current), ImGuiTreeNodeFlags_DefaultOpen);
					}
					if (!headerOpen) continue;
					const bool selected = (e.relativePath == m_selectedPath);
					ImGui::PushID(e.relativePath.c_str());
					if (ImGui::Selectable(e.fileName.c_str(), selected))
					{
						m_selectedPath = e.relativePath;
						if (m_onPicked) m_onPicked(e.relativePath);
					}
					ImGui::PopID();
				}
			}
		}
		ImGui::End();
#endif
	}
}
```

- [ ] **Step 3 : Vérification manuelle (binaire éditeur Windows)**

Compiler l'éditeur (`lcdlln_world_editor.exe`) via la CI Windows / VS, lancer, vérifier : le panneau « Asset Browser » liste les props groupés par catégorie ; cliquer un item le sélectionne (surbrillance) et, une fois le shell branché (Task 9), définit l'asset actif du PlacementTool.

> Note : pas de test ctest ici (ImGui Windows-only). La logique testée est dans `AssetCatalog` (Task 1).

- [ ] **Step 4 : Commit**

```bash
git add src/world_editor/panels/AssetBrowserPanel.h src/world_editor/panels/AssetBrowserPanel.cpp
git commit -m "feat(editor): AssetBrowserPanel liste les props par categorie (auberge T1)"
```

## Task 3 : groupId sur PropInstance + back-compat props.bin (T2)

**Files:**
- Modify: `src/client/world/instances/PropInstances.h`
- Test: `src/world_editor/tests/PlacementTests.cpp` (ajout d'un test)

- [ ] **Step 1 : Ajouter le test de round-trip v2 + back-compat v1 (rouge attendu)**

Dans `PlacementTests.cpp`, ajouter cette fonction avant `main()` :

```cpp
	void Test_PropsBin_GroupId_V2_And_BackCompat()
	{
		using engine::world::instances::PropInstance;
		// Round-trip v2 : groupId préservé.
		std::vector<PropInstance> props;
		PropInstance a; a.assetId = 5; a.instanceId = 1; a.groupId = 77;
		PropInstance b; b.assetId = 6; b.instanceId = 2; b.groupId = 0;
		props = { a, b };
		std::vector<uint8_t> bytes = engine::world::instances::SavePropsBin(props);
		std::vector<PropInstance> out; std::string err;
		REQUIRE(engine::world::instances::LoadPropsBin(bytes, out, err));
		REQUIRE(out.size() == 2);
		if (out.size() == 2)
		{
			REQUIRE(out[0].groupId == 77);
			REQUIRE(out[1].groupId == 0);
		}

		// Back-compat : un buffer "v1" (sans groupId) se charge, groupId = 0.
		// On forge un v1 en patchant l'octet de version (offset 4) à 1 et en
		// retirant le champ groupId de chaque instance.
		std::vector<PropInstance> single = { a };
		std::vector<uint8_t> v2 = engine::world::instances::SavePropsBin(single);
		// Reconstruire un v1 à la main : header (24) puis count + 1 instance sans groupId.
		// Plus simple : vérifier que LoadPropsBin accepte version=1. On force la
		// version à 1 et on tronque les 4 derniers octets (le groupId de l'unique
		// instance, écrit en dernier).
		v2[4] = 1; // version = 1
		std::vector<uint8_t> v1(v2.begin(), v2.end() - 4);
		std::vector<PropInstance> out1; std::string err1;
		REQUIRE(engine::world::instances::LoadPropsBin(v1, out1, err1));
		REQUIRE(out1.size() == 1);
		if (out1.size() == 1) REQUIRE(out1[0].groupId == 0 && out1[0].assetId == 5);
	}
```

Et l'appeler dans `main()` :

```cpp
	Test_PropsBin_GroupId_V2_And_BackCompat();
```

- [ ] **Step 2 : (CI) constater l'échec de compilation/exécution**

Run (CI) : `ctest -R placement_tests --output-on-failure`
Expected : FAIL (`PropInstance` n'a pas de membre `groupId`).

- [ ] **Step 3 : Ajouter `groupId`, bump version, lecture conditionnelle**

Dans `src/client/world/instances/PropInstances.h` :

1. Ajouter le champ à la struct (après `instanceId`) :

```cpp
		uint32_t instanceId = 0;   // incrémental, unique zone
		uint32_t groupId = 0;      // 0 = isolé ; >0 = membre d'un groupe (auberge…)
```

2. Bumper la version :

```cpp
	constexpr uint32_t kPropsVersion = 2u; // v2 : ajout groupId (rétro-compat lecture v1)
```

3. Dans `SavePropsBin`, écrire `groupId` en dernier champ de chaque instance (après `instanceId`) :

```cpp
			detail::PutU32(b, p.instanceId);
			detail::PutU32(b, p.groupId);
```

4. Dans `LoadPropsBin`, lire `groupId` seulement si `version >= 2` (sinon il reste à 0) — remplacer la fin de boucle d'instance :

```cpp
			detail::GetU32(bytes, p, pr.layerTag);
			if (!detail::GetU32(bytes, p, pr.instanceId)) { err = "props.bin: tronque (instanceId)"; return false; }
			if (version >= 2u)
			{
				if (!detail::GetU32(bytes, p, pr.groupId)) { err = "props.bin: tronque (groupId)"; return false; }
			}
			out.push_back(pr);
```

- [ ] **Step 4 : (CI) vérifier le vert**

Run (CI) : `ctest -R placement_tests --output-on-failure`
Expected : PASS (round-trip v2 + back-compat v1).

- [ ] **Step 5 : Commit**

```bash
git add src/client/world/instances/PropInstances.h src/world_editor/tests/PlacementTests.cpp
git commit -m "feat(editor): groupId sur PropInstance, props.bin v2 retro-compatible (auberge T2)"
```

## Task 4 : BuildingPreset + parsing JSON (T2)

**Files:**
- Create: `src/world_editor/structures/BuildingPreset.h`
- Create: `src/world_editor/structures/BuildingPresetIo.h`
- Create: `src/world_editor/structures/BuildingPresetIo.cpp`
- Test: `src/world_editor/structures/tests/BuildingStructuresTests.cpp`
- Modify: `CMakeLists.txt` (racine)

- [ ] **Step 1 : Définir les structs `BuildingPreset.h`**

```cpp
#pragma once

// Auberge éditable (T2) — Modèle de preset de bâtiment : liste d'éléments
// (mesh + transform relatif au pivot) + ancre de spawn. Header-only (data).

#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::editor::world::structures
{
	/// Un élément du bâtiment : un mesh posé à un offset relatif au pivot du
	/// groupe, avec une rotation Y propre et une échelle uniforme.
	struct BuildingPresetElement
	{
		std::string meshPath;            ///< "meshes/props/Wall_Plaster_Straight.gltf"
		engine::math::Vec3 offset{};     ///< offset (m) relatif au pivot, avant rotation de groupe
		float yawDeg = 0.0f;             ///< rotation Y propre (deg)
		float scale = 1.0f;              ///< échelle uniforme
		float collisionRadius = 0.0f;    ///< cylindre collision (m) ; 0 = non solide
		bool solid = true;               ///< bloque le joueur ?
	};

	/// Un preset de bâtiment réutilisable (ex. auberge). Le pivot n'est pas
	/// stocké ici : il est fourni au moment de l'instanciation/export.
	struct BuildingPreset
	{
		std::string id;                  ///< "auberge_demo"
		std::string displayName;         ///< "Auberge (démo)"
		engine::math::Vec3 spawnAnchor{};///< offset (m) relatif au pivot : point de respawn "inn"
		std::vector<BuildingPresetElement> elements;
	};
}
```

- [ ] **Step 2 : Déclarer le parseur `BuildingPresetIo.h`**

```cpp
#pragma once

#include "src/world_editor/structures/BuildingPreset.h"

#include <string>

namespace engine::editor::world::structures
{
	/// Parse le JSON d'un preset de bâtiment (parseur hand-rolled, pattern repo).
	///
	/// Format :
	/// ```json
	/// {
	///   "id": "auberge_demo",
	///   "displayName": "Auberge (démo)",
	///   "spawnAnchor": { "x": 0.0, "y": 0.0, "z": 2.0 },
	///   "elements": [
	///     { "mesh": "meshes/props/Floor_WoodDark.gltf",
	///       "x": 0, "y": 0, "z": 0, "yaw_deg": 0, "scale": 1,
	///       "collision_radius": 0, "solid": false }
	///   ]
	/// }
	/// ```
	/// \return false + `outError` sur erreur structurelle (JSON illisible, `id`
	/// manquant). Un élément sans `mesh` est ignoré (tolérant).
	bool ParseBuildingPresetJson(const std::string& jsonText, BuildingPreset& out,
		std::string& outError);

	/// Sérialise un preset en JSON (indentation simple). Round-trip avec
	/// ParseBuildingPresetJson.
	std::string SerializeBuildingPresetJson(const BuildingPreset& preset);
}
```

- [ ] **Step 3 : Écrire les tests (rouge attendu) `BuildingStructuresTests.cpp`**

```cpp
// Auberge éditable (T2/T4) — Tests structures : preset IO, instanciation,
// groupes, export world.scenery. Headless. Lié à engine_core.

#include "src/world_editor/structures/BuildingPreset.h"
#include "src/world_editor/structures/BuildingPresetIo.h"

#include <cmath>
#include <cstdio>

using namespace engine::editor::world::structures;

namespace
{
	int g_failed = 0;
#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)
	bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

	void Test_ParsePreset()
	{
		const std::string json = R"({
			"id": "auberge_demo",
			"displayName": "Auberge",
			"spawnAnchor": { "x": 0.0, "y": 0.0, "z": 2.0 },
			"elements": [
				{ "mesh": "meshes/props/Floor_WoodDark.gltf", "x": 0, "y": 0, "z": 0, "yaw_deg": 0, "scale": 1, "collision_radius": 0, "solid": false },
				{ "mesh": "meshes/props/Wall_Plaster_Straight.gltf", "x": 2.5, "y": 0, "z": 0, "yaw_deg": 90, "scale": 1, "collision_radius": 0.5, "solid": true }
			]
		})";
		BuildingPreset p; std::string err;
		REQUIRE(ParseBuildingPresetJson(json, p, err));
		REQUIRE(p.id == "auberge_demo");
		REQUIRE(p.displayName == "Auberge");
		REQUIRE(Near(p.spawnAnchor.z, 2.0f));
		REQUIRE(p.elements.size() == 2);
		if (p.elements.size() == 2)
		{
			REQUIRE(p.elements[0].meshPath == "meshes/props/Floor_WoodDark.gltf");
			REQUIRE(p.elements[0].solid == false);
			REQUIRE(Near(p.elements[1].offset.x, 2.5f));
			REQUIRE(Near(p.elements[1].yawDeg, 90.0f));
			REQUIRE(p.elements[1].solid == true);
			REQUIRE(Near(p.elements[1].collisionRadius, 0.5f));
		}
	}

	void Test_Preset_Roundtrip()
	{
		BuildingPreset p; p.id = "t"; p.displayName = "T";
		p.spawnAnchor = { 1.0f, 0.0f, -2.0f };
		BuildingPresetElement e; e.meshPath = "meshes/props/Door_2_Round.gltf";
		e.offset = { 3.0f, 0.0f, 4.0f }; e.yawDeg = 45.0f; e.scale = 1.2f;
		e.collisionRadius = 0.3f; e.solid = true;
		p.elements.push_back(e);
		const std::string json = SerializeBuildingPresetJson(p);
		BuildingPreset q; std::string err;
		REQUIRE(ParseBuildingPresetJson(json, q, err));
		REQUIRE(q.id == "t" && q.elements.size() == 1);
		if (q.elements.size() == 1)
		{
			REQUIRE(q.elements[0].meshPath == "meshes/props/Door_2_Round.gltf");
			REQUIRE(Near(q.elements[0].offset.z, 4.0f));
			REQUIRE(Near(q.elements[0].yawDeg, 45.0f));
			REQUIRE(Near(q.spawnAnchor.z, -2.0f));
		}
	}
}

int main()
{
	Test_ParsePreset();
	Test_Preset_Roundtrip();
	if (g_failed == 0) std::fprintf(stderr, "[OK] BuildingStructuresTests\n");
	else std::fprintf(stderr, "[FAIL] BuildingStructuresTests: %d\n", g_failed);
	return g_failed;
}
```

- [ ] **Step 4 : Implémenter `BuildingPresetIo.cpp`**

```cpp
#include "src/world_editor/structures/BuildingPresetIo.h"

#include <cstdio>

namespace engine::editor::world::structures
{
	namespace
	{
		void SkipWs(const std::string& s, size_t& p)
		{
			while (p < s.size() &&
				(s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
		}
		bool ReadString(const std::string& s, size_t& p, std::string& out)
		{
			SkipWs(s, p);
			if (p >= s.size() || s[p] != '"') return false;
			++p; out.clear();
			while (p < s.size() && s[p] != '"')
			{
				if (s[p] == '\\' && p + 1 < s.size())
				{
					const char esc = s[p + 1];
					if (esc == 'n') out.push_back('\n');
					else if (esc == 't') out.push_back('\t');
					else out.push_back(esc);
					p += 2;
				}
				else { out.push_back(s[p]); ++p; }
			}
			if (p >= s.size()) return false;
			++p; return true;
		}
		bool ReadNumber(const std::string& s, size_t& p, double& out)
		{
			SkipWs(s, p);
			const size_t start = p;
			if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
			while (p < s.size() && ((s[p] >= '0' && s[p] <= '9') || s[p] == '.' ||
				s[p] == 'e' || s[p] == 'E' || s[p] == '-' || s[p] == '+')) ++p;
			if (p == start) return false;
			try { out = std::stod(s.substr(start, p - start)); }
			catch (...) { return false; }
			return true;
		}
		bool ReadBool(const std::string& s, size_t& p, bool& out)
		{
			SkipWs(s, p);
			if (s.compare(p, 4, "true") == 0) { out = true; p += 4; return true; }
			if (s.compare(p, 5, "false") == 0) { out = false; p += 5; return true; }
			return false;
		}
		size_t MatchEnd(const std::string& s, size_t p, char open, char close)
		{
			if (p >= s.size() || s[p] != open) return std::string::npos;
			int depth = 0; bool inStr = false;
			for (size_t i = p; i < s.size(); ++i)
			{
				const char c = s[i];
				if (c == '"' && (i == 0 || s[i - 1] != '\\')) inStr = !inStr;
				if (inStr) continue;
				if (c == open) ++depth;
				else if (c == close) { --depth; if (depth == 0) return i; }
			}
			return std::string::npos;
		}
		bool SeekKey(const std::string& s, size_t start, size_t end,
			const std::string& key, size_t& outPos)
		{
			const std::string needle = "\"" + key + "\"";
			size_t pos = s.find(needle, start);
			while (pos != std::string::npos && pos < end)
			{
				size_t after = pos + needle.size();
				SkipWs(s, after);
				if (after < s.size() && s[after] == ':')
				{
					++after; SkipWs(s, after); outPos = after; return true;
				}
				pos = s.find(needle, after);
			}
			return false;
		}
		float SeekNum(const std::string& s, size_t start, size_t end,
			const std::string& key, float fallback)
		{
			size_t kp = 0;
			if (!SeekKey(s, start, end, key, kp)) return fallback;
			double v = fallback;
			if (!ReadNumber(s, kp, v)) return fallback;
			return static_cast<float>(v);
		}
	}

	bool ParseBuildingPresetJson(const std::string& s, BuildingPreset& out,
		std::string& outError)
	{
		out = BuildingPreset{};
		size_t pos = 0;
		if (!SeekKey(s, 0, s.size(), "id", pos) || !ReadString(s, pos, out.id) ||
			out.id.empty())
		{
			outError = "BuildingPresetIo: 'id' manquant"; return false;
		}
		if (SeekKey(s, 0, s.size(), "displayName", pos))
			(void)ReadString(s, pos, out.displayName);
		if (out.displayName.empty()) out.displayName = out.id;

		size_t anchorPos = 0;
		if (SeekKey(s, 0, s.size(), "spawnAnchor", anchorPos))
		{
			SkipWs(s, anchorPos);
			const size_t aEnd = MatchEnd(s, anchorPos, '{', '}');
			if (aEnd != std::string::npos)
			{
				out.spawnAnchor.x = SeekNum(s, anchorPos, aEnd, "x", 0.0f);
				out.spawnAnchor.y = SeekNum(s, anchorPos, aEnd, "y", 0.0f);
				out.spawnAnchor.z = SeekNum(s, anchorPos, aEnd, "z", 0.0f);
			}
		}

		size_t arrPos = 0;
		if (!SeekKey(s, 0, s.size(), "elements", arrPos)) return true; // toléré
		SkipWs(s, arrPos);
		const size_t arrEnd = MatchEnd(s, arrPos, '[', ']');
		if (arrEnd == std::string::npos)
		{
			outError = "BuildingPresetIo: 'elements' non terminé"; return false;
		}
		++arrPos;
		while (arrPos < arrEnd)
		{
			SkipWs(s, arrPos);
			if (arrPos >= arrEnd || s[arrPos] == ']') break;
			if (s[arrPos] == ',') { ++arrPos; continue; }
			if (s[arrPos] != '{') { outError = "BuildingPresetIo: element non-objet"; return false; }
			const size_t objStart = arrPos;
			const size_t objEnd = MatchEnd(s, objStart, '{', '}');
			if (objEnd == std::string::npos)
			{
				outError = "BuildingPresetIo: element non terminé"; return false;
			}
			BuildingPresetElement el;
			size_t mp = 0;
			if (SeekKey(s, objStart, objEnd, "mesh", mp)) (void)ReadString(s, mp, el.meshPath);
			el.offset.x = SeekNum(s, objStart, objEnd, "x", 0.0f);
			el.offset.y = SeekNum(s, objStart, objEnd, "y", 0.0f);
			el.offset.z = SeekNum(s, objStart, objEnd, "z", 0.0f);
			el.yawDeg = SeekNum(s, objStart, objEnd, "yaw_deg", 0.0f);
			el.scale = SeekNum(s, objStart, objEnd, "scale", 1.0f);
			el.collisionRadius = SeekNum(s, objStart, objEnd, "collision_radius", 0.0f);
			size_t sp = 0;
			if (SeekKey(s, objStart, objEnd, "solid", sp)) (void)ReadBool(s, sp, el.solid);
			if (!el.meshPath.empty()) out.elements.push_back(std::move(el));
			arrPos = objEnd + 1;
		}
		return true;
	}

	std::string SerializeBuildingPresetJson(const BuildingPreset& p)
	{
		char buf[512];
		std::string out;
		out += "{\n";
		out += "  \"id\": \"" + p.id + "\",\n";
		out += "  \"displayName\": \"" + p.displayName + "\",\n";
		std::snprintf(buf, sizeof(buf),
			"  \"spawnAnchor\": { \"x\": %.3f, \"y\": %.3f, \"z\": %.3f },\n",
			p.spawnAnchor.x, p.spawnAnchor.y, p.spawnAnchor.z);
		out += buf;
		out += "  \"elements\": [\n";
		for (size_t i = 0; i < p.elements.size(); ++i)
		{
			const BuildingPresetElement& e = p.elements[i];
			std::snprintf(buf, sizeof(buf),
				"    { \"mesh\": \"%s\", \"x\": %.3f, \"y\": %.3f, \"z\": %.3f, "
				"\"yaw_deg\": %.3f, \"scale\": %.3f, \"collision_radius\": %.3f, \"solid\": %s }%s\n",
				e.meshPath.c_str(), e.offset.x, e.offset.y, e.offset.z, e.yawDeg,
				e.scale, e.collisionRadius, e.solid ? "true" : "false",
				(i + 1 < p.elements.size()) ? "," : "");
			out += buf;
		}
		out += "  ]\n}\n";
		return out;
	}
}
```

- [ ] **Step 5 : Enregistrer dans `CMakeLists.txt`**

Sources `engine_core` (après `AssetCatalog.cpp` de Task 1) :

```cmake
  # Auberge editable (T2) — Preset de batiment (parsing JSON).
  src/world_editor/structures/BuildingPresetIo.cpp
```

Cible de test (après `asset_catalog_tests`) :

```cmake
# Auberge editable (T2/T4) — Tests structures (preset IO, instanciation, export).
add_executable(building_structures_tests src/world_editor/structures/tests/BuildingStructuresTests.cpp)
target_link_libraries(building_structures_tests PRIVATE engine_core)
add_test(NAME building_structures_tests COMMAND building_structures_tests)
```

- [ ] **Step 6 : (CI) vérifier le vert**

Run (CI) : `ctest -R building_structures_tests --output-on-failure`
Expected : PASS (`[OK] BuildingStructuresTests`).

- [ ] **Step 7 : Commit**

```bash
git add src/world_editor/structures CMakeLists.txt
git commit -m "feat(editor): BuildingPreset + parsing/serialisation JSON (auberge T2)"
```

## Task 5 : Instanciation preset → PropInstance + ancre spawn (T2)

**Files:**
- Create: `src/world_editor/structures/BuildingInstantiate.h`
- Create: `src/world_editor/structures/BuildingInstantiate.cpp`
- Modify: `src/world_editor/structures/tests/BuildingStructuresTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Déclarer `BuildingInstantiate.h`**

```cpp
#pragma once

// Auberge éditable (T2) — Instanciation : applique un transform de groupe
// (pivot monde + yaw) à un preset et produit des PropInstance posables, plus
// l'ancre de spawn en coordonnées monde.

#include <cstdint>
#include <vector>

#include "src/client/world/instances/PropInstances.h"
#include "src/world_editor/structures/BuildingPreset.h"

namespace engine::editor::world::structures
{
	/// Fait tourner un offset (m) autour de l'axe Y de `yawDeg` degrés.
	/// Convention : yaw horaire vu de dessus (x' = x cosθ + z sinθ ;
	/// z' = -x sinθ + z cosθ). Cohérente avec l'export world.scenery (yaw_deg).
	engine::math::Vec3 RotateYaw(const engine::math::Vec3& v, float yawDeg);

	/// Instancie `preset` au pivot monde `pivot` (m) avec rotation de groupe
	/// `groupYawDeg`. Chaque élément devient une PropInstance : position =
	/// pivot + RotateYaw(offset), yaw monde = groupYaw + élément.yaw, assetId =
	/// HashAssetPath(meshPath), layerTag = Structures, groupId = `groupId`,
	/// instanceId alloué via `allocInstanceId`.
	/// \param allocInstanceId foncteur retournant un instanceId unique (zone).
	std::vector<engine::world::instances::PropInstance> InstantiatePreset(
		const BuildingPreset& preset, const engine::math::Vec3& pivot,
		float groupYawDeg, uint32_t groupId,
		const std::function<uint32_t()>& allocInstanceId);

	/// Position monde (m) de l'ancre de spawn : pivot + RotateYaw(spawnAnchor).
	engine::math::Vec3 SpawnAnchorWorld(const BuildingPreset& preset,
		const engine::math::Vec3& pivot, float groupYawDeg);
}
```

> Ajouter `#include <functional>` en tête du fichier (pour `std::function`).

- [ ] **Step 2 : Ajouter les tests (rouge attendu)**

Dans `BuildingStructuresTests.cpp`, ajouter l'include et les tests :

```cpp
#include "src/world_editor/structures/BuildingInstantiate.h"
```

```cpp
	void Test_RotateYaw()
	{
		using engine::math::Vec3;
		// 90° : (1,0,0) -> (0,0,-1) avec la convention x'=x c + z s, z'=-x s + z c.
		Vec3 r = RotateYaw(Vec3(1, 0, 0), 90.0f);
		REQUIRE(Near(r.x, 0.0f, 1e-3f) && Near(r.z, -1.0f, 1e-3f));
		// 0° : identité.
		Vec3 r0 = RotateYaw(Vec3(2, 5, -3), 0.0f);
		REQUIRE(Near(r0.x, 2.0f) && Near(r0.y, 5.0f) && Near(r0.z, -3.0f));
	}

	void Test_Instantiate()
	{
		using engine::math::Vec3;
		BuildingPreset p; p.id = "x"; p.spawnAnchor = { 0, 0, 2 };
		BuildingPresetElement a; a.meshPath = "meshes/props/Floor_WoodDark.gltf";
		a.offset = { 0, 0, 0 };
		BuildingPresetElement b; b.meshPath = "meshes/props/Wall_Plaster_Straight.gltf";
		b.offset = { 1, 0, 0 };
		p.elements = { a, b };

		uint32_t next = 1;
		auto alloc = [&next]() { return next++; };
		// Pivot (10,0,20), pas de rotation.
		auto insts = InstantiatePreset(p, Vec3(10, 0, 20), 0.0f, 42, alloc);
		REQUIRE(insts.size() == 2);
		if (insts.size() == 2)
		{
			REQUIRE(Near(insts[0].position.x, 10.0f) && Near(insts[0].position.z, 20.0f));
			REQUIRE(Near(insts[1].position.x, 11.0f) && Near(insts[1].position.z, 20.0f));
			REQUIRE(insts[0].groupId == 42 && insts[1].groupId == 42);
			REQUIRE(insts[0].instanceId != insts[1].instanceId);
			REQUIRE(insts[0].layerTag ==
				static_cast<uint32_t>(engine::world::instances::PlacementLayer::Structures));
			REQUIRE(insts[0].assetId != 0);
		}
		// Ancre spawn monde = pivot + (0,0,2).
		Vec3 anchor = SpawnAnchorWorld(p, Vec3(10, 0, 20), 0.0f);
		REQUIRE(Near(anchor.x, 10.0f) && Near(anchor.z, 22.0f));
	}
```

Et les appeler dans `main()` :

```cpp
	Test_RotateYaw();
	Test_Instantiate();
```

- [ ] **Step 3 : Implémenter `BuildingInstantiate.cpp`**

```cpp
#include "src/world_editor/structures/BuildingInstantiate.h"

#include <cmath>

#include "src/world_editor/PlacementGeometry.h"
#include "src/world_editor/PlacementTool.h" // HashAssetPath

namespace engine::editor::world::structures
{
	namespace pg = engine::editor::world::placement;

	engine::math::Vec3 RotateYaw(const engine::math::Vec3& v, float yawDeg)
	{
		const float r = yawDeg * 3.14159265358979323846f / 180.0f;
		const float c = std::cos(r), s = std::sin(r);
		return engine::math::Vec3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
	}

	std::vector<engine::world::instances::PropInstance> InstantiatePreset(
		const BuildingPreset& preset, const engine::math::Vec3& pivot,
		float groupYawDeg, uint32_t groupId,
		const std::function<uint32_t()>& allocInstanceId)
	{
		using engine::world::instances::PropInstance;
		using engine::world::instances::PlacementLayer;
		std::vector<PropInstance> out;
		out.reserve(preset.elements.size());
		for (const BuildingPresetElement& e : preset.elements)
		{
			PropInstance inst;
			const engine::math::Vec3 rot = RotateYaw(e.offset, groupYawDeg);
			inst.position = engine::math::Vec3(
				pivot.x + rot.x, pivot.y + rot.y, pivot.z + rot.z);
			const float worldYaw = groupYawDeg + e.yawDeg;
			pg::BuildOrientation(worldYaw, engine::math::Vec3(0, 1, 0), false,
				inst.rotationQuat);
			inst.assetId = engine::editor::world::HashAssetPath(e.meshPath);
			inst.scale = engine::math::Vec3(e.scale, e.scale, e.scale);
			inst.layerTag = static_cast<uint32_t>(PlacementLayer::Structures);
			inst.groupId = groupId;
			inst.instanceId = allocInstanceId ? allocInstanceId() : 0u;
			out.push_back(inst);
		}
		return out;
	}

	engine::math::Vec3 SpawnAnchorWorld(const BuildingPreset& preset,
		const engine::math::Vec3& pivot, float groupYawDeg)
	{
		const engine::math::Vec3 r = RotateYaw(preset.spawnAnchor, groupYawDeg);
		return engine::math::Vec3(pivot.x + r.x, pivot.y + r.y, pivot.z + r.z);
	}
}
```

> Vérifier la déclaration exacte de `HashAssetPath` dans `src/world_editor/PlacementTool.h` (namespace `engine::editor::world`). Si la signature diffère, adapter l'appel ; la fonction existe (utilisée à `PlacementTool.cpp:37`).

- [ ] **Step 4 : Enregistrer la source dans `CMakeLists.txt`**

Sources `engine_core` (après `BuildingPresetIo.cpp`) :

```cmake
  src/world_editor/structures/BuildingInstantiate.cpp
```

- [ ] **Step 5 : (CI) vérifier le vert**

Run (CI) : `ctest -R building_structures_tests --output-on-failure`
Expected : PASS (RotateYaw + Instantiate + précédents).

- [ ] **Step 6 : Commit**

```bash
git add src/world_editor/structures CMakeLists.txt
git commit -m "feat(editor): instanciation preset -> PropInstance + ancre spawn (auberge T2)"
```

## Task 6 : Groupes dans PlacementDocument + MoveGroupCommand (T2)

**Files:**
- Modify: `src/world_editor/PlacementDocument.h`
- Create: `src/world_editor/structures/MoveGroupCommand.h`
- Modify: `src/world_editor/structures/tests/BuildingStructuresTests.cpp`

- [ ] **Step 1 : Ajouter les tests (rouge attendu)**

Dans `BuildingStructuresTests.cpp`, ajouter les includes :

```cpp
#include "src/world_editor/PlacementDocument.h"
#include "src/world_editor/structures/MoveGroupCommand.h"
#include "src/world_editor/core/CommandStack.h"
```

et le test :

```cpp
	void Test_Group_MoveAndRemove()
	{
		using engine::editor::world::PlacementDocument;
		using engine::editor::world::MoveGroupCommand;
		using engine::editor::world::CommandStack;
		using engine::math::Vec3;

		PlacementDocument doc;
		const uint32_t gid = doc.AllocGroupId();
		REQUIRE(gid != 0);

		BuildingPreset p; p.id = "x";
		BuildingPresetElement a; a.meshPath = "meshes/props/Floor_WoodDark.gltf";
		BuildingPresetElement b; b.meshPath = "meshes/props/Wall_Plaster_Straight.gltf";
		b.offset = { 1, 0, 0 };
		p.elements = { a, b };
		auto alloc = [&doc]() { return doc.AllocInstanceId(); };
		for (auto& inst : InstantiatePreset(p, Vec3(0, 0, 0), 0.0f, gid, alloc))
			doc.Add(inst);
		// Une instance isolée hors groupe ne doit pas bouger.
		engine::world::instances::PropInstance solo;
		solo.instanceId = doc.AllocInstanceId(); solo.position = { 100, 0, 100 };
		doc.Add(solo);
		REQUIRE(doc.All().size() == 3);

		// Déplacer le groupe de (+5,0,+5).
		CommandStack stack;
		stack.Push(std::make_unique<MoveGroupCommand>(doc, gid, Vec3(5, 0, 5)));
		for (const auto& inst : doc.All())
		{
			if (inst.groupId == gid)
				REQUIRE(inst.position.x >= 5.0f - 1e-3f); // décalé
			else
				REQUIRE(Near(inst.position.x, 100.0f)); // intact
		}
		stack.Undo();
		for (const auto& inst : doc.All())
			if (inst.groupId == gid)
				REQUIRE(inst.position.x <= 1.0f + 1e-3f); // revenu

		// RemoveByGroup retire uniquement les membres du groupe.
		doc.RemoveByGroup(gid);
		REQUIRE(doc.All().size() == 1);
		REQUIRE(doc.All()[0].groupId == 0);
	}
```

Et l'appeler dans `main()` : `Test_Group_MoveAndRemove();`

- [ ] **Step 2 : Étendre `PlacementDocument.h`**

Ajouter dans la classe `PlacementDocument` (après `RemoveById`) :

```cpp
		/// Alloue un identifiant de groupe unique non nul (auberge, bâtiment…).
		uint32_t AllocGroupId() { return m_nextGroupId++; }

		/// Retire toutes les instances membres du groupe `groupId` (no-op si 0).
		void RemoveByGroup(uint32_t groupId)
		{
			if (groupId == 0) return;
			m_props.erase(std::remove_if(m_props.begin(), m_props.end(),
				[groupId](const engine::world::instances::PropInstance& p)
				{ return p.groupId == groupId; }), m_props.end());
		}
```

et le membre privé (après `m_nextInstanceId`) :

```cpp
		uint32_t m_nextGroupId = 1;
```

- [ ] **Step 3 : Écrire `MoveGroupCommand.h`**

```cpp
#pragma once

// Auberge éditable (T2) — Déplacement réversible d'un groupe d'instances
// (translation appliquée à tous les membres du groupId). Opère sur le vecteur
// du PlacementDocument (référence non-owning).

#include "src/shared/math/Math.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/PlacementDocument.h"

namespace engine::editor::world
{
	/// Translate toutes les instances du groupe `groupId` de `delta` (m).
	class MoveGroupCommand final : public ICommand
	{
	public:
		MoveGroupCommand(PlacementDocument& doc, uint32_t groupId,
			engine::math::Vec3 delta)
			: m_doc(doc), m_groupId(groupId), m_delta(delta) {}

		const char* GetLabel() const override { return "Move group"; }
		size_t GetMemoryFootprint() const override { return sizeof(*this); }

		void Execute() override { Apply(m_delta); }
		void Undo() override
		{
			Apply(engine::math::Vec3(-m_delta.x, -m_delta.y, -m_delta.z));
		}

	private:
		void Apply(const engine::math::Vec3& d)
		{
			for (auto& p : m_doc.Mutable())
			{
				if (p.groupId != m_groupId) continue;
				p.position.x += d.x; p.position.y += d.y; p.position.z += d.z;
			}
		}
		PlacementDocument& m_doc;
		uint32_t m_groupId;
		engine::math::Vec3 m_delta;
	};
}
```

> Vérifier la signature de `ICommand` dans `src/world_editor/core/CommandStack.h` (méthodes `GetLabel`/`GetMemoryFootprint`/`Execute`/`Undo`) — mêmes overrides que `PlacePropsCommand`/`DeleteCommand`. Adapter si `GetMemoryFootprint` est absent de l'interface.

- [ ] **Step 4 : (CI) vérifier le vert**

Run (CI) : `ctest -R building_structures_tests --output-on-failure`
Expected : PASS (Group_MoveAndRemove + précédents).

- [ ] **Step 5 : Commit**

```bash
git add src/world_editor/PlacementDocument.h src/world_editor/structures CMakeLists.txt
git commit -m "feat(editor): groupes PlacementDocument + MoveGroupCommand (auberge T2)"
```

## Task 7 : Inspector rotation XYZ (T3, UI — vérif. manuelle)

**Files:**
- Modify: `src/world_editor/panels/InspectorPanel.cpp`

- [ ] **Step 1 : Remplacer Rotation Y par Rotation XYZ**

Dans `InspectorPanel.cpp`, dans le bloc `else` (transform éditable), remplacer :

```cpp
						const scene::EntityTransform cur = e->transform;
						float pos[3]   = { cur.position.x, cur.position.y, cur.position.z };
						float yaw      = cur.eulerDeg.y;
						float scale    = cur.uniformScale;

						bool changed = false;
						changed |= ImGui::DragFloat3("Position (m)", pos, 0.1f);
						changed |= ImGui::DragFloat("Rotation Y (deg)", &yaw, 0.5f);
						changed |= ImGui::DragFloat("Echelle", &scale, 0.01f, 0.01f, 1000.0f);
```

par :

```cpp
						const scene::EntityTransform cur = e->transform;
						float pos[3]   = { cur.position.x, cur.position.y, cur.position.z };
						float rot[3]   = { cur.eulerDeg.x, cur.eulerDeg.y, cur.eulerDeg.z };
						float scale    = cur.uniformScale;

						bool changed = false;
						changed |= ImGui::DragFloat3("Position (m)", pos, 0.1f);
						changed |= ImGui::DragFloat3("Rotation XYZ (deg)", rot, 0.5f);
						changed |= ImGui::DragFloat("Echelle", &scale, 0.01f, 0.01f, 1000.0f);
```

Et dans la construction de `next` (après `if (changed && canEdit)`), remplacer :

```cpp
							next.eulerDeg.y   = yaw;
```

par :

```cpp
							next.eulerDeg.x   = rot[0];
							next.eulerDeg.y   = rot[1];
							next.eulerDeg.z   = rot[2];
```

- [ ] **Step 2 : Vérification manuelle (éditeur Windows)**

Sélectionner une instance dans l'Outliner, vérifier que l'Inspector permet d'éditer la rotation X, Y et Z indépendamment et que l'undo (Ctrl+Z) fonctionne (commande coalescée).

> Pas de test ctest (ImGui Windows-only). `EntityTransform.eulerDeg` est déjà un `Vec3` (`EditorSceneModel.h:21`).

- [ ] **Step 3 : Commit**

```bash
git add src/world_editor/panels/InspectorPanel.cpp
git commit -m "feat(editor): Inspector edite la rotation Euler XYZ (auberge T3)"
```

## Task 8 : OutlinerPanel — bouton Supprimer (T3, UI — vérif. manuelle)

**Files:**
- Modify: `src/world_editor/panels/OutlinerPanel.h`
- Modify: `src/world_editor/panels/OutlinerPanel.cpp`

- [ ] **Step 1 : Ajouter un callback de suppression à l'en-tête**

Dans `OutlinerPanel.h`, ajouter (membres + setter) :

```cpp
	using OnDeleteEntity = std::function<void(scene::EntityId)>;
	void SetOnDeleteEntity(OnDeleteEntity cb) { m_onDelete = std::move(cb); }
```

et le membre privé :

```cpp
	OnDeleteEntity m_onDelete;
```

> Ajouter `#include <functional>` si absent.

- [ ] **Step 2 : Câbler un bouton « Supprimer » dans le rendu**

Dans `OutlinerPanel.cpp`, méthode `Render()`, après les appels `RenderKindGroup(...)` et avant la fermeture de l'`if (m_sceneModel == ...)`, ajouter :

```cpp
				ImGui::Separator();
				const bool canDelete = (m_selection != nullptr)
					&& m_selection->HasSelection()
					&& static_cast<bool>(m_onDelete)
					&& (m_selection->Current().kind == scene::EntityKind::LayoutInstance
					    || m_selection->Current().kind == scene::EntityKind::MeshInsert);
				if (!canDelete) ImGui::BeginDisabled();
				if (ImGui::Button("Supprimer la selection"))
				{
					m_onDelete(m_selection->Current());
				}
				if (!canDelete) ImGui::EndDisabled();
```

- [ ] **Step 3 : Vérification manuelle (éditeur Windows)**

Le shell branchera `SetOnDeleteEntity` (Task 9) sur une suppression réelle (`DeleteCommand` pour les `LayoutInstance`). Vérifier : sélectionner une instance → « Supprimer la selection » la retire ; undo la restaure.

> Pas de test ctest (ImGui). La logique réversible vit dans `DeleteCommand` (`src/world_editor/DeleteCommand.h`), déjà testée par ailleurs.

- [ ] **Step 4 : Commit**

```bash
git add src/world_editor/panels/OutlinerPanel.h src/world_editor/panels/OutlinerPanel.cpp
git commit -m "feat(editor): Outliner bouton Supprimer la selection (auberge T3)"
```

## Task 9 : Câblage shell (T1+T3, intégration — vérif. manuelle)

**Files:**
- Modify: `src/world_editor/core/WorldEditorShell.cpp` (et `.h` si besoin)

> Ce câblage relie les panneaux aux documents/outils. Pas de test ctest (couche shell ImGui). Lire d'abord `WorldEditorShell.{h,cpp}` pour suivre les patterns d'injection existants (le shell possède déjà `EditorSelection`, `PlacementDocument`, panneaux).

- [ ] **Step 1 : Brancher l'AssetBrowser**

À l'initialisation du shell (là où les panneaux sont construits) :
- Appeler `assetBrowser.Refresh(<contentRoot> + "/meshes/props", "meshes/props/")` (résoudre `<contentRoot>` comme les autres chargements d'assets du shell).
- `assetBrowser.SetOnAssetPicked([this](const std::string& rel){ /* maj PlacementParams.assetPath du PlacementTool actif */ });`

- [ ] **Step 2 : Brancher la suppression Outliner**

`outliner.SetOnDeleteEntity([this](scene::EntityId id){ ... })` :
- Pour `EntityKind::LayoutInstance` : pousser un `DeleteCommand` sur le `CommandStack` avec l'`instanceId` correspondant (résoudre via le document de layout/placement), puis vider la sélection.
- Pour `EntityKind::MeshInsert` : retirer via `MeshInsertDocument::Remove(guid)` (commande dédiée si elle existe).

- [ ] **Step 3 : Vérification manuelle bout-en-bout (éditeur Windows)**

Scénario : ouvrir l'éditeur, choisir un mur dans l'Asset Browser, le placer (PlacementTool), répéter pour quelques éléments, sélectionner/déplacer/supprimer via Outliner+Inspector. Sauvegarder la zone (`props.bin` v2 avec `groupId`).

- [ ] **Step 4 : Commit**

```bash
git add src/world_editor/core/WorldEditorShell.cpp src/world_editor/core/WorldEditorShell.h
git commit -m "feat(editor): cablage AssetBrowser + suppression Outliner dans le shell (auberge T1/T3)"
```

## Task 10 : Export world.scenery + respawn (T4)

**Files:**
- Create: `src/world_editor/structures/SceneryExport.h`
- Create: `src/world_editor/structures/SceneryExport.cpp`
- Modify: `src/world_editor/structures/tests/BuildingStructuresTests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1 : Déclarer `SceneryExport.h`**

```cpp
#pragma once

// Auberge éditable (T4) — Export : preset + transform de groupe → entrées
// world.scenery (config.json) + ancre respawn "inn". Logique pure de
// construction/sérialisation ; le splice fichier est textuel (marqueurs).

#include <string>
#include <vector>

#include "src/world_editor/structures/BuildingPreset.h"

namespace engine::editor::world::structures
{
	/// Une entrée world.scenery destinée à config.json.
	struct SceneryEntry
	{
		std::string meshPath;
		float x = 0.0f, z = 0.0f, yawDeg = 0.0f, scale = 1.0f;
		float collisionRadius = 0.0f;
		bool solid = true;
	};

	/// Construit les entrées scenery d'un preset posé au pivot (x,z) monde avec
	/// rotation `groupYawDeg`. (y de l'offset ignoré : world.scenery pose Y au
	/// sol au runtime.)
	std::vector<SceneryEntry> BuildSceneryEntries(const BuildingPreset& preset,
		float pivotX, float pivotZ, float groupYawDeg);

	/// Sérialise les entrées en lignes JSON `"<index>": { ... },` à partir de
	/// `startIndex` (numérotation des clés de l'objet world.scenery).
	std::string SerializeSceneryEntries(const std::vector<SceneryEntry>& entries,
		int startIndex);

	/// Remplace le texte entre les sentinelles `"_comment_auberge"` (incluse) et
	/// `"_comment_auberge_end"` (exclue) par `newBlock`. \return false si une
	/// sentinelle manque. (Le bloc auberge devient ainsi régénérable sans
	/// toucher au reste de config.json.)
	bool SpliceSceneryBlock(const std::string& configText,
		const std::string& newBlock, std::string& out, std::string& err);

	/// Remplace (ou ajoute) la ligne `inn` de respawn_points.txt pour la zone
	/// `zoneId` aux coordonnées (x,z). Conserve les autres lignes.
	std::string SpliceInnRespawn(const std::string& respawnText, uint32_t zoneId,
		float x, float z);
}
```

> Ajouter `#include <cstdint>`.

- [ ] **Step 2 : Ajouter les tests (rouge attendu)**

Dans `BuildingStructuresTests.cpp`, include + tests :

```cpp
#include "src/world_editor/structures/SceneryExport.h"
```

```cpp
	void Test_BuildSceneryEntries()
	{
		BuildingPreset p; p.id = "x";
		BuildingPresetElement a; a.meshPath = "meshes/props/Floor_WoodDark.gltf";
		a.offset = { 0, 0, 0 }; a.solid = false; a.collisionRadius = 0.0f;
		BuildingPresetElement b; b.meshPath = "meshes/props/Wall_Plaster_Straight.gltf";
		b.offset = { 2, 0, 0 }; b.yawDeg = 90.0f; b.solid = true; b.collisionRadius = 0.5f;
		p.elements = { a, b };
		auto entries = BuildSceneryEntries(p, 88.0f, 100.0f, 0.0f);
		REQUIRE(entries.size() == 2);
		if (entries.size() == 2)
		{
			REQUIRE(Near(entries[0].x, 88.0f) && Near(entries[0].z, 100.0f));
			REQUIRE(entries[0].solid == false);
			REQUIRE(Near(entries[1].x, 90.0f) && Near(entries[1].z, 100.0f));
			REQUIRE(Near(entries[1].yawDeg, 90.0f) && entries[1].solid == true);
		}
	}

	void Test_SpliceSceneryBlock()
	{
		const std::string cfg =
			"  AVANT\n"
			"  \"_comment_auberge\": \"x\",\n"
			"  \"310\": { \"mesh\": \"vieux\" },\n"
			"  \"_comment_auberge_end\": \"y\",\n"
			"  APRES\n";
		const std::string block =
			"  \"_comment_auberge\": \"AUBERGE generee\",\n"
			"  \"310\": { \"mesh\": \"neuf\" },\n";
		std::string out, err;
		REQUIRE(SpliceSceneryBlock(cfg, block, out, err));
		REQUIRE(out.find("neuf") != std::string::npos);
		REQUIRE(out.find("vieux") == std::string::npos);
		REQUIRE(out.find("AVANT") != std::string::npos);
		REQUIRE(out.find("APRES") != std::string::npos);
		REQUIRE(out.find("_comment_auberge_end") != std::string::npos);

		std::string out2, err2;
		REQUIRE(!SpliceSceneryBlock("pas de sentinelle", block, out2, err2));
	}

	void Test_SpliceInnRespawn()
	{
		const std::string txt =
			"# commentaire\n"
			"0 graveyard 120.0 1.5 120.0\n"
			"0 inn 88.0 1.5 100.0\n";
		std::string out = SpliceInnRespawn(txt, 0, 90.0f, 105.0f);
		REQUIRE(out.find("0 inn 90") != std::string::npos);
		REQUIRE(out.find("0 inn 88.0") == std::string::npos);
		REQUIRE(out.find("graveyard 120") != std::string::npos);
		// Ajout si absent.
		std::string out2 = SpliceInnRespawn("0 graveyard 1 1 1\n", 0, 5.0f, 6.0f);
		REQUIRE(out2.find("0 inn 5") != std::string::npos);
	}
```

Appels dans `main()` :

```cpp
	Test_BuildSceneryEntries();
	Test_SpliceSceneryBlock();
	Test_SpliceInnRespawn();
```

- [ ] **Step 3 : Implémenter `SceneryExport.cpp`**

```cpp
#include "src/world_editor/structures/SceneryExport.h"

#include <cstdio>
#include <sstream>

#include "src/world_editor/structures/BuildingInstantiate.h" // RotateYaw

namespace engine::editor::world::structures
{
	std::vector<SceneryEntry> BuildSceneryEntries(const BuildingPreset& preset,
		float pivotX, float pivotZ, float groupYawDeg)
	{
		std::vector<SceneryEntry> out;
		out.reserve(preset.elements.size());
		for (const BuildingPresetElement& e : preset.elements)
		{
			const engine::math::Vec3 r = RotateYaw(e.offset, groupYawDeg);
			SceneryEntry s;
			s.meshPath = e.meshPath;
			s.x = pivotX + r.x;
			s.z = pivotZ + r.z;
			s.yawDeg = groupYawDeg + e.yawDeg;
			s.scale = e.scale;
			s.collisionRadius = e.collisionRadius;
			s.solid = e.solid;
			out.push_back(std::move(s));
		}
		return out;
	}

	std::string SerializeSceneryEntries(const std::vector<SceneryEntry>& entries,
		int startIndex)
	{
		std::string out;
		char buf[512];
		for (size_t i = 0; i < entries.size(); ++i)
		{
			const SceneryEntry& s = entries[i];
			std::snprintf(buf, sizeof(buf),
				"            \"%d\": { \"mesh\": \"%s\", \"x\": %.2f, \"z\": %.2f, "
				"\"yaw_deg\": %.1f, \"scale\": %.2f, \"collision_radius\": %.2f, \"solid\": %s },\n",
				startIndex + static_cast<int>(i), s.meshPath.c_str(), s.x, s.z,
				s.yawDeg, s.scale, s.collisionRadius, s.solid ? "true" : "false");
			out += buf;
		}
		return out;
	}

	bool SpliceSceneryBlock(const std::string& configText,
		const std::string& newBlock, std::string& out, std::string& err)
	{
		const std::string startMark = "\"_comment_auberge\"";
		const std::string endMark = "\"_comment_auberge_end\"";
		const size_t s = configText.find(startMark);
		const size_t e = configText.find(endMark);
		if (s == std::string::npos || e == std::string::npos || e < s)
		{
			err = "SceneryExport: sentinelles _comment_auberge[_end] introuvables";
			return false;
		}
		// Reculer `s` au début de sa ligne (préserver l'indentation amont).
		size_t lineStart = configText.rfind('\n', s);
		lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
		out = configText.substr(0, lineStart) + newBlock + configText.substr(e);
		// `e` pointe sur le `"` d'ouverture de endMark : on conserve la ligne
		// endMark intacte (l'indentation amont a déjà été retirée via `lineStart`
		// du endMark ? non — on recolle à partir de e). Recoller proprement :
		// chercher le début de ligne de endMark.
		size_t endLineStart = configText.rfind('\n', e);
		endLineStart = (endLineStart == std::string::npos) ? 0 : endLineStart + 1;
		out = configText.substr(0, lineStart) + newBlock + configText.substr(endLineStart);
		return true;
	}

	std::string SpliceInnRespawn(const std::string& respawnText, uint32_t zoneId,
		float x, float z)
	{
		char line[128];
		std::snprintf(line, sizeof(line), "%u inn %.1f 1.5 %.1f", zoneId, x, z);
		std::istringstream in(respawnText);
		std::string row;
		std::string out;
		bool replaced = false;
		const std::string prefix = std::to_string(zoneId) + " inn ";
		while (std::getline(in, row))
		{
			if (row.rfind(prefix, 0) == 0)
			{
				out += line; out += "\n"; replaced = true;
			}
			else { out += row; out += "\n"; }
		}
		if (!replaced) { out += line; out += "\n"; }
		return out;
	}
}
```

- [ ] **Step 4 : Enregistrer la source dans `CMakeLists.txt`**

Sources `engine_core` (après `BuildingInstantiate.cpp`) :

```cmake
  src/world_editor/structures/SceneryExport.cpp
```

- [ ] **Step 5 : (CI) vérifier le vert**

Run (CI) : `ctest -R building_structures_tests --output-on-failure`
Expected : PASS (BuildSceneryEntries + SpliceSceneryBlock + SpliceInnRespawn + précédents).

- [ ] **Step 6 : Commit**

```bash
git add src/world_editor/structures CMakeLists.txt
git commit -m "feat(editor): export world.scenery + respawn inn (auberge T4)"
```

## Task 11 : Sentinelle config.json + branchement export shell (T4, intégration)

**Files:**
- Modify: `config.json`
- Modify: `src/world_editor/core/WorldEditorShell.cpp` (action « Exporter l'auberge »)

- [ ] **Step 1 : Ajouter la sentinelle de fin dans `config.json`**

Dans l'objet `world.scenery`, juste après la dernière entrée auberge actuelle (`"322"`, vers `config.json:566`) et avant la fermeture de l'objet, insérer :

```json
            "_comment_auberge_end": "Fin du bloc auberge regenerable (ne pas supprimer : marqueur d'export editeur)."
```

Vérifier que `"_comment_auberge"` (début, `:552`) et `"_comment_auberge_end"` encadrent bien toutes les clés auberge.

- [ ] **Step 2 : Brancher l'action d'export dans le shell**

Ajouter une action UI (menu/bouton « Exporter l'auberge dans la carte démo ») qui, pour le groupe auberge sélectionné :
1. charge le preset courant ;
2. `entries = BuildSceneryEntries(preset, pivotX, pivotZ, groupYaw)` ;
3. `block = "            \"_comment_auberge\": \"AUBERGE (auto-export). Point inn.\",\n" + SerializeSceneryEntries(entries, 310)` ;
4. lire `config.json`, `SpliceSceneryBlock(...)`, **mettre à jour `world.scenery.count`** (recompter les clés numériques), réécrire ;
5. `anchor = SpawnAnchorWorld(preset, pivot, groupYaw)` puis lire `respawn_points.txt`, `SpliceInnRespawn(text, 0, anchor.x, anchor.z)`, réécrire.

> `world.scenery.count` doit refléter le nombre total de clés numériques. Le plus sûr : recompter après splice, ou réserver une plage fixe (310..) et ajuster `count` = ancien_count − ancien_nb_auberge + nouveau_nb_auberge. Documenter le choix retenu dans le code.

- [ ] **Step 3 : Vérification manuelle**

Exporter depuis l'éditeur, rouvrir `config.json`, vérifier le bloc auberge régénéré + `count` cohérent + ligne `inn` mise à jour dans `respawn_points.txt`.

- [ ] **Step 4 : Commit**

```bash
git add config.json src/world_editor/core/WorldEditorShell.cpp
git commit -m "feat(editor): sentinelle + action export auberge vers config.json/respawn (auberge T4)"
```

## Task 12 : Preset auberge de référence + pose dans la carte démo (T5)

**Files:**
- Create: `game/data/assets/structures/presets/auberge_demo.json`
- Modify: `config.json` (via export)
- Modify: `game/data/respawn/respawn_points.txt` (via export)

- [ ] **Step 1 : Composer le preset auberge**

Créer `game/data/assets/structures/presets/auberge_demo.json` en assemblant une auberge fermée à partir du kit `meshes/props/` (murs `Wall_Plaster_*` / portes `Door_*` / fenêtres `Window_*` / toit `Roof_RoundTiles_*` / plancher `Floor_WoodDark` / mobilier `Table_Large`, `Bench`, `Barrel`, `Lantern_Wall`…). Offsets relatifs au pivot, `spawnAnchor` devant la porte d'entrée. Squelette minimal (à étoffer) :

```json
{
  "id": "auberge_demo",
  "displayName": "Auberge (démo)",
  "spawnAnchor": { "x": 0.0, "y": 0.0, "z": 3.0 },
  "elements": [
    { "mesh": "meshes/props/Floor_WoodDark.gltf", "x": 0, "y": 0, "z": 0, "yaw_deg": 0, "scale": 1, "collision_radius": 0, "solid": false },
    { "mesh": "meshes/props/Wall_Plaster_Door_Round.gltf", "x": 0, "y": 0, "z": 2.5, "yaw_deg": 0, "scale": 1, "collision_radius": 0.4, "solid": true },
    { "mesh": "meshes/props/Wall_Plaster_Straight.gltf", "x": 0, "y": 0, "z": -2.5, "yaw_deg": 180, "scale": 1, "collision_radius": 0.4, "solid": true },
    { "mesh": "meshes/props/Wall_Plaster_Window_Wide_Round.gltf", "x": 2.5, "y": 0, "z": 0, "yaw_deg": 90, "scale": 1, "collision_radius": 0.4, "solid": true },
    { "mesh": "meshes/props/Wall_Plaster_Window_Wide_Round.gltf", "x": -2.5, "y": 0, "z": 0, "yaw_deg": 270, "scale": 1, "collision_radius": 0.4, "solid": true },
    { "mesh": "meshes/props/Roof_RoundTiles_6x6.gltf", "x": 0, "y": 3.0, "z": 0, "yaw_deg": 0, "scale": 1, "collision_radius": 0, "solid": false },
    { "mesh": "meshes/props/Table_Large.gltf", "x": 0.8, "y": 0, "z": 0.5, "yaw_deg": 0, "scale": 1, "collision_radius": 0.9, "solid": true },
    { "mesh": "meshes/props/Bench.gltf", "x": 0.8, "y": 0, "z": 1.4, "yaw_deg": 0, "scale": 1, "collision_radius": 0, "solid": false },
    { "mesh": "meshes/props/Barrel.gltf", "x": -1.6, "y": 0, "z": -1.6, "yaw_deg": 0, "scale": 1, "collision_radius": 0.4, "solid": true },
    { "mesh": "meshes/props/Lantern_Wall.gltf", "x": 0.3, "y": 1.8, "z": 2.4, "yaw_deg": 0, "scale": 1, "collision_radius": 0, "solid": false }
  ]
}
```

> Les noms de mesh ci-dessus existent tous dans `game/data/meshes/props/` (cf. audit). Affiner offsets/échelle dans l'éditeur (preview visuelle).

- [ ] **Step 2 : Poser et exporter à (88,100)**

Dans l'éditeur : charger le preset, le poser à pivot (88,100), ajuster, puis lancer « Exporter l'auberge » (Task 11) → régénère le bloc `world.scenery` (clés 310+) et la ligne `inn` de `respawn_points.txt` (≈ ancre devant la porte).

- [ ] **Step 3 : Vérification manuelle en jeu**

Lancer le client de jeu sur la carte démo : l'auberge fermée (murs + toit + mobilier) est visible et collisionnée à (88,100) ; le point de réapparition `inn` pointe devant l'entrée. Confirmer que l'ancienne terrasse placeholder est remplacée.

- [ ] **Step 4 : Commit**

```bash
git add game/data/assets/structures/presets/auberge_demo.json config.json game/data/respawn/respawn_points.txt
git commit -m "feat(content): auberge demo composee + export carte demo (auberge T5)"
```

## Task 13 : Finalisation — doc, mémo déploiement, PR

- [ ] **Step 1 : Mettre à jour `CODEBASE_MAP.md`** (section éditeur) avec le nouveau sous-système `structures/` (preset auberge, export world.scenery) et `assets/AssetCatalog`.

- [ ] **Step 2 : Vérifier la suite complète en CI**

Run (CI) : `ctest --output-on-failure -R "asset_catalog_tests|building_structures_tests|placement_tests"`
Expected : 3 cibles PASS.

- [ ] **Step 3 : Commit + push + PR**

```bash
git add CODEBASE_MAP.md
git commit -m "docs: documente le sous-systeme auberge editable (structures/, AssetCatalog)"
git push -u origin HEAD
```

Ouvrir la PR. **Déploiement** (à inclure dans la description) :

> **Déploiement** : ✅ client + éditeur + données uniquement — **pas de redéploiement serveur**. L'auberge est rendue via `world.scenery` (déjà lu par `LoadScenery`) ; le respawn `inn` est écrit dans `respawn_points.txt` (autorité **client**). Le serveur (`shardd`) calcule son respawn depuis la DB (`graveyards`) et n'est pas affecté. Le couplage respawn serveur complet (migration `graveyards`) reste le ticket **T6**, différé, qui lui exigera un redéploiement `shardd` + migration DB.

---

## Self-review (rempli par l'auteur du plan)

- **Couverture spec :** T1 (Tasks 1–2, 9), T2 (Tasks 3–6), T3 (Tasks 7–8, 9), T4 (Tasks 10–11), T5 (Task 12), finalisation (Task 13). T6 hors périmètre (différé, annoncé). ✅
- **Placeholders :** aucun « TODO/TBD » de logique ; tout step de code montre le code. Les steps « vérification manuelle » sont explicitement marqués (ImGui Windows-only, non testable en ctest) — ce n'est pas un placeholder mais une contrainte d'environnement. ✅
- **Cohérence des types :** `PropInstance.groupId` (Task 3) utilisé par `InstantiatePreset`/`MoveGroupCommand`/`RemoveByGroup` (Tasks 5–6) ; `BuildingPreset`/`BuildingPresetElement` stables Tasks 4→12 ; `RotateYaw` partagé Tasks 5/10 ; `SceneryEntry` (Task 10) cohérent avec le format `config.json`. ✅
- **Points à confirmer à l'exécution (notés inline) :** signature exacte de `HashAssetPath` (`PlacementTool.h`) ; interface `ICommand` (`CommandStack.h`) ; patterns d'injection du `WorldEditorShell`. Ces vérifications sont des lectures, pas des trous de conception.
