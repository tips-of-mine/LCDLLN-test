# CMANGOS.13 — Database (SQLStorage / async / prepared)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.13_Database_sqlstorage_async_prepared.md](../../../../tickets/CMANGOS/CMANGOS.13_Database_sqlstorage_async_prepared.md)
> **Priorité** : P2 — gameplay essentiel (mais infra)
> **Cible** : cross (master + shard)

## 1. Statut implémentation

🟡 **Partiel** — `ConnectionPool` MySQL avec `Guard` RAII présent, mais
les 3 patterns cmangos demandés (`SQLStorage` cache RAM typé,
`SqlDelayThread` async worker, `SqlPreparedStatement` binding-typed)
sont **absents**.

## 2. Preuves dans le code

**Existant :**
- [engine/server/db/ConnectionPool.h](../../../../engine/server/db/ConnectionPool.h) — pool MySQL thread-safe
  (Acquire/Guard, health check ping, reconnect, warm-up)
- [engine/server/db/ConnectionPool.cpp](../../../../engine/server/db/ConnectionPool.cpp) — implémentation
- [engine/server/db/DbHelpers.h](../../../../engine/server/db/DbHelpers.h) + `.cpp` — helpers `DbQuery`/`DbFreeResult`
- [engine/server/db/DbLayerTests.cpp](../../../../engine/server/db/DbLayerTests.cpp) — tests existants
- [db/migrations/](../../../../db/migrations/) — 40+ migrations idempotentes
- M44.1 — Database migrations versioned schema + auto-apply (déjà livré)

**Manquant (vs spec ticket) :**
- ❌ `SQLStorage<T>` templated (cache RAM typé read-only chargé au boot)
- ❌ `SQLStorageDecl.h` (macros mapping table → struct)
- ❌ Hot-reload `.reload <storage_name>` GM command
- ❌ `SqlDelayThread` async worker (queue + futures + callbacks)
- ❌ `QueryResultFuture` pattern non-bloquant
- ❌ `SqlPreparedStatement` (wrapper `MYSQL_STMT` avec binding typed)
- ❌ Cache de prepared statements par connexion
- ❌ Config `db.delay_thread_enabled`, `db.delay_thread_queue_size`,
  `db.prepared_statement_cache_size_per_conn`,
  `db.sql_storage_log_load_durations`

## 3. Recouvrement milestones existantes

✅ **Couvert (partiellement)** — M44.1 (migrations) couvre la persistance
schema. ConnectionPool couvre l'accès. Mais les 3 patterns spécifiques
du ticket sont absents.

## 4. Écart par rapport à la spec CMANGOS

L'écart est important côté **performance/scalabilité** :

1. **SQLStorage** — actuellement, chaque consultation d'une table
   "statique" (ex: `creature_template`, `item_template`, `loot_template`)
   passe par une requête DB. Avec SQLStorage, on charge une fois au boot
   en RAM et accès O(1). **Énorme** différence quand le shard tourne avec
   1000+ mobs et que chaque tick consulte les templates.
2. **SqlDelayThread** — actuellement, les opérations DB (audit log,
   sauvegarde joueur, etc.) bloquent le worker IO ou le tick. Avec un
   worker async, le tick continue, l'opération DB se fait en background
   avec callback.
3. **SqlPreparedStatement** — actuellement, chaque query est parsée à
   chaque appel par MySQL. Avec prepared statements, parsing unique +
   binding rapide. Réduit la charge MySQL substantiellement.

Pas critique en V1 (load faible) mais devient critique dès qu'on tient
plus de quelques centaines de joueurs concurrents.

## 5. Effort estimé

**M-L** (3 PR) :
- PR 1 : `SQLStorage<T>` templated + 1-2 instanciations test
  (creature_template ou loot_template), tests load + lookup O(1)
- PR 2 : `SqlDelayThread` worker + `QueryResultFuture` + tests
  enqueue 100 queries → toutes complètent
- PR 3 : `SqlPreparedStatement` wrapper + cache par conn + tests
  binding 5 types

Pas de wire-breaking. Pas de migration DB. Refactor possible des
helpers existants pour utiliser le nouveau wrapper.

## 6. Valeur joueur/serveur

**Élevée** — invisible joueur mais déblocant pour la performance shard.
Pré-requis explicite pour CMANGOS.09 AuctionHouse (tick global, index
RAM), CMANGOS.07 AI (`SQLStorage<CreatureAIScript>`),
CMANGOS.17 Loot (`SQLStorage<LootTemplate>`).

Sans SQLStorage, chaque feature P2 qui tape dans une table statique
réimplémente son propre cache, ce qui crée de la dette.

## 7. Dépendances bloquantes

Aucune dépendance bloquante non livrée :
- ConnectionPool : déjà en place
- DbHelpers : déjà en place
- M44.1 migrations : déjà livré

→ **CMANGOS.13 est un déblocant amont** pour 5+ tickets P2 downstream.

## 8. Risque / piège ⚠️

- ⚠️ **SQLStorage hot-reload thread-safety** — si GM fait `.reload
  creature_template` en plein tick, swap pointer atomique requis. Bug
  subtil possible (nouveau lookup pointe vers ancien cache pendant le
  swap). Pattern : `std::shared_ptr<const StorageT>` + atomic swap.
- ⚠️ **SqlDelayThread queue overflow** — si la queue déborde (1024 par
  défaut), choisir : block caller (perf), drop op (cohérence), grow
  queue (mémoire). Politique à acter explicitement.
- ⚠️ **Async errors** — un callback d'erreur asynchrone arrive au
  caller probablement détruit. Pattern : pass `weak_ptr` au callback,
  check au callback.
- ⚠️ **Prepared statements cache eviction** — LRU par connexion. Une
  query rare peut éjecter une fréquente. Stats à instrumenter.
- ⚠️ **SQLStorage memory** — si `creature_template` a 100k entrées
  × 200 bytes = 20 MB. Multiplié par toutes les stores = potentiellement
  100s MB. À mesurer.
- ⚠️ **Mapping table → struct** — risque de désync si table change.
  Convention : test au boot qui valide les colonnes existent (`DESCRIBE
  table` + check).
- Pas de wire-breaking, pas de migration DB.

## 9. Recommandation finale

✅ **Faire en l'état** — **priorité haute** : c'est un **déblocant
amont** pour beaucoup de P2 downstream. Effort raisonnable, ROI direct
(perf) et indirect (debt prevention pour les tickets qui suivront) :

1. **Étape 1 (priorité haute)** : implémenter `SQLStorage<T>` +
   instancier sur 1-2 tables statiques (ex: factions, terrain
   archetypes). Mesurer gain perf vs accès DB direct.
2. **Étape 2** : implémenter `SqlPreparedStatement` + migrer 3-4
   queries hot path (auth login, character load) vers prepared.
   Mesurer gain.
3. **Étape 3** : implémenter `SqlDelayThread` + migrer audit log
   vers async (cas le moins critique pour rodage).
4. **Étape 4** : convention de mapping table → struct documentée +
   validation au boot.

À planifier **tôt** dans la roadmap P2 (avant CMANGOS.07/09/14/17 qui
en bénéficieront directement). Pas de risque architectural majeur.

---

*Audit du 2026-05-08. Mises à jour : —*
