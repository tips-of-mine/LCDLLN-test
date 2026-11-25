-- Migration 0099 — Roadmap items v18 : rendu lointain et océan de tentes de l'Oracle (lore v1.0.139)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title IN ('Paysages lointains plus nets', 'L''océan de tentes de l''Oracle');

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Paysages lointains plus nets' AS title,
         'De loin, les arbres gardent leur silhouette, les champs se lisent comme de vraies cultures et le relief se fond dans le ciel, de jour comme de nuit, au lieu de grands aplats blancs ou gris.' AS description,
         'completed' AS status, 'graphismes' AS category, 77 AS display_order
  UNION ALL
  SELECT 'L''océan de tentes de l''Oracle',
         'Autour du rocher de l''Oracle, des milliers de tentes de pèlerins s''étendent désormais en quinze rangs, avec leurs fanions, leurs feux et leurs bagages.',
         'completed', 'monde', 78
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
