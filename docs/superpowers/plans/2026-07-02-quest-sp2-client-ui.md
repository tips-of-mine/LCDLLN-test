# SP2 — Client : rendu & interaction des quêtes — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Afficher et rendre jouable côté client le système de quêtes du shard (SP1) : journal, tracker HUD, interaction PNJ (dialogue + panneau donneur), marqueur world-space, textes lisibles.

**Architecture:** 100 % client, aucun nouveau wire (SP1 fournit opcodes 92/93/94 + `Encode/Decode`). Réception giver-list via `UIModelBinding`; envoi accept/turn-in via `GameplayUdpClient`; nouveau `QuestImGuiRenderer` branché dans la boucle ImGui in-game; textes via `QuestTextCatalog` (`quest_texts.<lang>.json`); marqueur procédural réutilisant l'overlay world-space des interactables.

**Tech Stack:** C++20, ImGui, `src/client/{quest,render,net,ui_common,dialogue,localization}`, contenu `game/data/quests/`.

## Global Constraints

- **Commentaires en français** ; clarté > brièveté.
- **PascalCase** pour tout nouveau symbole/fichier.
- **Jamais le terme « CMANGOS »**.
- **Pas de build local** : vérification via CI (`build-linux` → `ctest`). Étapes « run test » = commande `ctest` attendue ; ne pas compiler en local.
- **Tests non-strippables** : la CI compile en Release (`-DNDEBUG`) → `assert()` strippé SAUF si le fichier fait `#undef NDEBUG` avant `<cassert>`. Pour tout NOUVEAU fichier de test, utiliser le style non-strippable (`std::cerr`/compteur + `return 1`, comme `QuestRuntimeTests.cpp`) OU mettre la garde `#undef NDEBUG` en tête.
- `questId` = `std::string` (système B) partout côté client quête.
- **Déploiement** : ✅ client only, mais **lock-step avec le shard SP1 déployé** (sinon pas de giver-list ni de transitions).
- **UI in-game** : `QuestImGuiRenderer` et le marqueur ne sont pas testables unitairement (validés en jeu / SP5) — validés par compilation CI + revue.

---

## Structure de fichiers

**Créés :**
- `src/client/quest/QuestTextCatalog.{h,cpp}` — charge `quest_texts.<lang>.json`, résout titres/descriptions/libellés.
- `src/client/quest/QuestGiverTable.{h,cpp}` — charge `quest_givers.json`, mappe `npcTargetId → [{questId, role}]`.
- `src/client/render/QuestImGuiRenderer.{h,cpp}` — journal + tracker + panneau donneur.
- `game/data/quests/quest_texts.fr.json`, `game/data/quests/quest_givers.json` — contenu (exemples).
- Tests : `QuestTextCatalogTests.cpp`, `QuestGiverTableTests.cpp`, ajouts à un test UIModel.

**Modifiés :**
- `src/client/ui_common/UIModel.{h,cpp}` — champ `giverList` + `ApplyQuestGiverList` + dispatch.
- `src/client/net/GameplayUdpClient.{h,cpp}` — `SendQuestAcceptRequest`/`SendQuestTurnInRequest`.
- `src/client/dialogue/DialogueTree.h` + `DialogueConfigLoader.cpp` + `DialoguePresenter.h` — champ `questKey` + callback enrichi.
- `src/client/app/Engine.{h,cpp}` — bind/rendu `QuestImGuiRenderer`, `QuestUiPresenter::Init`, callback dialogue → envoi shard, marqueur donneur, réception giver-list.
- `src/CMakeLists.txt` — nouvelles cibles de test + sources.

---

## Task 1 : QuestTextCatalog (textes lisibles)

**Files:** Create `src/client/quest/QuestTextCatalog.{h,cpp}`, `game/data/quests/quest_texts.fr.json`, `src/client/quest/QuestTextCatalogTests.cpp` ; Modify `src/CMakeLists.txt`.

**Interfaces — Produces:**
- `class QuestTextCatalog { bool Load(const engine::core::Config&, std::string_view locale); std::string Title(std::string_view questId) const; std::string Description(std::string_view questId) const; std::string StepLabel(std::string_view questId, size_t stepIndex, uint32_t current, uint32_t required) const; }`
- Fallback : clé absente → `Title` renvoie le `questId`, `StepLabel` renvoie `"<current>/<required>"`.

