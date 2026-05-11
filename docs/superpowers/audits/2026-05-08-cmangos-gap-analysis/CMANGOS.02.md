# CMANGOS.02 — Entities (hierarchy / ObjectGuid / UpdateFields)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.02_Entities_hierarchy_objectguid_updatefields.md](../../../../tickets/CMANGOS/CMANGOS.02_Entities_hierarchy_objectguid_updatefields.md)
> **Priorité** : P1 — squelette
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — aucune trace de la hiérarchie `Object → WorldObject → Unit`,
ni de `ObjectGuid` 64-bit, ni de `UpdateFields` / `UpdateMask`. L'architecture
actuelle LCDLLN diverge : mobs/spawners pilotés par JSON data-driven
(`SpawnerRuntime`) au lieu d'une hiérarchie OOP.

## 2. Preuves dans le code

**Existant (architecture alternative LCDLLN) :**
- [engine/server/SpawnerRuntime.h](../../../../engine/server/SpawnerRuntime.h) — data-driven JSON
  (`SpawnerDefinition` : `archetypeId`, position, `count`, `respawnSeconds`,
  `leashDistanceMeters`)
- [engine/server/SpatialPartition.cpp](../../../../engine/server/SpatialPartition.cpp) — partitionnement spatial
  (couvre une partie de CMANGOS.03 Grids, mais sans Visitor/GridStates)
