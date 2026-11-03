# ADM.2 — Commandes d'information

Aide en jeu : `/help information`.

| Commande | Alias | Rôle | Exécutée par | Effet |
|---|---|---|---|---|
| `/pos` | `/gps`, `/coords` | joueur | shard | sa position (x, y, z en m), orientation (0° = nord), cellule, zone, région, lieu proche |
| `/pos <joueur>` | | maître de jeu | shard | position d'un autre joueur (refus « Denied » pour un joueur) |
| `/where` | | joueur | shard | lieu connu le plus proche, distance et direction, région |
| `/online [page]` | `/players` | joueur | master | personnages en jeu, 30 noms par page |
| `/ping` | | joueur | master | « Pong ! » (vérifie connexion et chat) |
| `/serverstats` | `/stats` | modérateur | master | connexions, joueurs par rôle, shards (état, charge, saturation), commandes MJ en attente |
| `/info <joueur>` | `/pinfo` | maître de jeu | master + shard | compte (login, rôle, shard) puis fiche de jeu |
| `/target` | | maître de jeu | shard | fiche de la dernière cible (mob : archétype, niveau, IA, vie, spawner ; joueur : comme /info) |
| `/zone` | | maître de jeu | shard | joueurs, mobs (dont en combat), spawners actifs de la zone |
| `/nearby [rayon]` | | maître de jeu | shard | joueurs, mobs, sacs de butin à moins de N m (50 par défaut, 1000 max), 20 lignes |

## Repères

- Coordonnées **globales** en mètres : +X = est, +Z = sud (même repère que la
  carte du monde et `/tp`).
- Orientation : 0° = nord, sens horaire ; nom du point cardinal en clair.
- Région et lieu le plus proche : annuaire des lieux d'ADM.1 (`places.json` +
  villes de départ). `GmPlaceDirectory` gagne `RegionAt` (plus petite région
  contenant le point) et `Nearest` (ville, départ ou lieu remarquable le plus
  proche, régions ignorées).

## `/info` en deux temps

Le master remplace le relais générique de `/info` : il cherche le personnage en
jeu (nom sans casse), affiche la ligne de compte (login, rôle, shard), puis
relaie la commande **au shard du joueur visé** (pas forcément celui de
l'appelant), qui renvoie la fiche de jeu : niveau, XP, classe, faction, race,
genre, vie, ressource, état (mort), or, piles au sac, position, cellule, zone,
guilde, pouvoirs MJ actifs (vitesse, vol, gel…).

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/shared/server_bootstrap/ServerAppGmInfo.cpp` | /pos, /where, /info (partie shard), /target, /zone, /nearby |
| `src/masterd/admin/MasterGmCommands.cpp` (`RegisterInformation`) | /online, /ping, /serverstats, /info (partie master) |
| `src/shardd/gameplay/gm/GmPlaceDirectory.{h,cpp}` | `RegionAt`, `Nearest` |

## Tests

`gm_place_directory_tests` (région contenant un point, lieu le plus proche),
`command_help_tests` (cohérence du registre).

## Déploiement

**Master + shard** en lock-step (nouvelles commandes relayées et exécutées).
Client inchangé, pas de migration.
