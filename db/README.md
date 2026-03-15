# Base de données — Schéma initial (M21.1) et migrations (M21.2)

## Prérequis

- **MySQL 8.0** (client et serveur).

## Initialiser une base vierge

1. Démarrer le serveur MySQL 8.0.

2. Appliquer le schéma (DB vierge) :

   **Depuis un shell (Windows ou Linux) :**
   ```bash
   mysql -u root -p < schema.sql
   ```
   Ou en indiquant l’hôte et le port :
   ```bash
   mysql -h 127.0.0.1 -P 3306 -u root -p < schema.sql
   ```

   **Depuis le client MySQL :**
   ```sql
   SOURCE /chemin/vers/db/schema.sql;
   ```

3. Le script crée la base `lcdlln_master` si elle n’existe pas, puis les tables :
   - `accounts` (auth, unique email/login)
   - `sessions` (optionnel persistant)
   - `characters` (placeholder, max 5 par compte)
   - `shards` (pour M22)
   - `schema_version` (version 1 insérée).

## Vérification

Après application, vérifier la version du schéma :

```sql
USE lcdlln_master;
SELECT * FROM schema_version;
```

Résultat attendu : une ligne avec `version = 1`.

## Migrations versionnées (M21.2)

Les fichiers de migration sont dans `db/migrations/` (convention : `0001_init.sql`, `0002_...`, etc.). Voir `db/migrations/README.md` pour la convention et les règles.

Pour calculer le checksum SHA-256 de chaque fichier (pour vérification ou intégration avec un applicateur) :

```bash
# depuis la racine du projet (ou en passant le chemin du dossier migrations)
./pkg/tools/migration_checksum
# ou
./pkg/tools/migration_checksum db/migrations
```

Sortie : une ligne par fichier `version\tchecksum_hex\tpath`. Un applicateur (ex. M21.3) compare ces checksums à la table `schema_version` pour détecter les migrations en attente et pour échouer au démarrage en cas de mismatch.
