#!/usr/bin/env sh
# Lance lcdlln_server hors Docker en utilisant libmysqlclient du dossier lib/ du bundle.
# Usage : depuis deploy/docker —  ./run-lcdlln-server-host.sh -log -console
DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$DIR/bin/lcdlln_server" "$@"
