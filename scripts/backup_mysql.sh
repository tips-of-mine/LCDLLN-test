#!/usr/bin/env bash
# M21.5 — Backup MySQL quotidien : mysqldump --single-transaction + gzip, horodaté, rotation.
# Usage : BACKUP_DIR=/path RETAIN_DAYS=7 ./scripts/backup_mysql.sh
# Stockage configurable via BACKUP_DIR. Rotation : garder RETAIN_DAYS jours.

set -e

BACKUP_DIR="${BACKUP_DIR:-./backups}"
RETAIN_DAYS="${RETAIN_DAYS:-7}"
MYSQL_DATABASE="${MYSQL_DATABASE:-lcdlln_master}"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_FILE="${BACKUP_DIR}/${MYSQL_DATABASE}_${TIMESTAMP}.sql.gz"

mkdir -p "$BACKUP_DIR"

mysqldump --single-transaction --routines --triggers --events \
  "$MYSQL_DATABASE" | gzip -c > "$OUTPUT_FILE"

echo "[backup_mysql] Created: $OUTPUT_FILE"

# Rotation : supprimer les dumps de plus de RETAIN_DAYS jours
find "$BACKUP_DIR" -maxdepth 1 -name "${MYSQL_DATABASE}_*.sql.gz" -type f -mtime +"$RETAIN_DAYS" -delete
echo "[backup_mysql] Rotation: kept last $RETAIN_DAYS days in $BACKUP_DIR"
