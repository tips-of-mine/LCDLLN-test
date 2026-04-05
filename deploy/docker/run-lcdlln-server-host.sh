#!/usr/bin/env bash
# Lance lcdlln_server hors Docker avec libmysqlclient du dossier lib/ du bundle.
#
# Les binaires du CI sont liés contre glibc ≥ 2.38 (Ubuntu 24.04 / ubuntu-latest).
# Sur un hôte plus ancien (Debian 12, Ubuntu 22.04, …) : utilisez le master Docker
#   docker compose up -d
# ou recompilez l’engine sur une cible alignée avec votre glibc.
#
# Usage : depuis deploy/docker —  ./run-lcdlln-server-host.sh -log -console
set -euo pipefail
DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
BIN="$DIR/bin/lcdlln_server"
MIN_GLIBC="2.38"

need_glibc() {
  local cur
  cur=$(getconf GNU_LIBC_VERSION 2>/dev/null | awk '{print $2}')
  if [[ -z "$cur" ]]; then
    return 0
  fi
  local first
  first=$(printf '%s\n' "$MIN_GLIBC" "$cur" | sort -V | head -n1)
  if [[ "$first" != "$MIN_GLIBC" ]]; then
    echo "lcdlln_server : glibc système ($cur) < $MIN_GLIBC (binaire CI)." >&2
    echo "→ Lancez le master dans Docker : docker compose up -d" >&2
    echo "→ Ou compilez sur une machine avec glibc ≥ $MIN_GLIBC." >&2
    exit 1
  fi
}

if [[ ! -x "$BIN" ]]; then
  echo "Introuvable ou non exécutable : $BIN" >&2
  exit 1
fi

need_glibc
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$BIN" "$@"
