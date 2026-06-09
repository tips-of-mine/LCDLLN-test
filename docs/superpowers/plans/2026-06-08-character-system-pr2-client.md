# Système de Personnages — PR2 (client) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Rendre visible et jouable le nouveau système : à la création, choisir une faction (9 jouables) → une classe distincte → sexe, avec textes descriptifs ; envoyer `factionId`+`classId` au serveur ; migrer `races.json` (5 actives, désactivées masquées, `corrompus` supprimée).

**Architecture:** Le wire de création gagne un `factionId` (string optionnelle en fin de payload, rétro-compatible) ; `CharacterListEntry` gagne `faction_str`. Le presenter charge `factions.json` (modèle faction→classes) en plus des races. L'écran ImGui mono-page (`AuthImGuiCharacterCreate.cpp`) gagne un combo Faction → combo Classe + un panneau de description (localisé). Le handler master valide/stocke la faction depuis le payload. **Server (master) + client déployés en lock-step** (wire-breaking).

**Tech Stack:** C++20, ImGui, `engine::core::Config` (JSON), `LocalizationService::Translate`, tests plain-main, CI GitHub (build-windows compile client ; build-linux ctest).

---

## Périmètre & contexte découvert (lire avant de commencer)

- **L'écran réel** est `src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp` (`RenderCharCreateScreen`, ~lignes 40-294) : formulaire mono-page (nom + combo race `m_charRaceIdx` + radios genre `m_charGender` / teinte `m_charSkinTone` + sliders corps). Il n'utilise PAS `GetFilteredClasses`/`Next`/`SelectClass` du presenter (machinerie vestigiale). Le `classId` envoyé est `""` (codé en dur).
- **Soumission** : `ImGuiSubmitCharacterCreate(...)` → `StartCharacterCreateWorker` (`src/client/auth/screens/AuthScreenCharacterCreate.cpp`, ~ligne 175) appelle `BuildCharacterCreateRequestPayload(name, raceId, "", custom, gender)`.
- **Wire** : `src/shared/network/CharacterPayloads.{h,cpp}`. Strings = `uint16` length + bytes. Le parse de `CharacterCreateRequestPayload` lit les champs de fin de façon **optionnelle** (vérifie `Remaining()`), donc on appende `factionId` après `gender` sans casser les anciens clients côté parse. `CharacterListEntry` : champs ajoutés en fin (après `skin_color_idx`).
- **Serveur** : `src/masterd/handlers/character/CharacterCreateHandler.cpp` lit `parsed->raceId/classId/gender`, dérive la faction via `factionFromRace` (lignes 258-265), INSERT 14 binds (faction_str = bind 11).
- **Localisation** : `LocalizationService::Translate(key)` ; données plates `game/data/localization/<loc>/<loc>.json` ; clé manquante → renvoie la clé. Ajouter dans `fr.json` + `en.json`.
- **factions.json** (créé en PR1) : `game/data/races/factions.json` (id/name/race/selectable/classes[id,name,subclass,profile,resource]).
- **R1 (hors PR2)** : rendre les stats calculées **visibles en jeu live** (HUD combat) reste hors périmètre — dépend du chemin de réplication ECS. PR2 livre la création + l'identité ; pas l'affichage des 11 stats en jeu.

**Décision UI (conservatrice)** : ne PAS refondre en multi-écrans. Ajouter au formulaire existant un **combo Faction** puis un **combo Classe** (filtré par faction) + une **zone de texte** (description faction puis classe). Réutilise le pattern `ImGui::Combo` déjà fonctionnel. La race est déduite de la faction (combo race existant remplacé/asservi par la faction).

**Branche** : `feat/character-system-pr2-client` (déjà créée depuis `main` post-merge PR1).

---

## File Structure

