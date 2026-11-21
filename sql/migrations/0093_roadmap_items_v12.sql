-- Migration 0093 — Roadmap items v12 : terres ouvertes remplies (prairies, bosquets, petits lieux)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Des plaines pleines de vie';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Des plaines pleines de vie' AS title,
         'Les plaines et les collines ne sont plus nues : herbes hautes et taches de fleurs sous les pas, bosquets et haies le long des chemins, et, au détour d''une colline, des cercles de pierres levées, des ruines de fermes, des campements abandonnés et des champs clôturés. Le décor reste visible plus loin à l''horizon.' AS description,
         'completed' AS status, 'monde' AS category, 68 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
