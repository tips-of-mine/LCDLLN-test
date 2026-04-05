#!/usr/bin/env bash
# Copie db/schema.sql et db/migrations/ → deploy/docker/db/ (Dockerfile.master COPY + MySQL init).
# Appelé par pack-linux-docker-bundle.sh et par la CI avant/assemblage du zip.
# Usage : depuis la racine du dépôt :
#   ./scripts/sync-db-to-docker-deploy.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOCKER_DIR="$ROOT/deploy/docker"
SRC_DB="$ROOT/db"

if [[ ! -f "$SRC_DB/schema.sql" ]]; then
  echo "ERROR: $SRC_DB/schema.sql introuvable." >&2
  exit 1
fi
if [[ ! -d "$SRC_DB/migrations" ]]; then
  echo "ERROR: $SRC_DB/migrations introuvable (requis par le MigrationRunner du master)." >&2
  exit 1
fi

mkdir -p "$DOCKER_DIR/db/migrations"
cp -f "$SRC_DB/schema.sql" "$DOCKER_DIR/db/schema.sql"
rm -rf "$DOCKER_DIR/db/migrations"
mkdir -p "$DOCKER_DIR/db/migrations"
cp -r "$SRC_DB/migrations/." "$DOCKER_DIR/db/migrations/"
echo "OK — db/ synchronisé vers $DOCKER_DIR/db/"
