# Design — Éclatement de `config.json` par rôle

> Date : 2026-06-17
> Statut : design validé (brainstorming), en attente de plan d'implémentation
> Sujet : séparer le fourre-tout `config.json` (722 lignes) en fichiers à
> responsabilité unique : config moteur/client, config serveur, contenu de zone.

## 1. Contexte et problème

`config.json` (racine, 722 lignes) mélange **trois préoccupations** sans rapport :

1. **Config moteur/app (légitime)** — `log, render, audio, time, camera, controls,
   player, shadows, bloom, tonemap, color_grading, exposure, lod, gi, paths,
   editor, client`. C'est sa vocation.
2. **Config serveur** — `db` (credentials !), `accounts`, `globals`, `chat`.
   Des clés serveur dans le fichier de config que **le client embarque**.
3. **Données de map/contenu** — le bloc `world` (lignes 183–606, ~424 lignes) :
   `world.scenery` (340+ instances de décor avec coordonnées), `world.interactables`,
   `world.test_water`, `world.default_spawn`, plus des réglages de rendu par-zone.

Deux conséquences problématiques :

- 🔒 **Sécurité** : les credentials DB (`db.password`, `db.user`, `db.host`) partent
  potentiellement chez le client puisque tous les binaires chargent le même fichier.
- 🧹 **Maintenabilité / déploiement** : la config serveur est dupliquée à la main
  dans `deploy/docker/config/{master,shard}.config.json`, ce qui a déjà causé le bug
  « shard sans bloc `db` → aucune stat perso » (cf. mémoire `shard_db_config_gap`).

## 2. Faits techniques établis (analyse du code)

### 2.1 Mécanisme de chargement — `src/shared/core/Config.cpp`

- **Store plat à clés pointées** : le JSON imbriqué est aplati par `MergeJsonFlatten`
  en clés du type `world.scenery.0.x`, `world.fog.start_m`, etc. Tous les accès
  passent par `GetString/GetInt/GetDouble/GetBool("préfixe.pointé")`.
- **Chargement multi-fichiers déjà en place** : `Config::Load(path, argc, argv)`
  charge `config.json`, **puis** `config/server.ini` par-dessus (override), puis la CLI.
  Sémantique de merge : un `LoadFromFile` ultérieur **écrase** les clés existantes.
- **Le client empile déjà des fichiers satellites** par-dessus `config.json` :
  `keybinds.json`, `ui_theme.json`, `character_appearance.json`, `user_settings.json`
  (cf. `Engine.cpp` ~838-859).

**Conséquence de design majeure** : tant qu'on **conserve les préfixes de clés**
(`world.scenery.*`…), déplacer des blocs vers d'autres fichiers chargés en plus
**ne casse aucun call-site C++**.

### 2.2 Qui charge `config.json`

Tous les binaires appellent `Config::Load("config.json", argc, argv)` :
- Client : `src/client/app/Engine.cpp:1218`
- Master : `src/masterd/main_linux.cpp:163`
- Shard : `src/shardd/main_linux.cpp:48`, `src/shardd/main_win.cpp:101`
- Server bootstrap : `src/shared/server_bootstrap/main.cpp:89`
- Éditeur : `src/world_editor/main.cpp` (résolution du `config.json` du cwd)

### 2.3 Qui lit quoi (croisement des clés)

| Bloc / clé | Client | Master | Shard | Éditeur | Note |
|---|:--:|:--:|:--:|:--:|---|
| `log, render, audio, shadows, bloom, tonemap, lod, gi, …` | ✔ | – | – | ✔ | moteur |
| `client.*` (endpoints) | ✔ | – | – | – | client |
| `editor.*` | – | – | – | ✔ | éditeur |
| `db.*` | – | ✔ | ✔ | – | **credentials**, server only |
| `accounts.*` | – | (✔) | – | – | aucun lecteur littéral, server only |
| `chat.*` | – | ✔ | – | – | « relues au boot du master uniquement » |
| `globals.*` | – | – | – | – | **AUCUN lecteur → clé morte** |
| `game.worldclock.drift_check_sec` | ✔ | – | – | – | partagé |
| `game.worldclock.{epoch,timescale,lunar_period}` | – | ✔ | – | – | partagé |
| `world.lunar.*` | ✔ | ✔ | – | – | **temps global, autorité master** |
| `world.scenery / interactables / test_water / default_spawn` | ✔ | – | – | – | contenu de map |
| `world.{fog,volfog,dof,impostor,props,day_night,weather,…}` | ✔ | – | – | – | réglages de zone |

Vérifications notables :
- `globals.default_locale / fallback_locale / graveyard_default_faction_neutral_radius_m`
  n'ont **aucun lecteur** dans `src/` → **config morte** (à supprimer / archiver).
- L'éditeur **n'écrit pas** `world.scenery` dans cette branche (l'export « PR #906 »
  est côté GitHub, non présent localement) ; le `scenery` provient de
  `tools/world_gen/scatter_forest.py` (générateur déterministe).
- Le serveur ne lit **que** `world.lunar.*` dans tout le bloc `world` (master,
  `main_linux.cpp:929-931`).

## 3. Décisions validées (brainstorming)

- **Bloc `world`** : séparer contenu et réglages, et déplacer **tout** le bloc vers
  les fichiers de zone (réponse « 1 + 2 »), **sauf `world.lunar`** (temps global lu
  par le master) qui reste en config partagée.
- **Config serveur** : `db/accounts/globals/chat` vont dans un **fichier serveur
  chargé uniquement par master/shard/bootstrap** — plus jamais embarqué côté client.
