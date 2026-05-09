-- Migration 0037 — Roadmap items v3 : ajout des features livrees aujourd'hui
--
-- Suite de 0034. Cette migration ajoute les items de roadmap correspondant
-- aux features livrees lors de la session du 1er mai 2026 sur la PR #419
-- ("fix-server-ip-display-3ZZOn") :
--
--   * Vue a la 3eme personne (camera orbitale + avatar 3D placeholder +
--     locomotion + collision sol + synchro reseau du mouvement perso).
--   * Menu pause in-game (touche Echap) avec Quitter / Options / Deconnecter.
--   * Selection de race a la creation de personnage (combo UI + persistance
--     race_str + seed table races avec les 6 races jouables).
--   * Conditions Generales d'Utilisation : page d'acceptation operationnelle.
--
-- Items volontairement omis car non user-facing : tous les fixes UI
-- (centrage CGU, hauteur Options, glyphes manquants Windlass, etc.) sont
-- des polish details, pas des grandes etapes de roadmap.
--
-- Idempotent (INSERT IGNORE sur PK) ; reversible
-- (DELETE FROM roadmap_items WHERE id BETWEEN 21 AND 24).

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT IGNORE INTO roadmap_items (id, title, description, status, category, display_order) VALUES
(
  21,
  'Vue 3eme personne',
  'Camera orbitale autour du personnage (clic droit pour pivoter, molette pour zoomer), avatar visible dans le monde, collision avec le sol, animations placeholder de marche/course, synchronisation de la position avec le serveur.',
  'completed',
  'gameplay',
  21
),
(
  22,
  'Menu pause in-game',
  'Touche Echap ouvre un menu superpose au monde avec les actions Reprendre / Options / Se deconnecter / Quitter. Plus de fermeture brutale du client.',
  'completed',
  'gameplay',
  22
),
(
  23,
  'Selection de race a la creation',
  'Combo de choix de race lors de la creation d''un personnage. Six races jouables : humains, elfes, orcs (dzorak), nains, demons, chevaliers-dragons. La race choisie est persistee cote DB et affichee dans la liste de selection des personnages.',
  'completed',
  'gameplay',
  23
),
(
  24,
  'Conditions Generales d''Utilisation',
  'Ecran d''acceptation des CGU avec texte defilant, case d''accuse de lecture, et boutons Refuser / Accepter. Editions multilingues, traçabilite des acceptations par compte, gestion admin via le portail.',
  'completed',
  'auth',
  24
);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
