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
3. **Données de map/contenu** — le bloc `world` (lignes 183–606, ~424 lignes) :
   `world.scenery` (340+ instances), `world.interactables`, `world.test_water`,
   `world.default_spawn`, plus des réglages de rendu par-zone.

Conséquences problématiques :

- 🔒 **Sécurité** : le `config.json` racine est **packagé dans le client Windows**
  (cf. `build-windows.yml:207`). Les credentials DB ne doivent pas s'y trouver.
- 🧹 **Duplication serveur** : la config serveur est recopiée à la main dans
  `deploy/docker/config/{master,shard}.config.json`, ce qui a causé le bug « shard
  sans bloc `db` → aucune stat perso » (mémoire `shard_db_config_gap`).

## 2. Faits techniques établis (analyse du code)

### 2.1 Mécanisme de chargement — `src/shared/core/Config.cpp`

- **Store plat à clés pointées** : le JSON imbriqué est aplati par `MergeJsonFlatten`
  en clés du type `world.scenery.0.x`. Accès via `GetString/GetInt/...("préfixe.pointé")`.
- **Chargement multi-fichiers déjà en place** : `Config::Load(path, argc, argv)` charge
  `config.json`, **puis** `config/server.ini` (override), puis la CLI. Un `LoadFromFile`
  ultérieur **écrase** les clés existantes.
- **Le client empile déjà des satellites** : `keybinds.json`, `ui_theme.json`,
  `character_appearance.json`, `user_settings.json` (`Engine.cpp` ~838-859).

**Conséquence majeure** : tant qu'on **conserve les préfixes de clés** (`world.scenery.*`…),
déplacer des blocs vers d'autres fichiers chargés en plus **ne casse aucun call-site C++**.

### 2.2 Qui charge `config.json` — et la réalité Docker

Tous les binaires appellent `Config::Load("config.json", argc, argv)` :
Client `Engine.cpp:1218` · Master `masterd/main_linux.cpp:163` · Shard
`shardd/main_linux.cpp:48` & `main_win.cpp:101` · Bootstrap
`shared/server_bootstrap/main.cpp:89` · Éditeur `world_editor/main.cpp`.

**Mais en production (Docker), le serveur charge SA PROPRE config** via un montage qui
renomme le fichier :
- Master : `./config/master.config.json` → `/app/config.json:ro` (`docker-compose.yml:94`)
- Shard : `./config/shard.config.json` → `/app/config.json:ro` (`docker-compose.yml:135`)
- `./game/data` → `/app/game/data:ro` (97, 135)

➡️ **Le `config.json` racine du repo est de fait la config CLIENT/dev.** Le serveur de
prod utilise `master.config.json` / `shard.config.json`. C'est dans ces deux fichiers que
`db/accounts/chat` est aujourd'hui **dupliqué** (source du bug `shard_db_config_gap`).

### 2.3 Qui lit quoi (croisement des clés)

| Bloc / clé | Client | Master | Shard | Éditeur | Note |
|---|:--:|:--:|:--:|:--:|---|
| `log, render, audio, shadows, bloom, lod, gi, …` | ✔ | – | – | ✔ | moteur |
| `client.*` (endpoints) | ✔ | – | – | – | client |
| `editor.*` | – | – | – | ✔ | éditeur |
| `db.*` | – | ✔ | ✔ | – | **credentials**, server only |
| `accounts.*` | – | (✔) | – | – | server only |
| `chat.*` | – | ✔ | – | – | « relues au boot du master uniquement » |
| `globals.graveyard_*` | – | – | (✔ futur) | – | **futur contenu cimetière PAR-ZONE** |
| `globals.default_locale / fallback_locale` | – | – | – | – | aucun lecteur (à confirmer) |
| `game.worldclock.drift_check_sec` | ✔ | – | – | – | partagé |
| `game.worldclock.{epoch,timescale,lunar_period}` | – | ✔ | – | – | partagé |
| `world.lunar.*` | ✔ | ✔ | – | – | **temps global, autorité master** |
| `world.scenery / interactables / test_water / default_spawn` | ✔ | – | – | – | contenu de map |
| `world.{fog,volfog,dof,impostor,props,day_night,weather,…}` | ✔ | – | – | – | réglages de zone |
| respawn graveyard/inn (fichier `respawn/respawn_points.txt`) | – | – | ✔ | – | **par-zone, server-read** (`ServerApp.cpp:1929`) |

Vérifications notables :
- `globals.default_locale / fallback_locale` n'ont aucun lecteur (le `fallback_locale` de
  `TermsRepository` est la clé distincte `terms.fallback_locale`). À confirmer en début de plan.
