-- Migration 0026 — Roadmap items (points de développement du projet)
-- Nouvelle table : roadmap_items (14 items de base définis dans le projet)
-- Permet une gestion simplifiée de la roadmap publique via le portail web

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

CREATE TABLE IF NOT EXISTS roadmap_items (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  title VARCHAR(255) NOT NULL
         COMMENT 'Titre du point de roadmap',
  description TEXT NULL
              COMMENT 'Description détaillée',
  status ENUM('completed','in_progress','planned') NOT NULL DEFAULT 'planned'
         COMMENT 'État: complété, en cours, ou planifié',
  category VARCHAR(100) NULL
           COMMENT 'Catégorie du point (ex: backend, frontend, infrastructure)',
  display_order INT NOT NULL DEFAULT 0
                COMMENT 'Ordre d''affichage sur le portail',
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  KEY idx_roadmap_status (status),
  KEY idx_roadmap_display_order (display_order)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Points de roadmap du projet';

-- Insertion des 14 items de base (idempotent avec INSERT IGNORE)
INSERT IGNORE INTO roadmap_items (title, description, status, category, display_order) VALUES
(
  'Infrastructure de base',
  'Mise en place du serveur master, base MySQL, Docker et pipeline CI/CD.',
  'completed',
  'infrastructure',
  1
),
(
  'Système d''exploits',
  'Catalogue global, déblocage par compte/personnage, statistiques globales, paliers bug reports.',
  'completed',
  'gameplay',
  2
),
(
  'Récupération de mot de passe',
  'Profil de récupération, questions secrètes, tokens temporaires, envoi d''e-mail via SMTP.',
  'completed',
  'auth',
  3
),
(
  'Portail web (v1)',
  'Next.js 14, pages publiques, affichage des exploits, administration CGU.',
  'completed',
  'web',
  4
),
(
  'Formulaire de signalement de bugs',
  'Soumission authentifiée, catégorisation, paliers exploits.',
  'completed',
  'web',
  5
),
(
  'Authentification & Espace Joueur',
  'Fix auth, middleware, espace joueur complet avec profil, chroniques, contrôle parental, sécurité et vie privée.',
  'in_progress',
  'auth',
  6
),
(
  'Refonte UI/UX du portail',
  'Design moderne, navigation responsive, animations, pages enrichies.',
  'in_progress',
  'web',
  7
),
(
  'Interface Admin complète',
  'Panel administration : joueurs, CGU, roadmap, FAQ, suivi bugs.',
  'in_progress',
  'admin',
  8
),
(
  'Contrôle Parental',
  'Validation parentale pour les joueurs mineurs, blocage compte si non validé.',
  'in_progress',
  'auth',
  9
),
(
  'Visibilité de profil & Vie Privée',
  'Paramètres de confidentialité du profil joueur et gestion des CGU.',
  'in_progress',
  'auth',
  10
),
(
  'Monitoring & état des serveurs',
  'Dashboard temps réel affichant l''état des serveurs de jeu, le nombre de joueurs connectés et les événements planifiés.',
  'planned',
  'infrastructure',
  11
),
(
  'Système de tickets support',
  'Ouverture de tickets, suivi, réponse admin, historique par compte.',
  'planned',
  'support',
  12
),
(
  'Notifications & annonces',
  'Annonces globales, notifications par compte, alertes de maintenance et de mise à jour.',
  'planned',
  'web',
  13
),
(
  'Système MFA',
  'Authentification multi-facteurs (TOTP ou email).',
  'planned',
  'auth',
  14
);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
