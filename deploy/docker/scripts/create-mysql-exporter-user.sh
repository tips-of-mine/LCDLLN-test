#!/usr/bin/env bash
# MON.1 — crée (ou met à jour) le compte MySQL `exporter` utilisé par
# lcdlln-mysql-exporter (métriques Prometheus). Idempotent.
#
# Usage (sur le serveur, depuis deploy/docker) : bash scripts/create-mysql-exporter-user.sh
#
# - Si MYSQL_EXPORTER_PASSWORD est absent de .env, un mot de passe aléatoire est
#   généré et AJOUTÉ à .env (jamais affiché).
# - Le compte n'a que les droits de lecture nécessaires aux métriques
#   (PROCESS, REPLICATION CLIENT, SELECT) et 3 connexions au plus.
# - Le mot de passe root est lu DANS le conteneur (variable MYSQL_ROOT_PASSWORD
#   du service mysql) : il ne transite pas par la ligne de commande de l'hôte.
# N'affiche que le résultat (aucun secret).
set -euo pipefail
cd "$(dirname "$0")/.."
CONTAINER="${MYSQL_CONTAINER:-lcdlln-mysql}"

touch .env
if ! grep -q '^MYSQL_EXPORTER_PASSWORD=.\+' .env; then
  sed -i '/^MYSQL_EXPORTER_PASSWORD=/d' .env
  echo "MYSQL_EXPORTER_PASSWORD=$(tr -dc 'A-Za-z0-9' </dev/urandom | head -c 32)" >> .env
  echo "[exporter] mot de passe généré et ajouté à .env"
fi
EXPORTER_PW="$(grep '^MYSQL_EXPORTER_PASSWORD=' .env | tail -1 | cut -d= -f2-)"

docker exec -i -e EXPORTER_PW="$EXPORTER_PW" "$CONTAINER" sh -c '
mysql -uroot -p"$MYSQL_ROOT_PASSWORD" 2>/dev/null <<SQL
CREATE USER IF NOT EXISTS '"'"'exporter'"'"'@'"'"'%'"'"' IDENTIFIED BY '"'"'$EXPORTER_PW'"'"' WITH MAX_USER_CONNECTIONS 3;
ALTER USER '"'"'exporter'"'"'@'"'"'%'"'"' IDENTIFIED BY '"'"'$EXPORTER_PW'"'"' WITH MAX_USER_CONNECTIONS 3;
GRANT PROCESS, REPLICATION CLIENT, SELECT ON *.* TO '"'"'exporter'"'"'@'"'"'%'"'"';
FLUSH PRIVILEGES;
SQL
' && echo "[exporter] compte MySQL exporter prêt sur $CONTAINER" || { echo "[exporter] ÉCHEC : vérifier que $CONTAINER tourne"; exit 1; }
