# Combat SP3 — Sorts et auras par profil : Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Donner vie aux 8 kits de sorts validés (docs/superpowers/specs/2026-06-11-combat-sp3-kits-starter-proposal.md — VALIDÉ utilisateur, 5/5 points dont coûts en % ressource, régén 5 %/s hors combat / 2 %/s en combat, touches 1-4) : cast serveur-autoritaire, ressource runtime, auras DoT/HoT/buffs répliquées, barre d'action + barre de cast + BuffBar côté client.

**Architecture:** Data-driven de bout en bout : 8 fichiers `game/data/gameplay/spells/<profil>.json` chargés par une `SpellKitLibrary` serveur stricte (pattern CreatureArchetypeLibrary) ET par un `SpellKitCatalog` client tolérant (pattern CreatureCatalog). Le serveur résout le kit du joueur via `CharacterStatsTables::ClassEntry::profile` (existant). Les dégâts des sorts réutilisent le chemin SP2 (`ResolveAttackRoll` + flags crit/miss) avec un multiplicateur ; DoT/HoT/buffs = nouvelles `ActiveAura` tickées dans `Simulate`. Wire v10→v11 : kinds 81 CastRequest, 82 ResourceUpdate, 83 CastBarUpdate, 84 AuraUpdate. La BuffBar client passe par le `StatusEffectManager` existant (src/client/gameplay/StatusEffect.h) → `BuffBarPresenter` (orphelin M31.2, câblé ici).

**Tech Stack:** identique SP1/SP2 (C++20, parseurs JSON locaux, tests CTest CI build-linux, pas de toolchain locale).

