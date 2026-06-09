# Système de Personnages — Level-up runtime (item 2) Plan

**Goal:** Quand un joueur accumule assez d'XP (déjà gagnée au kill de mob), il **monte de niveau** : recalcul des stats, soin complet, re-push de la feuille (`SendPlayerStats`), et persistance du niveau. Plus une commande admin `/setlevel` pour tester.

**Architecture:** L'XP existe déjà (`ConnectedClient.experiencePoints`, gagnée via `DistributePartyXp` au kill). On ajoute la **boucle de level-up** après chaque gain : seuil `Formulas::XpToNextLevel(level, xpBase, xpFactor, levelMax)` (params des tables embarquées). Au level-up : `ComputeStats(nouveau niveau)` → `stats.maxHealth` + soin complet + `SendPlayerStats`. Niveau persisté via `PersistedCharacterState` (fichier, déjà écrit par le shard). Server-only (pas de wire/UI → pas de conflit avec PR client en cours).

**Branche:** `feat/character-levelup-runtime` (depuis main).

**Décisions :** `experiencePoints` = XP *dans le niveau courant* (décrémenté au level-up). Persistance niveau = fichier `PersistedCharacterState` ; enter-world prend le niveau persisté s'il est > 0, sinon DB. Synchro colonne DB `characters.level` côté master = suivi séparé (hors MVP).

---

## Contexte (cartographie item 2)
- XP gagnée : `DistributePartyXp` (ServerApp.cpp:5388) + grant solo (`attacker.experiencePoints += baseXp`, ~2397) au kill de mob (death à ServerApp.cpp:2683, `kBaseXpPerMobKill=10`).
- `ConnectedClient.experiencePoints` (ServerApp.h:122), `.level` (ServerApp.h:128, chargé DB à l'enter-world via `LoadSpawnFromDb`, jamais modifié).
- `PersistedCharacterState` (CharacterPersistence.h) a `experiencePoints` mais **pas** `level`. Save via `SaveConnectedClient` (ServerApp.cpp:1733) + autosave (`MaybeAutosaveCharacters` 1715).
- Recompute+push : `ComputeStats(...)` + `SendPlayerStats(client)` (ServerApp.cpp:4202) — déjà appelés à l'enter-world (~1277-1327). `client.stats.maxHealth` change → snapshot le réplique (BuildEntityState copie StatsComponent).
- Commandes : `HandleChatSlashCommand` (ServerApp.cpp:4673) ← `TryParseChatSlashCommand` (ChatCommandParser.cpp:114, enum `ChatSlashCommandKind` dans .h). RBAC : ex. `if(!sender.chatModeratorRole){...}` (4800) + `AuditLogModeration(...)`. Registre data : `game/data/config/slash_commands.json`. `FindConnectedClientByChatDisplayName`.
- `Formulas::XpToNextLevel(level, base, factor, levelMax)` (shared) ; tables embarquées exposent `xpBase`/`xpFactor`/`levelMax` (CharacterStatsTables).

---

## Task 1 — Helper pur testé : progression de niveau

**Files:** Create `src/shardd/gameplay/character/LevelProgression.{h,cpp}` + `LevelProgressionTests.cpp`; Modify `src/CMakeLists.txt`.

- [ ] **Step 1 (test first):** `LevelProgressionTests.cpp` (plain-main 0/1, NDEBUG-safe). Avec `xpBase`/`xpFactor` des tables embarquées (FromEmbedded) ou des params littéraux : 
  - gain insuffisant → pas de level-up (level inchangé, xp augmentée).
  - gain franchissant un seuil → +1 niveau, xp résiduelle correcte.
  - gros gain franchissant plusieurs seuils → +N niveaux.
  - au cap `levelMax` → plus de level-up, xp peut être bornée (clamp à 0 ou figée — choisir : figer xp à 0 au cap).
- [ ] **Step 2:** `LevelProgression.h` :
```cpp
#pragma once
#include <cstdint>
namespace engine::server::gameplay
{
	struct LevelGainResult { uint32_t newLevel; uint32_t newXpIntoLevel; uint32_t levelsGained; };
	/// Applique un gain d'XP et fait monter le niveau tant que le seuil est franchi.
	/// \param level niveau courant ; \param xpIntoLevel XP déjà acquise dans ce niveau.
	/// \param gainedXp XP gagnée ; \param xpBase/xpFactor params de courbe ; \param levelMax cap.
	/// Au cap, xpIntoLevel est figée à 0 (plus de progression).
	LevelGainResult ApplyXpGain(uint32_t level, uint32_t xpIntoLevel, uint32_t gainedXp,
	                            double xpBase, double xpFactor, uint32_t levelMax);
}
```
- [ ] **Step 3:** `LevelProgression.cpp` : boucle `while (level < levelMax && xpIntoLevel + gainedXp >= XpToNextLevel(level,...))` → consomme le seuil, `level++`. Inclure `Formulas.h`. Au cap : `xpIntoLevel=0`, ignorer le surplus.
- [ ] **Step 4:** CMake — ajouter `LevelProgression.cpp` aux cibles serveur compilant déjà le moteur (WIN32 server_app + shard_app), et enregistrer `level_progression_tests` (bloc explicite + gen include si le test lit les tables embarquées).
- [ ] **Step 5:** commit `feat(shardd): LevelProgression — helper level-up pur (+ tests)`.

---

## Task 2 — Boucle de level-up + recompute + persistance niveau

**Files:** Modify `src/shared/server_bootstrap/ServerApp.cpp`/`.h`, `src/shardd/gameplay/character/CharacterPersistence.h` (+ .cpp si save/load explicites).

- [ ] **Step 1:** Méthode privée `void ApplyLevelUpsAfterXp(ConnectedClient& client, uint32_t gainedXp);` (ServerApp). Si `m_statsTables` : `auto r = ApplyXpGain(client.level, client.experiencePoints, gainedXp, m_statsTables->xpBase, m_statsTables->xpFactor, m_statsTables->levelMax);` ; sinon fallback : `client.experiencePoints += gainedXp` (comportement actuel). Mettre `client.level=r.newLevel`, `client.experiencePoints=r.newXpIntoLevel`. Si `r.levelsGained>0` : recalculer `ComputeStats(*m_statsTables, factionId, classId, sex, client.level)` → `client.stats.maxHealth=d->hp; client.stats.currentHealth=d->hp;` (soin complet) ; `SendPlayerStats(client)` ; `SaveConnectedClient(client, "level_up")` ; LOG_INFO niveau atteint.
- [ ] **Step 2:** Remplacer les sites de gain d'XP (`experiencePoints += baseXp` dans `DistributePartyXp` et le grant solo) par un appel à `ApplyLevelUpsAfterXp(client, baseXp)`. (Vérifier tous les sites via grep `experiencePoints +=`.)
- [ ] **Step 3:** Persistance niveau : ajouter `uint32_t level = 1;` à `PersistedCharacterState` ; le remplir dans `SaveConnectedClient` (`state.level = client.level`) ; à l'enter-world, après le merge persisté et le `LoadSpawnFromDb`, si `persistedState.level > 0` utiliser `acceptedClient.level = max(dbLevel, persistedState.level)` (le niveau persisté gagne s'il est plus haut — préserve les level-ups). Commenter le choix.
- [ ] **Step 4:** commit `feat(shardd): level-up runtime — seuil XP -> recompute stats + soin + persistance niveau`.

