# Design — Service de présence unifié (hybride 2 niveaux)

Date : 2026-05-31
Branche : `feat/presence-service-consolidation`

## Problème

La présence « joueur en ligne (+ où) » est aujourd'hui dupliquée dans **plusieurs
stockages indépendants qui peuvent diverger** :

| Stockage | Côté | Contenu | Consommateur |
|---|---|---|---|
| `FriendSystem.m_presence` | shard | online/offline + nom | liste d'amis, notifs |
| `GuildSystem.m_onlinePlayers` | shard | set d'ids en ligne | routage guild-chat |
| `SessionManager` | master | sessions authentifiées | auth |
| `SessionCharacterMap` | master | perso en jeu par conn | chat, /status |
| `ShardPlayerPresenceCache` | master | online + zone + niveau | web-portal |

Objectif (demande utilisateur) : **une seule autorité de présence**. Un service
dit si un joueur est en ligne et, si oui, donne l'info (zone, niveau…). Les
systèmes amis / groupes / guildes / raids **s'y réfèrent** au lieu de tenir
chacun leur propre comptabilité.

## Décision : hybride à 2 niveaux

Une seule **source par niveau**, avec une synchro claire (le shard pousse, le
master agrège — c'est déjà le mécanisme du heartbeat enrichi).

### Niveau 1 — `ShardPresenceService` (shard, NOUVEAU)
Autorité **locale** des joueurs connectés à CE shard. Sert les chemins chauds
(routage guild-chat, notifs amis même-shard) sans aller-retour réseau.

- Enregistrement : `{ accountId, characterId, characterName, level, zoneId, status }`
  (`status` ∈ Online/Away/Busy, cf. `PresenceStatus`).
- API : `SetOnline(...)`, `SetOffline(accountId)`, `UpdateZone(accountId, zoneId)`,
  `UpdateLevel(accountId, level)`, `Get(accountId)`, `IsOnline(accountId)`,
  `Snapshot()`, `OnlineAccountIdsAmong(const std::vector<uint64_t>&)`.
- Propriété : `ServerApp`. Alimenté aux hooks login (`HandleHello`) / logout
  (`HandleGoodbye` / éviction) et sur changement de zone/niveau.
- C'est aussi LUI qui produit le snapshot envoyé au master via le heartbeat
  (remplace l'itération ad hoc de `m_clients` introduite côté #770).

### Niveau 2 — `GlobalPresenceService` (master) = `ShardPlayerPresenceCache` (existant, renommé/élargi)
Autorité **globale** : agrège la présence de **tous** les shards via les
heartbeats enrichis (PR #770). Seule source pour « le joueur X est-il en ligne
quelque part, et où (shard, zone, niveau) ». Cross-shard natif.

- Entrée : `{ accountId, characterId, level, zoneId, shardId, lastUpdate }`.
- Alimenté par `HandleHeartbeat` (déjà fait), purgé au shard-down (déjà fait).
- Consommateurs : web-portal (`/online-accounts`, déjà fait) ; futurs handlers
  master cross-shard (amis/guilde/raid répartis sur plusieurs shards).

### Synchro Niveau 1 → Niveau 2
`ShardPresenceService.Snapshot()` → heartbeat (`ShardPlayerPresence[]`, PR #770)
→ `GlobalPresenceService.Update(shardId, …)`. Aucun nouveau canal : on formalise
ce que #770 a déjà câblé.

## Qui consulte quoi

| Consommateur | Question | Niveau |
|---|---|---|
| Routage guild-chat (même shard, hot) | qui de la guilde est connecté ici ? | 1 (local) |
| Liste d'amis « en ligne ? » (même shard) | mon ami connecté ici ? | 1 (local) |
| Liste d'amis / guilde **cross-shard** | mon ami sur un AUTRE shard ? où ? | 2 (master) |
| Web-portal | tous les joueurs en ligne + où | 2 (master) |
| Futur : groupe / raid « où sont les membres » | localisation des membres | 1 si même shard, sinon 2 |

Tant qu'un seul shard tourne, le Niveau 1 répond à tout ; le Niveau 2 reste
indispensable pour le portail et pour le jour où il y aura plusieurs shards.

## Migration (incrémentale, sans big-bang)

- **Phase 1 — Amis (cette itération)** : introduire `ShardPresenceService` ;
  faire pointer `FriendSystem` dessus ; **supprimer `FriendSystem.m_presence`**
  (et `SetPresence/SetOffline/GetPresence` deviennent des délégations ou sont
  retirés au profit du service). Comportement fonctionnel identique (même-shard).
- **Phase 2 — Guilde** : retirer `GuildSystem.m_onlinePlayers` ; le routage
  guild-chat interroge `ShardPresenceService`.
- **Phase 3 — Cross-shard + nouveaux consommateurs** : handler master exposant
  « où est le compte X » à partir du Niveau 2 ; Party/Raid (à venir) consomment
  le même contrat. Décommissionner les reliquats.

Chaque phase = sa propre PR, testable et déployable seule (lock-step master+shard
seulement si la phase touche le wire ; Phase 1 est shard-only).

## Relation avec les PR en cours

- **#770 (serveur, heartbeat v9 + cache master)** : devient officiellement la
  **synchro Niveau 1 → Niveau 2** et le **Niveau 2**. À conserver/merger.
- **#771 (portail)** : consommateur du Niveau 2. À conserver.
- Ce design **ne remet pas en cause** #770/#771 ; il les cadre et enchaîne la
  consolidation des stockages shard (Friend/Guild) derrière le Niveau 1.

## Composants & frontières (clarté/isolation)

- `ShardPresenceService` : un fichier, une responsabilité (présence locale).
  Testable isolément (set/clear/get/zone/level, OnlineAccountIdsAmong).
- `GlobalPresenceService` : déjà isolé (`ShardPlayerPresenceCache`), thread-safe.
- `FriendSystem` / `GuildSystem` : perdent la responsabilité « présence » et
  gagnent une **dépendance** explicite vers `ShardPresenceService` (pointeur
  injecté par ServerApp, comme `AdmittedCharacterRegistry`).

## Tests

- `ShardPresenceServiceTests` : online/offline, update zone/niveau, IsOnline,
  OnlineAccountIdsAmong, snapshot.
- `FriendSystem` (Phase 1) : les requêtes de présence reflètent le service
  (pas de map interne). Adapter les tests existants si présents.
- Le round-trip heartbeat reste couvert par `shard_payloads_tests` (#770).

## Non-objectifs (YAGNI)

- Pas de persistance DB de la présence (volatile, reconstruite à la connexion).
- Pas de statuts riches au-delà de `PresenceStatus` existant (Online/Away/Busy).
- Phase 1 ne touche PAS le wire (shard-only) ; pas de bump protocole.
- Pas de refonte du chat ni des opcodes amis/guilde dans ce design — seulement
  la **source de présence** qu'ils consultent.
