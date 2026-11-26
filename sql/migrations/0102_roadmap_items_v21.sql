-- Migration 0102 — Roadmap items v21 : Lhynn en gradins et palais impérial
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Lhynn en gradins et le palais impérial';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Lhynn en gradins et le palais impérial' AS title,
         'La capitale de l''Empire s''élève désormais en gradins au bord de la mer de l''ouest, couronnée par le palais impérial : quatre terrasses, une falaise côté mer, et un escalier monumental qui monte de la grand-place jusqu''aux portes du palais. Façade gothique à statues colossales, hautes flèches, parvis aux braseros, salle du trône et jardins intérieurs ; la capitale est bien plus peuplée, avec des faubourgs hors les murs, de grands champs et un port où mouillent les navires.' AS description,
         'completed' AS status, 'monde' AS category, 80 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
