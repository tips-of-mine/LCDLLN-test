-- Exécuté une seule fois à l’init MySQL (volume vide), après 01_schema.sql.
-- Si schema_version.version=1 a encore l’ancien checksum factice, on l’aligne sur le SHA-256
-- réel de db/migrations/0001_init.sql (même valeur que dans schema.sql à jour).
UPDATE schema_version
SET checksum = 'e740eec07991bad0e5b8e13577ad7d0cf61ab5c652e2a9ef84b1565680cf45ae'
WHERE version = 1
  AND checksum = '0000000000000000000000000000000000000000000000000000000000000001';