- **Zones** : structure **multi-zones** `game/data/zones/<zone>/`.
- **Préfixes de clés** : on **garde `world.*`** (zéro changement de call-site). La
  séparation des rôles est portée par l'organisation **en fichiers**.
- **Deux fichiers par zone** : `scenery.json` (gros volume généré) + `zone.json`
  (reste contenu + réglages).

## 4. Architecture cible

### 4.1 Fichiers après split

**1) `config.json` (racine) — config MOTEUR + CLIENT partagée, non-secrète, non-contenu**
Chargé par **tous** les binaires (inchangé).
Contenu : `log, render, audio, time, camera, controls, player, shadows, bloom,
tonemap, color_grading, exposure, lod, gi, paths, editor, client, game`
(`worldclock` partagé) **+ `world.lunar`** (temps global) **+ `world.active_zone`**
(nouvelle clé, voir §4.2).

**2) `config/server.config.json` — config SERVEUR uniquement** 🔒
Chargé **uniquement** par master / shard / server_bootstrap (ajout d'un
`cfg.LoadFromFile("config/server.config.json")` après `Config::Load` dans chaque
`main.cpp` serveur). Jamais côté client/éditeur.
Contenu : `db, accounts, chat`. (`globals` : voir §6, candidat suppression.)

**3) `game/data/zones/<zone>/` — contenu + réglages de zone** 🗺️
Chargé par le client (zone active) et l'éditeur (zone éditée).
- **`scenery.json`** → `world.scenery.*` (340+ instances). Fichier que le générateur /
  futur export éditeur réécrit.
- **`zone.json`** → reste du bloc `world` :
  - section *contenu* : `world.interactables`, `world.test_water`, `world.default_spawn` ;
  - section *réglages* : `world.fog, volfog, dof, impostor, props, max_draw_distance,
    day_night, weather, atmosphere_path, zone_meta_path, probes_path`.

Les deux fichiers conservent les clés `world.*` (le client charge une seule zone à la
fois ; pas de collision).

### 4.2 Sélection de zone (multi-zones)

- Nouvelle clé `world.active_zone` dans `config.json` (ex. `"feyhin"`).
- Au boot, après `Config::Load`, le client résout
  `game/data/zones/<active_zone>/zone.json` puis `…/scenery.json` et les charge
  (`LoadFromFile`). Fallback propre si absent : log warning, pas de crash (le moteur
  garde ses valeurs par défaut).
- L'éditeur charge/édite la zone via le même chemin.

### 4.3 Changements de chargement par binaire

- `Config::Load` (partagé) : **inchangé** (générique).
- Client (`Engine.cpp`) : après `Config::Load`, charger les fichiers de zone résolus
  depuis `world.active_zone`.
- Master / Shard / Bootstrap (`main.cpp`) : après `Config::Load`, charger
  `config/server.config.json`.
- Éditeur (`main.cpp` / `WorldEditorShell`) : charger les fichiers de zone active.

## 5. Migration et compatibilité

- **Additive par construction** : les chargements supplémentaires fusionnent à plat.
  Pendant la transition, si un fichier de zone/serveur manque, les anciennes clés
  encore présentes dans `config.json` restent lisibles (aucune régression dure).
- Bascule **en lock-step** avec le déploiement : on retire les blocs déplacés de
  `config.json` **en même temps** que master/shard chargent `server.config.json`.
- Les fichiers `deploy/docker/config/{master,shard}.config.json` sont alignés sur le
  nouveau schéma (bloc serveur canonique → réduit le risque de bug « shard sans `db` »).

## 6. Code mort détecté (à traiter dans le plan)

- **`globals.*`** : aucun lecteur dans `src/`. Options : supprimer le bloc, ou le
  conserver server-side si une lecture indirecte/future est confirmée. À trancher au
  début du plan (un `grep` final de confirmation).

## 7. Impacts de déploiement

> **Déploiement** : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** (master **et** shard) — les
> blocs `db/accounts/chat` sont relocalisés dans `config/server.config.json` et les
> `main.cpp` serveur sont modifiés pour le charger. Le **client** change aussi (charge
> désormais les fichiers de zone) → déploiement **lock-step** client ↔ serveur. Aligner
> les fichiers `deploy/docker/config/*` sur le nouveau schéma.

## 8. Hors périmètre (chantiers futurs)

- Renommage des clés `world.*` → `zone.*` (refacto de call-sites, chantier dédié).
- Remplacement du contenu de zone par le vrai pipeline de zone (`props.bin`) au lieu
  d'un JSON chargé dans `Config` (cf. mémoire `config_json_dumping_ground` : le JSON
  reste un palliatif ; le vrai fix câble éditeur → jeu via le pipeline de zone).
- Export éditeur du `world.scenery` (PR #906, côté GitHub) : à rediriger vers
  `scenery.json` quand il sera intégré.

## 9. Critères d'acceptation

1. `config.json` ne contient plus que la config moteur/client partagée + `world.lunar`
   + `world.active_zone`.
2. `config/server.config.json` contient `db/accounts/chat` et n'est chargé que par
   master/shard/bootstrap ; le client ne l'embarque plus.
3. `game/data/zones/feyhin/{zone.json,scenery.json}` contiennent l'intégralité de
   l'ancien bloc `world` (hors `lunar`), chargés par le client au boot.
4. Aucun call-site C++ `GetX("world.…")` / serveur n'est modifié (préfixes conservés).
5. Le jeu démarre, affiche le décor (scenery), les interactibles et l'eau de test
   comme avant ; le master lit toujours `world.lunar` et `chat.*`.
6. `globals.*` est supprimé ou justifié.
7. `deploy/docker/config/*` et `CODEBASE_MAP.md` (§ config) sont mis à jour.
