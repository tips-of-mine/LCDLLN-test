-- Migration 0107 — Roadmap items v26 : Lhynn et l'Oracle plus fidèles, vues lointaines
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title IN ('La salle du trône de Lhynn',
--   'L''Oracle tel que dans la bande dessinée', 'Des villes visibles de loin');

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'La salle du trône de Lhynn' AS title,
         'Au sommet de Lhynn, la très haute porte du palais s''ouvre sur une longue nef couverte : une grande porte de bois sculptée et dorée, deux rangées de statues colossales encapuchonnées, une rosace, et tout au fond le trône de l''Empereur, en hauteur, au sommet d''une pyramide de marches couverte d''un tapis rouge.' AS description,
         'completed' AS status, 'monde' AS category, 85 AS display_order
  UNION ALL
  SELECT 'L''Oracle tel que dans la bande dessinée',
         'Le rocher de l''Oracle est maintenant un immense bloc bleu arrondi au milieu de la plaine craquelée, sans rien posé dessus. Deux portes seulement y donnent accès, la grande porte du sud-ouest et celle de l''est ; de grandes tentes pyramidales se dressent devant l''entrée et les menhirs bleus sont épars dans la plaine.',
         'completed', 'monde', 86
  UNION ALL
  SELECT 'Des villes visibles de loin',
         'Les villes ne disparaissent plus au-delà de quelques centaines de mètres : leurs toits et leurs remparts restent visibles de loin, si bien qu''on découvre une capitale en entier en arrivant. La distance de vue est allongée à tous les niveaux de qualité, les nuages ne se posent plus sur les collines et les fleurs ont retrouvé leurs couleurs.',
         'completed', 'graphismes', 87
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