| Fichier | Rôle |
|---|---|
| `game/data/races/races.json` (modifier) | `enabled` par race ; suppression entrée `corrompus`. |
| `game/data/configuration/races/corrompus.json` (supprimer) | Config race supprimée. |
| `game/data/ui/races/corrompus/` (supprimer) | Thème race supprimée. |
| `game/data/races/classes.json` (modifier) | Retirer `corrompus` des `allowedRaces` (fichier déprécié mais nettoyé). |
| `tools/asset_pipeline/gen_race_configs.py` (modifier) | Retirer le bloc `"corrompus"`. |
| `game/data/localization/fr/fr.json` + `en/en.json` (modifier) | Clés `faction.<id>.desc`, `class.<faction>.<class>.desc` ; retirer `auth.character_select.race.corrompus`. |
| `src/shared/network/CharacterPayloads.h` / `.cpp` (modifier) | `factionId` (create req) + `faction_str` (list entry) wire. |
| `src/shared/network/CharacterCreatePayloadsTests.cpp` (créer) | Round-trip wire create req avec factionId. |
| `src/client/character_creation/CharacterCreationUi.h` / `.cpp` (modifier) | Modèle factions (FactionDefinition + classes), Load, accesseurs, BuildRequestPayload(factionId,classId), suppression dépendance allowedRaces. |
| `src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp` (modifier) | Combo faction → combo classe + panneau description ; race déduite. |
| `src/client/auth/screens/AuthScreenCharacterCreate.cpp` (modifier) | Passer factionId+classId à BuildCharacterCreateRequestPayload. |
| `src/client/auth/AuthUi.h` + presenter (modifier) | Stocker faction/classe sélectionnées exposées au renderer. |
| `src/masterd/handlers/character/CharacterCreateHandler.cpp` (modifier) | Lire/valider `parsed->factionId`, stocker faction_str=factionId. |
| `src/client/character_creation/tests/CharacterCustomizationTests.cpp` (modifier) | Retirer `corrompus` (count + liste). |
| `src/client/character_creation/tests/FactionDefinitionTests.cpp` (créer) | 9 factions sélectionnables, classes distinctes, empire_hynn masquée. |

---

## Task 1 — races.json : flags `enabled` + suppression `corrompus`

**Files:** Modify `game/data/races/races.json`; Delete `game/data/configuration/races/corrompus.json`, `game/data/ui/races/corrompus/`; Modify `game/data/races/classes.json`, `tools/asset_pipeline/gen_race_configs.py`.

- [ ] **Step 1: races.json** — add `"enabled": true` to humains/elfes/orcs/nains/demons, `"enabled": false` to morts_vivants/divins ; **remove** the entire `corrompus` object (lines ~79-91).
- [ ] **Step 2: delete corrompus assets**
```bash
git rm game/data/configuration/races/corrompus.json
git rm -r game/data/ui/races/corrompus
```
(Leave models/textures/corrompus dirs — `.gitkeep` only, harmless ; remove if present and tracked: `git rm -r --ignore-unmatch game/data/models/characters/corrompus game/data/textures/characters/corrompus`.)
- [ ] **Step 3: classes.json** — remove `"corrompus"` from the 3 `allowedRaces` arrays (mage ~l27, rogue ~l40, necromancer ~l92). (classes.json is deprecated by factions.json but must not dangle-reference a removed race.)
- [ ] **Step 4: gen_race_configs.py** — remove the `"corrompus": { ... }` dict (~lines 237-266).
- [ ] **Step 5: validate JSON** (PowerShell `ConvertFrom-Json` on races.json + classes.json) and **commit**:
```bash
git add -A game/data/races/races.json game/data/races/classes.json tools/asset_pipeline/gen_race_configs.py
git commit -m "feat(data): races.json enabled flags + suppression corrompus partout"
```

---

## Task 2 — Localisation : descriptions factions/classes (+ retrait corrompus)

**Files:** Modify `game/data/localization/fr/fr.json`, `game/data/localization/en/en.json`.

- [ ] **Step 1:** Remove the key `"auth.character_select.race.corrompus"` from both files (~line 271).
- [ ] **Step 2:** Add description keys for the 9 selectable factions and each of their classes. Keys: `faction.<factionId>.desc` and `class.<factionId>.<classId>.desc`. Use the ids from `factions.json` (lumiere/justice/lune_noire/dzorak/legion/dragons/serpent/naine/elfe ; classes guerrier/archer/voleur/inquisiteur_chatieur/... etc.). Provide concise FR text in `fr.json` and EN text in `en.json`. (empire_hynn is non-selectable → no description needed, optional.)

