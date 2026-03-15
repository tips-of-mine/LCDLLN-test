# Récupération MySQL — Backup / Restore (M21.5)

Runbook pour les sauvegardes quotidiennes, la rotation et la restauration de la base `lcdlln_master`.

## Backup

- **Script** : `scripts/backup_mysql.sh`
- **Méthode** : `mysqldump --single-transaction` + gzip. Fichiers horodatés : `lcdlln_master_YYYYMMDD_HHMMSS.sql.gz`.
- **Stockage** : configurable via la variable d’environnement `BACKUP_DIR` (défaut : `./backups`).
- **Rotation** : conservation des dumps sur les **N derniers jours** (défaut `RETAIN_DAYS=7`).

### Variables d’environnement

| Variable        | Défaut           | Description                    |
|----------------|------------------|--------------------------------|
| `BACKUP_DIR`   | `./backups`      | Répertoire de sortie des dumps |
| `RETAIN_DAYS`  | `7`              | Nombre de jours à conserver     |
| `MYSQL_DATABASE` | `lcdlln_master` | Nom de la base à sauvegarder    |

### Exemple (depuis la racine du repo)

```bash
export BACKUP_DIR=/var/backups/mysql
export RETAIN_DAYS=14
./scripts/backup_mysql.sh
```

À planifier en cron (ex. quotidien à 2h) :

```cron
0 2 * * * cd /path/to/repo && BACKUP_DIR=/var/backups/mysql ./scripts/backup_mysql.sh
```

---

## Restore

- **Script** : `scripts/restore_mysql.sh <fichier_backup.sql.gz>`
- Après restauration, le script **vérifie** la table `schema_version` (affichage des versions appliquées).

### Restauration sur une base vide

1. Créer la base (si elle n’existe pas) :
   ```bash
   mysql -u root -p -e "CREATE DATABASE IF NOT EXISTS lcdlln_master CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
   ```
2. Lancer la restauration :
   ```bash
   ./scripts/restore_mysql.sh /var/backups/mysql/lcdlln_master_20250315_020000.sql.gz
   ```
3. Le script affiche le contenu de `schema_version` ; vérifier que les versions attendues sont présentes.

### Variables d’environnement (restore)

| Variable        | Défaut           | Description              |
|----------------|------------------|--------------------------|
| `MYSQL_DATABASE` | `lcdlln_master` | Base dans laquelle restaurer |

---

## Critères de validation DoD

- **Backup créé et rotaté** : exécuter le script backup, vérifier la présence du `.sql.gz` dans `BACKUP_DIR`, puis attendre ou modifier les dates de fichiers pour vérifier que les anciens dumps sont supprimés après `RETAIN_DAYS`.
- **Restore sur DB vide** : créer une base vide, exécuter `restore_mysql.sh` avec un backup valide, confirmer que les données et `schema_version` sont corrects.
