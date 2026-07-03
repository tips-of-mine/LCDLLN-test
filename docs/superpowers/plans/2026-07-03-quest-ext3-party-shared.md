# EXT-3 — Quêtes partagées en groupe — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development.

**Goal:** Flag `partyShared` par quête → crédit d'étape propagé aux membres du groupe à portée. Shard + éditeur, sans wire. Spec : `docs/superpowers/specs/2026-07-03-quest-ext3-party-shared-design.md`. **Stackée sur EXT-2** (`claude/quests-ext2`).

## Global Constraints
- FR ; PascalCase nouveaux symboles ; pas de « CMANGOS ». Pas de build local → CI = gate.
- Tests **non-strippables** (`std::cerr`+compteur+`return 1`).
- **Aucun wire** : ne pas toucher `ServerProtocol`/`kProtocolVersion`/le client. Pas de persistance nouvelle.
- Règle éditeur (CLAUDE.md) : doc `///` sur toute fonction ajoutée dans `src/world_editor/`.
- **Déploiement** : ⚠️ REDÉPLOIEMENT SHARD + éditeur ; client inchangé ; merger #933 → #934 avant.

---

## Task 1 : Shard — flag, filtre, fan-out groupe
**Files:** `QuestRuntime.{h,cpp}`, `ServerApp.{h,cpp}`, `QuestRuntimeTests.cpp`.

- [ ] **Step 1 (RED)** — Tests : `ApplyEvent(..., onlyPartyShared=true)` n'avance que les quêtes `partyShared` (fixture : 2 quêtes Active étape matchante, une `partyShared:true` une `false` → seule la 1re avance) ; `onlyPartyShared=false` (défaut) avance les deux ; parse `"partyShared"` (défaut false + true lu).
- [ ] **Step 2** — `QuestRuntime.h` : `bool partyShared = false;` dans `QuestDefinition` (après `autoComplete`). `LoadDefinitions` (~1292, miroir `autoComplete`) : parser `"partyShared"` optionnel (bool).
- [ ] **Step 3** — `ApplyEvent` : ajouter param `bool onlyPartyShared = false` (h + cpp) ; si `true`, `continue` sur toute def dont `!def.partyShared` avant d'appliquer la progression. Ne PAS changer le comportement par défaut.
- [ ] **Step 4** — `ServerApp` : extraire `FinalizeQuestDeltas(ConnectedClient&, std::vector<QuestProgressDelta>&, std::string_view reason)` depuis `ApplyQuestEvent` (le post-traitement EXT-2 : boucle autoComplete stamp+`GrantQuestReward`, envoi `SendQuestDelta`, `SaveConnectedClient`). L'acteur l'appelle.
- [ ] **Step 5** — `ServerApp::ApplyQuestEvent` : après le traitement acteur, boucle de fan-out (spec §3.2) — `FindPartyByMember`, radius `GetDouble("server.quest.party_share_radius_m", 30.0)`, skip acteur, `FindClientByClientId` (ajouter le helper s'il n'existe pas, via `m_clientIndexByClientId`), garde `zoneId` égal AVANT `DistanceSquaredXZ` vs `radiusSq`, `ApplyEvent(..., onlyPartyShared=true)` puis `FinalizeQuestDeltas` pour chaque coéquipier crédité.
- [ ] **Step 6 (GREEN)** + **Commit** : `feat(quests): quêtes partagées en groupe (partyShared) + fan-out à portée`.

## Task 2 : Éditeur — checkbox partyShared
**Files:** `QuestEditIo.{h,cpp}`, `QuestEditorPanel.{h,cpp}`, test QuestEditIo.

- [ ] **Step 1 (RED)** — Tests : round-trip `partyShared` (true→serialize→parse true) ; défaut false quand absent.
- [ ] **Step 2** — `EditedQuest.partyShared` (miroir `autoComplete`) : parse `"partyShared"` + sérialisation.
- [ ] **Step 3** — `QuestEditorPanel` : `ImGui::Checkbox("Partagé en groupe", &m_partySharedBuffer)` + buffer load/save/reset (miroir autoComplete `58/84/318`) ; doc `///`. Rappel CMake : fichiers dans `engine_core`.
- [ ] **Step 4 (GREEN)** + **Commit** : `feat(quests): checkbox partyShared dans l'éditeur de quêtes`.

## Task 3 : Docs + revue finale
- [ ] **Step 1** — `CODEBASE_MAP.md` (section Quêtes) : documenter `partyShared` (partage groupe, tous objectifs, rayon `server.quest.party_share_radius_m` défaut 30, même zone requise, pas de wire). Renvoyer à la spec EXT-3.
- [ ] **Step 2 : Commit** — `docs(quests): partyShared (EXT-3) dans CODEBASE_MAP`.
- [ ] **Step 3 : Revue finale de branche** — compile-safety (shard + éditeur) ; fan-out : nul-checks, skip acteur, garde zone-avant-rayon, pas de spam de deltas ; `ApplyEvent` défaut inchangé (non-régression EXT-1/EXT-2) ; doc éditeur ; zéro wire/client.

## Checklist finale
- [ ] `partyShared` + `ApplyEvent(onlyPartyShared)` + fan-out rayon/zone + config.
- [ ] Éditeur checkbox + tests ; parse défaut false (rétro-compat).
- [ ] CI verte. Rapport : ⚠️ REDÉPLOIEMENT SHARD + éditeur, client inchangé, stack après #934.
