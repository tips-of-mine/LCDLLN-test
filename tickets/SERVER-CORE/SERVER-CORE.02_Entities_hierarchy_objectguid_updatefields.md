# SERVER-CORE.02_Entities_hierarchy_objectguid_updatefields

## Objectif

Mettre en place le **socle d'entités du shard** LCDLLN, inspiré de
`src/game/Entities` server-core, qui sert de fondation à toute la simulation
côté serveur :

1. Une **hiérarchie Object → WorldObject → Unit → Player/Creature/Pet** +
   `GameObject`/`Item`/`Corpse` partageant des concepts communs (position,
   GUID, état actif).
2. Un **`ObjectGuid` universel** (64 bits) avec le type encodé dans les
   bits hauts, routeable entre master ↔ shard sans table de lookup
   secondaire.
3. **`UpdateFields` + `UpdateMask`** : sérialisation par delta des champs
   réseau (chaque champ a un index dans un tableau, un bitmask marque les
   « dirty »). Pré-requis #1 pour AoI scalable — sans ça, la bande
   passante explose dès 50 joueurs co-localisés.
4. **`TemporarySpawn`** comme classe dédiée pour les entités à durée de
   vie limitée (projectiles persistants, summons, totems) — évite de
   polluer le spawn pool persistant.
5. **`CreatureLinkingMgr`** data-driven : « si A meurt alors B respawn /
   B aggro » décrit en table DB, pas en code.

C'est un **P1 squelette** : la majorité des features gameplay (combat,
spells, AoI, mouvement) en dépendent.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.03 (Grids — pour exposer `WorldObject::AddToWorld()` qui insère dans la grille)
- SERVER-CORE.05 (Movement — `WorldObject` porte la position que Movement met à jour)

L'ordre conseillé d'implémentation est : Entities (squelette des classes
sans persistance complète) → Grids → Movement, en allant et venant.

## Livrables

### Côté shard (`engine/server/shard/entities/`)

- `Object.{h,cpp}` — racine de la hiérarchie, porte le `ObjectGuid` et le
  tableau `UpdateFields`.
- `WorldObject.{h,cpp}` — ajoute position 3D (x, y, z, orientation),
  zoneId, mapId, et l'API `AddToWorld()` / `RemoveFromWorld()`.
- `Unit.{h,cpp}` — ajoute stats (HP, mana, level, faction), state combat,
  équipement minimal, mouvement (référence vers MotionMaster, à arriver
  plus tard).
- `Player.{h,cpp}` — sous-classe de Unit avec session client.
- `Creature.{h,cpp}` — sous-classe de Unit, IA + spawn pool.
- `GameObject.{h,cpp}` — sous-classe de WorldObject (portes, coffres,
  panneaux).
- `Item.{h,cpp}` — sous-classe de Object (pas de position monde
  directement, vit dans un inventaire).
- `Corpse.{h,cpp}` — sous-classe de WorldObject (corps de joueur mort).
- `TemporarySpawn.{h,cpp}` — classe utilitaire pour spawn éphémère
  (durée, owner, despawn auto).
- `CreatureLinkingMgr.{h,cpp}` — singleton chargé au boot depuis la DB.

### Système GUID & UpdateFields (`engine/server/shard/entities/`)

- `ObjectGuid.{h,cpp}` — wrapper 64 bits, encode `(uint8 type, uint16 entry, uint64 counter)`.
  - Types : Player, Creature, Pet, GameObject, Item, Corpse, DynamicObject.
  - Méthodes : `IsPlayer()`, `IsCreature()`, `GetEntry()`, `GetCounter()`,
    `ToString()`, `FromString()`, `operator<<` pour `ByteBuffer`.
- `UpdateFields.h` — enum centralisée des indices de champs réseau, par
  type d'entité (`PLAYER_FIELD_HEALTH`, `UNIT_FIELD_LEVEL`,
  `OBJECT_FIELD_GUID`…).
- `UpdateMask.{h,cpp}` — bitmask compact, API `SetBit(idx)`, `GetBit(idx)`,
  `Serialize(ByteBuffer&)`, `Reset()`.

### Snapshot delta réseau

- `engine/server/shard/snapshot/SnapshotBuilder.{h,cpp}` — pour chaque
  joueur cible, parcourt les `WorldObject` dans son AoI (via Grids) et
  construit un payload `UPDATE_OBJECT` ne contenant que les champs dirty
  de chaque entité dirty. Reset des masks à la fin du tick.

### Configuration (`config.json`)

```json
"entities": {
  "max_active_per_grid": 200,
  "creature_linking_enabled": true,
  "temporary_spawn_default_lifetime_sec": 60
}
```

### Migration DB

