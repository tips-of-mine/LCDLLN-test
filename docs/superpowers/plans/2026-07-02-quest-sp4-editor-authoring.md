# SP4 — Éditeur monde : authoring de quêtes — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Un panneau `QuestEditorPanel` dans `lcdlln_world_editor.exe` pour créer/éditer des quêtes et écrire les 3 fichiers de contenu (`quest_definitions.json`, `quest_texts.fr.json`, `quest_givers.json` régénéré).

**Architecture:** Séparer l'**I/O + validation** (`QuestEditIo`, pur, testable) du **rendu** (`QuestEditorPanel`, `IPanel`, ImGui, intégration). Greffer dans `WorldEditorShell::Init` (après `BuildingEditorPanel`) ; rendu automatique via la boucle `RenderFrame`.

**Tech Stack:** C++20, ImGui (world editor), `src/world_editor/{panels,quests}`, contenu `game/data/quests/`.

## Global Constraints

- **Commentaires en français** ; clarté > brièveté.
- **Règle CLAUDE.md éditeur** : TOUTE fonction nouvelle de `src/world_editor/` (libre, méthode, lambda nommée) documentée `///` (Doxygen) : rôle (1-2 phrases), `\param` non-évidents, **effet de bord** (écriture disque !), **contrainte thread/timing** (main thread, phase ImGui). Non négociable.
- **PascalCase** pour tout nouveau symbole/fichier. **Jamais « CMANGOS »**.
- **Pas de build local** : vérification via CI (`build-linux` compile `world_editor_app` + `ctest`). Étapes « run test » = commande `ctest` attendue.
- **Tests non-strippables** : CI Release (`-DNDEBUG`) strippe `assert()` → style `std::cerr`+compteur+`return 1` (cf. `src/shardd/gameplay/quest/QuestRuntimeTests.cpp`), OU `#undef NDEBUG` en tête.
- **`quest_definitions.json` reste du PUR JSON** (tableaux `[]`) — NE PAS basculer au format Config `count`-indexé (casserait `QuestRuntime` shard).
- **Déploiement** : ✅ outillage éditeur (`world_editor_app`), pas de redéploiement jeu ; contenu créé → restart shard.

---

## Structure de fichiers

**Créés :**
- `src/world_editor/quests/QuestEditIo.{h,cpp}` — modèle `EditedQuest` + `Load`/`Validate`/`Save`.
- `src/world_editor/quests/QuestEditIoTests.cpp` — tests (non-strippables).
- `src/world_editor/panels/QuestEditorPanel.{h,cpp}` — panneau `IPanel`.

**Modifiés :**
- `src/world_editor/core/WorldEditorShell.{h,cpp}` — instancier + injecter `QuestEditorPanel`.
- `src/CMakeLists.txt` (+ racine `CMakeLists.txt` si nécessaire) — source `QuestEditIo.cpp`/`QuestEditorPanel.cpp` dans la cible `world_editor_app` ; test `quest_edit_io_tests`.

> **⚠️ Piège CMake (cf. SP2)** : `QuestEditIo.cpp` doit être compilé dans la cible **`world_editor_app`** (là où sont listés `BuildingEditorPanel.cpp`/`WorldEditorShell.cpp`) **et** dans le test `quest_edit_io_tests` (source directe), mais **PAS dans `engine_core`** (sinon doublon de symboles). Repérer la liste de sources `world_editor_app` dans le CMake avant d'ajouter.

---

## Task 1 : QuestEditIo — modèle + Load

**Files:** Create `src/world_editor/quests/QuestEditIo.{h,cpp}`, `src/world_editor/quests/QuestEditIoTests.cpp` ; Modify CMake.