---

## Task 3 — Commande admin `/setlevel <joueur> <niveau>`

**Files:** Modify `src/shardd/gameplay/chat/ChatCommandParser.{h,cpp}`, `src/shared/server_bootstrap/ServerApp.cpp`, `game/data/config/slash_commands.json`, `docs/slash_commands_rbac.md`.

- [ ] **Step 1:** `ChatCommandParser.h` : ajouter `SetLevel` à `ChatSlashCommandKind`. `ChatCommandParser.cpp` : parser `/setlevel` → `kind=SetLevel`, `argsRemainder` = "<joueur> <niveau>". (+ test parser si le fichier a des tests.)
- [ ] **Step 2:** `HandleChatSlashCommand` (ServerApp.cpp) : case `SetLevel` — **gate admin** (mirror le gate le plus strict existant, ex. rôle admin ; PAS juste chatModeratorRole si un rôle admin distinct existe — vérifier). Parser nom + niveau (clamp [1, levelMax]). `FindConnectedClientByChatDisplayName` ; si trouvé : `target->level = n; target->experiencePoints = 0;` recompute (`ComputeStats`) → maxHealth + soin + `SendPlayerStats(*target)` + `SaveConnectedClient(*target,"admin_setlevel")`. **Audit log serveur** (mirror `AuditLogModeration`). Notice chat à l'acteur + cible.
- [ ] **Step 3:** `slash_commands.json` : entrée `/setlevel` (category admin, minRole admin, serverInteraction+serverLogged true). Mettre à jour `docs/slash_commands_rbac.md`.
- [ ] **Step 4:** commit `feat(shardd): commande admin /setlevel (level-up testable, RBAC + audit)`.

---

## Task 4 — Vérif + push + PR
- [ ] grep : tous les sites `experiencePoints +=` passent par `ApplyLevelUpsAfterXp` ; `engine::server::gameplay::` qualifié dans ServerApp.
- [ ] push, PR base main. CI : build-linux (ctest `level_progression_tests`) + build-windows.

> **Déploiement** : ⚠️ redéploiement **shard** (level-up runtime + commande). Master : la commande `/setlevel` passe par le chat shard ; vérifier si l'enregistrement RBAC master de slash_commands.json nécessite un redéploiement master (si le master valide la commande). Pas de wire-breaking. Aucun changement client.

## Hors périmètre
Affichage du niveau/XP côté client (panneau/HUD) ; synchro colonne DB `characters.level` côté master ; courbe de récompense XP par mob selon niveau.