- `engine/server/migrations/00xx_creature_linking.sql` :
  ```sql
  CREATE TABLE creature_linking_template (
    entry          INT UNSIGNED NOT NULL,
    master_entry   INT UNSIGNED NOT NULL,
    flags          INT UNSIGNED NOT NULL,  -- bitmask: ON_DEATH_RESPAWN, ON_AGGRO_AGGRO, ...
    search_range   FLOAT NOT NULL DEFAULT 0,
    PRIMARY KEY (entry, master_entry)
  );
  ```

### Tests

- `engine/server/shard/entities/ObjectGuidTests.cpp` — round-trip
  encode/decode, `ToString` round-trip, ordering.
- `engine/server/shard/entities/UpdateMaskTests.cpp` — set/get, sérialisation
  compacte (un seul bit set ne devrait pas envoyer 32 bits).
- `engine/server/shard/snapshot/SnapshotBuilderTests.cpp` — un Unit dont
  seul HP change ne génère qu'un payload contenant l'index `UNIT_FIELD_HEALTH`.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : N/A
- Outils offline : N/A
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. `ObjectGuid` (64 bits)

```
[ 8 bits type | 16 bits entry (template id) | 40 bits counter ]
```

- `type` : `Player=1, Creature=2, Pet=3, GameObject=4, Item=5, Corpse=6, DynamicObject=7`.
- `entry` : `creature_template.entry` ou `gameobject_template.entry`.
  Pour Player/Item/Corpse, `entry = 0`.
- `counter` : auto-incrémenté à la création, unique par type. Persisté
  pour Player/Item (DB primary key), volatile pour Creature/Pet/Corpse
  (compteur runtime, reset au reboot du shard).

Routage master/shard : un `ObjectGuid` est self-describing — le master
peut router un message vers le bon shard sans avoir une table secondaire
si une convention `counter % nbShards = shardId` est appliquée pour les
entités shard-locales (à débattre dans un futur ticket).

### 2. `UpdateFields` + `UpdateMask`

Inspiré directement de server-core mais adapté C++20 :

```cpp
enum class ObjectField : uint16_t {
  Guid           = 0,  // 8 bytes (= 2 slots de 32 bits)
  Type           = 2,
  Entry          = 3,
  ScaleX         = 4,
  Padding        = 5,
  ObjectFieldEnd = 6
};

enum class UnitField : uint16_t {
  Health = ObjectField::ObjectFieldEnd,
  MaxHealth,
  Level,
  Faction,
  ...
  UnitFieldEnd
};
```

Chaque entité tient un `std::array<uint32_t, kFieldCount>` + un
`UpdateMask` de même taille. Modifier un champ via `SetField(idx, value)`
qui met `mask.SetBit(idx)`.

À chaque tick AoI :

```cpp
for (auto* obj : visibleObjects) {
  if (obj->HasDirtyFields()) {
    builder.AppendUpdate(obj);  // ne sérialise que les dirty
  }
}
// puis :
for (auto* obj : objects) obj->ResetDirtyFields();
```

**Format de sérialisation `UPDATE_OBJECT`** :

```
[ uint16 opcode ][ varint nbObjects ]
  pour chaque objet :
    [ ObjectGuid (8 bytes) ]
    [ UpdateMask compact (longueur variable) ]
    [ pour chaque bit set, valeur 4 bytes (ou 8 pour les champs 64 bits) ]
```

`UpdateMask compact` : RLE-encodé ou tableau de `uint32` avec préfixe de
longueur (à benchmarker). server-core utilise un préfixe de longueur en
nombre de blocs de 32 bits + le tableau brut — démarrer pareil, optimiser
si la profilage montre que le mask domine.

### 3. `WorldObject::AddToWorld()` / `RemoveFromWorld()`

Une fois Grids livré (SERVER-CORE.03), `AddToWorld()` insère l'entité dans la
cellule appropriée et la marque pour notification AoI. `RemoveFromWorld()`
inverse.

Avant Grids : ces méthodes sont des stubs qui marquent juste un flag
`m_inWorld`. Tag `// TODO(SERVER-CORE.03)` clairement.

### 4. `TemporarySpawn`

Sous-classe de `Creature` (ou `WorldObject` selon usage) avec :

- `m_lifetimeMs` : durée de vie maximale.
- `m_ownerGuid` : qui a invoqué (utilisé pour cleanup à la mort/déco du owner).
- `m_despawnReason` : enum (`Lifetime`, `OwnerGone`, `Combat`, `Manual`).

Auto-despawn dans le tick si `now > spawnTime + lifetime` ou
`owner == nullptr`.

### 5. `CreatureLinkingMgr`

Charge `creature_linking_template` au boot. API :

