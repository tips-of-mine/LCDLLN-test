-- Migration 0098 — Roadmap items v17 : bruits de pas refaits selon le sol
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Des pas qui sonnent juste';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Des pas qui sonnent juste' AS title,
         'Fini le bruit de tambour sur les routes : chaque pas fait désormais entendre le sol sous la botte. La terre battue des routes et des rues, la terre meuble, l''herbe qui froisse, le gravier qui crisse, la pierre, le marbre blanc de l''Oracle, la neige tassée, le sable, la boue qui colle, le clapotis d''un gué et le plancher qui craque ont chacun leur son, avec plus de variantes pour ne jamais entendre deux fois le même pas.' AS description,
         'completed' AS status, 'gameplay' AS category, 76 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
