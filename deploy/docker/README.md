# LCDLLN — stack Docker (MySQL + Master + Shard)

## Démarrage rapide (artefact CI Linux)

1. Téléchargez le fichier **`lcdlln-docker-linux-*.zip`** généré par le build Linux.
2. Décompressez-le : vous obtenez un dossier `deploy/docker/` avec notamment :
   - `bin/` — `lcdlln_server`, `lcdlln_shard`
   - `lib/` — **client MySQL** (`libmysqlclient.so.21` et liens) copiés depuis l’hôte de build CI ; utilisés par l’image Docker du master et optionnellement pour un lancement **hors** Docker
   - `db/schema.sql` — initialisation MySQL
   - `db/migrations/` — scripts SQL pour le **MigrationRunner** du master (copiés dans l’image Docker)
   - `config/` — `master.config.json`, `shard.config.json`
   - `game/data/` — contenu client requis par les serveurs
   - `web-portal/` — sources Next.js (build au premier `docker compose up -d` si l’image manque)
   - `traefik-web-portal.labels.reference.yml` — copie des labels Traefik du portail (publication / doc)
   - `docker-compose.yml`, `Dockerfile.master`, `Dockerfile.shard`
3. Dans un terminal :

```bash
cd deploy/docker
cp -n .env.example .env   # optionnel : éditer .env (mots de passe, ports)
docker compose up -d      # premier run : build des images master / shard / web-portal si besoin
```

**CI / CD** : le workflow Linux exécute `scripts/pack-linux-docker-bundle.sh`, qui enchaîne `sync-db-to-docker-deploy.sh`, **`sync-web-portal-to-docker-deploy.sh`**, puis binaires / `lib/` / `game/data/`. Le ZIP contient **`deploy/docker/`** en entier (y compris **`web-portal/`**).

**Depuis un clone Git en local** : le pack refait les synchros ; pour ne mettre à jour que le portail : `./scripts/sync-web-portal-to-docker-deploy.sh`. Le dossier `deploy/docker/db/` versionné sert de repli pour un `docker compose build` sans pack complet.

Ports par défaut : **3840** (master), **3843** (shard), **3000** (portail sur 127.0.0.1), MySQL sur **127.0.0.1:3306**.

Arrêt : `docker compose down`. Les données sont sur l’hôte : **`./data/mysql`** (InnoDB), journaux **`./data/logs/master`** et **`./data/logs/shard`**. Pour repartir d’une base neuve : `docker compose down`, puis `rm -rf data/mysql`, puis `up` (les scripts `docker-entrypoint-initdb.d` ne s’exécutent que si le datadir est vide au premier démarrage).

**Erreur OCI / bind mount** : `mount ... master.config.json ... not a directory` / monter un fichier sur un fichier alors que la source est un dossier — arrive si `config/master.config.json` **n’existait pas** sur l’hôte au premier `docker compose up` : Docker peut avoir créé un **répertoire** vide à la place du fichier.

1. Depuis `deploy/docker` : `ls -la config/master.config.json` — si c’est un dossier :  
   `rm -rf config/master.config.json`  
   puis recopiez le vrai `master.config.json` (depuis le dépôt ou le zip CI).
2. Depuis **M24.5**, le compose monte **`./config`** entier sur `/app/host-config` et un script d’entrée copie `master.config.json` / `shard.config.json` vers `/app/config.json` : refaites un build des images :  
   `docker compose build --no-cache master shard && docker compose up -d`

**Erreur `Checksum mismatch for version 1`** : le répertoire **`./data/mysql`** a été initialisé avec un ancien `schema.sql` qui insérait le checksum factice `0000…001` dans `schema_version`, alors que le master compare au SHA-256 réel de `db/migrations/0001_init.sql`.

À partir de **M24.6**, le **master** corrige automatiquement ce seul cas au démarrage (`UPDATE schema_version` pour la v1). Reconstruisez / redéployez le binaire `lcdlln_server` puis `docker compose up --build`.

- **Sans rebuild** (depuis `deploy/docker`, conteneur `mysql` en marche) :
  ```bash
  chmod +x repair-schema-checksum.sh
  ./repair-schema-checksum.sh
  docker compose restart master
  ```
- **Ou** en SQL manuel (même effet) :
  ```sql
  UPDATE schema_version SET checksum = 'e740eec07991bad0e5b8e13577ad7d0cf61ab5c652e2a9ef84b1565680cf45ae' WHERE version = 1;
  ```
- **Ou** recréer la base : `docker compose down`, `rm -rf data/mysql`, puis `up` (**toutes les données MySQL perdues**).

Les nouveaux déploiements avec ce dépôt montent aussi `db/02_fix_schema_v1_checksum.sql` en init : sur un **nouveau** datadir vide (`data/mysql`), si l’étape 01 avait encore le mauvais checksum, l’étape 02 le corrige (idempotent).

(Recalculer le checksum si vous modifiez `db/migrations/0001_init.sql` : `tools/migration_checksum` ou `sha256sum`.)

