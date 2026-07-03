# SP3 — POI minimap des quêtes — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Une minimap radar schématique (sans texture) affichant les POI des objectifs de quête actifs, positions résolues depuis `quest_poi.json`.

**Architecture:** `QuestPoiTable` (contenu, testable) → `QuestUiPresenter::RebuildMinimap` (réécrit, coords radar centrées joueur) → `QuestImGuiRenderer::RenderMinimap` (ImDrawList). 100 % client, aucun wire.

**Tech Stack:** C++20, ImGui, `src/client/{quest,render}`, contenu `game/data/quests/`.

## Global Constraints

- **Commentaires en français** ; PascalCase ; **jamais « CMANGOS »**.
- **Pas de build local** : CI (`build-linux` → `ctest`). Tests **non-strippables** (Release/NDEBUG strippe `assert()` → style `std::cerr`+compteur+`return 1`, cf. `QuestTextCatalogTests.cpp`).
- **`questId`/`targetId` = `std::string`**.
- **Déploiement** : ✅ client only, aucun wire, pas de redéploiement serveur ; contenu `quest_poi.json` ajouté.
- **⚠️ Piège CMake (leçon SP2/SP4)** : `QuestPoiTable.cpp` est utilisé par le presenter (`QuestUiPresenter`, dans `engine_core`) → le mettre **dans `engine_core`** dès le départ (à côté de `QuestTextCatalog.cpp`/`QuestGiverTable.cpp`), et son test ne liste QUE `QuestPoiTableTests.cpp` (il linke `engine_core`). NE PAS lister `QuestPoiTable.cpp` en source directe du test (doublon).

---

## Structure de fichiers

