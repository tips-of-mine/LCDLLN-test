# Cleanup — Retrait du système A (master, opcodes 59-67)

**Date** : 2026-07-02
**Statut** : design (à relire)
**Doc parent** : `2026-07-02-quest-system-overview-design.md`
**Portée** : supprimer le **système A** des quêtes (vieille status-machine côté MASTER, opcodes 59-67), remplacé par le système B (shard). Serveur + wire + client + DB + tests.

> ⚠️ **Déploiement** : **REDÉPLOIEMENT MASTER requis** (retrait du `QuestHandler` + opcodes 59-67) **en lock-step avec le client** (qui retire l'émission/réception système A). Pas de migration DB à rejouer (on retire juste la création de la table `account_quest_state` ; la table existante est laissée en place, non-DROP). NE touche PAS le système B (shard).

---

## 1. Verdict (cartographie 2026-07-02) : système A = 100 % mort

- `QuestUiPresenter::RequestQuestList()` (opcode 65) : appelé **uniquement** par la commande debug `/quest` (`Engine.cpp:~1903`), jamais au login/EnterWorld/gameplay.
- `AcceptQuest`/`CompleteQuest`/`RewardQuest` (59/61/63) : **plus aucun appel** (retirés en SP2 Task 7 ; le TODO Cleanup est dans `QuestUi.h`).
- Cache `m_questStates` (System A) : **jamais lu** pour l'affichage. `RebuildJournal`/`RebuildTracker`/`RebuildMinimap` lisent **uniquement** `UIModel.quests` (système B). `GetCachedStatus`/`GetCachedStates` : zéro appelant.
- Handlers `OnQuest*Response`/`OnQuestStateUpdate` (60/62/64/66/67) : drains sans effet visible.

→ Retrait sûr. Seul « effet » : la commande debug `/quest` disparaît (aucun joueur ne l'utilise).

**⚠️ NE PAS confondre avec le système B (à CONSERVER)** : `ServerProtocol` `MessageKind::QuestDelta`(13)/`QuestGiverList`(92), `QuestAccept/TurnInRequest`(93/94), `QuestDeltaMessage`/`QuestGiverListMessage`, `Encode/DecodeQuestDelta`/`...GiverList`, `kProtocolVersion=14`, `QuestRuntime` (shard), `UIModel::ApplyQuestDelta`/`ApplyQuestGiverList`, `QuestImGuiRenderer`, la lambda dialogue `SetQuestActionCallback`.

---

## 2. Retraits

### 2.1 Fichiers supprimés entièrement (système A seul)
- `src/masterd/quests/QuestState.h`
- `src/masterd/quests/QuestStateTests.cpp`
- `src/masterd/quests/MysqlQuestStateStore.{h,cpp}`
- `src/masterd/handlers/quest/QuestHandler.{h,cpp}`
- `src/shared/network/QuestPayloads.{h,cpp}`
- `src/shared/network/QuestPayloadsTests.cpp`
- `sql/migrations/0048_quest_state.sql` **et** `deploy/docker/sql/migrations/0048_quest_state.sql`

### 2.2 `src/shared/network/ProtocolV1Constants.h`
- Retirer les 9 constantes `kOpcodeQuest*` (59-67). Garder le reste (autres opcodes intacts).

### 2.3 `src/masterd/main_linux.cpp`
- Retirer l'`#include QuestHandler.h` ; le bloc de construction `QuestStateTracker`/`MysqlQuestStateStore`/`QuestHandler` + setters (~528-543) ; `&questHandler` de la capture de la lambda `SetPacketHandler` (~1000) ; le bloc de dispatch des opcodes 59/61/63/65 (~1144-1148).

### 2.4 `src/client/quest/QuestUi.{h,cpp}` — ⚠️ SURGICAL (NE PAS supprimer le fichier)
`QuestUiPresenter` porte le rendu système B (journal/tracker/minimap) : **le fichier reste**. Retirer UNIQUEMENT les membres/méthodes système A :
- méthodes `RequestQuestList`, `AcceptQuest`, `CompleteQuest`, `RewardQuest`, `OnQuestListResponse`, `OnQuestAcceptResponse`, `OnQuestCompleteResponse`, `OnQuestRewardResponse`, `OnQuestStateUpdate`, `GetCachedStatus`, `GetCachedStates` ;
- membres `m_questStates`, `m_send` ; `SetSendCallback` + le type `SendCallback` ;
- l'`#include "src/shared/network/QuestPayloads.h"` de `QuestUi.h/.cpp` ;
- le commentaire TODO Cleanup (obsolète).
- **Garder** : `Init`/`Shutdown`/`SetViewportSize`/`ApplyModel`/`SelectQuest`/`GetState`/`SetPoiTable`/`SetMinimapRadius` + tout le `Rebuild*` (système B).

### 2.5 `src/client/app/Engine.{h,cpp}` — SURGICAL
- Retirer l'`#include QuestPayloads.h` (Engine.cpp) ; le dispatch des cases 60/62/64/66/67 → `m_questUi.OnQuest*Response`/`OnQuestStateUpdate` (~2685-2741) ; le `m_questUi.SetSendCallback(...)` (System A, ~1550) ; la commande debug `/quest`/`/quests` qui appelle `RequestQuestList` (~1898-1908, + le flag `m_questVisible` s'il ne sert qu'à ça).
- **Garder** : le membre `m_questUi` + `Init`/`SetViewportSize`/`SetPoiTable`/`ApplyModel` (via l'observer `m_uiModelBinding`) + le bind au `QuestImGuiRenderer` (système B).

### 2.6 CMake
- `src/CMakeLists.txt` : retirer `quest_state_tests`.
- `CMakeLists.txt` (racine) : retirer `quest_payloads_tests`.
- **Ne pas** retirer `QuestPayloads.cpp` d'`engine_core` s'il y figure — vérifier : s'il est listé dans `engine_core`, le retirer aussi (le fichier est supprimé).

---

## 3. Décisions à confirmer (relecture)
1. **`QuestUi.{h,cpp}` = retrait chirurgical** (garder le presenter système B, retirer les pièces système A). *Corrige la cartographie qui suggérait une suppression.* OK ?
2. **Commande debug `/quest`** : la **supprimer** (seul appelant de `RequestQuestList` ; System A). OK ? (Alternative : la re-router vers un affichage système B — hors périmètre.)
3. **Table `account_quest_state`** : retirer les fichiers de migration 0048 (soft-deprecation, **pas** de migration DROP). La table existante reste en base, inerte. OK ? (Un DROP explicite = décision ops séparée.)
4. **Pas de bump de version wire** : aucun `kProtocolVersion`-équivalent pour ProtocolV1 (master↔client) n'a été identifié ; le retrait des opcodes 59-67 est géré par le **lock-step master+client**. OK ?

## 4. Tests
- La CI doit rester verte après retrait (compilation master + client + shard + `world_editor_app`). Les tests `quest_state_tests`/`quest_payloads_tests` disparaissent avec leur cible.
- Vérif : `grep` post-retrait de `kOpcodeQuest`/`QuestStateTracker`/`QuestHandler`/`QuestPayloads`/`m_questStates`/`RequestQuestList` = **zéro** occurrence résiduelle (hors système B).
- Pas de nouveau test (retrait pur).

## 5. Hors périmètre
- Tout le système B (shard + client rendu). Le DROP effectif de la table (décision ops). La dette parser JSON.

## 6. Definition of Done
- [ ] 11 fichiers système A supprimés ; opcodes 59-67 retirés ; handler master débranché ; parties système A retirées de `QuestUi`/`Engine` (presenter système B intact).
- [ ] CMake : `quest_state_tests`/`quest_payloads_tests` retirés ; `QuestPayloads.cpp` retiré d'`engine_core` si présent.
- [ ] `grep` de contrôle : zéro référence système A résiduelle. Le journal/tracker/minimap/dialogue (système B) compilent et fonctionnent inchangés.
- [ ] Migration 0048 retirée (×2), sans DROP.
- [ ] CI verte (les 3 binaires + éditeur). Rapport : ⚠️ REDÉPLOIEMENT MASTER + client lock-step.
