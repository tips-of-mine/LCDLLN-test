-- Migration 0091 — Roadmap items v10 : lacs, carte et bord du monde (lore v1.0.133, lot A)
--
-- Règle CLAUDE.md « Roadmap publique » : une fonctionnalité visible livrée met à
-- jour la roadmap dans la même PR. Insertion idempotente par titre, sans id imposé.
-- Réversible : DELETE FROM roadmap_items WHERE title = 'Lacs, carte et bord du monde';

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Lacs, carte et bord du monde' AS title,
         'Tous les lacs de la carte sont visibles en jeu, quelle que soit leur forme. La carte (M) s''adapte à la taille de sa fenêtre et suit votre personnage ; les carrés et le trait parasites ont disparu. Les remparts épousent le relief sans plus bloquer les joueurs, et la mer s''étend au-delà des côtes au lieu d''un vide gris.' AS description,
         'completed' AS status, 'monde' AS category, 66 AS display_order
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