- **`globals.graveyard_default_faction_neutral_radius_m`** : pas de lecteur *aujourd'hui*,
  mais c'est une donnée de **cimetière par-zone** (décision §3) → à conserver et migrer.
- L'éditeur **n'écrit pas** `world.scenery` dans cette branche (export « PR #906 » côté
  GitHub) ; le `scenery` provient de `tools/world_gen/scatter_forest.py`.
- Les points de réapparition (graveyard/inn) sont déjà **par-zone et lus côté serveur**
  depuis `respawn/respawn_points.txt` — pas depuis `config.json`.

## 3. Décisions validées (brainstorming)

- **Bloc `world`** : séparer contenu et réglages, déplacer **tout** le bloc vers les
  fichiers de zone, **sauf `world.lunar`** (temps global lu par le master) → reste en config partagée.
- **Config serveur** : `db/accounts/chat` vont dans un **`server.config.json` partagé**,
  chargé uniquement par master/shard/bootstrap, monté sur master **ET** shard (déduplique
  `db`). Jamais embarqué côté client.
- **Zones** : structure **multi-zones** `game/data/zones/<zone>/`. Zone de départ :
  `feyhin` (d'autres zones / modifications viendront).
- **Préfixes de clés** : on **garde `world.*`** (zéro changement de call-site).
- **Deux fichiers client par zone** : `scenery.json` + `zone.json`.
- **`globals` n'est PAS supprimé** : `graveyard_*` devient du **contenu de cimetière
  par-zone** (un **ou plusieurs cimetières par zone**, sélection selon la distance), donc
  rattaché au contenu de zone **côté serveur**.
- **La zone est une unité client + serveur** : fichiers client (`scenery.json`,
  `zone.json`) + données serveur (cimetières/respawn). Aligner sur le mécanisme existant
  `respawn/respawn_points.txt`.
- **CI/CD** : le packaging doit suivre (cf. §7).

## 4. Architecture cible

### 4.1 Fichiers après split

**1) `config.json` (racine) — config MOTEUR + CLIENT partagée, non-secrète, non-contenu**
Packagé dans le client (inchangé). Chargé par tous les binaires (les serveurs Docker
chargent leur propre `config.json` monté, cf. §2.2).
Contenu : `log, render, audio, time, camera, controls, player, shadows, bloom, tonemap,
color_grading, exposure, lod, gi, paths, editor, client, game` (`worldclock` partagé)
**+ `world.lunar`** (temps global) **+ `world.active_zone`** (nouvelle clé, §4.2).

**2) `config/server.config.json` — config SERVEUR uniquement** 🔒
Chargé **uniquement** par master / shard / server_bootstrap (ajout d'un
`cfg.LoadFromFile("config/server.config.json")` après `Config::Load` dans chaque `main.cpp`
serveur). Jamais packagé côté client.
Contenu : `db, accounts, chat`.
En Docker : un **seul** fichier `deploy/docker/config/server.config.json` monté sur master
**et** shard (`/app/config/server.config.json:ro`). `master.config.json` / `shard.config.json`
**perdent** ces blocs → fin de la duplication `db`.
(`globals.default_locale/fallback_locale` : y rester par défaut, ou supprimés si confirmés morts.)

**3) `game/data/zones/<zone>/` — contenu + réglages de zone** 🗺️
- **`scenery.json`** (client/éditeur) → `world.scenery.*` (340+ instances).
- **`zone.json`** (client/éditeur) → reste du bloc `world` :
  - *contenu* : `world.interactables`, `world.test_water`, `world.default_spawn` ;
  - *réglages* : `world.fog, volfog, dof, impostor, props, max_draw_distance, day_night,
    weather, atmosphere_path, zone_meta_path, probes_path`.
- **données serveur de zone** (cimetières/respawn) : aligner sur `respawn_points.txt`
  (1..N cimetières par zone + rayon, sélection par distance), lisible côté shard. Reprend
  la sémantique de `globals.graveyard_*`.

Les fichiers client conservent les clés `world.*` (une seule zone chargée à la fois).

### 4.2 Sélection de zone (multi-zones)

- Nouvelle clé `world.active_zone` dans `config.json` (ex. `"feyhin"`).
- Au boot, après `Config::Load`, le client résout `game/data/zones/<active_zone>/zone.json`
  puis `…/scenery.json` et les charge. Fallback propre si absent (log warning, pas de crash).
- L'éditeur charge/édite la zone via le même chemin.

### 4.3 Changements de chargement par binaire

