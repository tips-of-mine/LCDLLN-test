#!/bin/sh
set -eu
if [ -d /app/host-config ]; then
	if [ -d /app/host-config/shard.config.json ]; then
		echo "lcdlln-shard: deploy/docker/config/shard.config.json est un répertoire ; supprimez-le et restaurez le fichier JSON." >&2
		exit 1
	fi
	if [ ! -f /app/host-config/shard.config.json ]; then
		echo "lcdlln-shard: fichier absent : config/shard.config.json (dossier monté : /app/host-config)." >&2
		exit 1
	fi
	cp /app/host-config/shard.config.json /app/config.json
fi
exec /app/lcdlln_shard "$@"
