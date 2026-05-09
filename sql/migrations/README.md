# Migrations SQL versionnées (M21.2)

## Convention de nommage

- Fichiers : `NNNN_nom_descriptif.sql` (ex. `0001_init.sql`, `0002_add_x.sql`).
- `NNNN` = numéro de version sur 4 chiffres, **unique par fichier** : le `MigrationRunner` ne retient qu’un script par numéro ; deux `0010_*.sql` faisaient en sorte qu’une des deux migrations ne s’appliquait jamais (ordre non déterministe).
- Ordre d’application : tri par `NNNN` (puis ordre des fichiers pour un même numéro — à éviter : un numéro = un fichier).

## Checksum

- Chaque fichier de migration a un **checksum SHA-256** (64 caractères hexadécimaux) calculé sur le contenu entier du fichier.
- Lors de l’application, ce checksum est enregistré dans la table `schema_version` (colonne `checksum`).
- **Règle de sécurité :** au démarrage (ou avant d’appliquer de nouvelles migrations), recalculer le checksum de chaque fichier déjà appliqué et le comparer à la valeur en base : en cas de **mismatch**, déclencher un arrêt contrôlé (fail startup).

## Règles

- **Immuabilité :** une migration déjà mergée ne doit plus être modifiée (pas d’édition de 0001_init.sql après merge).
- **Nouvelle migration :** tout changement de schéma ou de données se fait via un **nouveau** fichier (0002_..., 0003_..., etc.).

## Détection des migrations en attente

- **Pending :** tout fichier `NNNN_*.sql` dont le numéro `NNNN` est strictement supérieur au `version` max présent dans `schema_version`.
- L’outil `migration_checksum` (voir `tools/migration_checksum`) liste les fichiers dans l’ordre et affiche leur checksum ; un applicateur (ex. M21.3) peut comparer avec la base pour savoir quelles migrations appliquer et pour vérifier qu’aucun checksum appliqué ne diffère du fichier (mismatch => arrêt).

## Application dans une transaction

Chaque migration doit être exécutée dans une transaction si le moteur le permet (MySQL : `START TRANSACTION;` … `COMMIT;`). En cas d’erreur : `ROLLBACK;`.