**Interfaces — Produces:**
```cpp
namespace engine::editor::world::quests {
  struct EditedStep { std::string type; std::string target; uint32_t requiredCount = 1; };
  struct EditedRewardItem { uint32_t itemId = 0; uint32_t quantity = 1; };
  struct EditedQuest {
    std::string id, giver, turnIn;
    std::vector<std::string> prereqs;
    std::vector<EditedStep> steps;
    uint32_t rewardXp = 0, rewardGold = 0;
    std::vector<EditedRewardItem> rewardItems;
    std::string title, description;       // depuis quest_texts.<lang>.json
    std::vector<std::string> stepLabels;  // 1 par étape
  };
  class QuestEditIo {
  public:
    /// Charge quest_definitions.json (pur JSON) + fusionne quest_texts.fr.json.
    bool Load(const std::string& contentRoot, std::vector<EditedQuest>& out, std::string& outError) const;
    // Validate / Save : Tasks 2 / 3.
  };
}
```

- [ ] **Step 1 : Test (échec attendu)** — `QuestEditIoTests.cpp` (style non-strippable). Écrire une fixture : un dossier temporaire `<tmp>/quests/quest_definitions.json` (2 quêtes, dont `kill_10_boars` giver/turnIn `npc:elder_marn`, step kill mob:100 x10, reward xp/gold) + `<tmp>/quests/quest_texts.fr.json` (title/description/steps pour `kill_10_boars`). Appeler `Load(<tmp>, out, err)`. Asserts : `out.size()==2` ; la quête `kill_10_boars` a `giver=="npc:elder_marn"`, `steps[0].type=="kill"`, `steps[0].target=="mob:100"`, `steps[0].requiredCount==10`, `title` non vide, `stepLabels[0]` non vide.

- [ ] **Step 2 : Enregistrer le test** — `src/CMakeLists.txt` : `lcdlln_add_simple_test(quest_edit_io_tests <…>/QuestEditIoTests.cpp <…>/QuestEditIo.cpp)` (engine_core lié par le helper → FileSystem dispo). Mirror une registration voisine (`quest_runtime_tests`).

- [ ] **Step 3 : Vérif échec CI** — `ctest -R quest_edit_io_tests` → FAIL.

- [ ] **Step 4 : Implémenter `Load`** — lire `<contentRoot>/quests/quest_definitions.json` (via `engine::platform::FileSystem::ReadAllTextContent` ou lecture fichier direct `std::ifstream` sur le chemin absolu) + parser avec le **`JsonParser` inline** (copier le patron de `src/shardd/gameplay/quest/QuestRuntime.cpp` : `JsonParser`/`JsonValue`/`FindObjectMember`/`JsonType`). Remplir `EditedQuest` (id/giver/turnIn/prereqs/steps/rewards). Puis lire `quest_texts.fr.json` (map questId → {title, description, steps[]}) et fusionner par id (champs vides si absent). `///` sur chaque fonction. Commentaires FR.

- [ ] **Step 5 : Vérif succès CI** — `ctest -R quest_edit_io_tests` → PASS.

- [ ] **Step 6 : Commit** — `feat(quests-editor): QuestEditIo::Load (quest_definitions + quest_texts)`.

---

## Task 2 : QuestEditIo — Validate

**Files:** Modify `src/world_editor/quests/QuestEditIo.{h,cpp}`, `QuestEditIoTests.cpp`.

**Interfaces — Produces:**
```cpp
/// Valide l'ensemble ; remplit outErrors (vide = OK). Pur, sans I/O.
bool Validate(const std::vector<EditedQuest>& quests, std::vector<std::string>& outErrors) const;
```

- [ ] **Step 1 : Test (échec attendu)** — ajouter des cas : (a) ensemble valide → `Validate` true, 0 erreur ; (b) 2 quêtes même `id` → false ; (c) `prereqs=["inconnu"]` → false (dangling) ; (d) cycle `A.prereqs=[B]`, `B.prereqs=[A]` → false ; (e) `giver==""` → false ; (f) quête sans étape → false ; (g) étape `target==""` → false.

- [ ] **Step 2 : Vérif échec CI**.