**Livraison : 2 PRs séquentielles base main** (pas de stack — CI) : **SP3-A serveur+wire** (Tasks 1-8) puis, après merge, **SP3-B client** (Tasks 9-13). Dépend du merge de SP2 (#876). Déploiement : ⚠️ wire v11, lock-step master+shardd+client.

**Décisions techniques actées** (déviations spec §6 justifiées) :
- Persistance des auras dans `PersistedCharacterState` (fichier, mécanisme de persistance réel du shard — inventaire/or/quêtes y sont déjà) ; la table `character_auras` (0061) reste réservée à la future synchro master.
- Les soins ne ratent pas et ne critiquent pas en V1.
- `inCombat` (régén) = impliqué dans un CombatEvent (attaquant ou cible) depuis < 5 s.
- Un cast est annulé si le joueur bouge de > 0.5 m ou meurt pendant le cast.
- Les sorts de dégâts directs utilisent les jets SP2 (précision/critique) ; DoT/HoT tickent sans jets.

---

### Task 1: Données — 8 kits JSON

**Files:** Create `game/data/gameplay/spells/{melee,tank,distance,voleur,healer,sacre,lanceur,pisteur}.json`

Schéma (valeurs = kits VALIDÉS, recopier depuis la proposition §2) :

```json
{
  "profile": "melee",
  "spells": [
    {
      "id": "melee_frappe_brutale",
      "name": "Frappe brutale",
      "slot": 1,
      "castTimeMs": 0,
      "cooldownMs": 6000,
      "resourceCostPercent": 15,
      "rangeMeters": 0,
      "targetType": "SingleEnemy",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "DirectDamage", "mult": 1.4 } ]
    },
    {
      "id": "melee_entaille",
      "name": "Entaille",
      "slot": 2,
      "castTimeMs": 0,
      "cooldownMs": 10000,
      "resourceCostPercent": 20,
      "rangeMeters": 0,
      "targetType": "SingleEnemy",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "DamageOverTime", "mult": 0.25, "tickPeriodMs": 2000, "durationMs": 8000 } ]
    },
    {
      "id": "melee_cri_de_guerre",
      "name": "Cri de guerre",
      "slot": 3,
      "castTimeMs": 0,
      "cooldownMs": 30000,
      "resourceCostPercent": 25,
      "rangeMeters": 0,
      "targetType": "SelfOnly",
      "areaRadiusMeters": 0,
      "effects": [ { "type": "BuffDamagePercent", "percent": 15, "durationMs": 10000 } ]
    }
  ]
}
```

- `rangeMeters: 0` = mêlée → le serveur substitue `combat.attackRangeMeters`.
- `targetType` ∈ `SingleEnemy | SingleAlly | SelfOnly | AreaAroundSelf` (SingleAlly V1 : soi ou membre du groupe à portée ; sans groupe = soi).
- Types d'effets et champs : `DirectDamage{mult}`, `DamageOverTime{mult,tickPeriodMs,durationMs}`, `DirectHeal{mult}`, `HealOverTime{mult,tickPeriodMs,durationMs}`, `BuffDamagePercent{percent,durationMs}`, `DebuffDamageTakenPercent{percent,durationMs}`, `TauntThreatMult{mult}`, `SlowMobPercent{percent,durationMs}`, `ThreatReducePercent{percent}`.
- Cas particuliers validés : tank « Second souffle » = HealOverTime mult 0 + `percentMaxHpPerTick: 3` (champ optionnel, soin = % PV max) ; pisteur « Morsure du piège » = 2 effets (DoT + Slow) ; sacre « Imposition des mains » targetType SingleAlly.

### Task 2: `SpellKitLibrary` serveur + tests

**Files:** Create `src/shardd/gameplay/spell/SpellKitLibrary.{h,cpp}` + `SpellKitLibraryTests.cpp` ; Modify `src/CMakeLists.txt` (2 listes sources comme CreatureArchetypeLibrary src/CMakeLists.txt:72/:1148 + `lcdlln_add_simple_test(spell_kit_library_tests …)`).

- Structs : `SpellEffectDef { enum class SpellEffectType {…9 types…}; float mult; float percent; uint32_t tickPeriodMs; uint32_t durationMs; float percentMaxHpPerTick; }` ; `SpellDef { std::string spellId; std::string name; uint32_t slot (1-4); uint32_t castTimeMs; uint32_t cooldownMs; uint32_t resourceCostPercent; float rangeMeters; enum class SpellTargetKind targetType; float areaRadiusMeters; std::vector<SpellEffectDef> effects; }`.
- API : `Init()` (scan des 8 fichiers `gameplay/spells/*.json`, strict : fichier invalide = échec boot), `LoadFromText` testable, `const std::vector<SpellDef>* FindKit(std::string_view profile)`, `const SpellDef* FindSpell(std::string_view profile, std::string_view spellId)`.
- Validation : slot ∈ [1,4] unique par kit, cooldownMs > 0 (sauf healer Soin rapide cd 0 autorisé), resourceCostPercent ≤ 100, effets non vides, champs requis par type.
- Tests (pattern CreatureArchetypeLibraryTests) : kit valide (3 sorts, slots, effets typés), slot dupliqué rejeté, type d'effet inconnu rejeté, percentMaxHpPerTick accepté, multi-effets accepté.

### Task 3: Ressource runtime + régénération (serveur)

**Files:** Modify `src/shared/server_bootstrap/ServerApp.h` (ConnectedClient : `uint32_t currentResource = 0; uint32_t maxResource = 0; uint64_t lastCombatInvolvementMs = 0;`) ; ServerApp.cpp.

- `maxResource = DerivedStats.resource` à l'enter-world et au level-up (dans le bloc `ApplyDerivedCombatStats` étendu ou à côté) ; `currentResource = maxResource` au spawn/level-up/respawn.
- `BroadcastCombatEvent` : tamponner `lastCombatInvolvementMs` (now) sur l'attaquant et la cible s'ils sont des joueurs.
- Dans `Simulate()` (ServerApp.cpp:1582 environ, à côté d'UpdateSpawners) : régén par tick = `maxResource × (inCombat ? 0.02 : 0.05) / tickHz` accumulée en float par client (champ `float resourceRegenCarry`), clamp à max. `inCombat` = `nowMs - lastCombatInvolvementMs < 5000`.
- Wire `ResourceUpdate` (Task 6) poussé quand `currentResource` change de ≥ 1 (au plus 1 par tick de snapshot).

### Task 4: Auras runtime + modificateurs (serveur)

**Files:** Modify ServerApp.h/.cpp.

- `struct ActiveAura { std::string spellId; SpellEffectType type; float value; float percentMaxHpPerTick; uint32_t stacks = 1; uint64_t expiresAtMs; uint64_t nextTickAtMs; uint32_t tickPeriodMs; EntityId casterEntityId; }` ; `std::vector<ActiveAura> auras;` sur ConnectedClient ET MobEntity.
- Application : refresh si même spellId+caster (durée réinitialisée, pas de stack V1).
- Tick dans `Simulate()` : DoT → dégâts = `mult × damage du casteur au moment du tick d'application` (snapshot de la valeur dans l'aura : champ `float tickAmount` calculé à l'apply — évite de retrouver le casteur déconnecté) + CombatEvent par tick (attacker = casterEntityId) ; HoT → soin (clamp max) + CombatEvent damage 0 ? NON : les soins n'émettent pas de CombatEvent V1 (le HUD cible suit les PV par snapshot) ; expiration → retrait + AuraUpdate.
- Modificateurs consommés : dans `HandleAttackRequest`/`TryMobAttackPlayer`/résolution de sort, dégâts finaux × `(1 + ΣBuffDamagePercent(attaquant)/100) × (1 + ΣDebuffDamageTakenPercent(cible)/100)` ; `SlowMobPercent` → `mob.moveSpeedMetersPerSecond` effectif multiplié dans l'IA de déplacement (multiplicateur recalculé au tick, la valeur de base n'est jamais écrasée).
- Mort (mob ou joueur) → purge des auras de l'entité ; respawn joueur → auras vides.

### Task 5: Pipeline de cast (serveur)

**Files:** Modify ServerApp.h/.cpp.

- ConnectedClient : `std::string activeCastSpellId; uint64_t castFinishAtMs = 0; float castStartPosX/Z; std::unordered_map<std::string, uint64_t> spellCooldownUntilMs;` + `std::string profileId;` (résolu à l'enter-world via `m_statsTables` ClassEntry — accessor à ajouter si absent : `const ClassEntry* FindClass(factionId, classId)`).
- `HandleCastRequest(endpoint, clientId, spellId)` : vivant, sort ∈ kit du profil, pas de cast en cours, cooldown du sort écoulé, `currentResource ≥ coût` (coût = `resourceCostPercent × maxResource / 100`), cible valide selon targetType (SingleEnemy = cible mob vivante à portée — le client envoie `targetEntityId` dans le message ; SelfOnly/AreaAroundSelf = pas de cible ; SingleAlly = soi ou membre du groupe à portée, fallback soi), portée vérifiée. Instant → `ResolveSpellCast` immédiat ; sinon `activeCast` + CastBarUpdate(start).
- Dans `Simulate()` : cast en cours → annulation si mort ou déplacement > 0.5 m depuis castStartPos (CastBarUpdate cancel) ; échéance → revalider cible/portée puis `ResolveSpellCast` (CastBarUpdate complete).
- `ResolveSpellCast` : débit ressource, démarrage cooldown, application des effets (Task 4) ; DirectDamage passe par `ResolveAttackRoll` (base = mult × damage) + modificateurs d'auras + chemin mort/loot/XP de SP2 (réutiliser le bloc existant de HandleAttackRequest — extraire un helper `ApplyDamageToMob(client, mob, rollDamage, eventFlags)` pour DRY) ; AoE = tous les mobs vivants à `areaRadiusMeters` du joueur (boucle m_mobs, même zone).

### Task 6: Wire v10→v11

**Files:** Modify `src/shared/network/ServerProtocol.{h,cpp}` + `ServerProtocolTests.cpp`.

- `kProtocolVersion = 11` (+ historique).
- Kinds : `CastRequest = 81` {clientId u32, targetEntityId u64 (0 si sans cible), spellId string u16-prefixed}, `ResourceUpdate = 82` {clientId u32, currentResource u32, maxResource u32}, `CastBarUpdate = 83` {clientId u32, status u8 (0 start/1 complete/2 cancel), durationMs u32, spellId string}, `AuraUpdate = 84` {targetEntityId u64, count u8, puis par aura : spellId string, type u8, remainingMs u32, stacks u8}.
- Encode/Decode + tests roundtrip chacun + rejets tronqués (pattern TestRespawnRequestRoundTrip).
- `AuraUpdate` broadcast aux clients intéressés (même périmètre que CombatEvent) à chaque apply/refresh/expire ; payload = liste complète des auras de l'entité (idempotent, pas de delta).

### Task 7: Persistance des auras (serveur) — ABANDONNÉE en V1 (décision d'exécution)

Toutes les auras des kits validés durent ≤ 12 s : persister un effet de quelques
secondes à travers un logout/login n'a aucun intérêt joueur et ajouterait un
format de sérialisation à maintenir. Reportée à l'arrivée de buffs longs
(heures) — la table `character_auras` (0061) reste réservée à cet usage.

### Task 8: PR SP3-A

- CODEBASE_MAP (section SP3-A), commit du plan + de la proposition de kits validée, push `combat-sp3-server`, PR base main, CI. **Déploiement : ⚠️ wire v11 lock-step** (à déployer avec SP3-B ; un shard v11 rejette les clients v10).

### Task 9-13: SP3-B client (PR séparée, après merge SP3-A)

9. **`SpellKitCatalog`** client (pattern CreatureCatalog, tolérant) : charge les 8 JSON, expose le kit d'un profil (le client connaît son profil via la création de perso / factions.json — à défaut, nouveau champ `profileId` dans PlayerStatsMessage ? NON : `resourceKey` y est déjà ; ajouter `profileId` string au PlayerStatsMessage est rétro-additif côté contenu mais change le payload → il est DANS le bump v11 (Task 6) : l'ajouter là).
10. **UIModel** : `ApplyResourceUpdate` → `playerStats.currentSecondaryResource` (la barre HUD ressource existante, PR #866, l'affiche déjà) ; `ApplyCastBarUpdate` → `UICastBarState {visible, spellName, progress01, durationMs}` ; `ApplyAuraUpdate` → push vers `StatusEffectManager` (membre du binding ou d'Engine) : mapping type wire → `StatusEffectType` client (Buff/Debuff/DoT/HoT/Slow existants).
11. **Barre d'action** (Engine, section HUD SP2) : 4 slots bas-centre (nom du sort, coût %, sweep cooldown via fg->AddRectFilled proportionnel) ; touches `Digit1..Digit4` (enum Key existant) → `SendCastRequest(clientId, targetEntityId, spellId)` (GameplayUdpClient, pattern SendAttackRequest).
12. **Barre de cast** : sous le cadre cible, progress = (now - start)/duration, annulation affichée.
13. **BuffBar** : `BuffBarPresenter` (orphelin M31.2) câblé — `UpdatePlayer(effects du joueur)` / `UpdateTarget(effects de la cible)` chaque frame depuis le StatusEffectManager ; rendu : icônes texte (nom abrégé + timer) sous les barres joueur / sous le cadre cible. PR SP3-B, CI, déploiement lock-step avec SP3-A.

---

## Self-review

- Couverture : 5 décisions utilisateur intégrées (kits §2 verbatim Task 1, coûts % Task 5, exclusions respectées — aucun effet vitesse joueur/stun/combo, régén Task 3, touches 1-4 Task 11).
- Tous les sorts des 8 kits sont représentables par les 9 SpellEffectDef (vérifié sort par sort, y compris les 2 cas particuliers Second souffle / Morsure du piège).
- Wire : 4 nouveaux kinds dans la plage libre 81-84 (80 = RespawnRequest SP2) ; profileId ajouté à PlayerStatsMessage dans le même bump v11.
- DRY : extraction `ApplyDamageToMob` partagée auto-attaque/sorts (Task 5) plutôt que duplication du bloc mort/loot/XP.
