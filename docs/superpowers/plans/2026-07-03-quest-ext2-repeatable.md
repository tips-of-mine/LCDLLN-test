# EXT-2 — Répétables / quotidiennes + auto-complete — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development.

**Goal:** Quêtes re-réalisables (mode par quête : repeatable/daily/weekly/cooldown) + `autoComplete`, côté shard + persistance + éditeur, sans wire. Spec : `docs/superpowers/specs/2026-07-03-quest-ext2-repeatable-design.md`. **Branche stackée sur EXT-1** (`claude/quests-ext1`).

## Global Constraints
- FR ; PascalCase nouveaux symboles ; pas de « CMANGOS ». Pas de build local → CI = gate.
- Tests **non-strippables** (Release `-DNDEBUG`) : `std::cerr`+compteur+`return 1`.
- **Aucun wire** : ne pas toucher `ServerProtocol`/`kProtocolVersion`/le client. La persistance (`CharacterPersistence`, format v2) est serveur, pas wire.
- Règle éditeur (CLAUDE.md) : doc `///` sur toute fonction ajoutée dans `src/world_editor/`.
- **Déploiement** : ⚠️ REDÉPLOIEMENT SHARD ; client inchangé ; merger #933 (EXT-1) avant.

---

## Task 1 : Shard — données, parse, autoComplete, refactor reward
**Files:** `QuestRuntime.{h,cpp}`, `ServerApp.cpp`, `QuestRuntimeTests.cpp`.

- [ ] **Step 1 (RED)** — Tests : parse `repeat`/`cooldownHours`/`autoComplete` (défauts + rejet mode invalide + rejet cooldown<=0 si mode cooldown) ; `ApplyEvent` avec `autoComplete=true` → statut `Completed` (pas `ReadyToTurnIn`), et `false` → `ReadyToTurnIn` (inchangé).
- [ ] **Step 2** — `QuestRuntime.h` : `enum class QuestRepeatMode`, champs `repeatMode`/`cooldownHours`/`autoComplete` dans `QuestDefinition` (spec §1.2).
- [ ] **Step 3** — `LoadDefinitions` (~1123, avant le push) : parser `"repeat"`/`"cooldownHours"`/`"autoComplete"` (optionnels, validation §5). Helper `ParseRepeatMode(string)->enum`.
- [ ] **Step 4** — `ApplyEvent` (740-750) : si `definition.autoComplete` → `Completed` au lieu de `ReadyToTurnIn` (§3.1).
- [ ] **Step 5** — `ServerApp` : extraire `GrantQuestReward(ConnectedClient&, const QuestDefinition&)` depuis `HandleTurnInQuest` (5437-5454) ; `HandleTurnInQuest` l'appelle. Dans `ServerApp::ApplyQuestEvent` (5225) : après `ApplyEvent`, pour chaque delta `status==Completed`, retrouver la def + appeler `GrantQuestReward` (§3.2). (`completedAtEpochMs` sera stampé en Task 2.)
- [ ] **Step 6 (GREEN)** + **Commit** : `feat(quests): repeatMode/autoComplete côté shard + refactor GrantQuestReward`.

## Task 2 : Shard — reset temporel + persistance
**Files:** `QuestRuntime.{h,cpp}`, `ServerApp.cpp`, `CharacterPersistence.cpp`, tests shard + `CharacterPersistence` tests.

- [ ] **Step 1 (RED)** — Tests : `ShouldRepeatReset` (none/repeatable/daily borne/weekly borne lundi/cooldown écoulé vs non, `now` fixes) ; `ApplyRepeatResets` (Completed→Locked + delta + stepProgress zéro) ; persistance round-trip `completed_at` + save v1 (défaut 0) + `format_version=2`.
- [ ] **Step 2** — `QuestState.completedAtEpochMs` (`QuestRuntime.h:65`). Helpers purs `UtcDayIndex`/`UtcWeekIndex` + `ShouldRepeatReset` + méthode `ApplyRepeatResets` (§2).
- [ ] **Step 3** — Stamping `completedAtEpochMs = NowUnixEpochMsUtc()` : dans `HandleTurnInQuest` et dans le chemin autoComplete de `ApplyQuestEvent` (Task 1 Step 5).
- [ ] **Step 4** — Call-sites `ApplyRepeatResets` : login (`ServerApp.cpp:~1525`, AVANT `SyncQuestStates`) + Talk/giver-list (localiser l'envoi `QuestGiverList`, appeler reset puis re-sync). Envoyer les deltas.
- [ ] **Step 5** — `CharacterPersistence` : serialize `completed_at` + `format_version=2` (~280) ; deserialize `completed_at` défaut 0 (~150). Compat v1.
- [ ] **Step 6 (GREEN)** + **Commit** : `feat(quests): reset répétable (daily/weekly/cooldown) + persistance completed_at (format v2)`.

## Task 3 : Éditeur
**Files:** `QuestEditIo.{h,cpp}`, `QuestEditorPanel.{h,cpp}`, test QuestEditIo.

- [ ] **Step 1 (RED)** — Tests : round-trip `repeat`/`cooldownHours`/`autoComplete` ; rejet mode invalide ; rejet cooldown<=0 en mode cooldown.
- [ ] **Step 2** — `EditedQuest` (repeatMode/cooldownHours/autoComplete) + parse + validation + sérialisation (§5).
- [ ] **Step 3** — `QuestEditorPanel` : `Combo` mode + `DragInt` cooldown (si mode==cooldown) + `Checkbox` autoComplete ; Load/Save/Reset ; doc `///`. Rappel CMake : fichiers dans `engine_core`, ne pas déplacer.
- [ ] **Step 4 (GREEN)** + **Commit** : `feat(quests): repeat/cooldown/autoComplete dans l'éditeur de quêtes`.

## Task 4 : Contenu, docs, revue finale
- [ ] **Step 1** — `game/data/quests/quest_definitions.json` : passer une quête existante (ex. `kill_10_boars`) en `"repeat":"daily"` OU ajouter une petite quête répétable d'exemple (validation en jeu). JSON validé via PowerShell `ConvertFrom-Json`.
- [ ] **Step 2** — `CODEBASE_MAP.md` : documenter repeatMode/cooldown/autoComplete + persistance v2 dans la section Quêtes. Renvoyer à la spec EXT-2.
- [ ] **Step 3 : Commit** — `docs(quests): répétables/auto-complete (EXT-2) + exemple contenu`.
- [ ] **Step 4 : Revue finale de branche** — compile-safety (shard+persistance+éditeur), double-versement reward impossible, compat save v1, borne semaine lundi correcte, doc éditeur, zéro wire.

## Checklist finale
- [ ] Modes par quête + autoComplete + reset login/Talk + persistance v2.
- [ ] Pas de double reward ; compat v1 ; tests verts.
- [ ] CI verte. Rapport : ⚠️ REDÉPLOIEMENT SHARD, client inchangé, stack #933→cette PR.