- [ ] **Step 1 : Test (échec attendu)** — créer `QuestTextCatalogTests.cpp`, style non-strippable (`std::cerr`+compteur+`return 1`, cf. `src/shardd/gameplay/quest/QuestRuntimeTests.cpp`). Écrire un `quest_texts` de fixture (`{ "q1": {"title":"T","description":"D","steps":["Tués {current}/{required}"]} }`) via le même patron de fixture que `QuestRuntimeTests` (temp dir + `Config::SetValue("paths.content", …)` + `FileSystem::WriteAllTextContent`). Asserts : `Title("q1")=="T"`, `StepLabel("q1",0,3,10)=="Tués 3/10"`, `Title("absent")=="absent"`, `StepLabel("absent",0,1,2)=="1/2"`.

- [ ] **Step 2 : Enregistrer le test** dans `src/CMakeLists.txt` : `lcdlln_add_simple_test(quest_text_catalog_tests <…>/QuestTextCatalogTests.cpp <…>/QuestTextCatalog.cpp)`.

- [ ] **Step 3 : Vérif échec CI** — `ctest -R quest_text_catalog_tests` → FAIL (symboles absents).

- [ ] **Step 4 : Implémenter `QuestTextCatalog`** — parse JSON (réutiliser `JsonParser`/`FindObjectMember` comme `QuestRuntime::LoadDefinitions`), stocke `unordered_map<string, {title, description, vector<string> stepTemplates}>`. `StepLabel` : si un template existe, remplacer `{current}`/`{required}` (helper de substitution simple) ; sinon `to_string(current)+"/"+to_string(required)`. Résout le chemin `quests/quest_texts.<locale>.json` (fallback `fr` si locale absente). Commentaires FR.

- [ ] **Step 5 : Contenu** — écrire `game/data/quests/quest_texts.fr.json` avec au moins l'entrée `kill_10_boars` (titre/description/étape « Sangliers tués : {current}/{required} »).

- [ ] **Step 6 : Vérif succès CI** — `ctest -R quest_text_catalog_tests` → PASS.

- [ ] **Step 7 : Commit** — `feat(quests-client): QuestTextCatalog + quest_texts.fr.json`.

---

## Task 2 : QuestGiverTable (mapping PNJ → quêtes)

**Files:** Create `src/client/quest/QuestGiverTable.{h,cpp}`, `game/data/quests/quest_givers.json`, `src/client/quest/QuestGiverTableTests.cpp` ; Modify `src/CMakeLists.txt`.

**Interfaces — Produces:**
- `struct QuestGiverLink { std::string questId; uint8_t role; }; // 0=giver,1=turnIn`
- `class QuestGiverTable { bool Load(const engine::core::Config&); const std::vector<QuestGiverLink>* ForNpc(std::string_view npcTargetId) const; }`

- [ ] **Step 1 : Test (échec attendu)** — `QuestGiverTableTests.cpp` (style non-strippable + fixture). Fixture `quest_givers.json` : `{ "npc:elder_marn": [ {"questId":"kill_10_boars","role":0}, {"questId":"kill_10_boars","role":1} ] }`. Asserts : `ForNpc("npc:elder_marn")->size()==2` ; `ForNpc("npc:inconnu")==nullptr`.

- [ ] **Step 2 : Enregistrer le test** (`lcdlln_add_simple_test quest_giver_table_tests …`).

- [ ] **Step 3 : Vérif échec CI**.

- [ ] **Step 4 : Implémenter `QuestGiverTable`** — parse JSON (map de `npcTargetId` → `vector<QuestGiverLink>`), chemin `quests/quest_givers.json`. Rejet clair si un `role` n'est pas 0/1 ou un `questId` vide. Commentaires FR.

- [ ] **Step 5 : Contenu** — `game/data/quests/quest_givers.json` cohérent avec `quest_definitions.json` (mêmes `giver`/`turnIn`).

- [ ] **Step 6 : Vérif succès CI** ; **Step 7 : Commit** — `feat(quests-client): QuestGiverTable + quest_givers.json`.

---

## Task 3 : Réception QuestGiverList (UIModelBinding)

**Files:** Modify `src/client/ui_common/UIModel.{h,cpp}` ; Test : ajouter à un test UIModel existant OU créer `UIModelQuestGiverTests.cpp` + CMake.

**Interfaces — Produces:**
- `UIModel` gagne `struct UIQuestGiverEntry { std::string questId; uint8_t role; }; struct UIQuestGiverList { std::string npcTargetId; std::vector<UIQuestGiverEntry> entries; } giverList;`
- `bool UIModelBinding::ApplyQuestGiverList(std::span<const std::byte> packet);`

