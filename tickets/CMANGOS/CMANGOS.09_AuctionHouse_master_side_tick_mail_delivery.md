# CMANGOS.09_AuctionHouse_master_side_tick_mail_delivery

## Objectif

Mettre en place l'**hôtel des ventes** (HV) côté master LCDLLN, inspiré
de `src/game/AuctionHouse` cmangos. Master-side car cross-shard naturel
(acheteur et vendeur peuvent être sur des shards différents). Quatre
piliers :

1. **3 maisons séparées** (variantes faction/région) avec taux de
   commission différents — pattern « instance par faction/zone »
   adaptable.
2. **Update tick globale** balayant les enchères expirées (vs un timer
   par enchère) — scalable à 100k items.
3. **Livraison via système mail** (CMANGOS.18) plutôt que retour direct
   dans l'inventaire : découple le HV de la session du joueur (acheteur
   peut être offline).
4. **Index secondaire en RAM** (par item entry, par owner, par
   expiration) reconstruit au boot — recherches O(1) sans hit DB.

C'est un **P2 master**, à activer quand l'économie est en place
(crafting / loot / monnaie).

## Dépendances

- M00.1 (build base)
- CMANGOS.13 (Database — SQLStorage et async)
- CMANGOS.18 (Mails) — livraison
- Économie existante (item, gold) — pré-requis fonctionnel.

## Livrables

### Côté master (`engine/server/auction/`)

- `AuctionEntry.h` — struct enchère :
  ```cpp
  struct AuctionEntry {
    uint32_t auctionId;
    uint32_t houseId;
    ObjectGuid itemGuid;
    uint32_t ownerAccountId;
    uint64_t startBid;
    uint64_t buyout;
    uint64_t currentBid;
    uint32_t bidderAccountId;    // 0 = no bid
    int64_t  expirationTs;
    uint32_t deposit;
  };
  ```
- `AuctionHouseManager.{h,cpp}` :
  - `Load(ConnectionPool&)`
  - `uint32_t PostAuction(houseId, sellerAccountId, itemGuid, startBid, buyout, durationSec)`
  - `bool BidAuction(auctionId, bidderAccountId, amount)`
  - `void ResolveExpired(int64_t nowMs)` — appelé chaque tick
  - `std::vector<AuctionEntry> Search(houseId, filter)`
- `AuctionHouseHandler.{h,cpp}` — opcodes :
  - `kOpcodeAhPostAuction` (client → master)
  - `kOpcodeAhBidAuction`
  - `kOpcodeAhSearchAuctions`
  - `kOpcodeAhCancelAuction`
- `AuctionIndex.{h,cpp}` — index secondaire RAM :
  - `byItem` : `std::unordered_multimap<itemEntry, auctionId>`
  - `byOwner` : `std::unordered_multimap<accountId, auctionId>`
  - `byExpiration` : `std::set<expirationTs, auctionId>` (pour resolve périodique)

### Migration DB

```sql
CREATE TABLE auction_house (
  house_id        TINYINT UNSIGNED NOT NULL,
  name            VARCHAR(64) NOT NULL,
  commission_pct  TINYINT UNSIGNED NOT NULL DEFAULT 5,
  faction_filter  TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- 0 = neutre
  PRIMARY KEY (house_id)
);

CREATE TABLE auctions (
  auction_id        INT UNSIGNED NOT NULL AUTO_INCREMENT,
  house_id          TINYINT UNSIGNED NOT NULL,
  item_guid         BIGINT UNSIGNED NOT NULL,
  item_entry        INT UNSIGNED NOT NULL,
  owner_account_id  INT UNSIGNED NOT NULL,
  start_bid         BIGINT UNSIGNED NOT NULL,
  buyout            BIGINT UNSIGNED NOT NULL DEFAULT 0,
  current_bid       BIGINT UNSIGNED NOT NULL DEFAULT 0,
  bidder_account_id INT UNSIGNED NOT NULL DEFAULT 0,
  expiration_ts     BIGINT NOT NULL,
  deposit           BIGINT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (auction_id),
  KEY idx_house (house_id),
  KEY idx_owner (owner_account_id),
  KEY idx_expiration (expiration_ts)
);
```

### Configuration (`config.json`)

```json
"auction": {
  "expired_check_interval_sec": 60,
  "max_auctions_per_player": 50,
  "deposit_pct": 5,
  "commission_pct_default": 5,
  "min_duration_sec": 7200,
  "max_duration_sec": 172800
}
```

