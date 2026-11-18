#!/usr/bin/env bash
# PORTAL.1 — publie les affiches du monde dans le portail web du bundle Docker.
#
# Entrée  : $PORTAL_MAPS_DIR/<monde>/portal_map.{png,json}, produits par
#           `lcdlln_world_gen --portal-out <dossier>/<monde>` (CI, build-linux.yml).
# Sortie  : deploy/docker/web-portal/public/maps/<monde>/
#             portal_map.json        étiquettes (sans secrets)
#             portal_map.webp        pleine définition
#             portal_map_1600.webp   aperçu de la page /monde
#           Repli sans ImageMagick : portal_map.png seul (le portail sait l'afficher).
# Absence d'affiche (build local sans --portal-out) : rien n'est publié, la page
# /monde affiche « carte bientôt disponible ». Ne fait jamais échouer le bundle.
#
# Appelé par pack-linux-docker-bundle.sh APRÈS sync-web-portal-to-docker-deploy.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="${PORTAL_MAPS_DIR:-$ROOT/build/portal-maps}"
DST_ROOT="$ROOT/deploy/docker/web-portal/public/maps"

if [[ ! -d "$SRC_DIR" ]]; then
  echo "[portal-maps] $SRC_DIR absent : aucune carte publiée (page /monde : bientôt disponible)."
  exit 0
fi

CONVERT=""
if command -v magick >/dev/null 2>&1; then CONVERT="magick"
elif command -v convert >/dev/null 2>&1; then CONVERT="convert"
fi

for dir in "$SRC_DIR"/*/; do
  world="$(basename "$dir")"
  png="$dir/portal_map.png"
  json="$dir/portal_map.json"
  if [[ ! -s "$png" || ! -s "$json" ]]; then
    echo "[portal-maps] $world : affiche incomplète, ignorée."
    continue
  fi
  dst="$DST_ROOT/$world"
  rm -rf "$dst"
  mkdir -p "$dst"
  cp "$json" "$dst/portal_map.json"
  if [[ -n "$CONVERT" ]] \
    && "$CONVERT" "$png" -quality 82 "$dst/portal_map.webp" \
    && "$CONVERT" "$png" -resize 1600x1600 -quality 80 "$dst/portal_map_1600.webp"; then
    echo "[portal-maps] $world : WebP publiés ($(du -h "$dst/portal_map.webp" | cut -f1) + aperçu $(du -h "$dst/portal_map_1600.webp" | cut -f1))."
  else
    rm -f "$dst"/*.webp
    cp "$png" "$dst/portal_map.png"
    echo "[portal-maps] $world : ImageMagick/WebP indisponible, PNG publié tel quel."
  fi
done
