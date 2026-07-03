# Cleanup — Retrait système A — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development.

**Goal:** Retirer complètement le système A (master, opcodes 59-67), sans toucher au système B.

**Architecture:** Retrait COORDONNÉ serveur+client+wire en **un seul commit** (les états intermédiaires ne compilent pas — `QuestPayloads.h`/opcodes 59-67 sont inclus des deux côtés). Détail exhaustif dans la spec `2026-07-02-quest-cleanup-systeme-a-design.md` §2.

## Global Constraints
- Commentaires FR ; pas de « CMANGOS » nouveau. Pas de build local → CI (build-linux/windows) = gate.
- **NE PAS toucher au système B** : `ServerProtocol` QuestDelta(13)/QuestGiverList(92)/93/94, `kProtocolVersion 14`, `QuestRuntime`, `UIModel::ApplyQuestDelta/GiverList`, `QuestImGuiRenderer`, la lambda dialogue.
- **Retrait chirurgical** de `QuestUi.{h,cpp}` et `Engine.{h,cpp}` : garder le presenter/wiring système B, retirer seulement le système A.
- **Déploiement** : ⚠️ REDÉPLOIEMENT MASTER + client lock-step.

---

## Task 1 : Retrait coordonné du système A

**Files:** voir spec §2 (11 suppressions + éditions ProtocolV1Constants/main_linux/QuestUi/Engine/CMake×2/migration×2).

- [ ] **Step 1** — Supprimer les 11 fichiers système A (spec §2.1).
- [ ] **Step 2** — `ProtocolV1Constants.h` : retirer les 9 opcodes 59-67 (§2.2).
- [ ] **Step 3** — `main_linux.cpp` : retirer include + construction QuestHandler + capture lambda + dispatch 59/61/63/65 (§2.3).
- [ ] **Step 4** — `QuestUi.{h,cpp}` : retrait chirurgical système A (méthodes, `m_questStates`, `m_send`, `SetSendCallback`, include QuestPayloads.h), GARDER le presenter système B (§2.4).
- [ ] **Step 5** — `Engine.{h,cpp}` : retirer include QuestPayloads.h, dispatch 60/62/64/66/67, `SetSendCallback`, commande `/quest` ; GARDER `m_questUi` + wiring système B (§2.5).
- [ ] **Step 6** — CMake : retirer `quest_state_tests` (`src/CMakeLists.txt`), `quest_payloads_tests` (racine) ; retirer `QuestPayloads.cpp` d'`engine_core` s'il y figure (§2.6).
- [ ] **Step 7 : GREP DE CONTRÔLE** (obligatoire) — `grep -rn` de `kOpcodeQuest`, `QuestStateTracker`, `QuestHandler`, `QuestPayloads`, `MysqlQuestStateStore`, `m_questStates`, `RequestQuestList`, `SetSendCallback.*quest`, `OnQuestListResponse` → **doit renvoyer ZÉRO** (hors docs/specs). Corriger tout résidu. Vérifier que les symboles système B (QuestDelta/QuestGiverList/ApplyQuestDelta/QuestRuntime/QuestImGuiRenderer) sont **intacts**.
- [ ] **Step 8 : Commit** — `refactor(quests): retrait complet du système A (master, opcodes 59-67)`.

---

## Task 2 : Documentation
- [ ] **Step 1** — `CODEBASE_MAP.md` : mettre à jour la mention système A (le cas échéant) → « système A retiré (Cleanup) ; source unique = système B (shard) ». Renvoyer à la spec Cleanup.
- [ ] **Step 2 : Commit** — `docs(quests): système A retiré dans CODEBASE_MAP`.

## Checklist finale
- [ ] Grep de contrôle = zéro résidu système A ; système B intact.
- [ ] CI verte (master + client + shard + world_editor_app). Rapport : ⚠️ redéploiement master + client lock-step.
