-- Migration 0027 — FAQ items (questions/réponses pour le portail web)
-- Nouvelle table : faq_items
-- Permet la gestion d''une FAQ publique sur le portail web avec contrôle de publication

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

CREATE TABLE IF NOT EXISTS faq_items (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  question VARCHAR(500) NOT NULL
            COMMENT 'Question affichée dans la FAQ',
  answer TEXT NOT NULL
         COMMENT 'Réponse détaillée (plain text ou markdown)',
  category VARCHAR(100) NULL
           COMMENT 'Catégorie de la question (ex: gameplay, account, technical)',
  display_order INT NOT NULL DEFAULT 0
                COMMENT 'Ordre d''affichage (ascendant)',
  published TINYINT(1) NOT NULL DEFAULT 0
            COMMENT '0=brouillon, 1=publié',
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  KEY idx_faq_published (published),
  KEY idx_faq_category (category),
  KEY idx_faq_display_order (display_order)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='FAQ items publiés sur le portail web';

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
