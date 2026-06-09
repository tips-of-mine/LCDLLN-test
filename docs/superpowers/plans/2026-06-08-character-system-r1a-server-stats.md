# Système de Personnages — R1-A (PV calculés à l'enter-world) Plan

**Goal:** À l'entrée en jeu, calculer les PV (et PV courants) du joueur depuis le moteur de stats (PR1) et les écrire dans le `StatsComponent` répliqué — la barre de vie affiche enfin la vraie valeur (ex. Guerrier Nain niv.60 = 3078 PV). 100 % serveur, **aucun changement de wire** (`currentHealth`/`maxHealth` se répliquent déjà).

**Architecture:** Le shard charge faction/classe depuis la DB à l'enter-world, charge les tables embarquées une fois au boot, appelle `ComputeStats`, et remplit `ConnectedClient.stats`. Logique de résolution extraite dans une fonction pure **testée**. La ressource secondaire et les 9 autres stats restent pour R1-B (nécessitent opcode/UI).

**Branche:** `feat/character-system-r1a-server-stats` (depuis main, après PR1).

---

## Contexte (cartographie)
- Joueur live = `ConnectedClient` (struct, `src/shared/server_bootstrap/ServerApp.cpp` ~85-154 du .h) avec `StatsComponent { currentHealth, maxHealth }`. PV par défaut codés en dur (`kDefaultPlayerHealth`).
- `HandleHello()` (~1029-1250) : enter-world. `LoadSpawnFromDb()` (~987-1027) charge `spawn_*, name, gender, level` (PAS faction/classe). `acceptedClient.level` posé ~1195. État persisté fusionné ~1142 (`acceptedClient.stats = persistedState.stats`).
- Moteur PR1 : `engine::server::gameplay::ComputeStats(tables, factionId, classId, Sex, level) -> optional<DerivedStats>` ; `CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson)` (headers générés `CharacterStatsData.h`/`FactionsData.h`, dispo sur les targets de ServerApp via `${LCDLLN_GEN_DIR}`).
- `ConnectedClient` a déjà `level`, `gender`, `characterName`.

---

## Task 1 — Helper pur testé : résolution des PV

**Files:** Create `src/shardd/gameplay/character/SpawnStatsResolver.{h,cpp}`, `src/shardd/gameplay/character/SpawnStatsResolverTests.cpp`; Modify `src/CMakeLists.txt`.

But : isoler la logique testable hors de `HandleHello` (intégration non testable unitairement).

- [ ] **Step 1 (test first):** `SpawnStatsResolverTests.cpp` — table embarquée via `FromEmbedded(kCharacterStatsJson, kFactionsJson)`. Cas :
  - Guerrier Nain (faction `naine`, classe `guerrier`, H, niv.60), pas de PV persistés (0) → `maxHealth` ∈ [3076,3080], `currentHealth == maxHealth`.
  - Même perso, PV persistés = 100 → `maxHealth` ∈ [3076,3080], `currentHealth == 100` (conservé, ≤ max).
  - PV persistés > max (ex. 999999) → `currentHealth == maxHealth` (clamp).
  - Faction/classe inconnue → `resolved == false` (l'appelant garde le défaut).
  Pattern plain-main return-0/1 (PAS assert — NDEBUG). Inclut `CharacterStatsData.h`/`FactionsData.h`.

- [ ] **Step 2:** `SpawnStatsResolver.h` :
```cpp
#pragma once
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include <cstdint>
#include <string>
namespace engine::server::gameplay
{
	/// Résultat de la résolution des PV à l'enter-world.
	struct SpawnHealth { bool resolved=false; uint32_t maxHealth=0; uint32_t currentHealth=0; };

	/// Calcule maxHealth (=hp dérivé) et currentHealth pour un joueur entrant.
	/// \param persistedCurrentHealth PV courants restaurés (0 = aucun → plein).
	/// Si la faction/classe est inconnue, resolved=false (l'appelant garde le défaut).
	SpawnHealth ResolveSpawnHealth(const CharacterStatsTables& tables,
	                               const std::string& factionId, const std::string& classId,
	                               Sex sex, uint32_t level, uint32_t persistedCurrentHealth);
}
```

- [ ] **Step 3:** `SpawnStatsResolver.cpp` : appelle `ComputeStats`; si nullopt → `{false,0,0}`; sinon `maxHealth=hp`; `currentHealth = (persisted>0 ? min(persisted,max) : max)`.

- [ ] **Step 4:** CMake — ajouter `SpawnStatsResolver.cpp` aux listes sources des targets serveur compilant déjà le moteur (WIN32 `server_app`, `shard_app`), et enregistrer `spawn_stats_resolver_tests` (bloc explicite avec `${LCDLLN_GEN_DIR}` + `add_dependencies(... lcdlln_gen_character_data)` + moteur + tables, comme `character_stats_engine_tests`).

- [ ] **Step 5:** commit `feat(shardd): SpawnStatsResolver — PV dérivés à l'enter-world (+ tests)`.

---

## Task 2 — Charger faction/classe en DB + appeler le resolver dans HandleHello

**Files:** Modify `src/shared/server_bootstrap/ServerApp.cpp` (+ `.h`).

- [ ] **Step 1:** Charger les tables une fois : membre `std::optional<engine::server::gameplay::CharacterStatsTables> m_statsTables;` initialisé au boot (ou lazy au 1er HandleHello) via `FromEmbedded(kCharacterStatsJson, kFactionsJson)` ; log si nullopt. Include `CharacterStatsData.h`/`FactionsData.h` + le resolver.
- [ ] **Step 2:** `LoadSpawnFromDb()` : ajouter `faction_str, class_str` au SELECT (~ligne 1007) ; renvoyer/retourner ces deux strings (via out-params ou struct). Ajouter `factionId`/`classId` à `ConnectedClient` (ou variables locales suffisantes pour l'appel).
- [ ] **Step 3:** Dans `HandleHello`, après que `level`/`gender`/faction/classe sont connus ET après la fusion de l'état persisté (~après 1195), appeler `ResolveSpawnHealth(*m_statsTables, factionId, classId, sex, level, acceptedClient.stats.currentHealth)`. `sex` = `gender=="female"?Female:Male`. Si `m_statsTables` présent et `resolved` : `acceptedClient.stats.maxHealth = r.maxHealth; acceptedClient.stats.currentHealth = r.currentHealth;`. Sinon laisser le défaut (rétro-compat persos sans faction/classe).
- [ ] **Step 4:** Log INFO : faction/classe/niveau → maxHealth calculé (utile au diag).
- [ ] **Step 5:** commit `feat(shardd): enter-world calcule les PV du joueur depuis le moteur de stats`.

---

## Task 3 — Vérif + push + PR
- [ ] grep : aucun nouveau wire/opcode ; `kProtocolVersion` inchangé.
- [ ] push, PR base main. CI : build-linux (ctest `spawn_stats_resolver_tests`) + build-windows.

> **Déploiement R1-A** : ⚠️ redéploiement serveur (shard) requis — nouveau binaire (PV calculés au boot du joueur). **Aucun changement client/wire** → pas de lock-step client. Indépendant de PR2 (peut se merger/déployer séparément, après PR1).

## Hors périmètre (R1-B)
Ressource secondaire + 9 autres stats répliquées + opcode « feuille de perso » + panneau UI client.
