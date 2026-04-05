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
