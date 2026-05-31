# Design — Présence en ligne enrichie (login, perso, niveau, zone)

Date : 2026-05-31
Branche serveur : `feat/online-presence-enriched`

## Contexte

La route master `GET /online-accounts` (HealthEndpoint) renvoie aujourd'hui :
```json
{ "authenticated": [accountId, ...], "inWorld": [accountId, ...] }
```
On veut **enrichir** chaque joueur en ligne avec : login (utilisateur), nom du
personnage, niveau, et zone in-game (région). Affichage : endpoint **et** UI
admin (Gestion des joueurs).

Disponibilité des données :
- `login` : MySQL `accounts.login` (master a le pool DB).
- `character` (nom) : `SessionCharacterMap` côté master (déjà peuplé à l'EnterWorld).
- `level`, `zoneId` : **live côté shard** (`Unit::GetLevel()`, `WorldObject::GetZoneId()`),
  PAS dans le master. Le heartbeat shard→master ne transporte que
  `(shard_id, current_load, timestamp)`.

Décisions (utilisateur) : zone = **zoneId in-game réel** ; transport = **enrichir
le heartbeat existant** ; affichage = **endpoint + UI admin**. Le niveau et la
zone viennent du **live shard** (autoritatif), pas de la DB (évite valeur périmée).

## Architecture / flux

```
shard (live: account/char/level/zone)
   │  heartbeat enrichi (périodique)
   ▼
master  ──► ShardPlayerPresenceCache (accountId → {characterId, level, zoneId, shardId, updatedAt})
   │
   │  GET /online-accounts : jointure
   │    login   ← MySQL accounts (lookup par accountId)
   │    character← SessionCharacterMap
   │    level/zoneId ← ShardPlayerPresenceCache
   ▼
portail (server-side fetch) ──► Admin > Gestion des joueurs (sous le nom + tooltip pastille)
```

## 1. Protocole (wire-breaking → bump `kProtocolVersion` 8 → 9)

`src/shared/network/ServerProtocol.h` : `kProtocolVersion = 9`.

`src/shared/network/ShardPayloads.{h,cpp}` — heartbeat enrichi (champs en queue,
parsing tolérant si absents pour robustesse, mais version bump impose lock-step) :
- `struct ShardPlayerPresence { uint64_t accountId; uint64_t characterId; uint32_t level; uint32_t zoneId; };`
- `ShardHeartbeatPayload` : ajouter `std::vector<ShardPlayerPresence> players;`.
- Wire après les 16 octets fixes : `u16 playerCount`, puis `playerCount` × `{u64 accountId, u64 characterId, u32 level, u32 zoneId}` (24 octets/joueur).
- `BuildShardHeartbeatPayload(shard_id, current_load, timestamp, const std::vector<ShardPlayerPresence>& players = {})`.
- `ParseShardHeartbeatPayload` : lit le tableau si `Remaining() > 0`.
- Tests `ShardPayloadsTests` : round-trip avec 0 / N joueurs.

## 2. Shard — collecte et envoi

`src/shared/network/ShardToMasterClient.{h,cpp}` :
- `SetCurrentLoad` existe déjà ; ajouter un fournisseur de présence injecté :
  `SetPlayerPresenceProvider(std::function<std::vector<ShardPlayerPresence>()>)`.
- `SendHeartbeat()` appelle le provider (si câblé) et passe la liste à `BuildShardHeartbeatPayload`.

`src/shardd/main_linux.cpp` (ou le composant monde) : câbler le provider pour
itérer les joueurs connectés et produire `{accountId, characterId, level, zoneId}`
depuis l'AdmittedCharacterRegistry (account/char) + l'entité monde du joueur
(`GetLevel()`, `GetZoneId()`). Doit s'exécuter sur le thread monde (snapshot cohérent).

## 3. Master — cache + endpoint

Nouveau `src/masterd/shards/ShardPlayerPresenceCache.{h,cpp}` (thread-safe) :
- `Update(uint32_t shardId, const std::vector<ShardPlayerPresence>&)` : remplace
  l'ensemble des entrées de CE shard (clé accountId).
- `Clear(uint32_t shardId)` : purge les entrées d'un shard (appelé au shard-down / evict).
- `std::optional<Entry> Get(uint64_t accountId)` et `Snapshot()`.

`ShardRegisterHandler::HandleHeartbeat` : après `UpdateHeartbeat`, appelle
`presenceCache.Update(shard_id, parsed->players)`. Le callback shard-down
(`SetShardDownCallback`) appelle `presenceCache.Clear(shard_id)`.

`main_linux.cpp` `onlineAccountsProvider` enrichi → JSON :
```json
{
  "authenticated":[1], "inWorld":[1],
  "players":[
    {"accountId":1,"login":"thedjinhn","character":"homme","level":7,"zoneId":42,"inWorld":true}
  ]
}
```
Construction des `players` :
- Pour chaque accountId in-world : `login` (MySQL), `character` (SessionCharacterMap),
  `level`/`zoneId` (presenceCache).
- Pour chaque accountId authentifié non in-world : `login` seul, `character`=null,
  `level`/`zoneId` absents, `inWorld`:false.
- `login` : un seul `SELECT id, login FROM accounts WHERE id IN (...)` (batch),
  échappé via la fonction `escapeJson` déjà présente.

Note : `zoneId` part en numérique ; le **nom de région** est résolu côté portail
(tableau maintenable), pas sur le wire — minimise le payload et garde la
connaissance "présentation" côté portail.

## 4. Portail — consommation + UI

`web-portal/lib/serverStatus.ts` :
- `OnlineAccounts` gagne `players: Map<number, OnlinePlayer>` où
  `OnlinePlayer = { login?: string; character?: string|null; level?: number|null; zoneId?: number|null; inWorld: boolean }`.
- Parse défensif du tableau `players` (rétro-compat : si absent, map vide ;
  les sets `authenticated`/`inWorld` continuent d'alimenter la pastille).

`web-portal/lib/zones.ts` (nouveau) : `regionName(zoneId): string` via table
maintenable, fallback `"Zone {id}"`.

`web-portal/app/admin/players/page.tsx` :
- Sous le nom d'un joueur en ligne : ligne discrète `perso • niveau N • région`
  (uniquement si in-world ; sinon rien de plus que la pastille).
- `title` de la pastille enrichi (déjà présent) avec ces infos.

## Découpage PR

- **PR A — serveur** (`feat/online-presence-enriched`) : protocole (v9) + shard
  (collecte/envoi) + master (cache + endpoint). Lock-step **master + shard**.
- **PR B — portail** : `serverStatus` + `zones.ts` + UI admin. Client only.
  Dégrade gracieusement si le master ne renvoie pas encore `players`.

## Tests

- `ShardPayloadsTests` : round-trip heartbeat 0 / N joueurs (build↔parse).
- `ShardPlayerPresenceCacheTests` : Update remplace par shard, Clear purge, Get.
- Endpoint : vérif manuelle `curl /online-accounts` (JSON `players`).
- Portail : pas de framework JS ; `regionName` et le parse restent purs/typés.

## Déploiement

⚠️ **Redéploiement master ET shard en lock-step** (bump `kProtocolVersion` →
incompatibilité client/serveur ancien). PR portail = client only.

## Non-objectifs (YAGNI)

- Pas de position x/y/z exposée (seulement la zone).
- Pas d'historique de présence / métriques temporelles.
- Pas de rafraîchissement temps réel (toujours au chargement de page, cf. choix précédent).
- Pas de persistance DB de la présence (tout en mémoire master, alimenté par le shard).
