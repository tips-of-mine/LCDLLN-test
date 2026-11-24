-- Migration 0097 — Roadmap items v16 : carte du monde en parchemin (touche M)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Carte du monde en parchemin';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Carte du monde en parchemin' AS title,
         'La carte du monde s''ouvre comme un vrai parchemin : elle se déroule entre deux rouleaux de bois, au bruit de la feuille qu''on déroule, et se range de même. Elle n''est plus encombrée de noms : celui d''une ville, d''un hameau, d''un lieu ou d''une région n''apparaît qu''au passage du curseur.' AS description,
         'completed' AS status, 'monde' AS category, 75 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
