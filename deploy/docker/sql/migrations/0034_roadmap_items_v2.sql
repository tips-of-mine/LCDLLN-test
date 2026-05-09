-- Migration 0034 — Enrichissement de la roadmap publique
-- Ajout d'items reflétant les grandes features livrées depuis la migration 0026 :
--   chat in-game, audio runtime, système de personnages, reconnexion automatique.
-- Mise à jour du statut de quelques items existants devenus pertinents à requalifier.
--
-- Idempotent : INSERT IGNORE sur (title) (clé unique implicite via title déjà présent).
-- Réversible : aucune suppression de données existantes ; seuls des UPDATE de statut
-- sont effectués sur des titres explicites.
--
-- Items volontairement omis (outils internes / non user-facing) :
--   * World Editor (mode --editor)
--   * Editor Hub overlay ImGui
--   * Logging JSONL structuré
--   * Refactoring serveur, opcodes protocole, etc.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- Nouveaux items (display_order 15+)
INSERT IGNORE INTO roadmap_items (title, description, status, category, display_order) VALUES
(
  'Système de chat in-game',
  'Canaux global, zone, guilde, amis et messages privés (whisper). HUD overlay ImGui post-authentification, persistance des messages et routage par le serveur master.',
  'completed',
  'gameplay',
  15
),
(
  'Audio immersif',
  'Ambiances sonores par zone, sons de pas adaptés à la couche de terrain (splat), gestion des distances et boucles. Configuration via éditeur de monde.',
  'completed',
  'gameplay',
  16
),
(
  'Système de personnages',
  'Création, suppression réversible (soft-delete), sauvegarde automatique de la position et de la progression, identité persistante côté serveur (uint64 character_id de bout en bout), strings de race et classe.',
  'completed',
  'gameplay',
  17
),
(
  'Reconnexion automatique',
  'En cas de coupure réseau ou de bascule de shard, la session master et le client gameplay se reconnectent automatiquement sans perte de progression.',
  'completed',
  'infrastructure',
  18
),
(
  'Système de guildes & amis',
  'Création de guilde, gestion des membres et rangs, liste d''amis avec invitations, présence en ligne, canaux de chat dédiés.',
  'in_progress',
  'gameplay',
  19
),
(
  'Boutique & monnaie en or',
  'Échange entre joueurs, monnaie de jeu (or), portail de voyage inter-mondes payant, transactions journalisées côté serveur.',
  'planned',
  'gameplay',
  20
);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
