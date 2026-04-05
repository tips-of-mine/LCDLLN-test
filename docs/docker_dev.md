# Stack Docker locale (M24.4)

Stack de développement locale : MySQL 8, Master, Shard. Images dev (pas de durcissement prod).

## Prérequis

- Docker et Docker Compose v2
- Soit l’**artefact CI** `lcdlln-docker-linux-*.zip` (voir `deploy/docker/README.md`) — décompresser puis `cd deploy/docker` —, soit un build Linux local.

## Contenu attendu dans `deploy/docker/`

| Élément | Rôle |
|--------|------|
| `bin/lcdlln_server`, `bin/lcdlln_shard` | Binaires Linux (CI les copie via `scripts/pack-linux-docker-bundle.sh`) |
| `lib/libmysqlclient.so*` | Client MySQL runtime (copié depuis `libmysqlclient21` sur l’hôte de build ; image master : `COPY lib/` + `ldconfig`) |
| `db/schema.sql` | Init MySQL (copie de `db/schema.sql` à la racine du repo) |
| `game/data/` | Données jeu montées dans les conteneurs (`paths.content`) |
| `config/*.json`, `docker-compose.yml`, Dockerfiles | Config et stack |

En local après `cmake --build --preset linux-x64-release` :

```bash
BUILD_DIR=build/linux-x64-release ./scripts/pack-linux-docker-bundle.sh
```

## Démarrage de la stack

Depuis `deploy/docker` :

```bash
cd deploy/docker
docker compose up -d
```

Pour voir les logs en direct :

```bash
docker compose up
```

## Volumes

- **Config** : `config/master.config.json` et `config/shard.config.json` montés en lecture seule.
- **Contenu** : `./game/data` → `/app/game/data` (master et shard).
- **MySQL** : `./db/schema.sql` monté dans `docker-entrypoint-initdb.d` au premier démarrage.
- **Logs** : `./data/logs/master`, `./data/logs/shard`.
- **MySQL datadir** : `./data/mysql`.

Le service **web-portal** (Next.js) démarre avec **`docker compose up -d`**. Le zip CI inclut **`web-portal/`** sous `deploy/docker/` (pack).

## Arrêt

```bash
docker compose down
```

Pour repartir d’une base MySQL neuve (bind mount `./data/mysql`) :

```bash
docker compose down
rm -rf data/mysql
docker compose up -d
```

## Services

| Service | Image        | Port (défaut) |
|---------|--------------|----------------|
| mysql   | mysql:8.0    | 3306 (localhost) |
| master  | lcdlln-master | 3840 |
| shard   | lcdlln-shard  | 3843 |
| web-portal | lcdlln-web-portal | 3000 (localhost) |

MySQL est initialisé au premier démarrage avec `deploy/docker/db/schema.sql` (base `lcdlln_master`).
