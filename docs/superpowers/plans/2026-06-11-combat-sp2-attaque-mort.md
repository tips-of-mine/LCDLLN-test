# Combat SP2 — Attaque, précision/critique, mort/respawn : Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Boucler le combat joueur↔créature de bout en bout : le joueur cible (clic+Tab), attaque (T), voit dégâts/critiques/ratés dans le HUD combat enfin câblé, peut mourir et réapparaître au spawn.

**Architecture:** Le serveur a déjà la validation/dégâts/riposte/mort (constat spec §3.bis) — SP2 injecte les 11 stats calculées dans le `CombatComponent`, ajoute les jets précision/critique via un résolveur pur testable (`AttackResolver`), un flag crit/miss sur `CombatEventMessage` (wire v9→v10) et un kind `RespawnRequest` (80). Le client gagne l'envoi d'`AttackRequest`, la sélection de cible (écran-espace via `WorldToScreenPx`, pas de raycast 3D), le câblage des présentateurs combat existants (CombatHud, AdvancedCombatUi) et l'écran de mort (pattern menu pause).

**Tech Stack:** C++20, protocole UDP maison, ImGui, tests CTest (CI build-linux). Pas de toolchain locale — validation par CI.

**Livraison : 1 PR `combat-sp2` (serveur + client, base main)** — lock-step de toute façon (wire v10), et les PRs stackées n'ont pas de CI ici.

