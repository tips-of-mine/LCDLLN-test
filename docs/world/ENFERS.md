# Les Enfers — seconde carte jouable

« Les Enfers » est une **vraie seconde carte**, séparée du continent des Royaumes
(`docs/world/ROYAUMES.md`) : son propre repère global, ses propres cellules, sa
propre grille d'intérêt. Ce n'est pas un groupe de cellules réservées dans la
grille du continent.

## 1. La carte

| Point | Valeur |
|---|---|
| Source | `game/data/world/enfers/world_master.json` (`world_id` `enfers`) |
| Cellules | **9 cellules** de 10 km en carré 3 × 3 (v1.0.71) : la croix `N000_E000` (centre), `N001_E000`, `N000_E001`, `S001_E000`, `N000_W001` et les 4 coins `N001_W001`, `N001_E001`, `S001_W001`, `S001_E001` |
| Biome | croix `V` (volcanic_area) ; coins `M` (mountain_range), générés ; couronne extérieure `M` (N002/S002, W002/E002) non générée (mur invisible) dans la grille 5 × 5 |
| Niveaux | zone de départ de la Légion infernale, **niveau 1 à 10** ; seuls les boss et les abords des portails peuvent être plus élevés |
| Dossiers | `zones/enfers_nXXX_eYYY/`, `instances/zone_enfers_*/`, `world/enfers/` |
| Identifiants | zone ids `2 000 000 + (n+500)·1000 + (e+500)` ; carte `kEnfersMapId` = 999 998 |
| Code | `src/shared/world/WorldCells.h` : `CellMap::Enfers`, `CellDirName(c, map)`, `CellZoneId(c, map)`, `CellMapOfZoneId`, `MapCell` |

La carte d'un joueur se déduit de sa **zone** (`character_progress.zone_id`) et
jamais de sa position (les deux repères se recouvrent : (0, 0) existe sur les
deux cartes). Aucune migration de base : un nouveau personnage sans zone est
sur la carte du départ de sa race.

### Région de départ (v1.0.71)

**Décisions du propriétaire (09/11/2026)** : les 4 coins sont ouverts (région de départ 3 × 3 de la
Légion = les 9 cellules) ; les Enfers sont une zone de niveau 1 à 10 (boss et abords des portails
exceptés) ; pas de hameau, la Citadelle reste la seule ville. Les coins gardent le code `M` :
`tools/world_gen` génère les cellules `M` comme sur le continent (S008_E009 à S008_E011) ; pentes
médianes ≈ 20° contre ≈ 15° sur la croix, 91 % du sol sous 35°. Leurs mines procédurales sont tenues
par la Légion (race `demons`, `Places.cpp`), pas par des nains. POI `[GD]` (genres existants) :

| Case | Biome | POI |
|---|---|---|
| N001_W001 | `M` | Ruines du Bastion des Damnés (`ruine`) |
| N001_E001 | `M` | Camp d'épreuve des Recrues (`camp`) |
| S001_W001 | `M` | Autel des Pactes (`sanctuaire`) |
| S001_E001 | `M` | Faille des Cohortes (`faille`) |
| S001_E000 | `V` | Poste de garde du Portail I (`camp`), à 400 m à l'est du portail |

Aucune route : une seule ville, donc rien à relier pour le générateur, et le réseau routier du lore
(`routes`) ne couvre pas les Enfers. Faune procédurale de la carte (`Population.cpp`, `CreatureFor`) :
diablotins errants (150) près de la Citadelle, chiens des cendres (151) ensuite, ogres (161, niveau 10)
au plus loin ; aucun élémentaire de braise (164, niveau 12). Lore et faune : [`LORE_BD.md`](LORE_BD.md) §15.8.

## 2. La Citadelle de la Légion

Au centre (`citadelle_legion`, kind `camp`, race `demons`, `start_for` `["legion"]` : faction de jeu
« Légion infernale », v1.0.61, lore `faction_legion_infernale`) :

