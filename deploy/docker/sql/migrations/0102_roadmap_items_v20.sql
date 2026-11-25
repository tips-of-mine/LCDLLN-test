-- Migration 0102 — Roadmap items v20 : personnalisation des personnages
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Personnalisation des personnages';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Personnalisation des personnages' AS title,
         'À la création, chaque race jouable propose son visage, sa coiffure, la couleur des cheveux, des yeux et de la peau, la barbe et la moustache, les oreilles des elfes, les défenses des orcs, les cornes, la queue et les ailes des démons — tout est visible d''un coup d''œil, avec un aperçu qui zoome sur le visage. Nains, orcs, elfes et démons ont désormais leur propre silhouette, et les autres joueurs vous voient tel que vous vous êtes créé.' AS description,
         'completed' AS status, 'gameplay' AS category, 75 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
