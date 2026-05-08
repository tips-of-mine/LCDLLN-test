# CMANGOS.22 — Pools (weighted spawns / nested)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.22_Pools_weighted_spawns_nested.md](../../../../tickets/CMANGOS/CMANGOS.22_Pools_weighted_spawns_nested.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — pas de système de pools pondérés. SpawnerRuntime
LCDLLN spawne des entités JSON-driven sans concept de pool/rareté/nested.

## 2. Preuves dans le code

**Existant (concepts proches mais distincts) :**
- [engine/server/SpawnerRuntime.h](../../../../engine/server/SpawnerRuntime.h) — spawners JSON par zone (sans
  poids, sans pool, sans nested)
- M15.2 — Spawners respawn + leash (lifecycle de base, pas pondération)

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/pools/` — dossier inexistant
- ❌ `PoolTemplate` + `PoolMember` structs
- ❌ `PoolManager` (Load + SpawnPool + OnMemberDespawn)
- ❌ `PoolState` runtime per-instance
- ❌ `WeightedSelector` alias method O(1)
- ❌ Pools imbriqués (`pool_pool` table)
- ❌ Tables DB `pool_template`, `pool_creature`, `pool_gameobject`,
  `pool_pool`
- ❌ Migration DB

## 3. Recouvrement milestones existantes

❌ **Non couvert** — M15.2 spawners est lifecycle de base, pas
pondération/pools. Pas de milestone pools/raretés.

## 4. Écart par rapport à la spec CMANGOS

100% absent. Sans pools pondérés :
- Pas de **rare spawns** (mobs apparition contrôlée 1% du temps)
- Pas de **scaling éditorial** (zone avec 10 mobs templates → 4 actifs
  max, choisis pondéré)
- Pas de **content variety** (sans pools, soit 1 mob fixe, soit tous
  spawnés en même temps)

C'est un pattern **simple mais puissant** : transforme N templates en
M actifs choisis pondérés, sans logique custom par mob.

## 5. Effort estimé

**M** (2 PR) :
- PR 1 : `PoolTemplate`/`PoolMember` + `PoolManager` simple (pas
  nested) + migration DB + tests
- PR 2 : pools imbriqués + `WeightedSelector` alias method + tests
  distribution sampling

Pas de wire-breaking (server-only).

## 6. Valeur joueur/serveur

**Moyenne → Élevée** — invisible joueur direct, mais **gros gain
content design**. Permet aux designers de remplir une zone avec variété
sans intervention dev.

Pas critique court-terme. Devient critique dès qu'on a beaucoup de
contenu PvE à pondérer.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.02 Entities** — pool spawne des Creature/GameObject
- **CMANGOS.13 Database** — SQLStorage cache
- **CMANGOS.19 Maps** — pool persisté par instance via
  MapPersistentState (pour les boss kills lockés au reset)

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — 4 tables (`pool_template` + `pool_creature` +
  `pool_gameobject` + `pool_pool`). Idempotent.
- ⚠️ **Pools imbriqués cycles** — `pool A` contient `pool B` contient
  `pool A` = boucle infinie. Détection DFS au load.
- ⚠️ **`max_active` invariant** — si 5 membres pondérés et `max_active=2`,
  toujours exactement 2 actifs (sauf transition respawn). Bug subtil
  possible si mort + spawn intercalés.
- ⚠️ **WeightedSelector** — alias method O(1) ou roulette wheel O(N).
  Pour pools < 100 membres, roulette OK. Pour 1000+, alias method.
- ⚠️ **Pools partagés cross-instance** — si pool global à la map vs
  par instance. Convention claire (cmangos = par instance).
- ⚠️ **Persistance respawn timer** — au reboot shard, perte des timers
  in-flight. Recharger via `MapPersistentState` ou repartir tous à 0.

## 9. Recommandation finale

🔧 **Adapter et faire**, **après** CMANGOS.13 + CMANGOS.19 :

1. **Étape 0** : valider que le besoin **rare spawns / pools pondérés**
   est sur la roadmap (sinon reporter).
2. **Étape 1** : `PoolTemplate`/`PoolMember`/`PoolManager` simple +
   migration DB + tests (pas nested).
3. **Étape 2** : intégration avec `MapPersistentState` (CMANGOS.19) pour
   timers + lockouts.
4. **Étape 3** : `WeightedSelector` alias method si volumétrie
   justifie (sinon roulette).
5. **Étape 4** : pools imbriqués (`pool_pool`) avec détection cycles.
6. **Étape 5** : refactor `SpawnerRuntime` pour utiliser `PoolManager`
   (ou conserver les 2 pour cas simples vs complexes).

À planifier **après** la chaîne P1 + .19 Maps. Effort moyen, ROI
content design élevé.

---

*Audit du 2026-05-08. Mises à jour : —*
