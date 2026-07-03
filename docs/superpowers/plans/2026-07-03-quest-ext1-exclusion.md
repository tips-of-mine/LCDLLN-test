# EXT-1 — Exclusion / contre-quêtes — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development.

**Goal:** Ajouter le champ `excludes` (quêtes mutuellement exclusives) au système B (shard) + support éditeur, sans toucher au wire ni au client. Spec : `docs/superpowers/specs/2026-07-03-quest-ext1-exclusion-design.md`.

## Global Constraints
- Commentaires FR ; PascalCase nouveaux symboles ; pas de « CMANGOS » nouveau. Pas de build local → CI = gate.
- Tests **non-strippables** (CI Release `-DNDEBUG` strippe `assert()` nu) : `std::cerr` + compteur + `return 1`, sauf fichier faisant `#undef NDEBUG`.
- **Aucun changement de wire** : `excludes` est purement serveur (le client ne voit que le statut via `QuestDelta`). Ne pas toucher `ServerProtocol`/`kProtocolVersion`/le client.
- Règle éditeur (CLAUDE.md) : doc `///` sur **toute** fonction ajoutée dans `src/world_editor/`.
- **Déploiement** : ⚠️ REDÉPLOIEMENT SHARD ; client inchangé.

---

## Task 1 : Shard — données, gates, tests
**Files:** `src/shardd/gameplay/quest/QuestRuntime.{h,cpp}`, `src/shared/server_bootstrap/ServerApp.cpp`, `src/shardd/gameplay/quest/QuestRuntimeTests.cpp`.

- [ ] **Step 1 (RED)** — Écrire les tests QuestRuntime AVANT le code (spec §4 a–e) : offer-sync garde B `Locked` si A engagée ; accept refusé si A engagée (via un helper appelable, cf. Step 4) ; symétrie ; pas de blocage si seulement `Offered` ; rétro-compat sans `excludes`. Construire les fixtures via le pattern `MakeRuntimeWithFixture(json)` existant.
- [ ] **Step 2** — `QuestRuntime.h` : `std::vector<std::string> excludedQuestIds;` dans `QuestDefinition` ; déclarer `bool IsBlockedByExclusion(const std::vector<QuestState>&, const QuestDefinition&) const;` (public) + `bool IsQuestEngaged(const std::vector<QuestState>&, const std::string&) const;` (privé).
- [ ] **Step 3** — `QuestRuntime.cpp LoadDefinitions` (~885) : parser `"excludes"` optionnel, **miroir exact** du bloc `prereqs` (~885-909).
- [ ] **Step 4** — `QuestRuntime.cpp` : implémenter `IsQuestEngaged` (status ∈ {Active, ReadyToTurnIn, Completed}) + `IsBlockedByExclusion` (sens direct `def.excludedQuestIds` **ET** sens symétrique : balayer `m_definitions` pour toute def dont `excludedQuestIds` contient `def.questId`).
- [ ] **Step 5** — `SyncQuestStates` (~612) : ne passer à `Offered` que si `prerequisitesComplete && !IsBlockedByExclusion(states, definition)`.
- [ ] **Step 6** — `ServerApp::HandleAcceptQuest` (5356) : après `CanAccept` (et cohérent avec le gate `IsClientNearNpc`), ajouter `if (m_questRuntime.IsBlockedByExclusion(client->questStates, *def)) { LOG_WARN(...); return; }`.
- [ ] **Step 7 (GREEN)** — Vérifier que les tests passent (raisonnement, pas de build local). Ajouter la cible test au `CMakeLists` **si** un nouveau fichier test est créé (sinon étendre `QuestRuntimeTests.cpp` existant, déjà câblé).
- [ ] **Step 8 : Commit** — `feat(quests): champ excludes (quêtes mutuellement exclusives) côté shard`.

## Task 2 : Éditeur — I/O, panneau, tests
**Files:** `src/world_editor/quests/QuestEditIo.{h,cpp}`, `src/world_editor/panels/QuestEditorPanel.{h,cpp}`, le test QuestEditIo existant.

- [ ] **Step 1 (RED)** — Tests QuestEditIo : round-trip `excludes` (parse→serialize→parse identiques) ; rejet auto-exclusion (`id` dans son propre `excludes`) ; rejet id inexistant ; sortie relisible.
- [ ] **Step 2** — `EditableQuest.excludes` (vector<string>) ; parse `"excludes"` (miroir prereqs ~576) ; validation (existence + anti-auto-exclusion) ; **pas** de cycle-check.
- [ ] **Step 3** — Sérialisation `"excludes"` (miroir prereqs ~807), JSON pur.
- [ ] **Step 4** — `QuestEditorPanel` : `m_excludesBuffer` + `RenderExcludesSection()` (miroir `RenderPrereqSection`, cases à cocher des autres quêtes, `id != q.id`) ; Load/Save buffer (miroir 45/62/80). Doc `///` sur `RenderExcludesSection` + tout helper.
- [ ] **Step 5 (GREEN)** — Vérifier tests. Rappel **contrainte CMake** : `QuestEditorPanel.cpp`/`QuestEditIo.cpp` sont dans `engine_core` (car `WorldEditorShell` dans engine_core les instancie, et `engine_app` jeu linke engine_core via `--editor-world`) — ne rien déplacer, juste éditer.
- [ ] **Step 6 : Commit** — `feat(quests): champ excludes dans l'éditeur de quêtes (I/O + panneau)`.

## Task 3 : Docs + revue finale
- [ ] **Step 1** — `CODEBASE_MAP.md` : dans la section Quêtes, documenter `excludes` (exclusion mutuelle, serveur-seul, symétrique). Renvoyer à la spec EXT-1.
- [ ] **Step 2 : Commit** — `docs(quests): champ excludes documenté dans CODEBASE_MAP`.
- [ ] **Step 3 : Revue finale de branche** — compile-safety (shard + éditeur + engine_app), aucun wire touché, rétro-compat contenu, doc éditeur présente.

## Checklist finale
- [ ] 2 gates (offer-sync + accept), exclusion symétrique, tests shard 5 cas verts.
- [ ] Éditeur : I/O + validation + panneau + tests round-trip.
- [ ] CI verte (shard + éditeur + client inchangé). Rapport : ⚠️ REDÉPLOIEMENT SHARD, client inchangé.
