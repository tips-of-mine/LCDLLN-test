#!/usr/bin/env bash
# Corrige schema_version pour la migration 1 quand la base a été créée avec un ancien
# schema.sql (checksum factice 000…001) alors que le master attend le SHA-256 de 0001_init.sql.
#
# Usage (depuis deploy/docker, MySQL déjà démarré) :
#   chmod +x repair-schema-checksum.sh
#   ./repair-schema-checksum.sh
set -euo pipefail
cd "$(dirname "$0")"

if [[ -f .env ]]; then
	set -a
	# shellcheck disable=SC1091
	source .env
	set +a
fi

CK="e740eec07991bad0e5b8e13577ad7d0cf61ab5c652e2a9ef84b1565680cf45ae"
ROOT_PW="${MYSQL_ROOT_PASSWORD:-lcdlln_root_dev}"
DB_NAME="${MYSQL_DATABASE:-lcdlln_master}"

docker compose exec -T mysql mysql -uroot -p"${ROOT_PW}" "${DB_NAME}" \
	-e "UPDATE schema_version SET checksum='${CK}' WHERE version=1; SELECT version, LEFT(checksum,16) AS checksum_prefix FROM schema_version WHERE version=1;"

echo "OK — redémarrez le master : docker compose restart master"
