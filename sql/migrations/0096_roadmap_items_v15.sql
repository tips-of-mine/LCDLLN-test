-- Migration 0096 — Roadmap items v15 : atelier décors, tomes 6 à 14 (lore v1.0.138)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Nouveaux décors : fêtes, nécropoles et chantiers';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Nouveaux décors : fêtes, nécropoles et chantiers' AS title,
         'Les places des villes se parent de guirlandes de fanions ; les grandes villes ont un mausolée au fond de leur cimetière ; le désert cache de rares obélisques brisés et les pieds d''un colosse ; les forges naines tournent un grand engrenage et les chantiers dressent leur grue ; les champs de bataille gardent une table de campagne, un bûcher, un bélier couvert et des levées de terre. Bibliothèques, pupitres, tentures, armoires peintes, rosaces de vitrail et sphères armillaires rejoignent le mobilier.' AS description,
         'completed' AS status, 'monde' AS category, 74 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