- [ ] **Step 1 : Test (échec attendu)** — round-trip : `EncodeQuestGiverList` (fourni par SP1) → `ApplyQuestGiverList` → vérifier `model.giverList.npcTargetId` + `entries`. Style non-strippable.

- [ ] **Step 2 : Enregistrer/rattacher le test** au CMake.

- [ ] **Step 3 : Vérif échec CI**.

- [ ] **Step 4 : Implémenter** — dans `UIModelBinding::Apply(...)` (dispatch `MessageKind`, `UIModel.cpp:~570`) ajouter `case engine::server::MessageKind::QuestGiverList: return ApplyQuestGiverList(packet);`. Implémenter `ApplyQuestGiverList` (décode via `engine::server::DecodeQuestGiverList` + copie vers `m_model.giverList`), sur le modèle de `ApplyQuestDelta` (`UIModel.cpp:1266`). Déclarer la struct + le champ dans `UIModel.h`.

- [ ] **Step 5 : Vérif succès CI** ; **Step 6 : Commit** — `feat(quests-client): réception QuestGiverList → UIModel.giverList`.

---

## Task 4 : Envoi accept/turn-in + câblage dialogue (questKey)

**Files:** Modify `src/client/net/GameplayUdpClient.{h,cpp}`, `src/client/dialogue/DialogueTree.h`, `src/client/dialogue/DialogueConfigLoader.cpp`, `src/client/dialogue/DialoguePresenter.h`, `src/client/app/Engine.cpp` ; Test : `DialogueConfigLoaderTests` (existe ? sinon ajouter un cas au test dialogue existant).

**Interfaces — Produces:**
- `bool GameplayUdpClient::SendQuestAcceptRequest(uint32_t clientId, const std::string& questId, const std::string& giverTargetId);`
- `bool GameplayUdpClient::SendQuestTurnInRequest(uint32_t clientId, const std::string& questId, const std::string& npcTargetId);`
- `DialogueChoice` gagne `std::string questKey;`
- `SetQuestActionCallback` signature enrichie : `std::function<void(DialogueAction, int questId, const std::string& questKey)>`.

- [ ] **Step 1 : Envoi shard** — dans `GameplayUdpClient.cpp`, ajouter `SendQuestAcceptRequest`/`SendQuestTurnInRequest` **exactement** sur le patron de `SendTalkRequest` (`GameplayUdpClient.cpp:246-262`) : construire `QuestAcceptRequestMessage{clientId, questId, giverTargetId}` (resp. `QuestTurnInRequestMessage{…, npcTargetId}`), `Encode…`, `SendBytes`, logs FR. Déclarer dans le `.h`.

- [ ] **Step 2 : `questKey` (test parse d'abord)** — ajouter/parser `questKey` : test que le loader lit `"questKey":"kill_10_boars"` sur un choix. (Suivre le test de dialogue existant s'il y en a un ; sinon ajouter un cas minimal.) FAIL attendu.

- [ ] **Step 3 : Implémenter `questKey`** — champ `std::string questKey;` dans `DialogueChoice` (`DialogueTree.h`) ; parse dans `DialogueConfigLoader.cpp` (à côté de `questId`/`action`) ; test PASS.

- [ ] **Step 4 : Callback enrichi** — changer `SetQuestActionCallback` (`DialoguePresenter.h:88`) pour transmettre `questKey` (récupéré du choix) en plus de l'`action`/`questId`. Mettre à jour l'appel du callback dans `DialoguePresenter.cpp` (là où il fait `m_questActionCb(choice.action, choice.questId)`) → ajouter `choice.questKey`.

- [ ] **Step 5 : Brancher dans Engine** — au point où `m_dialogue.SetQuestActionCallback(...)` est (ou doit être) défini, implémenter : `AcceptQuest` → `m_gameplayUdp.SendQuestAcceptRequest(clientId, questKey, currentNpcTargetId)` ; `CompleteQuest` → `SendQuestTurnInRequest(...)`. Le `currentNpcTargetId` = cible du PNJ en cours d'interaction (même valeur que le `targetId` passé à `SendTalkRequest` pour ce PNJ — repérer comment Engine la connaît). Documenter le mapping action→opcode.

- [ ] **Step 6 : Vérif CI** (tests parse verts ; le reste = compilation) ; **Step 7 : Commit** — `feat(quests-client): envoi accept/turn-in shard + questKey dialogue`.

---

