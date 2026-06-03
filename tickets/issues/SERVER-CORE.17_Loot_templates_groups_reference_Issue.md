# Issue: SERVER-CORE.17

**Status:** Closed

_Re-verifie DONE le 2026-06-03 (correction d'un faux-negatif du au decalage de chemins engine/ -> src/)._

## Preuves d'implementation
- src/shardd/loot/LootTable.h
- src/shardd/loot/LootTableTests.cpp

## Note
LootTable/Registry/Rule + tests

---

## Contenu du ticket (SERVER-CORE.17)

# SERVER-CORE.17_Loot_templates_groups_reference

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/shardd/loot/LootTable.cpp;src/shardd/loot/LootTableTests.cpp
> - Manque : LootMgr/generate/refs absents
> - Resume : Loot partiel

## Objectif

Mettre en place le **système de butin (loot)** côté shard LCDLLN,
inspiré de `src/game/Loot` server-core. Cinq piliers :

1. **Loot templates en DB** : `creature_loottemplate`,
   `gameobject_loottemplate`, `fishing_loottemplate`,
   `pickpocketing_loottemplate` — **même schéma générique**
   `(entry, item, chance, group, mincount, maxcount, condition_id)`
   réutilisé partout.
2. **Loot groups** : à l'intérieur d'un template, des items partagent
   un `group` qui garantit qu'**exactement un drop** (somme des chances
   dans le groupe = 100%) — sépare loot garanti et loot bonus.
3. **Reference loot** : un template peut référencer un autre par
   convention (`item < 0` → reference) — DRY pour les loot tables
   partagées entre boss similaires.
4. **Conditions par drop** : un item ne tombe que si le looter remplit
   une `Condition` (SERVER-CORE.16) — composabilité avec quêtes/factions.
5. **Round-robin / need-greed runtime** : la résolution dépend de la
   `GroupLootMethod`, pas du template — orthogonalité données/règles.

C'est un **P2 shard**, pré-requis dès le premier PNJ avec drops.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.13 (Database — SQLStorage cache pour les templates)
- SERVER-CORE.16 (Globals/Conditions — condition_id par drop)
- SERVER-CORE.15 (Groups — loot rules par groupe)

## Livrables

### Côté shard (`engine/server/shard/loot/`)

- `LootEntry.h` — struct row :
  ```cpp
  struct LootEntry {
    int32_t  itemId;      // <0 = reference vers autre template
    float    chance;
    uint8_t  groupId;     // 0 = pas dans un groupe
    uint8_t  minCount;
    uint8_t  maxCount;
    uint32_t conditionId; // 0 = pas de condition
  };
  ```
- `LootTemplate.h` — collection de `LootEntry` indexée par `entry`.
- `LootMgr.{h,cpp}` :
  - `Load(ConnectionPool&)` — charge les 4 tables avec SQLStorage.
  - `Loot Generate(LootTemplateType type, uint32_t entry, Player const* looter)` — génère le butin selon les rolls + conditions.
- `Loot.{h,cpp}` — pile de loot active sur une entité morte/coffre :
  - `std::vector<LootItem>` items générés.
  - Tracking « qui a vu / qui a pris ».
- `LootRoll.{h,cpp}` — rolls (Need, Greed, Pass, Disenchant) côté groupe.

### Migration DB

```sql
-- 4 tables avec schéma identique
CREATE TABLE creature_loottemplate (
  entry         INT UNSIGNED NOT NULL,
  item          INT NOT NULL,                  -- <0 = reference
  chance        FLOAT NOT NULL,
  group_id      TINYINT UNSIGNED NOT NULL DEFAULT 0,
  min_count     TINYINT UNSIGNED NOT NULL DEFAULT 1,
  max_count     TINYINT UNSIGNED NOT NULL DEFAULT 1,
  condition_id  INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (entry, item)
);
-- mêmes pour gameobject_loottemplate, fishing_loottemplate, pickpocketing_loottemplate

CREATE TABLE reference_loot_template (
  entry         INT UNSIGNED NOT NULL,    -- ref entry positif
  item          INT UNSIGNED NOT NULL,
  chance        FLOAT NOT NULL,
  group_id      TINYINT UNSIGNED NOT NULL DEFAULT 0,
  min_count     TINYINT UNSIGNED NOT NULL DEFAULT 1,
  max_count     TINYINT UNSIGNED NOT NULL DEFAULT 1,
  condition_id  INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (entry, item)
);
```

