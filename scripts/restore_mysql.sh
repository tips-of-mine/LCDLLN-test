#!/usr/bin/env bash
# M21.5 — Restauration MySQL depuis un dump compressé. Vérifie schema_version après restore.
# Usage : ./scripts/restore_mysql.sh /path/to/lcdlln_master_YYYYMMDD_HHMMSS.sql.gz
# La base doit exister (créer avec CREATE DATABASE si besoin). Voir docs/recovery_db.md.

set -e

if [ $# -lt 1 ]; then
  echo "Usage: $0 <backup.sql.gz>" >&2
  exit 1
fi

BACKUP_FILE="$1"
MYSQL_DATABASE="${MYSQL_DATABASE:-lcdlln_master}"

if [ ! -f "$BACKUP_FILE" ]; then
  echo "[restore_mysql] Error: file not found: $BACKUP_FILE" >&2
  exit 1
fi

echo "[restore_mysql] Restoring $BACKUP_FILE into $MYSQL_DATABASE..."
gunzip -c "$BACKUP_FILE" | mysql "$MYSQL_DATABASE"
echo "[restore_mysql] Restore done."

echo "[restore_mysql] Verifying schema_version..."
mysql "$MYSQL_DATABASE" -e "SELECT version, applied_at, checksum FROM schema_version ORDER BY version;"
echo "[restore_mysql] schema_version check OK."
