-- Migration 0007 — CGU / Terms of Service (versionnées, multilingues, acceptations joueur).
-- Appliquer après 0006. Contenu édité via outil d’admin (écriture SQL/API), pas via le client jeu.
--
-- Modèle :
--   terms_editions        : une ligne par version logique (v1.0, v2.0), statut et date de publication.
--   terms_localizations   : texte par (edition, langue).
--   account_terms_acceptances : historique des acceptations (compliance).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- ---------------------------------------------------------------------------
-- Éditions de CGU (une version affichée comme v1.0, v2.0, …)
-- status: draft | published | retired
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS terms_editions (
  id             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  version_label  VARCHAR(32)     NOT NULL
                 COMMENT 'e.g. v1.0, v1.1, v2.0 (display + ordering lexicographic ok for semver-like)',
  published_at   TIMESTAMP       NOT NULL
                 COMMENT 'effective visibility start',
  status         ENUM('draft','published','retired') NOT NULL DEFAULT 'draft',
  created_at     TIMESTAMP       NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at     TIMESTAMP       NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_terms_editions_status_published (status, published_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Terms of service editions (CGU)';

-- ---------------------------------------------------------------------------
-- Contenu localisé (titre + corps). Fallback langue géré côté serveur (ex. en).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS terms_localizations (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  edition_id  BIGINT UNSIGNED NOT NULL,
  locale      VARCHAR(16)     NOT NULL COMMENT 'ISO 639-1 or BCP47 prefix, e.g. en, fr, pt-BR',
  title       VARCHAR(512)    NOT NULL DEFAULT ''
              COMMENT 'short title for UI',
  content     MEDIUMTEXT      NOT NULL
              COMMENT 'full CGU body (plain text or markdown; client renders)',
  PRIMARY KEY (id),
  UNIQUE KEY uq_terms_loc_edition_locale (edition_id, locale),
  CONSTRAINT fk_terms_loc_edition
    FOREIGN KEY (edition_id) REFERENCES terms_editions (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Localized CGU text per edition';

-- ---------------------------------------------------------------------------
-- Acceptations utilisateur (pas de FK vers accounts : master peut utiliser store mémoire).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS account_terms_acceptances (
  account_id   BIGINT UNSIGNED NOT NULL,
  edition_id   BIGINT UNSIGNED NOT NULL,
  accepted_at  TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  ip_address   VARCHAR(45)     NULL DEFAULT NULL COMMENT 'optional client IP at accept time',
  user_agent   VARCHAR(512)    NULL DEFAULT NULL,
  PRIMARY KEY (account_id, edition_id),
  KEY idx_terms_accept_edition (edition_id),
  CONSTRAINT fk_terms_accept_edition
    FOREIGN KEY (edition_id) REFERENCES terms_editions (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Per-account acceptance of a given CGU edition';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
