-- Migration 0103 — Roadmap items v21 : le Camp de l'Oracle (salle de l'Oracle, salle des
-- Échos, rocher de cristal, camp extérieur)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Numéro 0103 : 0101 et 0102 sont pris par la branche de personnalisation des
-- personnages (feature/personnalisation-personnage), encore ouverte.
-- Réversible : DELETE FROM roadmap_items WHERE title IN ('Le Camp de l''Oracle : la salle de l''Oracle',
--   'Le Camp de l''Oracle : l''Épreuve des Échos');

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Le Camp de l''Oracle : la salle de l''Oracle' AS title,
         'Le rocher de l''Oracle devient un tumulus de cristal bleu, ses huit grandes portes peintes se dressent sur des socles de brique, et l''océan de tentes s''agrandit : tentes à hauteur d''homme, toiles de couleur, fanions, feux de camp la nuit et bassins des pèlerins. Répondez à l''appel de l''Oracle auprès du veilleur de la grande porte : la porte du sud-ouest s''ouvre alors sur la salle de l''Oracle, ses colonnes, son dais gravé et sa colonne de brume lumineuse.' AS description,
         'completed' AS status, 'monde' AS category, 79 AS display_order
  UNION ALL
  SELECT 'Le Camp de l''Oracle : l''Épreuve des Échos',
         'Derrière la porte de l''est du rocher, la salle des Échos attend les groupes d''au moins trois compagnons : un gardien de cristal et ses échos y veillent. Une épreuve à relever chaque semaine, confiée par la prêtresse du dais.',
         'completed', 'gameplay', 80
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