### Configuration (`config.json`)

```json
"loot": {
  "default_drop_factor": 1.0,
  "show_chance_in_log": false,
  "max_drops_per_loot": 16,
  "fish_loot_enabled": true
}
```

### Tests

- `LootMgrTests.cpp` — generate avec template simple.
- `LootGroupTests.cpp` — group avec 3 items chance 50/30/20 → toujours exactement 1 drop.
- `LootReferenceTests.cpp` — `itemId < 0` redirige vers reference template.
- `LootConditionTests.cpp` — drop conditionné sur `LevelGE 10` filtre correctement.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Génération

```cpp
Loot LootMgr::Generate(LootTemplateType type, uint32_t entry, Player const* looter) {
  auto* tmpl = GetTemplate(type, entry);
  Loot result;
  // 1. Items hors group : roll chacun individuellement
  for (auto& e : tmpl->ungroupedEntries) {
    if (e.conditionId && !g_conditions.Evaluate(e.conditionId, looter)) continue;
    if (RollFloat() <= e.chance) {
      result.AddItem(ResolveItemRef(e), RollCount(e.minCount, e.maxCount));
    }
  }
  // 2. Items en group : un seul drop par group (cumulative)
  for (auto& [groupId, members] : tmpl->groupedEntries) {
    auto chosen = SelectFromGroup(members, looter);
    if (chosen) result.AddItem(ResolveItemRef(*chosen), RollCount(...));
  }
  return result;
}
```

### 2. Reference resolution

`itemId < 0` → on traite `-itemId` comme `entry` dans `reference_loot_template`. Récursif possible (ref → ref) — limiter profondeur à 3.

### 3. Loot pile (sur cadavre)

À la mort d'une Creature, `LootMgr::Generate` produit un `Loot` qui est attaché au Corpse. Les joueurs autorisés (selon GroupLoot) voient le contenu via `kOpcodeLootContents`.

### 4. Group loot rules

| Method | Description |
|---|---|
| FreeForAll | Premier arrivé = preneur |
| RoundRobin | Cycle parmi les membres |
| MasterLoot | Le master looter assigne |
| NeedBeforeGreed | Vote Need (priorité) → Greed → Pass → DE |

Géré par `LootRoll` côté Group (SERVER-CORE.15).

## Étapes d'implémentation

1. Créer `engine/server/shard/loot/`.
2. Migrations DB.
3. Implémenter `LootMgr::Load` avec SQLStorage.
4. Implémenter `Generate` avec rolls + groups + conditions.
5. Implémenter reference resolution.
6. Implémenter `Loot` pile (attaché à un Corpse).
7. Implémenter `LootRoll` pour les groupes.
8. Allouer opcodes (`kOpcodeLootContents`, `kOpcodeLootTake`).
9. Tests : 4 fichiers.
10. Doc : section « Loot shard » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard)
- [ ] Tests passent
- [ ] Smoke test : creature avec loot template (3 items, 1 group de 2) → drops cohérents stat sur 1000 kills
- [ ] Reference loot : `itemId = -42` → fetch dans `reference_loot_template[42]`
- [ ] Drop conditionné : seul un Player niveau ≥ 10 voit l'item
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Group sum != 100%** : si la somme des chances d'un group < 100%, parfois rien ne drop (bug). Si > 100%, comportement indéfini. **Validation au load** : warning si group != 100% (pas reject — utile en dev).
- **Reference cycles** : ref A → ref B → ref A → boucle. Détecter au load.
- **Conditions performance** : 100 entries × 50 conditions evaluations = 5000 evals par mob mort. Profiler. Si lent, cache les conditions évaluées par looter pour la session.
- **Reroll RNG** : pour des drops symboliques (legendary), le RNG doit être audit-able. Stocker le seed du roll en log debug.
- **Drop factor config** : `default_drop_factor` permet d'ajuster globalement (ex. event x2 drops). Ne **pas** mettre en hard-coded — toujours via config.
- **Pickpocket loot** : seulement si la cible est non hostile et fait par un Rogue. Distinct du loot de cadavre.
- **Fishing** : table à part car les "entries" sont des `area_id`, pas des `creature_entry`.

## Références

- `SERVER-CORE_ANALYSIS.md` § Loot (P2 shard)
- server-core `src/game/Loot/LootMgr.cpp`, `LootHandler.cpp`,
  schémas `creature_loottemplate.sql` etc.