## Task 5 : QuestImGuiRenderer (journal + tracker + panneau donneur)

**Files:** Create `src/client/render/QuestImGuiRenderer.{h,cpp}` ; Modify `src/client/app/Engine.{h,cpp}`. **Intégration — pas de test unitaire** (UI in-game, validée compilation CI + revue + smoke SP5).

- [ ] **Step 1 : Créer le renderer** — patron **exact** de `src/client/render/ChatImGuiRenderer.{h,cpp}` (`BindQuestUi(QuestUiPresenter*, const QuestTextCatalog*, const Config*)` + `Render(float w, float h, bool inWorldShard)`, no-op si non bindé). Trois panneaux depuis `QuestUiPresenter::GetState()` (`QuestUiState`) et `QuestTextCatalog` :
  - **Journal** : quêtes `Active`/`ReadyToTurnIn` (exclure `Offered`), détail (titre/description/étapes `StepLabel`/récompenses). Bounds : `QuestUiState.journalPanelBounds`.
  - **Tracker HUD** : `QuestUiState.trackerSteps`, encart compact.
  - **Panneau donneur** : depuis `UIModel.giverList` (passer le `UIModel`/giverList au renderer ou l'exposer via le presenter) — boutons Accepter/Terminer déclenchant un callback fourni par Engine (qui appelle `GameplayUdpClient::SendQuestAcceptRequest/TurnIn`).
- [ ] **Step 2 : Brancher dans Engine** — instancier `m_questImGui`, `BindQuestUi(&m_questUi, &m_questTextCatalog, &m_cfg)`, appeler `m_questImGui->Render(dw, dh, m_authUi.IsInWorldShard())` dans la boucle ImGui in-game (près de `Engine.cpp:10556`, à côté de `m_dialogueImGui->Render`). Appeler `m_questUi.Init(m_cfg)` à l'init (jamais fait). Charger `m_questTextCatalog`/`m_questGiverTable` à l'init.
- [ ] **Step 3 : Vérif compilation CI** (`ctest` complet reste vert ; nouveau code compile) ; **Step 4 : Commit** — `feat(quests-client): QuestImGuiRenderer (journal, tracker, panneau donneur)`.

---

## Task 6 : Marqueur world-space donneur (rune procédurale)

**Files:** Modify `src/client/app/Engine.cpp` (bloc overlay interactables ~L10580) + `config.json` (clé). **Intégration — pas de test unitaire.**

- [ ] **Step 1 : Rendu marqueur** — dans le bloc overlay des interactables (`Engine.cpp:~10580`, qui itère `m_interactables` et projette via `WorldToScreenPx`), ajouter : pour chaque PNJ, croiser son `npcTargetId` (voir comment le client dérive le `targetId` du Talk pour cet interactable) avec `m_questGiverTable.ForNpc(...)` et les états de `UIModel.quests` → si une quête liée est `Offered` (role giver) → variante « proposer » (doré plein) ; si `ReadyToTurnIn` (role turnIn) → variante « rendre ». Dessiner une **rune procédurale** à l'`ImDrawList` foreground (ex. losange + anneau via `AddConvexPolyFilled`/`AddPolyline`, 2 teintes), ancrée au-dessus de la tête (offset ~2.2 m).
- [ ] **Step 2 : Culling distance** — n'afficher que si `distance(joueur, pnj) <= m_cfg.GetDouble("client.quest.giver_marker_distance_m", 35.0)`, et respecter le masque « masqué en dialogue/menu » déjà appliqué à l'overlay.
- [ ] **Step 3 : Vérif compilation CI** ; **Step 4 : Commit** — `feat(quests-client): marqueur donneur world-space (rune procédurale, 2 variantes, culling distance)`.

---

## Task 7 : Cesser les envois système A (accept/complete/reward)

**Files:** Modify `src/client/quest/QuestUi.cpp` (+ éventuels call-sites Engine).

- [ ] **Step 1** — les actions d'accept/turn-in passent désormais par le shard (Tasks 4/5). Faire en sorte que le client n'émette plus les opcodes master **59/61/63** pour accept/complete/reward : soit retirer les appels `AcceptQuest/CompleteQuest/RewardQuest` (System A) de tout call-site actif, soit les neutraliser proprement avec un commentaire renvoyant au Cleanup (retrait complet du système A = sous-projet Cleanup). **Ne pas** casser la compilation ; documenter. (Le cache `m_questStates` peut rester dormant, retrait complet = Cleanup.)
- [ ] **Step 2 : Vérif compilation CI** ; **Step 3 : Commit** — `refactor(quests-client): accept/turn-in via shard, arrêt des envois système A`.

---

## Task 8 : Documentation

- [ ] **Step 1** — mettre à jour `CODEBASE_MAP.md` (section quêtes) : côté client, `QuestImGuiRenderer` + `QuestTextCatalog`/`QuestGiverTable` + marqueur + interaction dialogue/panneau donneur ; renvoyer aux specs SP2.
- [ ] **Step 2 : Commit** — `docs(quests): rendu client SP2 dans CODEBASE_MAP`.

---

## Checklist finale (DoD SP2)

- [ ] `QuestTextCatalog` + `quest_texts.fr.json` (libellés lisibles, fallback) — testé.
- [ ] `QuestGiverTable` + `quest_givers.json` — testé.
- [ ] Réception `QuestGiverList` → `UIModel.giverList` — testé (round-trip).
- [ ] `SendQuestAcceptRequest`/`SendQuestTurnInRequest` + `questKey` dialogue + callback branché.
- [ ] `QuestImGuiRenderer` (journal/tracker/panneau donneur) bindé + rendu ; `QuestUiPresenter::Init` appelé.
- [ ] Marqueur donneur (rune procédurale, 2 variantes, culling distance, masqué en dialogue).
- [ ] Client n'émet plus 59/61/63 (System A) pour accept/complete/reward.
- [ ] Tests CI verts ; commentaires FR ; rapport final : ✅ client only, lock-step déploiement shard SP1.

---

## Self-review (effectuée)
- **Couverture spec** : §3.1→T3 ; §3.2→T4 ; §3.3→T5 ; §3.4→T1 ; §3.5→T4 ; §3.6→T6 ; retrait A→T7.
- **Points « lire le voisin »** (assumés, non inventés) : patron `ChatImGuiRenderer` pour le renderer ; comment Engine dérive le `npcTargetId` d'un interactable (Talk) ; où `SetQuestActionCallback` est/doit être posé ; `WorldToScreenPx` + le masque de culling de l'overlay.
- **Cohérence types** : `questId`=`std::string` ; `role` uint8 (0=giver,1=turnIn) cohérent entre `QuestGiverTable`, `UIModel.giverList` et le wire SP1.
- **Testabilité** : logique (catalogue, table, réception) en TDD ; UI (renderer, marqueur) en intégration validée compilation + jeu (SP5).

---

## Task 4b : `npcTargetId` des interactables + Talk à l'ouverture du dialogue (fondation)

**Motif (revue T4)** : `OpenDialogue` ouvre le dialogue PNJ **sans Talk**, donc `giverList.npcTargetId` est vide/périmé → l'accept/turn-in par dialogue cible le mauvais PNJ (serveur rejette). `InteractableEntity` n'a pas d'id réseau. Fondation aussi requise par le **marqueur (Task 6)**.

**Files:** Modify `src/client/app/Engine.{h,cpp}`, `config.json` (+ contenu). **Intégration — pas de test unitaire** (validée compil CI + jeu).

- [ ] **Step 1** — `InteractableEntity` (`Engine.h:854`) gagne `std::string npcTargetId;`. Chargé depuis `world.interactables.<i>.npc_target_id` (`Engine.cpp:5279-5300`), fallback vide.
- [ ] **Step 2** — À `OpenDialogue` (`Engine.cpp:~9882`), si `entity.npcTargetId` non vide et le réseau gameplay est prêt : `m_gameplayUdp.SendTalkRequest(m_gameplayUdp.ServerClientId(), entity.npcTargetId)` (fire l'event Talk + déclenche `QuestGiverList` pour CE PNJ) et stocker `m_currentDialogueNpcTargetId = entity.npcTargetId`.
- [ ] **Step 3** — La lambda `SetQuestActionCallback` (Task 4, `Engine.cpp:~8790`) utilise **`m_currentDialogueNpcTargetId`** (au lieu de `giverList.npcTargetId`) comme cible d'accept/turn-in. Réinitialiser à la fermeture du dialogue.
- [ ] **Step 4** — Contenu : ajouter `npc_target_id` (ex. `"npc:elder_marn"`) au(x) interactable(s) PNJ dans `config.json`, cohérent avec `quest_givers.json`/`quest_definitions.json`.
- [ ] **Step 5 : Vérif compil CI** ; **Commit** — `feat(quests-client): npcTargetId interactables + Talk à l'ouverture du dialogue`.
