-- Migration 0071 — Table portal_sessions
--
-- Refactor sécurité critique : avant cette migration, l'authentification du
-- web-portal reposait sur deux cookies non signés (`lcdlln_portal_account` =
-- account_id en clair, `lcdlln_portal_role` = role en clair). Bien que
-- `httpOnly: true` empêchait le JS de page d'y toucher, DevTools / curl /
-- proxy / extensions pouvaient les modifier librement, ce qui permettait :
--   1. Impersonation totale : qui que ce soit pouvait poser
--      `lcdlln_portal_account=1` et se faire passer pour le compte 1 sans
--      aucun mot de passe. Les account_id étant auto-increment, deviner un
--      compte admin était trivial.
--   2. Escalation de privilèges : un joueur normal pouvait poser
--      `lcdlln_portal_role=SUPERADMIN` et accéder à 14 routes /api/admin/*
--      qui faisaient confiance au cookie sans re-vérifier en DB.
--
-- Nouveau modèle : un seul cookie `lcdlln_portal_session` contient un
-- session_token cryptographique random (256 bits, 64 chars hex). À chaque
-- requête authentifiée, `getSession()` fait une LECTURE DB sur cette table
-- pour matérialiser la session ; account_id et role viennent donc TOUJOURS
-- de la DB (table accounts via JOIN), jamais d'un cookie. Modifier le
-- cookie côté client ne permet plus rien : un token inexistant en DB
-- retourne null (= non authentifié), et il est impossible de générer un
-- token valide sans passer par /api/auth/login (qui exige des credentials).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

CREATE TABLE IF NOT EXISTS portal_sessions (
  id              BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  session_token   CHAR(64)        NOT NULL
                  COMMENT 'Hex 64 chars = 32 octets random = 256 bits. Généré par /api/auth/login via crypto.randomBytes(32).',
  account_id      BIGINT UNSIGNED NOT NULL
                  COMMENT 'Compte propriétaire de la session (FK vers accounts.id, CASCADE delete pour purger les sessions d''un compte supprimé).',
  created_at      DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_seen_at    DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP
                  ON UPDATE CURRENT_TIMESTAMP
                  COMMENT 'MAJ par getSession() à chaque requête authentifiée. Sert à expirer les sessions inactives indépendamment de expires_at fixe.',
  expires_at      DATETIME        NOT NULL
                  COMMENT 'Borne haute absolue de la session (login + 7 jours par défaut, cf. PORTAL_SESSION_MAX_AGE_DAYS).',
  user_agent      VARCHAR(255)    NULL
                  COMMENT 'Capture du User-Agent au login pour observabilité (audit, détection abus).',
  ip              VARCHAR(45)     NULL
                  COMMENT 'IP au login (IPv6 jusqu''à 45 chars). Idem audit.',
  PRIMARY KEY (id),
  UNIQUE KEY uk_session_token (session_token),
  KEY idx_account_id (account_id),
  KEY idx_expires_at (expires_at),
  CONSTRAINT fk_portal_sessions_account
    FOREIGN KEY (account_id) REFERENCES accounts (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Sessions authentifiées du web-portal. Cookie côté client = session_token random ; identité réelle (account_id, role) lue ici par lookup à chaque requête.';

SET FOREIGN_KEY_CHECKS = 1;