### Tests

- `AuctionHouseTests.cpp` — post + bid + resolve.
- `AuctionIndexTests.cpp` — index `byItem` retourne les enchères du même item.
- `AuctionExpirationTests.cpp` — `ResolveExpired` traite toutes les expirées.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Tick global d'expirations

Toutes les `expired_check_interval_sec`, parcourir `byExpiration` (set
trié) et résoudre les expirées :

- Pas de bidder → item retourné au seller via mail.
- Bidder → item livré au bidder, gold (current_bid - commission) au seller, deposit retourné, le tout via mails.

### 2. Livraison via mail

Pas d'écriture directe dans l'inventaire. Toujours via `MailManager::Send`
(CMANGOS.18). Bénéfices :
- Joueur offline : reçoit le mail au prochain login.
- Pas de lock cross-shard sur l'inventaire.
- Trace écrite : le mail est une preuve pour les disputes.

### 3. Multi-maisons

`house_id = 0` : maison neutre (commission haute, accessible à tous).
`house_id = 1` : maison faction A.
`house_id = 2` : maison faction B.

Chaque maison a sa table `auctions` filtrée par `house_id`. Joueur faction A
ne voit pas les auctions de la maison faction B (sauf neutre).

### 4. Search

```cpp
struct AuctionFilter {
  std::optional<uint32_t> itemEntryFilter;
  std::optional<std::string> nameLike;
  std::optional<uint32_t> minLevelFilter;
  std::optional<uint8_t> qualityFilter;
  uint32_t pageOffset = 0;
  uint32_t pageSize = 50;
};
```

Lookup via `byItem` index si itemEntry précis ; sinon scan filtré
(coût plus élevé, à mitiger via cache).

## Étapes d'implémentation

1. Créer `engine/server/auction/`.
2. Migrations DB.
3. Implémenter `AuctionEntry` + `AuctionIndex`.
4. Implémenter `AuctionHouseManager::Load` (depuis DB + reconstruct index).
5. Implémenter `PostAuction`, `BidAuction`, `CancelAuction`.
6. Implémenter `ResolveExpired` (tick périodique).
7. Câbler livraison via Mail (dépendance CMANGOS.18).
8. Implémenter `Search` avec filtres.
9. Allouer opcodes + handler.
10. Tests : 3 fichiers.
11. Doc : section « Auction master » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : post auction → bid → buyout → item livré au bidder, gold au seller
- [ ] Smoke test expiration : auction sans bidder expire → item retourne au seller via mail
- [ ] Search par itemEntry est O(1) (vérifier via timing)
- [ ] Migrations idempotentes
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Locks** : `BidAuction` est sensible à la concurrence (2 joueurs bid en même temps). Lock par `auction_id` (mutex map ou row lock DB).
- **Index reconstruction** : au boot, `Load` itère 100k+ rows + reconstruit l'index. Mesurer le temps. Si > 30s, lazy load.
- **Snipe** : un bid dans les dernières 30s prolonge l'expiration de 30s (anti-snipe) ? Convention WoW classique : oui. À implémenter ou non selon vision LCDLLN.
- **Deposit refund** : si seller cancel avant expiration, deposit perdu (pénalité anti-spam). Si cancel après bid, **interdit** (déjà engagé).
- **Item delete protection** : un item en auction ne doit **pas** être supprimable de l'inventaire. Marqueur `is_in_auction` ou détacher l'item de l'inventaire dès le post.
- **Double-spending** : entre `PostAuction` et le commit DB, l'item ne doit pas être tradeable. Lock côté master.
- **Search performance** : si pas de cache, chaque search = scan 100k auctions. Indexer côté DB (`KEY idx_item_entry`) + cache RAM.
- **Mail chain** : un winner reçoit 1 mail (item) ; un loser bidder reçoit 1 mail (refund de son bid) ; le seller reçoit 1 mail (gold). 3 mails par auction résolue. À 100k expirations/tick, ça fait 300k mails — batcher via `MassMailMgr` (CMANGOS.18).

## Références

- `CMANGOS_ANALYSIS.md` § AuctionHouse (P2 master)
- cmangos `src/game/AuctionHouse/AuctionHouseMgr.cpp`,
  `AuctionHouseHandler.cpp`, `AuctionHouseBot/` (à ignorer pour P4)
