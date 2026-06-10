# Spec — Système de Combat (chantier SP1→SP4)

> Validée le 2026-06-10. Issue de l'audit global du même jour (le module combat
> client existe, compilé, jamais câblé) et de la décision : **terminer plutôt que
> supprimer**. Ordre des chantiers acté : combat → groupes/party → métiers/récolte
> → prédiction client. Cette spec couvre le combat : SP1/SP2 en détail, SP3/SP4 en
> cadrage (chacun aura sa propre passe de design détaillé).

## 1. Objectif

Faire vivre l'UI combat client déjà écrite (`src/client/combat/` : CombatHud,
AdvancedCombatUi, BuffBarPresenter, AuraFXSystem, AoEPreviewSystem) en livrant la
logique métier serveur manquante (shardd), la réplication, et le câblage client —
en server-first, par sous-projets livrables séparément.

## 2. Décisions de gameplay (validées utilisateur)

| Sujet | Décision |
|---|---|
| Ciblage | **Clic** (raycast écran→monde sur les entités) **+ Tab** (cycle des hostiles par distance). |
| Mort du joueur | **Écran de mort** (overlay + bouton « Réapparaître ») → respawn au **spawn de zone**, PV pleins. Cimetières/résurrection : plus tard. |
| XP au kill | **Oui dès SP2** — `xpReward` par archétype, attribué au tueur, boucle sur la chaîne XP/level-up/recompute stats existante (PR #867). |
| Riposte | **Dès SP2** : la créature rend les coups (sans déplacement). Poursuite/leash = SP4. |
| Kits de sorts SP3 | **Kits starter par profil** (8 profils × 3-4 sorts) proposés par Claude, **validés par l'utilisateur avant implémentation**, déclinés par classe via `factions.json`. |

## 3. Existant sur lequel on s'appuie (état 2026-06-10)

- **Serveur** : `Unit` avec 11 stats dérivées dirty-trackées (hp, resource, damage,
  accuracy, range, critRate, critMult, speeds, stamina, perception, stealth +
  secondaryResource) ; `CharacterStatsEngine` déterministe (character_stats.json
  embarqué) ; `Creature.h` (Unit + templateEntry + spawnId) ; spawners JSON par zone
  (`game/data/zones/*/spawners.json` : id, archetypeId, position, count, respawnSec,
  leashDistanceMeters) ; `HostileRefManager` (Wave 19) + `ThreatList` (Wave 8) ;
  `SpellMgr`/`SpellTemplate`/`Aura` header-only (Wave 23) ; migration
  `0061_spell_aura.sql` (tables `character_auras`, `spell_proc_template`) déjà en base.
- **Réseau** : snapshots UDP périodiques (`SnapshotEntity` : entityId, EntityState
  {pos, yaw, vel, currentHealth, maxHealth, stateFlags}, playerClientId,
  characterName, gender, animationState) ; `CombatEventMessage` (struct + Encode/
  Decode existants, **sans opcode assigné**) ; plage d'opcodes **206+ libre**.
- **Client** : `UIModel` avec `combatLog` (`UICombatLogEntry`), `playerStats`,
  `targetStats`, HUD PV/ressource câblé (PR #866) ; présentateurs combat complets
  (cf. §1) qui ne demandent que des données.
- **Ticket existant** : M14.1 « Combat serveur : validation + damage + events » —
  SP2 le réalise et l'étend.

### 3.bis État réel constaté à la lecture du code (2026-06-10, avant plan SP1)

La lecture directe de `src/shared/server_bootstrap/ServerApp.{h,cpp}` et du
protocole révèle un existant **bien plus avancé** que la cartographie initiale :

- **Le serveur gameplay UDP (ServerApp, exécuté par shardd) a déjà un combat MVP
  complet** : `MessageKind::AttackRequest = 8` et `CombatEvent = 9` sont des kinds
  wire **déjà définis et encodables/décodables** ; `HandleAttackRequest`
  (ServerApp.cpp:2596) valide endpoint/clientId/mort/cooldown/portée et applique
  les dégâts ; `TryMobAttackPlayer` (riposte) existe, y compris la **mort du
  joueur** (flag + save) ; les mobs ont une **IA patrol/aggro/leash**
  (`MobAiState`, `threatTable`, `aggroTargetEntityId`), du **loot** (loot bags +
  visibilité party) et l'**XP de groupe au kill** (`DistributePartyXp`).
- Les mobs sont **déjà spawnés** (SpawnerRuntime JSON par zone) et **déjà inclus
  dans les snapshots** (`TryBuildSnapshotEntity`, branche MobEntity).
- Côté client, `UIModelBinding::ApplyCombatEvent` décode déjà CombatEvent et
  remplit `UIModel::combatLog`.

**Les manques réels sont donc** : (a) stats mobs **codées en dur**
(`kDefaultMobHealth=60`, `kDefaultMobDamage`, `kDefaultPlayerDamage=10`,
`kBaseXpPerMobKill=10`) ; (b) les 11 stats calculées (`ComputeStats`) sont
envoyées au client (`SendPlayerStats`) mais **pas injectées** dans le
`CombatComponent` du joueur ; (c) pas de jet de précision ni de critique ;
(d) pas de respawn joueur après la mort ; (e) le `SnapshotEntity` ne porte
**pas** l'`archetypeId` (le client ne peut pas savoir à quoi ressemble un mob) ;
(f) le client **ne rend pas** les entités `playerClientId == 0` (ni mesh ni
nameplate), **n'envoie jamais** `AttackRequest`, et n'instancie pas les
présentateurs combat. Les périmètres SP1/SP2 ci-dessous sont à lire à travers
ce correctif : SP1/SP2 consistent surtout à **données + branchements**, pas à
créer la logique de combat serveur (elle existe).

## 4. SP1 — Créatures visibles

### 4.1 Données : `game/data/creatures/archetypes.json` (NOUVEAU)

Catalogue d'archétypes ; la clé `id` correspond à l'`archetypeId` des spawners.

```json
{
  "archetypes": [
    {
      "id": 100,
      "name": "Sanglier des collines",
      "level": 2,
      "stats": { "hp": 180, "damage": 12, "accuracy": 80.0, "range": 2.5,
                 "critRate": 2.0, "critMult": 1.5, "attackPeriodMs": 2000 },
      "xpReward": 35,
      "model": { "mesh": "orcs", "scale": 0.9 }
    }
  ]
}
```

- Stats **directes** (pas les profils de classes joueurs) — équilibrage indépendant.
- `model.mesh` : V1 = nom d'un set de mesh de race existant (réutilisation en
  attendant des assets créatures) ; `scale` pour différencier visuellement.
- Chargé par shardd au boot (même pattern que `skills/*.json` : scan/lecture au
  démarrage, log si archetypeId inconnu référencé par un spawner). Le **client**
  charge le même fichier pour le mapping visuel (mesh, scale, name) — pas de
  duplication d'un catalogue côté client.

### 4.2 Serveur (shardd) — révisé d'après §3.bis

- Les mobs sont déjà spawnés et répliqués : le travail SP1 serveur est
  d'**appliquer les stats d'archétype** au spawn (hp, damage, portée,
  `attackPeriodMs` → cooldownTicks) à la place de `kDefaultMobHealth`/
  `kDefaultMobDamage`, et de porter `xpReward` par archétype dans
  `DistributePartyXp` (à la place de `kBaseXpPerMobKill`).
- Validation au boot : tout spawner référençant un `archetypeId` inconnu fait
  échouer l'init (même politique stricte que SpawnerRuntime).
- La riposte/mort/loot existants restent actifs (pas de régression volontaire).

### 4.3 Réplication (wire-breaking)

- `SnapshotEntity` + champ `archetypeId` (uint32, 0 = joueur) → **bump
  `kProtocolVersion`** (UDP gameplay). Client ancien ↔ shard nouveau incompatibles.

### 4.4 Client

- Rendu des entités `archetypeId != 0` sur le chemin des avatars distants :
  mesh/scale depuis archetypes.json, nameplate (name + niveau) + barre de PV
  au-dessus (même style que les joueurs distants).
- Aucune interaction en SP1.

### 4.5 Livraison SP1

- **PR-A (serveur)** : archetypes.json + loader + spawn créatures + champ
  archetypeId dans les snapshots + bump protocole + tests (loader, spawn, encode/
  decode snapshot). La spec (ce fichier) part avec cette PR.
- **PR-B (client)** : rendu créatures + nameplates. Stackée sur PR-A (partage le
  bump protocole).
- **Déploiement** : ⚠️ lock-step shardd + client (wire-breaking snapshots).

## 5. SP2 — Attaque, dégâts, mort, riposte, XP

### 5.1 Protocole (UDP gameplay) — révisé d'après §3.bis

`AttackRequest` (kind 8) et `CombatEvent` (kind 9) **existent déjà** sur le wire
UDP gameplay ; SP2 n'ajoute que : un champ `flags` (bit crit, bit miss) à
`CombatEventMessage`, et un kind `RespawnRequest` (client→shard, payload vide)
dans la suite des `MessageKind` existants. `CombatEvent` reste broadcast à
l'attaquant, la cible et les observateurs (périmètre snapshots, déjà codé).

### 5.2 Serveur — résolution d'attaque (révisé d'après §3.bis)

La validation (`HandleAttackRequest`), la riposte (`TryMobAttackPlayer` + IA
aggro/leash existante) et le tick sont **déjà en place**. SP2 serveur ajoute :

- **Stats réelles** : injecter les 11 stats calculées (`ComputeStats`) dans le
  `CombatComponent` du joueur à l'enter-world et au level-up (damage, range,
  accuracy, critRate, critMult) à la place de `kDefaultPlayerDamage`.