- les **démons naissent ici** (faction `legion`, repli race `demons` ; `world/enfers/start_locations.json`, fusionné par
  `StartLocationTable::LoadForWorld` côté master et shard) ;
- **Maltheus** (`demon_heraut`) et **Ysviel** (`demon_ysviel`) y ont été déplacés
  depuis l'ancien camp de Tsaroth (retiré du continent) ;
- un **cimetière neutre** (faction `-`, toutes factions) est généré au centre
  (respawn : `SameMap`, seuls les joueurs des Enfers l'utilisent).

## 3. Les portails

Les 5 Portes des enfers du continent (`poi_porte_1..5`) sont reliées une à une
aux 5 portails des Enfers (champ `link` des POI, `"<monde>:<id>"`) :

| Continent | Cellule | Enfers | Cellule |
|---|---|---|---|
| Porte des enfers I | `S013_E000` | Portail I | `S001_E000` |
| Porte des enfers II | `N008_E007` | Portail II | `N001_E000` |
| Porte des enfers III | `S005_W011` | Portail III | `N000_W001` |
| Porte des enfers IV | `S005_E011` | Portail IV | `N000_E001` |
| Porte des enfers V | `N008_W007` | Portail V | `N000_E000` |

Le générateur écrit `world/<monde>/portals.json` (position globale, altitude du
sol, `radius_m` 6, `arrival_offset_m` 8). Le shard (`src/shardd/world/PortalNetwork`) :

- **toutes les races** passent, **dans les deux sens**, par n'importe quelle porte ;
- un joueur **mort** ne passe pas ;
- déclenchement dans le rayon (6 m) ; arrivée à 8 m au sud du portail apparié,
  hors de son rayon ;
- **réarmement** après 5 s passées hors de tout rayon (aussi à la connexion) ;
- passage = `ServerApp::TransferToMap` : sauvegarde, `ZoneChange` (canal fiable)
  puis `ForcePosition` (nouvelle époque de mouvement, hauteur reprise du client),
  anti-triche remis à zéro. Les MJ changent aussi de carte par `/tp <lieu>`
  (lieux des Enfers dans l'annuaire), `/goto`, `/summon`, `/back`.

Côté client (`EngineCells.cpp`), une seule carte est résidente : à l'annonce
d'une autre carte (`FollowServerCellMap`), `SwitchCellMap` vide tuiles,
heightmaps, eau, PNJ statiques et relit cellules, eau, POI de quêtes et carte du
monde de la nouvelle carte, puis charge synchronement l'arrivée (comme un
téléport : pas d'écran de chargement dédié). La carte à découvrir ne couvre que
le continent ; la carte des Enfers est entièrement visible.

## 4. Hostilité de la Légion envers les humains

Les PNJ démons de la Citadelle (archétypes 240 citoyen, 241 légionnaire,
242 marchand) portent dans `creatures/archetypes.json` :

```json
"behavior": { "kind": "ambient", "hostileToRaces": ["humains"], "aggroRadius": 18.0 }
```

- Shard (`ServerApp::EngageHostileRaces`) : un PNJ d'ambiance hostile quitte sa
  routine dès qu'un joueur vivant d'une race listée entre dans `aggroRadius` m,
  combat avec l'IA des créatures (poursuite, laisse du spawner), puis regagne son
  poste et reprend sa routine.
- Ces joueurs peuvent **riposter** (`CanBeAttackedByRace`) ; les autres races ne
  peuvent toujours pas frapper ces PNJ.
- Client : pour un joueur de ces races, le PNJ a une plaque d'ennemi et se cible
  au Tab.

La liste est configurable par archétype (toute race, tout PNJ d'ambiance).

## 5. Générer

```powershell
.\lcdlln_world_gen.exe --content game/data --world enfers
```

~2 min (9 cellules). La CI génère `royaumes` puis `enfers` avant l'empaquetage
et vérifie `world/enfers/start_locations.json` et les deux `portals.json`.
Déploiement : client, master et shard **en lock-step** (`kProtocolVersion` 20).
