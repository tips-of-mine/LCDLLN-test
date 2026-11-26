-- Migration 0103 — Roadmap items v22 : Magistrya, la ville-tour du Magister
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Magistrya, la ville-tour du Magister';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Magistrya, la ville-tour du Magister' AS title,
         'Magistrya se dresse désormais au milieu de son lac : une tour de pierre de 250 mètres qui s''enroule comme un serpent autour d''un pic rocheux. On y arrive par un long ponton de bois gardé par des gibets, puis on monte par une rampe en spirale, d''escalier en escalier, entre les maisons, les tourelles et les arches. Au socle, les geôles voûtées de la ville ; au sommet, le palais du Magister, sa salle du trône aux colonnes torsadées et son laboratoire.' AS description,
         'completed' AS status, 'monde' AS category, 81 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