- [engine/server/ZoneTransitions.cpp](../../../../engine/server/ZoneTransitions.cpp) — transitions de zone

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/entities/` — dossier inexistant (et même `engine/server/shard/`)
- ❌ `Object`, `WorldObject`, `Unit`, `Player`, `Creature`, `GameObject`, `Item`, `Corpse`
- ❌ `ObjectGuid` (64 bits encodant type + entry + counter) — grep 0 résultat
- ❌ `UpdateFields` enum + `UpdateMask` bitmask compact
- ❌ `SnapshotBuilder` (delta réseau dirty-only)
- ❌ Opcode `kOpcodeUpdateObject` côté wire
- ❌ `TemporarySpawn` (entités à durée de vie limitée)
- ❌ `CreatureLinkingMgr` data-driven (table `creature_linking_template`)
- ❌ Migration `creature_linking_template.sql`

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement, approche différente)** — plusieurs milestones
LCDLLN couvrent le sujet sous un angle différent :
- M13.2 Spatial partition cells 64m + interest set
- M13.3 Replication spawn-despawn state updates by interest
- M13.4 Zone transitions
- M15.2 Spawners respawn + leash + activation par présence joueurs

Ces milestones partent d'une logique **data-driven JSON** (pas d'héritage
OOP profond), ce qui crée un **conflit d'approche architecturale** avec
CMANGOS.02.

## 4. Écart par rapport à la spec CMANGOS

100% des livrables sont absents — mais c'est **partiellement intentionnel** :
LCDLLN a fait le choix d'une architecture data-driven (spawner JSON +
archetype id) plutôt que la hiérarchie OOP cmangos. La question n'est pas
"est-ce qu'on porte ?" mais "est-ce qu'on a besoin de la hiérarchie pour
scaler ?".

Les éléments les plus utiles à porter même dans une archi data-driven :
- `ObjectGuid` 64-bit — utile pour réseau cross-shard, indépendamment du
  modèle d'héritage
- `UpdateFields` / `UpdateMask` delta — **pré-requis AoI scalable** sans
  ça, la bande passante explose dès 50 joueurs co-localisés (cf. ticket §3)
- `TemporarySpawn` lifecycle — concept utile pour summons/projectiles

Les éléments **moins** prioritaires en archi data-driven :
- Hiérarchie `Object → Unit → Player` 6 niveaux (overkill si entités =
  POD + systèmes)
- `CreatureLinkingMgr` (peut être remplacé par un trigger system data-driven
  type DBScripts)

## 5. Effort estimé

**XL** (multi-sprints) — refonte transversale. Si on l'adopte tel quel :
- 9 nouvelles classes hiérarchiques + virtuels
- Système GUID + UpdateMask + tests
- SnapshotBuilder + opcode wire
- Migration DB + CreatureLinkingMgr
- **Plus** : refonte de tout le code shard existant qui touche aux entités
  (SpawnerRuntime, ZoneTransitions, GatheringSystem, CraftingSystem) pour
  qu'ils passent par la nouvelle hiérarchie.

Si on adapte (cherrypick `ObjectGuid` + `UpdateFields` uniquement) → **L** (1 sprint).

## 6. Valeur joueur/serveur

**Critique (architecturalement)** mais **invisible joueur**. Sans
`UpdateFields/UpdateMask` delta, l'AoI scalable est impossible (le bandwidth
explose) → impact direct sur la limite de joueurs co-localisés que le shard
peut tenir. C'est un déblocant performance pour les zones populeuses
(capitales, arènes).

## 7. Dépendances bloquantes

- **CMANGOS.03 Grids** — `WorldObject::AddToWorld()` insère dans la grille
  (ticket le mentionne explicitement comme co-requis)
- **CMANGOS.04 Movement** — `WorldObject` porte la position que Movement
  met à jour
- Aucune dépendance milestone bloquante (`SpatialPartition` actuelle suffit
  comme stub temporaire)

## 8. Risque / piège ⚠️

- ⚠️ **Wire-breaking** — nouvel opcode `kOpcodeUpdateObject` + bump
  `kProtocolVersion` → **redéploiement serveur master + shard + client
  lock-step**.
- ⚠️ **Migration DB** — table `creature_linking_template` à créer dans
  `db/migrations/00xx_creature_linking.sql`.
- ⚠️ **Redéploiement** — nouveaux handlers shard, refonte des handlers
  entités existants.
- ⚠️ **Conflit architectural** — l'approche cmangos (héritage profond)
  est en tension avec l'approche LCDLLN (data-driven JSON). **Décision
  archi à valider en amont** sinon double-implémentation = dette.
- ⚠️ **Refonte transversale** — touche tout le code shard existant qui
  référence des entités. Risque de régressions massives si fait à la va-vite.
- ⚠️ **GUID counter overflow / volatile** — Creature/Pet counter reset au
  reboot shard. Si client ressuscite avec GUID stale → `OBJECT_DESTROYED`
  + nouveau `OBJECT_CREATED` requis (§Notes du ticket).
- ⚠️ **Cycles linking** — si A meurt → B aggro et B meurt → A respawn,
  boucle infinie. Manager doit détecter au load.

## 9. Recommandation finale

🔧 **Adapter et faire** — **ne pas porter cmangos littéralement**. Décision
archi à prendre en amont :

1. **Étape 0 (décision)** : statuer sur le modèle entité du shard.
   - Option A : conserver l'archi data-driven (SpawnerRuntime + archetype),
     porter uniquement `ObjectGuid` + `UpdateFields/UpdateMask` (effort L).
   - Option B : adopter la hiérarchie cmangos complète (effort XL,
     refonte transversale).
   - **Recommandation** : Option A (cherry-pick) — le pattern dirty-fields
     est éprouvé et orthogonal à l'archi entité.
2. **Étape 1** : implémenter `ObjectGuid` (64-bit, type+entry+counter),
   le plus isolé, testable indépendamment.
3. **Étape 2** : implémenter `UpdateFields` enum + `UpdateMask` bitmask
   + SnapshotBuilder côté shard.
4. **Étape 3** : allouer `kOpcodeUpdateObject`, bump `kProtocolVersion`,
   intégrer côté client.
5. **Étape 4** : décider plus tard pour `TemporarySpawn` et
   `CreatureLinkingMgr` selon le besoin réel (probablement DBScripts via
   CMANGOS.14 fera mieux que `CreatureLinkingMgr`).

À reporter à plus tard si M13.3 (Replication spawn-despawn state updates)
est déjà livré et tient la charge — auquel cas il existe déjà une réponse
LCDLLN au problème AoI delta.

---

*Audit du 2026-05-08. Mises à jour : —*
