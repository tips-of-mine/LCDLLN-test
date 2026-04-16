#!/usr/bin/env bash
# Après export runtime depuis le World Editor (zones/<ZONE_ID>/ incluant layout_from_editor.json),
# invoque zone_builder pour générer chunks/ et méta-zone packagée.
#
# Variables d'environnement :
#   ZONE_ID           (obligatoire)
#   LCDLLN_BUILD_DIR  optionnel, défaut : build/linux-x64
#   ZONE_BUILDER      optionnel, chemin absolu vers l'exécutable zone_builder
#
# Usage (depuis la racine du dépôt) :
#   export ZONE_ID=ma_zone
#   ./tools/world/export_zone_with_chunks.sh

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

if [[ -z "${ZONE_ID:-}" ]]; then
  echo "Erreur: définir ZONE_ID (ex. export ZONE_ID=ma_zone)" >&2
  exit 1
fi

BUILD_DIR="${LCDLLN_BUILD_DIR:-build/linux-x64}"

if [[ -n "${ZONE_BUILDER:-}" && -x "$ZONE_BUILDER" ]]; then
  ZB="$ZONE_BUILDER"
else
  ZB="$ROOT/$BUILD_DIR/pkg/zone_builder/zone_builder"
fi

if [[ ! -x "$ZB" ]]; then
  echo "Erreur: zone_builder introuvable ou non exécutable: $ZB" >&2
  echo "Compiler le projet ou définir ZONE_BUILDER (chemin absolu)." >&2
  exit 1
fi

CONFIG="$ROOT/config.json"
if [[ ! -f "$CONFIG" ]]; then
  echo "Erreur: config.json introuvable: $CONFIG" >&2
  exit 1
fi

LAYOUT_REL="zones/${ZONE_ID}/layout_from_editor.json"
echo "[export_zone_with_chunks] zone_builder: $ZB"
echo "[export_zone_with_chunks] layout (relatif content): $LAYOUT_REL"
echo "[export_zone_with_chunks] output: zones/${ZONE_ID}"

"$ZB" --layout "$LAYOUT_REL" --output "zones/${ZONE_ID}" --zone-id "$ZONE_ID" --config "$CONFIG"
echo "[export_zone_with_chunks] OK (code 0)."
