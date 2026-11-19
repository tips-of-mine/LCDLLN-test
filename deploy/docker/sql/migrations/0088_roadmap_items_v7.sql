-- Migration 0088 — Roadmap items v7 : cartes du monde (PORTAL.1) et galerie des villes et lieux (PORTAL.2) sur le portail
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title IN ('Cartes du monde sur le portail', 'Villes et lieux vus du ciel');

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Cartes du monde sur le portail' AS title,
         'Nouvelle page « Le monde » : les cartes des Royaumes et des Enfers telles qu''elles existent dans le jeu, avec les grandes villes, les régions et quelques lieux remarquables. Mises à jour automatiquement à chaque version du monde ; les lieux secrets n''y figurent pas.' AS description,
         'completed' AS status, 'portail' AS category, 60 AS display_order
  UNION ALL SELECT 'Villes et lieux vus du ciel',
         'Galerie du portail : chaque ville et chaque lieu construit du monde, vu du ciel tel qu''il existe dans le jeu (rues, maisons, remparts, routes, forêts), avec sa description, région par région. Sans les lieux secrets.',
         'completed', 'portail', 61
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
