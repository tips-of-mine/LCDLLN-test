#!/bin/sh
set -eu
# Monte ./config vers /app/host-config (fichier, pas /app/config.json) pour éviter l’erreur OCI
# « mount a directory onto a file » quand master.config.json manquait et qu’un dossier a été créé.
if [ -d /app/host-config ]; then
	if [ -d /app/host-config/master.config.json ]; then
		echo "lcdlln-master: deploy/docker/config/master.config.json est un répertoire (souvent après un premier lancement sans fichier)." >&2
		echo "Corrigez sur l’hôte : rm -rf config/master.config.json puis restaurez le vrai fichier JSON depuis le dépôt ou le zip CI." >&2
		exit 1
	fi
	if [ ! -f /app/host-config/master.config.json ]; then
		echo "lcdlln-master: fichier absent : config/master.config.json (dossier monté : /app/host-config)." >&2
		exit 1
	fi
	cp /app/host-config/master.config.json /app/config.json
fi
exec /app/lcdlln_server "$@"
