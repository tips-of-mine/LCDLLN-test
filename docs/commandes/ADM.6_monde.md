# ADM.6 — Monde : créatures, réapparitions, carte

Aide en jeu : `/help world`. Rôle : **maître de jeu**.

| Commande | Alias | Effet |
|---|---|---|
| `/npc add <archétype\|n°> [1-25]` | `/creature` | fait apparaître des mobs devant soi (en cercle si plusieurs) |
| `/npc delete` | | supprime le mob ciblé (mob de spawner : réapparaît au délai du spawner) |
| `/npc move` | | amène le mob ciblé à sa position (nouveau poste) |
| `/npc info` | | fiche du mob ciblé (= `/target`) |
| `/npcsearch <mot\|n°>` | `/lookupnpc` | archétypes : numéro, nom, niveau |
| `/cleanup [rayon]` | | retire les mobs apparus par un MJ (tous, ou dans le rayon) |
| `/respawn [rayon]` | | avance la réapparition des spawners proches (100 m par défaut) |
| `/revealmap [all] [joueur]` | | révèle la carte à découvrir : cellule courante ou monde entier |
| `/setweather …` | | **prévue** (météo pas encore simulée par le shard) |
| `/object …` | | **prévue** (objets interactifs pas encore gérés) |

## Mobs apparus par un maître de jeu

`ServerApp::SpawnGmMob` crée un mob **hors spawner** (`MobEntity::gmSpawned`,
indice de spawner invalide : ni routine ni agenda). Il a les statistiques de son
archétype, son point d'apparition comme poste (laisse de 40 m), donne butin et
expérience comme les autres. À sa mort il est retiré (`PurgeDeadGmMobs`, à chaque
tick) sans réapparaître. `ServerApp::RemoveMobByGm` retire un mob quelle que soit
son origine : spawner (réapparition programmée), événement dynamique, ou MJ.

## Carte à découvrir

`MapDiscoveryTracker::RevealWholeCell` marque les 25 600 cases d'une cellule de
10 km ; la sauvegarde du personnage les fusionne en base (`character_map_discovery`),
le client les voit à la prochaine ouverture de la carte. « Cacher » la carte n'est
pas proposé : la fusion en base est un OU (ce qui est découvert le reste).

## Fichiers

| Fichier | Rôle |
|---|---|
| `src/shared/server_bootstrap/ServerAppGmWorld.cpp` | 5 commandes |
| `src/shared/server_bootstrap/ServerApp.cpp` | `SpawnGmMob`, `RemoveMobByGm`, `PurgeDeadGmMobs` |
| `src/shardd/world/MapDiscoveryTracker.{h,cpp}` | `RevealWholeCell` (+ test) |
| `src/shardd/gameplay/creature/CreatureArchetypeLibrary.h` | `All()` |

## Déploiement

**Master + shard**. Client inchangé, pas de migration.
