-- Migration 0101 — Roadmap items v20 : vagues de la mer et plages
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Vagues sur la mer et plages de sable';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Vagues sur la mer et plages de sable' AS title,
         'La mer n''est plus plate : la houle la soulève au large et s''apaise près du bord, et à chaque vague l''eau monte sur la plage dans un rouleau d''écume avant de se retirer, au son du ressac. On flotte sur la vague qu''on voit en nageant. Les plages prennent la couleur du sable, plus sombre et luisant là où les vagues l''ont mouillé. Les lacs, eux, restent calmes.' AS description,
         'completed' AS status, 'graphismes' AS category, 79 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
