-- Migration 0088 — Roadmap items v7 : cartes du monde sur le portail (PORTAL.1)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Cartes du monde sur le portail';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Cartes du monde sur le portail' AS title,
         'Nouvelle page « Le monde » : les cartes des Royaumes et des Enfers telles qu''elles existent dans le jeu, avec les grandes villes, les régions et quelques lieux remarquables. Mises à jour automatiquement à chaque version du monde ; les lieux secrets n''y figurent pas.' AS description,
         'completed' AS status, 'portail' AS category, 60 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
