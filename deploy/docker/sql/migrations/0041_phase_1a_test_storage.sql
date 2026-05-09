-- 0041 — Phase 1a CMANGOS Database : table de test minimaliste pour SQLStorage<T>.
-- Read-only après seed initial. Utilisée par engine/server/db/SQLStorageTests.cpp.

CREATE TABLE IF NOT EXISTS phase_1a_test_storage (
  entry        INT UNSIGNED NOT NULL,
  name         VARCHAR(64) NOT NULL,
  value        INT NOT NULL,
  PRIMARY KEY (entry)
);

-- Seed 3 lignes idempotentes (UPSERT pattern via INSERT IGNORE).
INSERT IGNORE INTO phase_1a_test_storage (entry, name, value) VALUES
  (1, 'alpha', 100),
  (2, 'beta', 200),
  (3, 'gamma', 300);