- **Jets** : précision (`accuracy` % → touché/raté) puis critique (`critRate` %
  → dégâts × `critMult`), reportés dans `CombatEventMessage.flags` (RNG injecté
  pour les tests). **Pas d'armure/réduction en V1.**
- **Respawn joueur** : handler `RespawnRequest` — joueur mort uniquement →
  téléport au spawn de zone, PV/ressource pleins, flag dead retiré.

### 5.3 Serveur — mort, respawn, XP (révisé d'après §3.bis)

- **Mort créature** : déjà implémentée (flag dead, despawn, respawn spawner,
  loot, XP party). L'`xpReward` par archétype est livré en **SP1** avec le
  catalogue.
- **Mort joueur** : flag + save déjà implémentés ; SP2 ajoute la purge de menace
  côté mobs au décès et le respawn via `RespawnRequest` (cf. §5.2).

### 5.4 Client

- **Ciblage** : clic = raycast écran→monde sur les entités répliquées ; Tab =
  cycle des créatures hostiles vivantes par distance croissante. La cible
  alimente `UIModel::targetStats` (le cadre cible du CombatHud existe déjà).
- **Attaque** : une touche/clic déclenche l'envoi d'`AttackRequest` (cadence
  plafonnée client à la période d'auto-attaque ; le serveur revalide).
- **Câblage des présentateurs existants** : `CombatEvent` décodé → entrées
  `UICombatLogEntry` dans `UIModel::combatLog` → CombatHud (log court, barre PV
  cible via targetCurrentHealth/Max) + AdvancedCombatUi (DPS meter, combat log
  500 lignes, filtres). Cooldown d'auto-attaque affiché dans le slot existant.
- **Écran de mort** (NOUVEAU, petit) : overlay sombre + « Réapparaître » →
  `RespawnRequest`.
- **Binds** : le bind d'attaque et Tab passent par un **registre central de binds
  in-game** (nouveau, périmètre minimal : déclaration centralisée touche→action
  pour les nouvelles touches, base pour résorber les collisions A/Y/G relevées
  par l'audit — la migration des binds existants n'est pas dans ce chantier).

### 5.5 Hors périmètre SP2

Pas de loot, pas d'armure, pas de sorts (SP3), pas de déplacement créature (SP4),
pas de PvP (cibles = créatures uniquement), pas de migration DB.

### 5.6 Livraison SP2 (3-4 PRs)

1. **PR serveur combat core** : opcodes + validation + dégâts + riposte + mort/
   respawn/evade + XP + tests (résolution, bornes de portée, cooldown, evade,
   attribution XP).
2. **PR client ciblage + attaque + HUD** : picking, Tab, AttackRequest, décodage
   CombatEvent, câblage CombatHud + AdvancedCombatUi, registre de binds minimal.
3. **PR client écran de mort + RespawnRequest** (peut fusionner avec la 2 si
   petite).
- **Déploiement** : ⚠️ lock-step shardd + client (nouveaux opcodes UDP).

## 6. SP3 — Sorts et auras par classe (cadrage)

- **Kits starter par profil** (8 profils : melee, distance, voleur, healer, sacre,
  lanceur, pisteur, tank) : 3-4 sorts mécaniques chacun, proposés et **validés
  utilisateur avant implémentation**, déclinés par classe via `factions.json`.
  Les coûts utilisent la ressource secondaire de la classe (ferveur, souffle…).
- **Données** : sorts en JSON data-driven mappés sur `SpellTemplate` (castTimeMs,
  cooldownMs, basePoints, durationMs, tickPeriodMs, rangeMeters, targetType).
  Les 3 skills de test existants (fireball, combo_builder, combo_spender) migrent
  vers ce format et deviennent fonctionnels.
- **Serveur** : pipeline de cast (file de cast par Unit : temps de cast,
  interruption, coût ressource, application des effets/auras via `Aura` existant,
  ticks DoT/HoT) ; persistance des auras dans `character_auras` (0061).
- **Réplication** : opcodes aura apply/remove/update → `StatusEffect` client →
  câblage **BuffBarPresenter** (buff/debuff bars, stacks, timers).
- Design détaillé (et liste des sorts) : passe dédiée au démarrage de SP3.

## 7. SP4 — Agro et menace, complet (cadrage)

- Déplacement/poursuite des créatures sur la heightmap, leash via
  `leashDistanceMeters` (déjà dans spawners.json), retour au spawn + evade.
- Menace multi-cibles alimentée et tickée (`ThreatList`), changement de cible par
  agro, opcode ThreatUpdate → câblage du **threat meter** d'AdvancedCombatUi
  (`UpdateThreat` existe déjà côté présentateur).
- Câblage **AuraFXSystem** (FX visuels d'auras) et **AoEPreviewSystem**
  (prévisualisation des zones d'effet, utile avec les sorts AoE de SP3).
- Design détaillé : passe dédiée au démarrage de SP4.

## 8. Tests et CI

- Serveur : tests CTest (exécutés par build-linux) sur loader d'archétypes,
  spawn, résolution d'attaque (hit/miss/crit déterministes via RNG injecté),
  bornes de portée/cooldown, evade, mort/respawn, attribution XP, encode/decode
  des nouveaux messages.
- Client : tests de présentation existants (CombatHud/AdvancedCombatUi en ont
  déjà) étendus au câblage UIModel ; pas de toolchain locale → validation par CI.
- Rappel build : tout nouveau .cpp partagé (src/shared) doit être ajouté **aussi**
  à la liste `server_app` de `src/CMakeLists.txt`.

## 9. Déploiement (récapitulatif)

Chaque SP introduit du wire-breaking UDP (champ snapshot en SP1, opcodes en
SP2/SP3/SP4) : **redéploiement shardd + client en lock-step à chaque SP**, à
signaler dans chaque PR. Le master n'est pas concerné par SP1/SP2 (tout passe
par l'UDP gameplay du shard).
