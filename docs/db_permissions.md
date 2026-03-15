# Permissions MySQL — Master vs Shard (M21.4)

Deux utilisateurs distincts sont utilisés pour respecter le principe du moindre privilège :

- **master_user** : serveur Master (auth, migrations, registre shards). Accès complet à la base.
- **shard_user** : serveur Shard (gameplay). Accès limité aux tables gameplay uniquement (pas d’accès aux comptes/sessions).

## Tables concernées

| Catégorie   | Tables              | master_user | shard_user |
|------------|---------------------|-------------|------------|
| Auth       | accounts, sessions  | RW          | —          |
| Gameplay   | characters (etc.)    | RW          | RW         |
| Registre   | shards              | RW          | —          |
| Migrations | schema_version      | RW          | —          |

## Installation

1. Créer la base et le schéma (si ce n’est pas déjà fait) :
   - appliquer `db/schema.sql` ou lancer les migrations via le Master (M21.3).
2. Exécuter le script utilisateurs en tant qu’administrateur (ex. `root`) :
   ```bash
   mysql -u root -p < db/users.sql
   ```
3. Changer les mots de passe par défaut (`CHANGEME_master`, `CHANGEME_shard`) :
   ```sql
   ALTER USER 'master_user'@'%' IDENTIFIED BY 'votre_mot_de_passe_master';
   ALTER USER 'shard_user'@'%' IDENTIFIED BY 'votre_mot_de_passe_shard';
   FLUSH PRIVILEGES;
   ```
4. Configurer le serveur Master avec `db.user` = `master_user` et le serveur Shard avec `db.user` = `shard_user` (voir `config.json` / variables d’environnement).

## Vérification DoD

- **shard_user ne peut pas SELECT/UPDATE accounts**  
  En tant que `shard_user` :
  ```sql
  SELECT * FROM lcdlln_master.accounts;
  ```
  → Erreur attendue (ex. `SELECT command denied`).

- **master_user peut migrer**  
  En tant que `master_user`, lancer le serveur Master : les migrations (M21.3) s’appliquent normalement (lecture/écriture de `schema_version` et exécution des scripts dans `db/migrations/`).

## Ajout de tables gameplay

Lorsqu’une nouvelle table purement gameplay (ex. `inventory`) est ajoutée au schéma, mettre à jour :

1. `db/users.sql` : ajouter une ligne du type  
   `GRANT SELECT, INSERT, UPDATE, DELETE ON lcdlln_master.<table> TO 'shard_user'@'%';`
2. Ce document : ajouter la table dans le tableau « Tables concernées ».