Example (fr.json, add near other `auth.*` keys — flat keys, comma-separated):
```json
"faction.lumiere.desc": "Ordre saint jurant fidélité à la Lumière. Défenseurs des cités humaines.",
"class.lumiere.guerrier.desc": "Combattant de mêlée robuste. Ressource : Endurance.",
"class.lumiere.archer.desc": "Tireur à distance précis. Ressource : Souffle.",
"class.lumiere.voleur.desc": "Frappe furtive et critiques. Ressource : Réflexes.",
"class.lumiere.inquisiteur_chatieur.desc": "Inquisiteur offensif de mêlée. Ressource : Ferveur.",
"class.lumiere.inquisiteur_hospitalier.desc": "Inquisiteur soigneur. Ressource : Ferveur."
```
(Repeat for the 9 factions and all their classes — full set; mirror in en.json.)
- [ ] **Step 3:** Validate both JSON files (ConvertFrom-Json) and **commit**:
```bash
git add game/data/localization/fr/fr.json game/data/localization/en/en.json
git commit -m "feat(l10n): descriptions factions/classes (fr+en) + retrait clé corrompus"
```

---

## Task 3 — Wire : `factionId` (create req) + `faction_str` (list entry)

**Files:** Modify `src/shared/network/CharacterPayloads.h` / `.cpp`; Create `src/shared/network/CharacterCreatePayloadsTests.cpp`; Modify `src/CMakeLists.txt`.

- [ ] **Step 1 (test first):** Create `CharacterCreatePayloadsTests.cpp` — build a `CharacterCreateRequestPayload` with name/raceId/classId/customization/gender/factionId, serialize via `BuildCharacterCreateRequestPayload(...)`, parse via `ParseCharacterCreateRequestPayload(...)`, assert all fields round-trip including `factionId`. Also assert backward-compat: a payload built WITHOUT factionId parses with empty factionId. Use the plain-main return-0/1 pattern (NOT assert — NDEBUG). Mirror `CharacterSavePositionPayloadsTests.cpp` style.
- [ ] **Step 2:** `CharacterPayloads.h` — add `std::string factionId;` to `CharacterCreateRequestPayload` (after `gender`). Add a `factionId` param to `BuildCharacterCreateRequestPayload(...)` signature (trailing, default `{}`). Add `std::string faction_str;` to `CharacterListEntry` (after `skin_color_idx`).
- [ ] **Step 3:** `CharacterPayloads.cpp`:
  - `ParseCharacterCreateRequestPayload`: after the optional gender read, add an optional `factionId` read (same `if (r.Remaining() > 0) r.ReadString(out.factionId);` pattern).
  - `BuildCharacterCreateRequestPayload`: after writing gender, write `factionId` string.
  - `ParseCharacterListResponsePayload`: after `skin_color_idx`, add optional `faction_str` read (guard remaining for old rows).
  - `BuildCharacterListResponsePacket`: after writing `skin_color_idx`, write `faction_str`.
- [ ] **Step 4:** Register `character_create_payloads_tests` in `src/CMakeLists.txt` (mirror `character_save_position_payloads_tests`).
- [ ] **Step 5: commit**:
```bash
git add src/shared/network/CharacterPayloads.h src/shared/network/CharacterPayloads.cpp src/shared/network/CharacterCreatePayloadsTests.cpp src/CMakeLists.txt
git commit -m "feat(wire): factionId (create req) + faction_str (list entry) + tests round-trip"
```

---

## Task 4 — Serveur (master) : valider/stocker la faction depuis le payload

**Files:** Modify `src/masterd/handlers/character/CharacterCreateHandler.cpp`.