**Décisions reprises de la spec** : ciblage clic+Tab ; mort = écran + bouton « Réapparaître » → spawn de zone PV pleins ; pas d'armure ; cadence d'attaque joueur inchangée (0,5 s MVP — la période 2 s envisagée en spec est abandonnée pour ne pas changer l'équilibrage et le cooldown HUD existant). Touches : **Tab** = cycle cible, **T** = attaque, **J** = panneau combat avancé (vérifier liberté de J au moment du câblage ; sinon prendre K).

---

### Task 1: `AttackResolver` pur + tests (serveur)

**Files:**
- Create: `src/shardd/gameplay/combat/AttackResolver.h` (header-only, pur)
- Create: `src/shardd/gameplay/combat/AttackResolverTests.cpp`
- Modify: `src/CMakeLists.txt` (lcdlln_add_simple_test `attack_resolver_tests`, à côté de `creature_archetype_library_tests`)

```cpp
// AttackResolver.h — résultat d'un jet d'attaque. Pur et déterministe : les
// jets aléatoires [0,1) sont passés en paramètres (RNG injecté par l'appelant),
// ce qui rend toutes les branches testables sans graine.
struct AttackRollResult { bool miss; bool crit; uint32_t damage; };
/// roll01Accuracy >= accuracy/100 → miss (damage 0) ;
/// sinon roll01Crit < critRate/100 → crit (damage = base × critMult arrondi) ;
/// sinon hit normal (damage = base). accuracy clampé [0,100], critRate [0,100],
/// critMult plancher 1.0.
AttackRollResult ResolveAttackRoll(uint32_t baseDamage, float accuracy,
	float critRate, float critMult, float roll01Accuracy, float roll01Crit);
```

Tests : hit garanti (accuracy 100, roll 0.99) ; miss garanti (accuracy 0) ; crit garanti (critRate 100 → damage = base×mult) ; pas de crit (critRate 0) ; clamps (accuracy 150 = jamais de miss ; critMult 0.5 → plancher 1.0) ; base 0 → damage 0 même en crit. Harnais `bool Test*()` + main, pattern ThreatListTests.

### Task 2: `CombatComponent` étendu + stats injectées (serveur)

**Files:**
- Modify: `src/shared/network/ReplicationTypes.h` (CombatComponent :22-28) — ajouter `float accuracy = 100.0f; float critRate = 0.0f; float critMult = 1.5f;` (composant serveur, PAS sur le wire).
- Modify: `src/shared/server_bootstrap/ServerApp.cpp` :
  - `ApplyArchetypeStatsToMob` (~:195) : remplir aussi `mob.combat.accuracy/critRate/critMult` depuis l'archétype.
  - Enter-world, après le bloc R1-A `ResolveSpawnHealth` (:1313-1334) : `ComputeStats(...)` complet → helper `ApplyDerivedCombatStats(ConnectedClient&, const DerivedStats&)` (namespace anonyme) : `damagePerHit = d.damage`, `attackRangeMeters = (d.range > 0 ? d.range : kDefaultAttackRangeMeters)` (range 0 = mêlée pure), `accuracy/critRate/critMult` copiés.
  - `ApplyLevelUpsAfterXp` (:5556-5562) : appeler le même helper après le recompute.

### Task 3: Jets dans les deux sens + purge de menace (serveur)

**Files:**
- Modify: `src/shared/server_bootstrap/ServerApp.h` — membre `std::mt19937 m_combatRng;` (+ include `<random>`), méthode privée `float NextCombatRoll01();`, méthode privée `void PurgeThreatForEntity(EntityId entityId);`.
- Modify: `src/shared/server_bootstrap/ServerApp.cpp` :
  - Seed du RNG au constructeur (`std::random_device` une fois — pas de besoin déterministe au runtime, les tests passent par AttackResolver pur).
  - `HandleAttackRequest` (:2596+) : après la validation portée, consommer le cooldown PUIS `ResolveAttackRoll(client->combat.damagePerHit, client->combat.accuracy, ...)`. Miss → CombatEvent `flags |= kCombatEventFlagMiss`, damage 0, PV cible inchangés (PAS de early-return : l'event part pour le log/HUD). Crit → `flags |= kCombatEventFlagCrit`. Le reste (mort, loot, XP) inchangé, exécuté seulement si dégâts > 0.
  - `TryMobAttackPlayer` (:3398+) : même logique avec `mob.combat.*` (le mob rate/critique aussi).
  - Mort du joueur (dans TryMobAttackPlayer, branche `target.stats.currentHealth == 0`) : appeler `PurgeThreatForEntity(target.entityId)` — boucle sur `m_mobs` : retirer l'entrée de `threatTable`, remettre `aggroTargetEntityId = 0` si c'était lui (l'IA repassera en patrouille au prochain tick).

### Task 4: Wire — `CombatEventMessage.flags` + `RespawnRequest` (v9→v10)

**Files:**
- Modify: `src/shared/network/ServerProtocol.h` — `kProtocolVersion = 10` (+ ligne d'historique « Combat SP2 — bump 9→10 : CombatEvent gagne flags (u32 : bit0 crit, bit1 miss) ; nouveau kind RespawnRequest = 80 ») ; `CombatEventMessage` (:318) gagne `uint32_t flags = 0;` + constantes `inline constexpr uint32_t kCombatEventFlagCrit = 1u << 0; kCombatEventFlagMiss = 1u << 1;` ; enum `RespawnRequest = 80,` après `PlayerStats = 79` ; `struct RespawnRequestMessage { uint32_t clientId = 0; };` + déclarations Encode/Decode (RespawnRequest et **EncodeAttackRequest** qui manque côté client).
- Modify: `src/shared/network/ServerProtocol.cpp` — `EncodeCombatEvent` : taille 32→36, `WriteU32(message.flags)` en queue ; `DecodeCombatEvent` : check `!= 36`, lecture flags ; `EncodeAttackRequest` (payload 12 : clientId u32 + targetEntityId u64, symétrique du Decode existant) ; `EncodeRespawnRequest`/`DecodeRespawnRequest` (payload 4).
- Modify: `src/shared/network/ServerProtocolTests.cpp` — roundtrip CombatEvent avec flags crit|miss ; roundtrip AttackRequest (Encode→Decode) ; roundtrip RespawnRequest + rejet payload tronqué.

### Task 5: Respawn serveur

**Files:**
- Modify: `src/shared/server_bootstrap/ServerApp.h` — `ConnectedClient` gagne `float spawnPositionMetersX/Y/Z = 0.0f;` (mémorisé à l'admission) ; déclaration `void HandleRespawnRequest(const Endpoint&, uint32_t clientId);`.
- Modify: `src/shared/server_bootstrap/ServerApp.cpp` :
  - Fin de `HandleHello` (après résolution position persistée/DB, avant `UpdateClientInterest`) : mémoriser `spawnPositionMeters* = positionMeters*`.
  - Dispatch (:692 zone des `case MessageKind::`) : router `RespawnRequest` → handler.
  - `HandleRespawnRequest` : client connu + clientId cohérent + **mort uniquement** (sinon warn+ignore) → téléport au `spawnPositionMeters*`, `stats.currentHealth = stats.maxHealth`, clear `kEntityStateDead`, mise à jour grille spatiale (même appel que le déplacement : `UpsertEntity`), `PurgeThreatForEntity`, `SaveConnectedClient(client, "respawn")`, log.

### Task 6: Client — envoi UDP + ciblage

**Files:**
- Modify: `src/client/net/GameplayUdpClient.{h,cpp}` — `SendAttackRequest(uint64_t targetEntityId)` et `SendRespawnRequest()` (pattern exact de `SendTalkRequest` : struct + Encode + SendBytes, clientId du handshake).
- Modify: `src/client/ui_common/UIModel.h/.cpp` :
  - `UIModelBinding::SetLocalTarget(engine::server::EntityId)` : cherche l'entité dans `m_model.remoteEntities`, remplit `targetStats` (entityId, PV, flags, position, hasTarget/hasPosition), `NotifyObservers(UIModelChangeCombat)` ; `ClearLocalTarget()`.
  - `ApplySnapshot` (:752, bloc cible) : en plus de la position, rafraîchir `currentHealth/maxHealth/stateFlags` de la cible depuis le snapshot ; si la cible a disparu de l'AoI ou est morte → conserver hasTarget mais marquer mort (le HUD griser/le cycle Tab l'ignorera).
- Modify: `src/client/app/Engine.cpp` (bloc input in-game, voisin des toggles :7582-7717, gardes `inGameNoMenu && !chatBlocks`) :
  - **Registre de binds minimal** : petit tableau local commenté `// Binds combat SP2 (embryon Lot E) : Tab=cycle cible, T=attaque, J=panneau combat` — les nouvelles touches déclarées à UN endroit.
  - **Clic gauche** (`WasMousePressed(Left)`, hors éditeur, hors capture ImGui `ImGui::GetIO().WantCaptureMouse`) : pour chaque `remoteEntities` avec `archetypeId != 0` et vivante, `WorldToScreenPx(position lissée + 0.5f)` ; sélectionne la plus proche du curseur sous ~40 px ; trouvé → `SetLocalTarget`, sinon ne rien faire (pas de déselection au clic raté — V1).
  - **Tab** : cycle les mobs vivants de l'AoI triés par distance monde croissante (suivant de la cible courante, wrap).
  - **T** : si cible vivante → `m_gameplayUdp.SendAttackRequest(target.entityId)` avec throttle local 250 ms (le serveur revalide le cooldown réel).

### Task 7: Client — câblage CombatHud + AdvancedCombatUi + rendu

**Files:**
- Modify: `src/client/app/Engine.h` — membres `engine::client::CombatHudPresenter m_combatHud;` et `engine::client::AdvancedCombatPresenter m_advancedCombat;` (namespaces exacts à reprendre des headers), `bool m_advancedCombatVisible = false;` (+ includes).
- Modify: `src/client/app/Engine.cpp` :
  - Observer existant (:12157-12163) : ajouter `m_combatHud.ApplyModel(model, changeMask)` et `m_advancedCombat.ApplyModel(model, changeMask)`.
  - Update : `Tick(deltaSeconds)` des deux + `m_combatHud.SetViewportSize(w, h)` au resize/init.
  - Rendu HUD (section in-game, près du HUD PV/ressource existant) depuis `GetState()` :
    - **Cadre cible** (si `targetStats.hasTarget`) : nom (CreatureCatalog via archetypeId de la cible, fallback « Cible ») + barre PV + « MORT » grisé si flag dead.
    - **Log combat HUD** : `combatLogLines` du CombatHudState (déjà formatées), ~6 dernières lignes, coin bas-droit ; les events miss/crit affichent « raté »/« CRITIQUE » (formatage : si flags présents dans UICombatLogEntry — ajouter `bool wasCrit/wasMiss` à `UICombatLogEntry` et les remplir dans `ApplyCombatEvent` depuis `message.flags`).
    - **Panneau combat avancé** (touche J) : fenêtre ImGui listant le DPS meter (`dpsMeter[]` : nom + barre + valeur) et le log 20 lignes avec filtres (boutons Damage/Healing/Deaths → `SetLogFilter`).
- Modify: `src/client/ui_common/UIModel.h/.cpp` — `UICombatLogEntry` gagne `bool wasCrit = false; bool wasMiss = false;` remplis depuis `m_combatEventMessage.flags`.
- Modify: `src/client/combat/CombatHud.cpp` / `AdvancedCombatUi.cpp` — formatage des lignes : suffixe « (CRITIQUE) » / « raté » selon les nouveaux booléens (toucher uniquement les fonctions Format*).

### Task 8: Client — écran de mort

**Files:**
- Modify: `src/client/app/Engine.cpp` — sur le pattern du menu pause (:10480-10550) : si `playerStats.stateFlags & kEntityStateDead` (et in-game) → overlay sombre plein écran « Vous êtes mort » + bouton « Réapparaître » → `SendRespawnRequest()`. Pendant la mort : bloquer l'envoi d'Input de déplacement (gate dans UpdateGameplayNet) et les touches combat. Pas d'état persistant : l'overlay disparaît quand le flag dead retombe (snapshot post-respawn).

### Task 9: Docs + PR + CI

- Modify: `CODEBASE_MAP.md` (section Combat : SP2), commit de ce plan.
- Push `combat-sp2`, PR base main, CI (~35 min Windows). **Déploiement : ⚠️ wire v10 — master + shardd + client lock-step.**

---

## Self-review

- **Spec §5 couverte** : 5.1 wire (Task 4), 5.2 stats+jets+respawn (Tasks 1-3, 5), 5.3 purge menace + respawn (Tasks 3, 5), 5.4 client complet (Tasks 6-8), 5.5 hors périmètre respecté (pas de loot/armure/sorts/PvP), 5.6 livraison adaptée (1 PR au lieu de 3 — justifié par CI/lock-step).
- **Types cohérents** : flags u32 partout ; `DerivedStats.damage` u32 = `CombatComponent.damagePerHit` u32 ; rolls float [0,1).
- **Pas de placeholder** : les deux points vérifiés à l'exécution (liberté touche J, position exacte du rendu HUD PV existant) sont bornés avec alternative.
