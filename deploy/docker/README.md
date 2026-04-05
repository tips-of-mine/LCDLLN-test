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
   - `docker-compose.yml`, `Dockerfile.master`, `Dockerfile.shard`
3. Dans un terminal :

```bash
cd deploy/docker
cp -n .env.example .env   # optionnel : éditer .env (mots de passe, ports)
docker compose up -d      # ou : docker compose up --build -d
```

**CI / CD** : le workflow Linux exécute `scripts/pack-linux-docker-bundle.sh`, qui appelle d’abord `scripts/sync-db-to-docker-deploy.sh` pour recopier **`db/schema.sql`** et **`db/migrations/`** vers **`deploy/docker/db/`** — rien à faire à la main sur l’intégration continue.

**Depuis un clone Git en local** : soit lancez `./scripts/sync-db-to-docker-deploy.sh` après avoir modifié `db/`, soit le pack complet (`pack-linux-docker-bundle.sh`) refait cette copie automatiquement. Le dossier `deploy/docker/db/` versionné sert de repli pour un `docker compose build` sans avoir lancé le pack.

Ports par défaut : **3840** (master), **3843** (shard), MySQL sur **127.0.0.1:3306**.

Arrêt : `docker compose down`. Supprimer aussi les données MySQL : `docker compose down -v`.

**Erreur OCI / bind mount** : `mount ... master.config.json ... not a directory` / monter un fichier sur un fichier alors que la source est un dossier — arrive si `config/master.config.json` **n’existait pas** sur l’hôte au premier `docker compose up` : Docker peut avoir créé un **répertoire** vide à la place du fichier.

1. Depuis `deploy/docker` : `ls -la config/master.config.json` — si c’est un dossier :  
   `rm -rf config/master.config.json`  
   puis recopiez le vrai `master.config.json` (depuis le dépôt ou le zip CI).
2. Depuis **M24.5**, le compose monte **`./config`** entier sur `/app/host-config` et un script d’entrée copie `master.config.json` / `shard.config.json` vers `/app/config.json` : refaites un build des images :  
   `docker compose build --no-cache master shard && docker compose up -d`

**Erreur `Checksum mismatch for version 1`** : le volume MySQL a été initialisé avec un ancien `schema.sql` qui insérait le checksum factice `0000…001` dans `schema_version`, alors que le master compare au SHA-256 réel de `db/migrations/0001_init.sql`.

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
- **Ou** recréer la base : `docker compose down -v` puis `up` (**toutes les données MySQL perdues**).

Les nouveaux déploiements avec ce dépôt montent aussi `db/02_fix_schema_v1_checksum.sql` en init : sur un **nouveau** volume vide, si l’étape 01 avait encore le mauvais checksum, l’étape 02 le corrige (idempotent).

(Recalculer le checksum si vous modifiez `db/migrations/0001_init.sql` : `tools/migration_checksum` ou `sha256sum`.)

**Message `pull access denied for lcdlln-master`** : normal si l’image est uniquement construite en local ; le compose utilise `pull_policy: build`. Si une vieille version de Compose refuse `pull_policy`, retirez ces lignes dans `docker-compose.yml` ou mettez à jour Docker / Compose Plugin.

**Erreur migration 0004** (`fk_characters_server_id` : colonnes incompatibles, `Duplicate column name 'active_session'`, ou `server_id` resté en BIGINT après une tentative ratée) : le script `0004_auth_mvp_m33_1.sql` est **idempotent** (MySQL **8.0.29+** requis pour `ADD COLUMN IF NOT EXISTS`, `DROP FOREIGN KEY IF EXISTS`, etc.). Il normalise `server_id` en `INT UNSIGNED` après avoir retiré les FK éventuelles. Après mise à jour des fichiers `db/migrations`, **rebuild** du master (`docker compose build master`). En **dev**, si la base est trop incohérente, `docker compose down -v` puis `up` repart d’un volume neuf.

**Checksum mismatch sur la version 4** : si vous avez modifié le fichier `0004_*.sql` alors que `schema_version` indique déjà la v4 appliquée, le master refusera de démarrer. Mettez à jour `schema_version.checksum` pour la ligne `version = 4` (hash SHA-256 du fichier actuel via `tools/migration_checksum` ou `sha256sum`), ou repartez avec un volume neuf en dev.

**Migrations 0006, 0008, 0010, 0011 et nouvelle 0015** : ces scripts ont été rendus idempotents ou renommés ; le **checksum** en base des versions déjà appliquées ne correspond plus au fichier actuel → mettre à jour `schema_version.checksum` pour chaque version concernée, ou `down -v` en dev. **Correction historique** : deux fichiers `0010_*.sql` partageaient le même numéro de version ; un seul était exécuté. La partie « exploits / vues / is_secret » est maintenant **`0015_exploits_visibility_stats.sql`**. Après déploiement, vérifiez que les tables du portail (`account_recovery_*`, etc.) **et** la colonne `exploits.is_secret` sont bien présentes ; en cas d’écart, exécutez à la main le script manquant puis alignez les checksums.

## Master sans Docker (optionnel)

Si vous exécutez `bin/lcdlln_server` directement sur la machine (sans conteneur), les `.so` du bundle doivent être visibles :

```bash
cd deploy/docker
chmod +x run-lcdlln-server-host.sh
./run-lcdlln-server-host.sh -log -console
```

Ou : `export LD_LIBRARY_PATH="$(pwd)/lib:$LD_LIBRARY_PATH"` puis lancer `bin/lcdlln_server`.  
Les images **master** / **shard** utilisent **Ubuntu 24.04** (glibc ≥ 2.38), comme `ubuntu-latest` sur GitHub Actions : les binaires du zip CI et les `.so` dans `lib/` y sont compatibles. Si vous compilez sur une distro plus ancienne, le binaire reste en général exécutable dans ces conteneurs ; l’inverse (binaire récent + image 22.04) provoque des erreurs `GLIBC_2.38 not found`.

## Prérequis

- Docker Engine + plugin Compose v2
- ~2 Go de RAM libre recommandé pour MySQL + services
- Image **mysql:8.0** ≥ **8.0.29** pour les migrations idempotentes (notamment la 0004) ; l’image officielle `mysql:8.0` courante le satisfait en général

## Portail web (Next.js)

Le service `web-portal` n’est **pas** inclus dans le zip CI (sources hors de `deploy/docker`). Il reste utilisable depuis un clone complet du dépôt :

```bash
# à la racine du repo, pas depuis le zip seul
docker compose -f deploy/docker/docker-compose.yml --profile portal up -d
```

## Développement local (copier les binaires à la main)

Après `cmake --build --preset linux-x64-release` :

```bash
BUILD_DIR=build/linux-x64-release ./scripts/pack-linux-docker-bundle.sh
cd deploy/docker && docker compose up --build -d
```
