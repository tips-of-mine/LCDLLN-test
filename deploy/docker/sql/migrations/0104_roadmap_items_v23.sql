-- Migration 0104 — Roadmap items v23 : Kendrhir d'après le tome 1 (lore v1.0.142)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Kendrhir, la cité des Grands Mages, se relève';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Kendrhir, la cité des Grands Mages, se relève' AS title,
         'Kendrhir retrouve le visage de la bande dessinée : un rempart de rondins de vingt mètres hérissé de tours de bois, avec ses portes et ses contreforts, des pans calcinés et des brèches laissés par le siège, et d''autres déjà relevés en bois neuf sous les échafaudages. Au nord, un donjon à flèche domine la ville, sa flèche encore en chantier. Les rues se bordent de hautes maisons de planches, et la plaine garde les restes du siège : tour d''assaut effondrée, mangonneaux brûlés, échelles abandonnées.' AS description,
         'completed' AS status, 'monde' AS category, 82 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