- `Config::Load` (partagé) : **inchangé** (générique).
- Client (`Engine.cpp`) : après `Config::Load`, charger les fichiers de zone (`world.active_zone`).
- Master / Shard / Bootstrap (`main.cpp`) : après `Config::Load`, charger `config/server.config.json`.
- Éditeur : charger les fichiers de la zone active.

## 5. Migration et compatibilité

- **Additive par construction** : les chargements supplémentaires fusionnent à plat. Pendant
  la transition, si un fichier de zone/serveur manque, les anciennes clés restantes dans
  `config.json` restent lisibles (aucune régression dure).
- Bascule **en lock-step** avec le déploiement : on retire les blocs déplacés de
  `config.json` **en même temps** que master/shard chargent `server.config.json`.

## 6. Config morte / à confirmer

- `globals.default_locale / fallback_locale` : aucun lecteur trouvé. Confirmer par un
  `grep` final ; supprimer si mort, sinon laisser dans `server.config.json`.
- `globals.graveyard_default_faction_neutral_radius_m` : **non mort** — migrer vers le
  contenu de cimetière par-zone (§4.1.3).

## 7. Impacts CI/CD et déploiement

> **Déploiement** : ⚠️ **REDÉPLOIEMENT SERVEUR REQUIS** (master **et** shard) — les blocs
> `db/accounts/chat` sont relocalisés dans `config/server.config.json` et les `main.cpp`
> serveur le chargent désormais. Le **client** change aussi (charge les fichiers de zone)
> → déploiement **lock-step** client ↔ serveur.

CI/CD à modifier :
- **`deploy/docker/docker-compose.yml`** : ajouter le montage
  `./config/server.config.json:/app/config/server.config.json:ro` sur **master ET shard**.
- **`deploy/docker/config/server.config.json`** (nouveau, versionné) : `db/accounts/chat`
  avec interpolation `.env` comme aujourd'hui ; inclus automatiquement dans le bundle Linux
  (la CI zippe `deploy/docker/` en entier, `build-linux.yml:170`).
- **`deploy/docker/config/{master,shard}.config.json`** : retirer `db/accounts/chat`
  (ne gardent que le rôle/ports non-secrets).
- **Client Windows** : `build-windows.yml` copie déjà `config.json` + `config/server.ini`
  mais **pas** la config serveur — vérifier que `server.config.json` n'y est **jamais** ajouté.
- **`game/data/zones/<zone>/*`** : embarqués automatiquement (client via copie récursive
  `game/data` ; serveur via `pack-linux-docker-bundle.sh` + montage `:ro`). Aucun changement
  de script attendu.
- **Dev local serveur** (Windows `main_win` / bootstrap) : charge `config/server.config.json`
  à côté de `config.json`.
- **`CODEBASE_MAP.md`** (§ config) + mémoire `config_json_dumping_ground` à mettre à jour.

## 8. Hors périmètre (chantiers futurs)

- Renommage des clés `world.*` → `zone.*` (refacto de call-sites, chantier dédié).
- Remplacement du contenu de zone par le vrai pipeline de zone (`props.bin`) au lieu d'un
  JSON dans `Config` (mémoire `config_json_dumping_ground`).
- Export éditeur du `world.scenery` (PR #906, GitHub) : à rediriger vers `scenery.json`.
- Sélection runtime du cimetière le plus proche multi-cimetières (si pas déjà gérée par
  `respawn_points.txt`) — étendre si besoin.

## 9. Critères d'acceptation

1. `config.json` racine ne contient plus que la config moteur/client partagée + `world.lunar`
   + `world.active_zone` ; **aucun secret DB**.
2. `config/server.config.json` contient `db/accounts/chat`, chargé uniquement par
   master/shard/bootstrap, monté sur les deux services Docker, retiré de master/shard.config.json.
3. `game/data/zones/feyhin/{zone.json,scenery.json}` contiennent l'ancien bloc `world`
   (hors `lunar`), chargés par le client au boot.
4. Le contenu cimetière (ex-`globals.graveyard_*`) est rattaché à la zone, lisible côté serveur.
5. Aucun call-site C++ `GetX("world.…")` / serveur n'est modifié (préfixes conservés).
6. Le jeu démarre et affiche décor, interactibles et eau de test comme avant ; le master lit
   toujours `world.lunar` et `chat.*` ; le shard lit `db` depuis `server.config.json`.
7. `globals.default_locale/fallback_locale` supprimé ou justifié.
8. CI verte (build-windows + build-linux/ctest) ; `deploy/docker/*` et `CODEBASE_MAP.md` à jour.
