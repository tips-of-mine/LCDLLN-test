# CMANGOS.09 — AuctionHouse (master-side / tick / mail delivery)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.09_AuctionHouse_master_side_tick_mail_delivery.md](../../../../tickets/CMANGOS/CMANGOS.09_AuctionHouse_master_side_tick_mail_delivery.md)
> **Priorité** : P2 — gameplay essentiel
> **Cible** : master

## 1. Statut implémentation

🟡 **Partiel** — un `AuctionHouseService` shard-side existe avec listings,
bids, buyout, expiration et settlement. Mais l'architecture est **shard
(M35.4)** au lieu de **master (cmangos)**, mono-maison au lieu de
multi-maisons, et l'index RAM secondaire est absent (recherches DB-bound).

## 2. Preuves dans le code

**Existant :**
- [engine/server/AuctionHouse.h](../../../../engine/server/AuctionHouse.h) (60+ lignes) — `AuctionListingRecord`,
  `AuctionSettlement`, `AuctionHouseService` (single house, fee 5%)
- [engine/server/AuctionHouse.cpp](../../../../engine/server/AuctionHouse.cpp) (382 lignes) — implémentation
- [db/migrations/0013_auction_listings.sql](../../../../db/migrations/0013_auction_listings.sql) — schéma listings
- [tickets/M35/M35.4_Auction_house_listings_+_bidding_+_buyout.md](../../../../tickets/M35/M35.4_Auction_house_listings_+_bidding_+_buyout.md) — milestone source
- Persistance via fichier INI (`server.auction_listings_path`) commenté
  comme fallback "M35.4"

**Manquant (vs spec ticket) :**
- ❌ **Architecture master-side** — actuellement shard. Cross-shard
  natural (acheteur+vendeur sur shards différents) impossible.
- ❌ **Multi-maisons** (3 houses séparées avec `commission_pct` /
  `faction_filter` distincts) — actuellement 1 seule maison
  (`kAuctionHouseFeePercent = 5%` hardcodé)
- ❌ **AuctionIndex RAM** secondaire (`byItem`, `byOwner`, `byExpiration`)
  reconstruit au boot pour recherches O(1)
- ❌ **Tick global expirations** balayant 100k items (vs timer par
  enchère). Le code actuel utilise `expiresAtTick` mais il faut vérifier
  que le balayage est efficace.
- ❌ **Livraison via Mails** (CMANGOS.18) — actuellement settlement direct
  sur la session connectée + INI mailbox de fallback
- ❌ Migration DB pour `auction_house` table (config maisons)
- ❌ Opcodes master-side (`kOpcodeAhPostAuction`, `kOpcodeAhBidAuction`,
  `kOpcodeAhSearchAuctions`, `kOpcodeAhCancelAuction`)
- ❌ Config `auction.expired_check_interval_sec`, multi-house params

## 3. Recouvrement milestones existantes

✅ **Couvert** — M35.4 livre une version v1 fonctionnelle. CMANGOS.09 va
plus loin (multi-maisons, master-side, index RAM, livraison mail).

## 4. Écart par rapport à la spec CMANGOS

L'écart **fonctionnel** est modéré (les opérations CRUD existent), mais
l'écart **architectural** est important :

1. **Shard → master** : déplacer le service côté master pour permettre
   cross-shard. Cela change qui héberge les données et qui répond aux
   opcodes. Wire-breaking si les opcodes changent de cible.
2. **Mono → multi-maisons** : étendre `AuctionListingRecord` avec
   `houseId`, créer table `auction_house`, propager dans tous les
   handlers/queries.
3. **Index RAM** : ajouter `AuctionIndex` au-dessus de la persistance
   pour recherches O(1).
4. **Livraison mail** : refonte de `AuctionSettlement` pour envoyer mail
   au lieu d'un settlement direct.

Décision préalable : **garder shard-side V1 (M35.4 deployed)** ou
**migrer master-side V2** ? Choix architectural avec impact plusieurs PR.

## 5. Effort estimé

**L** (1 sprint complet) si migration master-side. **M** si on étend
seulement shard-side avec multi-maisons + index + livraison mail
(option pragmatique).

## 6. Valeur joueur/serveur

**Élevée** — l'économie joueur est centrale dans un MMO. Une V2 robuste
(multi-maisons, index, livraison mail) débloque plus de content design
(taxes différenciées par faction, événements de saison sur HV).

Mais **pas critique court-terme** : la V1 M35.4 fonctionne pour les
besoins immédiats. Migrer maintenant = optimisation prématurée tant que
le serveur tient son load.

## 7. Dépendances bloquantes

Le ticket dépend explicitement de :
- **CMANGOS.13 Database** — `SQLStorage` cache RAM + async pour le tick
  global expirations
- **CMANGOS.18 Mails** — bloquant pour la livraison mail. Si pas livré,
  on garde le settlement direct (V1).

Économie pré-existante (item, gold) déjà en place côté LCDLLN
(`db/migrations/0012_player_wallet_currency.sql`,
`db/migrations/0019_player_professions.sql`).

## 8. Risque / piège ⚠️

- ⚠️ **Migration DB** — table `auction_house` à créer ; `auctions`
  (existante) à étendre avec `house_id`, `item_entry`, `deposit`. Idempotent.
- ⚠️ **Wire-breaking** — si on déplace les opcodes shard → master, les
  clients existants cassent. Bump `kProtocolVersion` + lock-step.
- ⚠️ **Redéploiement** — soit shard (extension), soit master+shard
  (migration). Selon le choix.
- ⚠️ **Tick global** — balayage 100k items chaque 60s. Bench requis
  (`std::set<expirationTs>` est O(log N) par insertion mais O(K) pour
  pop K éléments expirés — OK en théorie).
- ⚠️ **Index RAM cohérence** — reconstruction au boot + maintien à
  chaque insertion/suppression. Bug possible si transaction DB échoue
  après update RAM (incohérence). Pattern : DB d'abord, RAM ensuite,
  rollback RAM si DB échoue.
- ⚠️ **Cross-shard si master-side** — vendeur sur shard A, acheteur
  sur shard B, item à teleporter cross-shard via mail. Coordination
  master-shard à designer.
- ⚠️ **Anti-fraude** — déposit (deposit fee), limite par compte,
  auto-buyout instantané (joueur achète sa propre enchère). Patterns
  classiques à prévoir.

## 9. Recommandation finale

🔧 **Adapter et faire** (V2), **mais reporter** tant que la V1 M35.4
tient sa charge :

1. **Décision préalable** : valider l'option architecturale
   - Option A (pragmatique) : étendre shard-side avec multi-houses +
     index + livraison mail. **Effort M, peu de risque, peu de gain
     cross-shard.**
   - Option B (cmangos pure) : migrer master-side. **Effort L,
     wire-breaking, gain cross-shard.**

   → **Recommandation Option A** pour V2 incrémentale, B reportée à plus
   tard si besoin cross-shard avéré.

2. **Étape 1** : ajouter table `auction_house` + colonnes `house_id`/`deposit`
   sur `auctions` (migration idempotente).
3. **Étape 2** : ajouter `AuctionIndex` RAM (3 maps) avec test cohérence
   reload.
4. **Étape 3** : tick global expirations (bench 10k+ entries).
5. **Étape 4** : intégration livraison mail (après CMANGOS.18 livré).

⏸ **Reporter** jusqu'à ce que CMANGOS.18 (Mails) soit livré ou que la
V1 montre des limites (perf, manque cross-shard, manque taxes
différenciées par faction).

---

*Audit du 2026-05-08. Mises à jour : —*
