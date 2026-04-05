#!/usr/bin/env bash
# Assemble sous deploy/docker/ un bundle prêt pour docker compose (binaires + db + game/data + lib MySQL).
# Usage : depuis la racine du dépôt, après build Release Linux :
#   BUILD_DIR=build/linux-x64-release ./scripts/pack-linux-docker-bundle.sh
#
# Prérequis pack : paquet runtime libmysqlclient21 (Debian/Ubuntu) pour copier libmysqlclient.so.21 dans lib/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/linux-x64-release}"
DOCKER_DIR="$ROOT/deploy/docker"

for bin in lcdlln_server lcdlln_shard; do
  if [[ ! -f "$BUILD_DIR/pkg/server/$bin" ]]; then
    echo "Missing $BUILD_DIR/pkg/server/$bin — build Linux Release first." >&2
    exit 1
  fi
done

# Bibliothèques client MySQL (même ABI que l’hôte de build → image ubuntu:24.04 du Dockerfile.master).
pack_mysql_client_libs() {
  local dest="$DOCKER_DIR/lib"
  rm -rf "$dest"
  mkdir -p "$dest"
  local found=0
  if command -v dpkg-query >/dev/null 2>&1 && dpkg-query -W -f='${Status}' libmysqlclient21 2>/dev/null | grep -q 'install ok installed'; then
    while IFS= read -r f; do
      if [[ -f "$f" || -L "$f" ]] && [[ "$f" == *.so* ]]; then
        cp -a "$f" "$dest/"
        found=1
      fi
    done < <(dpkg -L libmysqlclient21 2>/dev/null || true)
  fi
  if [[ "$found" -eq 0 ]]; then
    shopt -s nullglob
    for pattern in /usr/lib/x86_64-linux-gnu/libmysqlclient.so* /usr/lib64/mysql/libmysqlclient.so*; do
      if [[ -e "$pattern" ]]; then
        cp -a "$pattern" "$dest/"
        found=1
      fi
    done
    shopt -u nullglob
  fi
  if [[ ! -f "$dest/libmysqlclient.so.21" && ! -L "$dest/libmysqlclient.so.21" ]]; then
    echo "ERROR: libmysqlclient.so.21 introuvable. Installez le paquet runtime, ex. :" >&2
    echo "  sudo apt install libmysqlclient21" >&2
    exit 1
  fi
}

mkdir -p "$DOCKER_DIR/bin" "$DOCKER_DIR/db" "$DOCKER_DIR/game/data"
pack_mysql_client_libs
cp -f "$BUILD_DIR/pkg/server/lcdlln_server" "$BUILD_DIR/pkg/server/lcdlln_shard" "$DOCKER_DIR/bin/"
chmod +x "$DOCKER_DIR/bin/lcdlln_server" "$DOCKER_DIR/bin/lcdlln_shard"
cp -f "$ROOT/db/schema.sql" "$DOCKER_DIR/db/schema.sql"
if [[ ! -d "$ROOT/db/migrations" ]]; then
  echo "ERROR: $ROOT/db/migrations introuvable (requis par lcdlln_server au démarrage)." >&2
  exit 1
fi
rm -rf "$DOCKER_DIR/db/migrations"
mkdir -p "$DOCKER_DIR/db/migrations"
cp -r "$ROOT/db/migrations/." "$DOCKER_DIR/db/migrations/"
if [[ -d "$ROOT/game/data" ]]; then
  rm -rf "$DOCKER_DIR/game/data"
  mkdir -p "$DOCKER_DIR/game/data"
  cp -r "$ROOT/game/data/." "$DOCKER_DIR/game/data/"
else
  echo "Warning: game/data missing" >&2
fi
echo "OK — bundle prêt dans $DOCKER_DIR (docker compose up -d)."
