# SERVER-CORE.44_AuctionHouseBot_internal_client_low_pop

## Objectif

Mettre en place un **bot interne** alimentant l'hôtel des ventes en
items/enchères pour simuler une économie active sur serveur low-pop.
Inspiré de `src/game/AuctionHouseBot` server-core.

**À activer uniquement si la pop tombe trop bas** pour avoir un HV
organique. Sinon ignorer.

C'est un **P4 master**.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.09 (AuctionHouse)
- SERVER-CORE.13 (Database)

## Livrables

### Côté master (`engine/server/auction/bot/`)

- `AuctionHouseBot.{h,cpp}` :
  - `Run()` — démarré au boot si activé. Tick périodique :
    - Maintenir N enchères actives par HV, par catégorie d'item.
    - Si trop peu : poster de nouvelles enchères avec items aléatoires
      (tirés selon rareté config) et prix simulés.
    - Périodiquement, "acheter" des enchères organiques (prix
      raisonnable) pour donner l'illusion de transactions.
- `AuctionBotConfig.{h,cpp}` — config externe :
  ```
  [auction_house_bot]
  enabled = false
  target_active_auctions_per_house = 100
  category_weights_armor = 25
  category_weights_weapon = 20
  ...
  ```

### Configuration (`config.json`)

```json
"auction_bot": {
  "enabled": false,
  "target_active_auctions_per_house": 100,
  "post_interval_min": 30,
  "buy_interval_min": 60,
  "max_post_per_tick": 5,
  "price_variance_pct": 20
}
```

### Tests

- `AuctionBotTests.cpp` — start, génération d'enchères, achat simulé.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Bot = client interne via opcodes

**Pas** d'écriture DB directe. Le bot envoie les **mêmes opcodes** que
les vrais joueurs (`kOpcodeAhPostAuction`, `kOpcodeAhBidAuction`). Toute
correction de bug HV s'applique aussi au bot, et la prod stress-test
le code en permanence.

### 2. Tirage pondéré par rareté

```
quality   weight    price_min    price_max
common    50        1g           5g
uncommon  30        5g           20g
rare      15        20g          200g
epic      4         200g         5000g
legendary 1         5000g        100000g
```

Tirage selon `weight`. Génération d'un prix aléatoire dans `[min, max]`.

### 3. Désactivation par défaut

`enabled = false`. À activer manuellement seulement.

## Étapes d'implémentation

1. (Décision GO) Créer `engine/server/auction/bot/`.
2. Implémenter `AuctionHouseBot`.
3. Câbler avec `AuctionHouseManager` (SERVER-CORE.09).
4. Tests.
5. Doc.

## Definition of Done (DoD)

- [ ] Build Linux OK (master)
- [ ] Tests passent
- [ ] Smoke test : `enabled=true` → 100 enchères actives apparaissent en HV en quelques minutes
- [ ] Désactivation via config = no-op
- [ ] Aucun dossier racine non autorisé
- [ ] Rapport final

## Notes / pièges à éviter

- **Économie inflatée** : si bot post sans cesse, prix montent. Calibrer pour qu'il **achète** aussi (vide les enchères vieilles) pour stabiliser.
- **Items uniques** : ne pas générer 100× le même item legendary, ça casse la rareté perçue. Anti-spam par item entry.
- **Bot detection** : un joueur avancé peut détecter les patterns (toujours même posteur via `auction.owner`). Soit créer plusieurs comptes "bots", soit accepter la transparence.
- **Décision activation** : ne pas activer en alpha/beta. Production avec pop < 50 joueurs en ligne après 1 mois → considérer.

## Références

- `SERVER-CORE_ANALYSIS.md` § AuctionHouseBot (P4 master)
- server-core `src/game/AuctionHouseBot/AuctionHouseBot.cpp`
