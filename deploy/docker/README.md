# LCDLLN — stack Docker (MySQL + Master + Shard)

## Démarrage rapide (artefact CI Linux)

1. Téléchargez le fichier **`lcdlln-docker-linux-*.zip`** généré par le build Linux.
2. Décompressez-le : vous obtenez un dossier `deploy/docker/` avec notamment :
   - `bin/` — `lcdlln_server`, `lcdlln_shard`
   - `lib/` — **client MySQL** (`libmysqlclient.so.21` et liens) copiés depuis l’hôte de build CI ; utilisés par l’image Docker du master et optionnellement pour un lancement **hors** Docker
   - `db/schema.sql` — initialisation MySQL
   - `config/` — `master.config.json`, `shard.config.json`
   - `game/data/` — contenu client requis par les serveurs
   - `docker-compose.yml`, `Dockerfile.master`, `Dockerfile.shard`
3. Dans un terminal :

```bash
cd deploy/docker
cp -n .env.example .env   # optionnel : éditer .env (mots de passe, ports)
docker compose up -d      # ou : docker compose up --build -d
```

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
L’ABI doit rester compatible (glibc, x86_64) avec l’Ubuntu 22.04 utilisée pour le build CI.

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