- [ ] **Step 3 : Implémenter `Validate`** — règles : id non vide + unique ; giver/turnIn non vides ; ≥1 étape ; chaque `type` ∈ {kill,collect,talk,enter}, `target` non vide, `requiredCount≥1` ; chaque prereq existe dans l'ensemble ; **détection de cycle** par DFS sur le graphe prereqs (coloriage blanc/gris/noir) ; items reward `itemId>0 && quantity≥1`. Messages d'erreur lisibles (FR). `///` sur chaque fonction/aide.

- [ ] **Step 4 : Vérif succès CI** ; **Step 5 : Commit** — `feat(quests-editor): QuestEditIo::Validate (unicité, prereqs, cycle, formes)`.

---

## Task 3 : QuestEditIo — Save (3 fichiers, pur JSON)

**Files:** Modify `src/world_editor/quests/QuestEditIo.{h,cpp}`, `QuestEditIoTests.cpp`.

**Interfaces — Produces:**
```cpp
/// Écrit les 3 fichiers sous <contentRoot>/quests/ (pur JSON pretty). Effet de bord DISQUE.
/// quest_givers.json est RÉGÉNÉRÉ depuis giver/turnIn. \return false + outError si échec.
bool Save(const std::string& contentRoot, const std::vector<EditedQuest>& quests, std::string& outError) const;
```

- [ ] **Step 1 : Test (échec attendu)** — round-trip : partir d'un `std::vector<EditedQuest>` en mémoire → `Save(<tmp>, quests, err)` → `Load(<tmp>, reloaded, err)` → égalité structurelle (id/giver/turnIn/steps/rewards/title/stepLabels). PLUS : parser le `quest_givers.json` écrit et vérifier que `npc:elder_marn` a `{kill_10_boars, role0}` ET `{kill_10_boars, role1}` (giver+turnIn = même npc). PLUS : vérifier que le `quest_definitions.json` écrit est du **pur JSON** (contient `"quests": [` et PAS `"count"`).

- [ ] **Step 2 : Vérif échec CI**.

- [ ] **Step 3 : Implémenter `Save`** — sérialiser à la main via `std::ostringstream` (helpers `JsonEscape`/`Num` copiés du patron `BuildingTemplateLibrary.cpp`) :
  - `quest_definitions.json` : `{ "quests": [ {id,giver,turnIn,prereqs[],steps[{type,target,requiredCount}],rewards{xp,gold,items[{itemId,quantity}]}} ] }` — **pur JSON, tableaux**.
  - `quest_texts.fr.json` : `{ "<id>": {title,description,steps[]} }`.
  - `quest_givers.json` : parcourir toutes les quêtes ; pour chaque `giver` ajouter `{questId,role:0}` sous la clé npc, pour chaque `turnIn` ajouter `{questId,role:1}` ; grouper par npcTargetId. Écrire `{ "<npc>": [ {questId,role} ] }`.
  - Écrire chaque fichier via `std::ofstream(path, binary|trunc)`. `///` + effet de bord disque documenté.

- [ ] **Step 4 : Vérif succès CI** ; **Step 5 : Commit** — `feat(quests-editor): QuestEditIo::Save (3 fichiers pur JSON + régénération givers)`.

---

## Task 4 : QuestEditorPanel (IPanel) + enregistrement shell

**Files:** Create `src/world_editor/panels/QuestEditorPanel.{h,cpp}` ; Modify `src/world_editor/core/WorldEditorShell.{h,cpp}`, CMake. **Intégration — pas de test unitaire** (UI éditeur, validée compil CI + en éditeur).

- [ ] **Step 1 : Créer `QuestEditorPanel`** — hérite de `IPanel` (`GetName()="Quest Editor"`, `Render()`, `IsVisible/SetVisible`). Injections : `SetContentRoot(std::string)`, `SetIo(QuestEditIo*)` (ou possède un `QuestEditIo` interne). Membres : `std::vector<EditedQuest> m_quests;` (chargées au 1er Render via `m_io->Load`), index sélectionné, buffers d'édition (id/giver/turnIn/steps/rewards/textes), `std::string m_status`.
  `Render()` (patron `BuildingEditorPanel::Render`) :
  - combo « Charger » (liste des ids) + bouton « Nouvelle quête » ;
  - champs id/giver/turnIn ; multi-sélection prereqs ; liste d'étapes (combo type + target + requiredCount, +/×) ; rewards (xp/gold/items) ; textes (title/description/stepLabels) ;
  - bouton **Enregistrer** → `m_io->Validate(m_quests, errs)` ; si OK `m_io->Save(m_contentRoot, m_quests, err)` ; sinon afficher les erreurs dans `m_status`.
  **`///` sur CHAQUE méthode/lambda nommée** (rôle, effet de bord disque au save, thread main/ImGui). Commentaires FR. Pas d'aperçu 3D.