**Message `pull access denied for lcdlln-master`** : normal si l’image est uniquement construite en local ; le compose utilise `pull_policy: build`. Si une vieille version de Compose refuse `pull_policy`, retirez ces lignes dans `docker-compose.yml` ou mettez à jour Docker / Compose Plugin.

**Erreur migration 0004** (`fk_characters_server_id`, colonnes dupliquées, `server_id` en BIGINT, ou erreur SQL du type `near IF NOT EXISTS active_session`) : le script `0004_auth_mvp_m33_1.sql` est **idempotent** via `information_schema` et `PREPARE` (sans `ADD COLUMN IF NOT EXISTS`, refusé sur certains moteurs / parseurs malgré un affichage « 8.0.x »). Il normalise `server_id` en `INT UNSIGNED` après avoir retiré les FK si présentes. Après mise à jour des fichiers `db/migrations`, **rebuild** du master (`docker compose build master`). En **dev**, si la base est trop incohérente, supprimez **`data/mysql`** puis relancez `up`.

**Checksum mismatch sur la version 4** : si vous avez modifié le fichier `0004_*.sql` alors que `schema_version` indique déjà la v4 appliquée, le master refusera de démarrer. Mettez à jour `schema_version.checksum` pour la ligne `version = 4` (hash SHA-256 du fichier actuel via `tools/migration_checksum` ou `sha256sum`), ou repartez avec un **`data/mysql`** neuf en dev.

**Migrations 0006, 0008, 0010, 0011 et nouvelle 0015** : ces scripts ont été rendus idempotents ou renommés ; le **checksum** en base des versions déjà appliquées ne correspond plus au fichier actuel → mettre à jour `schema_version.checksum` pour chaque version concernée, ou effacer **`data/mysql`** en dev. **Correction historique** : deux fichiers `0010_*.sql` partageaient le même numéro de version ; un seul était exécuté. La partie « exploits / vues / is_secret » est maintenant **`0015_exploits_visibility_stats.sql`**. Après déploiement, vérifiez que les tables du portail (`account_recovery_*`, etc.) **et** la colonne `exploits.is_secret` sont bien présentes ; en cas d’écart, exécutez à la main le script manquant puis alignez les checksums.

## Master sans Docker (optionnel)

Les binaires du zip CI exigent **glibc ≥ 2.38** et une **libstdc++** récente (build type Ubuntu 24.04). Sur un serveur **Debian 12 / Ubuntu 22.04** ou plus ancien, `run-lcdlln-server-host.sh` **refuse de lancer** et affiche un message explicite : utilisez **`docker compose up -d`** pour le master, ou recompilez l’engine sur une cible alignée avec votre hôte.

Sur une machine **Ubuntu 24.04+** (ou équivalent glibc 2.38+) :

```bash
cd deploy/docker
chmod +x run-lcdlln-server-host.sh
./run-lcdlln-server-host.sh -log -console
```

Ou : `export LD_LIBRARY_PATH="$(pwd)/lib:$LD_LIBRARY_PATH"` puis `bin/lcdlln_server`. Les images **master** / **shard** restent la méthode recommandée dès que l’hôte diverge du toolchain CI.

## Prérequis

- Docker Engine + plugin Compose v2
- ~2 Go de RAM libre recommandé pour MySQL + services
- Image **mysql:8.0** recommandée ; les migrations idempotentes s’appuient sur `information_schema` + `PREPARE` (pas de prérequis strict 8.0.29 pour `IF NOT EXISTS` sur colonnes)

## Portail web (Next.js)

Le service **`web-portal`** est démarré avec **`docker compose up -d`** (plus de profil Compose).

Le **ZIP CI** inclut **`deploy/docker/web-portal/`**. En clone Git sans pack : `./scripts/sync-web-portal-to-docker-deploy.sh`.

**Traefik** : le compose crée le réseau **`traefik_front_network`** au premier `up` (plus besoin de `docker network create`). Les labels sont sur le service `web-portal` ; une **copie** pour documentation / publication est dans **`traefik-web-portal.labels.reference.yml`**. Pour connecter un conteneur Traefik déjà en place, rattachez-le à ce réseau : `docker network connect traefik_front_network <conteneur_traefik>`.

Si un réseau du **même nom** existait déjà (ancienne config `external`) et que Compose refuse de démarrer : arrêtez les services, supprimez le réseau orphelin ou repassez ce réseau en **`external: true`** dans `docker-compose.yml` comme avant (voir commentaire dans le fichier).

Accès local : [http://127.0.0.1:3000](http://127.0.0.1:3000) (`WEB_PORTAL_PORT` dans `.env`). Host Traefik par défaut dans les labels : `lcdlln-portal.tips-of-mine.com`. `DATABASE_URL` du portail suit `MYSQL_USER` / `MYSQL_PASSWORD` / `MYSQL_DATABASE`.

## Développement local (copier les binaires à la main)

Après `cmake --build --preset linux-x64-release` :

```bash
BUILD_DIR=build/linux-x64-release ./scripts/pack-linux-docker-bundle.sh
cd deploy/docker && docker compose up --build -d
```