```cpp
class CreatureLinkingMgr {
public:
  void Load(ConnectionPool& db);
  // À l'événement, retourne la liste de creatures liées à exécuter.
  std::vector<LinkAction> GetLinksFor(uint32_t entry, LinkEvent event) const;
};
```

`LinkEvent` : `OnDeath`, `OnAggro`, `OnRespawn`, `OnEvade`.

Câblé dans `Creature::OnDeath()`, etc., pour déclencher les actions
liées (`RESPAWN_LINKED`, `AGGRO_LINKED`).

## Étapes d'implémentation

1. Créer le dossier `engine/server/shard/entities/`.
2. **Implémenter `ObjectGuid`** + ses tests (le plus isolé, le plus testable).
3. **Implémenter `UpdateMask`** + ses tests.
4. **Implémenter `Object`** (header avec `m_guid`, `m_updateFields`, `m_updateMask`).
5. **Implémenter `WorldObject`** (Object + position).
6. **Implémenter `Unit`** + `Player` + `Creature` minimaux (champs réseau, pas de gameplay).
7. **Implémenter `GameObject`, `Item`, `Corpse`** comme stubs.
8. **Implémenter `SnapshotBuilder`** côté serveur shard (peut être stubbé sans Grids : itère tous les WorldObject de la map).
9. **Définir l'opcode `kOpcodeUpdateObject`** côté wire (master+shard+client).
10. **Implémenter `TemporarySpawn`** (peut attendre une feature qui le requiert ; au minimum la classe + cleanup tick).
11. **Migration DB + `CreatureLinkingMgr`**.
12. **Tests** : 3 fichiers listés.
13. **Doc** : ajouter une section « Entités shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard) via presets existants
- [ ] Tests `ObjectGuidTests`, `UpdateMaskTests`, `SnapshotBuilderTests` passent
- [ ] Smoke test : créer un Player+Creature côté shard, modifier 2 champs, vérifier que le snapshot ne contient que ces 2 champs (pas le tableau complet)
- [ ] Round-trip `ObjectGuid::ToString()` + `FromString()` ok pour les 7 types
- [ ] Migration `creature_linking_template` appliquée et idempotente
- [ ] `kOpcodeUpdateObject` documenté côté master/shard/client (même valeur, même format)
- [ ] Aucun nouveau dossier racine non autorisé créé
- [ ] Rapport final : fichiers modifiés + commandes + résultats + DoD

## Notes / pièges à éviter

- **Sizing des UpdateFields** : server-core a des champs réseau **et** des champs runtime (cooldowns, threats…). Ne jamais sérialiser les champs runtime — sinon fuite d'info ou bande passante gaspillée. Garder les deux séparés (`UpdateFields` réseau ; membres C++ classiques pour le runtime).
- **Reset du mask** : appeler `ResetDirtyFields()` **après** que tous les joueurs cibles aient reçu le snapshot. Si on reset trop tôt (au milieu du tick), des cibles loupent les updates.
- **GUID counter overflow** : 40 bits = 1 trillion, suffisant pour des décennies. Mais le compteur Creature/Pet est volatile (reset au reboot) — si un client ressuscite avec un GUID stale, il faut renvoyer un `OBJECT_DESTROYED` avant le nouveau `OBJECT_CREATED`. Le snapshot builder doit gérer ça.
- **Hiérarchie virtuelle** : Object → WorldObject → Unit → Player. **6 niveaux d'héritage** est tolérable mais à ne pas étendre à la légère ; chaque niveau ajoute un offset et complique le debug. Garder Camera **hors** hiérarchie (composition, pas héritage) pour ne pas alourdir.
- **TemporarySpawn et Creature::OnDeath** : si une TemporarySpawn meurt en combat, **ne pas** déclencher le respawn timer normal de Creature. Override explicite.
- **`CreatureLinkingMgr` : éviter les cycles** : si A meurt → B aggro et B meurt → A respawn, on a une boucle infinie. Le mgr doit détecter les cycles au load et les rejeter avec un log d'erreur.
- **ObjectGuid 64 bits vs `uint64_t` brut** : utiliser une **classe** (pas un alias), pour bloquer les conversions implicites accidentelles (`uint64_t` à `int` perd les bits hauts). Définir `operator==`, `operator<`, `std::hash<ObjectGuid>`.

## Références

- `SERVER-CORE_ANALYSIS.md` § Entities (P1 shard)
- server-core `src/game/Entities/Object.h`, `ObjectGuid.h`, `UpdateData.cpp`,
  `UpdateMask.h`, `Unit.h`, `Player.h`, `Creature.h`, `TemporarySpawn.h`,
  `CreatureLinkingMgr.h`