- [ ] **Step 1:** Replace the `factionFromRace` lambda usage (lines 258-265): if `parsed->factionId` is non-empty, use it as `factionStr` (truncated to 32). Keep the race-derived fallback ONLY when `factionId` is empty (legacy clients). Validate: `factionId` non-empty + alphanumeric/underscore (reuse name-charset style) ; if invalid, BAD_REQUEST. (Optional: validate against the `factions` table — but minimal scope = trust+sanitize string ; a DB existence check can be a follow-up.)
- [ ] **Step 2:** The INSERT already has a `faction_str` column (bind 11). Just feed the chosen `factionStr`. Update the log line to show the source (payload vs derived).
- [ ] **Step 3: commit**:
```bash
git add src/masterd/handlers/character/CharacterCreateHandler.cpp
git commit -m "feat(master): CharacterCreateHandler stocke faction_str depuis le payload (factionId)"
```

---

## Task 5 — Presenter : modèle factions + BuildRequestPayload(factionId, classId)

**Files:** Modify `src/client/character_creation/CharacterCreationUi.h` / `.cpp`.

- [ ] **Step 1:** Add a `FactionDefinition` struct (id, displayName, raceId, selectable, and a vector of `FactionClass { id, displayName, subclass, profile, resourceKey }`). Add `bool LoadFactions(const Config&, const std::string& relPath="races/factions.json")` that parses `factions.json` (iterate `factions[i]` + nested `classes[j]`, mirror existing `LoadRaces` index pattern). Store `std::vector<FactionDefinition> m_factions;`. Call it in `Init`.
- [ ] **Step 2:** Add accessors: `GetSelectableFactions()` (filters `selectable`), `GetFactionClasses(factionIndex)`, `GetRaceIdForFaction(factionIndex)`. Add selection state `selectedFactionIndex`, `selectedFactionClassIndex` to `CharacterCreationState`.
- [ ] **Step 3:** Update `BuildRequestPayload()` to populate `factionId` (from selected faction) + `classId` (from selected faction class) + `raceId` (derived from faction) — instead of the old race/class model. Keep customization/gender.
- [ ] **Step 4:** Mark the old `allowedRaces`/`GetFilteredClasses`/`LoadClasses` path as deprecated (keep compiling for RaceDefinitionTests, but the live flow uses factions). Do NOT delete `LoadRaces` (still used for race meshes/colors).
- [ ] **Step 5: commit**:
```bash
git add src/client/character_creation/CharacterCreationUi.h src/client/character_creation/CharacterCreationUi.cpp
git commit -m "feat(client): presenter charge factions.json (faction->classes) + payload factionId/classId"
```

---

## Task 6 — UI ImGui : combo Faction → combo Classe + description ; race déduite

**Files:** Modify `src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp`, `src/client/auth/AuthUi.h` (+ presenter core if needed for state), `src/client/auth/screens/AuthScreenCharacterCreate.cpp`.

This is the visible UI task. Implementer MUST read the current `RenderCharCreateScreen` and mirror its existing `ImGui::Combo`/label patterns; do not restructure the 2-column layout.

- [ ] **Step 1:** In the right column, ABOVE the existing race combo, add a **Faction combo** (`m_charFactionIdx`) populated from `presenter->GetSelectableFactions()` displayNames. On change, reset class index and set the race to the faction's race (drive `m_charRaceIdx` from the faction's raceId so the existing 3-D preview keeps working).
- [ ] **Step 2:** Add a **Classe combo** (`m_charClassIdx`) populated from `GetFactionClasses(m_charFactionIdx)` — display `name` (+ " · " + subclass when present).
- [ ] **Step 3:** Replace the standalone race combo: either hide it (race now implied by faction) or make it read-only display of the faction's race name. Keep gender + skin + body sliders as-is.
- [ ] **Step 4:** Add a **description panel** (`ImGui::TextWrapped`) showing `Localize("faction.<id>.desc")` then `Localize("class.<factionId>.<classId>.desc")`, updated on selection. Use the project `LocalizationService` (find how the renderer accesses it — likely via the render model or a service pointer; mirror existing localized labels in this file).
- [ ] **Step 5:** On submit, pass the selected `factionId` and `classId` through `ImGuiSubmitCharacterCreate` → `StartCharacterCreateWorker` → `BuildCharacterCreateRequestPayload(name, raceId, classId, custom, gender, factionId)`. Add `m_characterFactionId` / `m_characterClassId` members alongside the existing `m_characterRaceId`.
- [ ] **Step 6:** Ensure only `enabled` races appear and non-`selectable` factions are excluded (the presenter accessors already filter; the race combo, if kept, must skip `enabled==false`). Requires `RaceDefinition.enabled` — add that field + parse it in `LoadRaces` (Task 5 or here).
- [ ] **Step 7: commit**:
```bash
git add src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp src/client/auth/AuthUi.h src/client/auth/screens/AuthScreenCharacterCreate.cpp
git commit -m "feat(client): écran création — faction -> classe + description localisée, race déduite"
```

