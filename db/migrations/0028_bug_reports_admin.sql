-- Migration 0028 — Extensions table bug_reports pour le suivi admin
-- Ajout de colonnes : admin_status, admin_comment, exploit_awarded
-- Permet un suivi détaillé des signalements et leur résolution avec récompenses exploits

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

-- Admin status
SET @m28_c1 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'bug_reports' AND column_name = 'admin_status'
);
SET @m28_s1 := IF(@m28_c1 = 0,
  'ALTER TABLE bug_reports ADD COLUMN admin_status ENUM(''pending'',''confirmed'',''in_progress'',''resolved'',''not_a_bug'') NOT NULL DEFAULT ''pending'' COMMENT ''État du suivi admin''',
  'SELECT 1');
PREPARE m28_p1 FROM @m28_s1;
EXECUTE m28_p1;
DEALLOCATE PREPARE m28_p1;

-- Admin comment
SET @m28_c2 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'bug_reports' AND column_name = 'admin_comment'
);
SET @m28_s2 := IF(@m28_c2 = 0,
  'ALTER TABLE bug_reports ADD COLUMN admin_comment TEXT NULL COMMENT ''Commentaire de l''administrateur''',
  'SELECT 1');
PREPARE m28_p2 FROM @m28_s2;
EXECUTE m28_p2;
DEALLOCATE PREPARE m28_p2;

-- Exploit awarded flag
SET @m28_c3 := (
  SELECT COUNT(*) FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'bug_reports' AND column_name = 'exploit_awarded'
);
SET @m28_s3 := IF(@m28_c3 = 0,
  'ALTER TABLE bug_reports ADD COLUMN exploit_awarded TINYINT(1) NOT NULL DEFAULT 0 COMMENT ''0=non octroyé, 1=exploit octroyé au compte''',
  'SELECT 1');
PREPARE m28_p3 FROM @m28_s3;
EXECUTE m28_p3;
DEALLOCATE PREPARE m28_p3;

SET FOREIGN_KEY_CHECKS = 1;