**Créés :**
- `src/client/quest/QuestPoiTable.{h,cpp}` — charge `quest_poi.json`, `Positions(targetId)`.
- `src/client/quest/QuestPoiTableTests.cpp` — test (non-strippable).
- `game/data/quests/quest_poi.json` — contenu (positions d'objectifs).

**Modifiés :**
- `src/client/quest/QuestUi.{h,cpp}` — `RebuildMinimap` réécrit (radar centré joueur) + injection `SetPoiTable` + helper pur de conversion radar.
- `src/client/render/QuestImGuiRenderer.{h,cpp}` — `RenderMinimap` + appel dans `Render`.
- `src/client/app/Engine.cpp` — charger `QuestPoiTable`, l'injecter au presenter, (config).
- `config.json` — `client.quest.minimap`.
- root `CMakeLists.txt` — `QuestPoiTable.cpp` dans `engine_core` ; `src/CMakeLists.txt` — test.

---

## Task 1 : QuestPoiTable (contenu → positions)

**Files:** Create `src/client/quest/QuestPoiTable.{h,cpp}`, `game/data/quests/quest_poi.json`, `src/client/quest/QuestPoiTableTests.cpp` ; Modify root `CMakeLists.txt` (engine_core) + `src/CMakeLists.txt` (test).

**Interfaces — Produces:**
```cpp
namespace engine::client {
  struct QuestPoiPosition { float x = 0.0f; float z = 0.0f; };
  class QuestPoiTable {
  public:
    bool Load(const engine::core::Config& cfg);   // charge quests/quest_poi.json (paths.content)
    const std::vector<QuestPoiPosition>* Positions(std::string_view targetId) const; // nullptr si absent
  };
}
```

- [ ] **Step 1 : Test (échec attendu)** — `QuestPoiTableTests.cpp` (non-strippable, patron `QuestTextCatalogTests.cpp` : temp dir + `Config::SetValue("paths.content",…)` + `FileSystem::WriteAllTextContent`). Fixture `quest_poi.json` : `{ "mob:100": [[12,-28],[-10,-34]], "npc:elder_marn": [[4,0]] }`. Asserts : `Positions("mob:100")->size()==2` ; `->at(0).x==12 && ->at(0).z==-28` ; `Positions("npc:elder_marn")->size()==1` ; `Positions("inconnu")==nullptr`.

- [ ] **Step 2 : Enregistrer** — root `CMakeLists.txt` : ajouter `src/client/quest/QuestPoiTable.cpp` à la liste `engine_core` (à côté de `QuestTextCatalog.cpp`). `src/CMakeLists.txt` : `lcdlln_add_simple_test(quest_poi_table_tests <…>/QuestPoiTableTests.cpp)` **sans** lister `QuestPoiTable.cpp` (résolu via engine_core), en miroir de `quest_text_catalog_tests`.

- [ ] **Step 3 : Vérif échec CI** — `ctest -R quest_poi_table_tests` → FAIL.

- [ ] **Step 4 : Implémenter** — parse JSON (patron `QuestGiverTable`/`QuestTextCatalog` : `JsonParser` inline + `FindObjectMember`), map `targetId → vector<QuestPoiPosition>`. Chaque valeur = tableau de paires `[x,z]`. Rejet clair si une entrée n'est pas un tableau de paires numériques. Chemin `quests/quest_poi.json`. Commentaires FR.

- [ ] **Step 5 : Contenu** — `game/data/quests/quest_poi.json` : `"mob:100"` = les 4 positions de `feyhin/spawners.json` (`[12,-28],[-10,-34],[22,-40],[-18,-22]`), `"npc:elder_marn"` = `[[4,0]]`. (Ajouter d'autres cibles au besoin.)

- [ ] **Step 6 : Vérif succès CI** ; **Step 7 : Commit** — `feat(quests-client): QuestPoiTable + quest_poi.json`.

---

## Task 2 : RebuildMinimap radar + injection

**Files:** Modify `src/client/quest/QuestUi.{h,cpp}` ; Test : ajouter à un test presenter OU extraire un helper pur testable.

**Interfaces — Produces:**
- `void QuestUiPresenter::SetPoiTable(const QuestPoiTable* table);` (injection ; non possédé).
- Helper pur (libre ou statique, testable) : `bool WorldToRadarUv(float px, float pz, float playerX, float playerZ, float radiusM, float& outU, float& outV, bool& outOffRadar);` — `u = 0.5 + (px-playerX)/(2*radiusM)`, idem v avec z ; `outOffRadar = (dist > radiusM)` ; en off-radar, clamp u/v au bord du disque/cadre.

- [ ] **Step 1 : Test du helper (échec attendu)** — `WorldToRadarUv` : joueur en (0,0), radius 60 : POI (0,0) → (0.5,0.5), pas off-radar ; POI (30,0) → u=0.75 ; POI (120,0) → off-radar=true, u clampé ≤ 1. (Test non-strippable ; extraire le helper dans le `.h`/anonyme pour le tester sans ImGui.)

- [ ] **Step 2 : Vérif échec CI**.

- [ ] **Step 3 : Réécrire `RebuildMinimap`** (`QuestUi.cpp:~357-410`) — pour chaque quête `Active` de `model.quests`, pour chaque `UIQuestStep` non terminé (`currentCount < requiredCount`) : `positions = m_poiTable ? m_poiTable->Positions(step.targetId) : nullptr` ; pour chaque position → `WorldToRadarUv(pos, playerPos, radiusM, …)` → `MinimapPoiView{u,v,label,visible}` (+ un champ de teinte/type = `step.stepType`). `playerPos` depuis `model.playerStats.positionX/Z`. `radiusM` depuis config (défaut 60). Marqueur joueur au centre (0.5,0.5). Retirer l'ancien placeholder `zone:` central. `SetPoiTable` déclaré + membre `const QuestPoiTable* m_poiTable = nullptr;`. Libellé court dérivé de `targetId`/`stepType`.

- [ ] **Step 4 : Vérif succès CI** ; **Step 5 : Commit** — `feat(quests-client): RebuildMinimap radar centré joueur (POI via QuestPoiTable)`.

---

## Task 3 : RenderMinimap + câblage Engine + config

**Files:** Modify `src/client/render/QuestImGuiRenderer.{h,cpp}`, `src/client/app/Engine.cpp`, `config.json`. **Intégration — pas de test unitaire** (UI, validé en jeu).

- [ ] **Step 1 : `RenderMinimap`** — nouvelle méthode privée de `QuestImGuiRenderer`, appelée dans `Render()` après `RenderTracker()`. Depuis `m_presenter->GetState()` (`minimapBounds`, `questPois`, `playerMarker`) : fenêtre ImGui non-interactive aux bornes config (défaut coin haut-droit, `size_px`) ; `ImDrawList` → fond `AddRectFilled` semi-transparent + bord `AddRect` (ou `AddCircle`) + croix centrale ; pour chaque POI `AddCircleFilled` (teinte par type : kill=rouge, talk=jaune, enter=bleu) + `AddText` label ; joueur = point/triangle central. Masqué si `client.quest.minimap.enabled=false` ou `inWorldShard==false`. `#if defined(_WIN32)` comme les autres méthodes du renderer.

- [ ] **Step 2 : Câblage Engine** — au boot (là où `m_questTextCatalog`/`m_questGiverTable` sont chargés) : charger `m_questPoiTable.Load(m_cfg)` et injecter `m_questUi.SetPoiTable(&m_questPoiTable)` (ajouter le membre `engine::client::QuestPoiTable m_questPoiTable;` à Engine.h). Le presenter lit `radiusM`/bornes via config passée à `Init`/`SetViewportSize` (ou le renderer lit la config directement).

- [ ] **Step 3 : Config** — `config.json` : `"client":{"quest":{"minimap":{"enabled":true,"size_px":200,"radius_m":60.0}}}` avec commentaire.

- [ ] **Step 4 : Vérif compilation CI** ; **Step 5 : Commit** — `feat(quests-client): RenderMinimap radar (POI teintés + joueur) + câblage Engine + config`.

---

## Task 4 : Documentation

- [ ] **Step 1** — `CODEBASE_MAP.md` : compléter la section quêtes client (SP3) : minimap radar (`RenderMinimap`), POI via `QuestPoiTable`/`quest_poi.json`, radar centré joueur. Renvoyer à la spec SP3.
- [ ] **Step 2 : Commit** — `docs(quests): minimap POI SP3 dans CODEBASE_MAP`.

---

## Checklist finale (DoD SP3)
- [ ] `QuestPoiTable` + `quest_poi.json` + test vert (dans engine_core ; test sans source directe).
- [ ] `WorldToRadarUv` helper pur testé ; `RebuildMinimap` résout les POI actifs (radar centré joueur), off-radar clampé, joueur au centre.
- [ ] `RenderMinimap` (radar schématique) branché ; masquage cohérent.
- [ ] Config `client.quest.minimap.*`.
- [ ] CI verte ; FR ; rapport : ✅ client only, pas de redéploiement serveur.

## Self-review (effectuée)
- Couverture spec : §2/§3 → Tasks 1,2 ; §4 rendu → Task 3 ; §5 contenu/config → Tasks 1,3.
- CMake trap traité dès Task 1 (engine_core + test sans source directe).
- Points « lire le voisin » : patron `QuestTextCatalog`/`QuestGiverTable` (parse + CMake) ; `RebuildMinimap`/`TryConvertWorldToUv` existants (QuestUi.cpp) ; `QuestImGuiRenderer::Render` (où insérer) ; `UIModel.playerStats` ; le point de chargement des catalogues dans Engine.
