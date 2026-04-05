#!/usr/bin/env bash
# Copie web-portal/ → deploy/docker/web-portal/ (contexte Docker Compose ./web-portal, ZIP CI autonome).
# Exclut node_modules, .next, .git. Appelé par pack-linux-docker-bundle.sh ; utilisable seul après édition Next.js.
# Usage : depuis la racine du dépôt : ./scripts/sync-web-portal-to-docker-deploy.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOCKER_DIR="$ROOT/deploy/docker"
SRC="$ROOT/web-portal"
DST="$DOCKER_DIR/web-portal"

if [[ ! -d "$SRC" ]]; then
  echo "Warning: $SRC absent — deploy/docker/web-portal non créé." >&2
  rm -rf "$DST"
  exit 0
fi

rm -rf "$DST"
mkdir -p "$DST"

if command -v rsync >/dev/null 2>&1; then
  rsync -a \
    --exclude 'node_modules' \
    --exclude '.next' \
    --exclude '.git' \
    --exclude '.env' \
    --exclude '.env.local' \
    --exclude '.env.*' \
    "$SRC/" "$DST/"
else
  (cd "$SRC" && tar -cf - \
    --exclude='node_modules' \
    --exclude='.next' \
    --exclude='.git' \
    --exclude='.env' \
    --exclude='.env.local' \
    .) | (cd "$DST" && tar -xf -)
fi

echo "OK — $DST"
