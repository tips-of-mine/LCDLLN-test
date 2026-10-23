#!/usr/bin/env bash
# Assemble sous deploy/docker/ un bundle prêt pour docker compose :
#   binaires + sql + game/data + lib MySQL + sources web-portal (image web-portal au docker compose up).
# Usage : depuis la racine du dépôt, après build Release Linux :
#   BUILD_DIR=build/linux-x64-release ./scripts/pack-linux-docker-bundle.sh
#
# Prérequis pack : paquet runtime libmysqlclient21 (Debian/Ubuntu) pour copier libmysqlclient.so.21 dans lib/.
# Schéma + migrations : voir scripts/sync-db-to-docker-deploy.sh (également exécuté en CI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/linux-x64-release}"
DOCKER_DIR="$ROOT/deploy/docker"

bash "$ROOT/scripts/sync-db-to-docker-deploy.sh"
bash "$ROOT/scripts/sync-web-portal-to-docker-deploy.sh"

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

mkdir -p "$DOCKER_DIR/bin" "$DOCKER_DIR/game/data"
pack_mysql_client_libs
cp -f "$BUILD_DIR/pkg/server/lcdlln_server" "$BUILD_DIR/pkg/server/lcdlln_shard" "$DOCKER_DIR/bin/"
chmod +x "$DOCKER_DIR/bin/lcdlln_server" "$DOCKER_DIR/bin/lcdlln_shard"
if [[ -d "$ROOT/game/data" ]]; then
  rm -rf "$DOCKER_DIR/game/data"
  mkdir -p "$DOCKER_DIR/game/data"
  # Monde « royaumes » : le master et le shard ne lisent dans les cellules que les
  # .json / .txt (zone, spawners, respawn…). Les données de RENDU générées
  # (heightmap, splat, herbe, décor, bâtiments, eau : ~40 Mo par cellule, plusieurs
  # Go au total) restent dans le seul paquet client : exclues DÈS la copie (pas de
  # copie puis suppression, qui doublerait l'espace disque du runner).
  (cd "$ROOT/game/data" && tar -cf - \
    --exclude='./zones/cell_*/*.r16h' --exclude='./zones/cell_*/*.slap' --exclude='./zones/cell_*/*.grms' \
    --exclude='./instances/zone_cell_*' .) | (cd "$DOCKER_DIR/game/data" && tar -xf -)
else
  echo "Warning: game/data missing" >&2
fi
echo "OK — bundle prêt dans $DOCKER_DIR (docker compose up -d)."