- [ ] **Step 2 : Enregistrer dans le shell** — `WorldEditorShell::Init` (`WorldEditorShell.cpp:~136`, après le bloc `BuildingEditorPanel`) : instancier `QuestEditorPanel`, `SetContentRoot(cfg.GetString("paths.content","game/data"))`, `SetIo(&m_questEditIo)` (ajouter un membre `QuestEditIo m_questEditIo;` au shell), `m_panels.emplace_back(std::move(...))`. Le rendu est **automatique** (boucle `RenderFrame:431`). `///` sur toute lambda/fonction ajoutée.

- [ ] **Step 3 : CMake** — ajouter `QuestEditIo.cpp` **et** `QuestEditorPanel.cpp` à la liste de sources de la cible **`world_editor_app`** (repérer où `BuildingEditorPanel.cpp`/`WorldEditorShell.cpp` sont listés). **PAS** dans `engine_core` (QuestEditIo.cpp est déjà dans le test ; l'y mettre dans engine_core créerait un doublon au link du test).

- [ ] **Step 4 : Vérif compilation CI** — `cmake --build --preset linux-x64-release --target world_editor_app` (via CI) doit passer ; `ctest` reste vert.

- [ ] **Step 5 : Commit** — `feat(quests-editor): QuestEditorPanel + enregistrement dans WorldEditorShell`.

---

## Task 5 : Documentation

- [ ] **Step 1** — `CODEBASE_MAP.md` : section « Quêtes — authoring éditeur (SP4) » : `QuestEditorPanel` + `QuestEditIo` (load/validate/save 3 fichiers, régénération givers), greffe dans le shell, restart shard pour charger. Renvoyer à la spec SP4.

- [ ] **Step 2 : Commit** — `docs(quests): authoring éditeur SP4 dans CODEBASE_MAP`.

---

## Checklist finale (DoD SP4)

- [ ] `QuestEditIo` Load/Validate/Save + tests verts (round-trip, validation, cycle, régénération givers).
- [ ] `quest_definitions.json` écrit en **pur JSON** (round-trip relisible par le shard).
- [ ] `quest_givers.json` régénéré cohérent ; `quest_texts.fr.json` écrit.
- [ ] `QuestEditorPanel` (IPanel) : charger/éditer/valider/enregistrer + statut ; greffé dans `WorldEditorShell::Init`.
- [ ] CMake : sources dans `world_editor_app` (pas engine_core) ; test enregistré.
- [ ] **Toutes** les fonctions `src/world_editor/` nouvelles documentées `///` (FR).
- [ ] CI verte (build-linux world_editor_app + ctest). Rapport : ✅ outillage éditeur, restart shard pour le contenu créé.

---

## Self-review (effectuée)
- **Couverture spec** : §2/§5 I/O → Tasks 1,3 ; §4 validation → Task 2 ; §3 UX panneau → Task 4 ; §8 doc rule → toutes les tasks + Task 5.
- **Points « lire le voisin »** : liste de sources `world_editor_app` dans le CMake (Task 4 Step 3) ; helpers `JsonEscape`/`Num` (BuildingTemplateLibrary) ; `JsonParser` inline (QuestRuntime) ; call-site d'instanciation (WorldEditorShell.cpp:128-136).
- **Piège CMake** explicité (QuestEditIo.cpp dans world_editor_app + test, PAS engine_core).
- **Doc rule** rappelée à chaque task (spécificité éditeur).