> NOTE (no local build / no visual check): keep additions minimal and pattern-matched. The user will verify the visual result in-game once PR2 is testable.

---

## Task 7 — Tests client : corrompus retiré + factions chargées

**Files:** Modify `src/client/character_creation/tests/CharacterCustomizationTests.cpp`; Create `src/client/character_creation/tests/FactionDefinitionTests.cpp`; Modify `src/CMakeLists.txt`.

- [ ] **Step 1:** `CharacterCustomizationTests.cpp` — remove `"corrompus"` from the expected races array (line ~54) and lower the `RaceCount() >= 8` check to `>= 7` (now 7 races: 5 active + morts_vivants + divins remain in config; corrompus removed). Verify the test no longer references corrompus. NOTE: if these tests rely on `assert` (disabled under NDEBUG/Release in CI), convert the new/changed assertions to a robust check (return-code or abort) — confirm by reading the file's REQUIRE macro (it uses a custom REQUIRE that increments a failure counter → already robust; keep that pattern).
- [ ] **Step 2:** Create `FactionDefinitionTests.cpp` — load via `CharacterCreationPresenter::Init` pointing at repo content; assert: exactly 9 selectable factions; `empire_hynn` present but `selectable==false` (excluded from `GetSelectableFactions()`); each selectable faction has ≥4 classes with non-empty ids; class ids are distinct within a faction; the localization-key inputs (faction id / class id) are well-formed. Use the file's robust REQUIRE pattern (counter + non-zero exit), NOT bare assert.
- [ ] **Step 3:** Register `faction_definition_tests` in `src/CMakeLists.txt` (mirror `race_definition_tests`).
- [ ] **Step 4: commit**:
```bash
git add src/client/character_creation/tests/CharacterCustomizationTests.cpp src/client/character_creation/tests/FactionDefinitionTests.cpp src/CMakeLists.txt
git commit -m "test(client): retrait corrompus + FactionDefinitionTests (9 factions, empire_hynn masquée)"
```

---

## Task 8 — Vérification finale + push + PR

- [ ] **Step 1:** `grep -rn "corrompus" src game/data --include=*.json --include=*.cpp --include=*.h` → confirm only legacy/unrelated ("corrupt files") remain; no race reference.
- [ ] **Step 2:** Confirm `BuildCharacterCreateRequestPayload` callers all pass factionId; confirm server handler reads it.
- [ ] **Step 3:** Push branch, open PR (base main).
- [ ] **Step 4:** Monitor CI: build-windows (client compiles), build-linux (ctest: character_create_payloads_tests, faction_definition_tests, character_customization_tests, race_definition_tests green). Fix to green.

> **Déploiement PR2** : ⚠️ redéploiement serveur (master) requis — wire-breaking (champ `factionId` ajouté au payload de création + `faction_str` au list entry) + handler master modifié. **Client + master en lock-step.** Merger après PR1 déjà déployée.

---

## Self-Review (à faire après rédaction du code)
- Couverture spec : flux faction→classe→sexe (T5/T6), 9 factions + distinct classes (T5/T6/T7), textes descriptifs localisés (T2/T6), races.json migré + corrompus supprimée (T1), wire factionId (T3), master stocke faction (T4), tests (T7).
- Hors PR2 (tracé) : affichage des 11 stats en jeu live (R1) ; classes.json suppression complète (gardé nettoyé, dépréciation douce).
- Risque connu : la partie ImGui (T6) n'est pas validable visuellement sans build local — additions minimales, pattern-matched, à vérifier en jeu par l'utilisateur.
