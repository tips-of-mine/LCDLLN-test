-- Migration 0092 — Roadmap items v11 : routes sans doublons (lore v1.0.134, lot B)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Des routes sans doublons';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Des routes sans doublons' AS title,
         'Fini les routes en double entre deux mêmes villes : les routes qui partagent un trajet se rejoignent à un carrefour et empruntent la même chaussée, et les chemins des villages s''arrêtent à la route au lieu de la longer.' AS description,
         'completed' AS status, 'monde' AS category, 67 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
