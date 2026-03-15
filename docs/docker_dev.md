# Stack Docker locale (M24.4)

Stack de développement locale : MySQL 8, Master, Shard. Images dev (pas de durcissement prod).

## Prérequis

- Docker et Docker Compose
- Binaires Linux du projet : `lcdlln_server` (master) et `lcdlln_shard` (shard), par exemple issus du CI

## Préparation des binaires

Les images Docker attendent les binaires dans `deploy/docker/bin/` :

- `deploy/docker/bin/lcdlln_server` — serveur master
- `deploy/docker/bin/lcdlln_shard` — serveur shard

Copier les binaires Linux (build CI ou build manuel Linux) dans ces chemins avant de construire les images.

Exemple (depuis la racine du repo) :

```text
mkdir -p deploy/docker/bin
cp /chemin/vers/lcdlln_server deploy/docker/bin/
cp /chemin/vers/lcdlln_shard deploy/docker/bin/
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

- **Config** : `config/master.config.json` et `config/shard.config.json` sont montés en lecture seule ; modifier ces fichiers pour changer la config sans rebuild.
- **Logs** : volumes nommés `master-logs` et `shard-logs` ; les logs des serveurs persistent entre redémarrages.

## Arrêt

```bash
docker compose down
```

Pour supprimer aussi les données MySQL :

```bash
docker compose down -v
```

## Services

| Service | Image        | Port (défaut) |
|---------|--------------|----------------|
| mysql   | mysql:8.0    | 3306 (localhost) |
| master  | lcdlln-master | 3840 |
| shard   | lcdlln-shard  | 3843 |

MySQL est initialisé au premier démarrage avec le schéma `db/schema.sql` (base `lcdlln_master`).
