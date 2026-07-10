-- Migration 0073 — Roadmap items v5 : rattrapage des systemes de jeu livres
--
-- Suite de 0037/0039. La roadmap publique (portail web) s'etait arretee aux
-- features de mai 2026 (vue 3e personne, menu pause, selection de race, chat,
-- audio) plus les capacites raciales DESIGN (0039, planned). Depuis, de
-- nombreux GRANDS systemes de jeu ont ete livres : personnages, combat,
-- competences, quetes, metiers, economie, social, fenetre Personnage unifiee,
-- meteo, et l'equipement d'objets. Cette migration les ajoute a la roadmap.
--
-- Convention : status 'completed' = livre en jeu ; 'in_progress' = en cours de
-- livraison (PR ouvertes / deploiement lock-step a faire). La categorie n'est
-- pas affichee sur le portail (metadonnee de tri uniquement).
--
-- On repousse aussi les 7 items raciaux PLANIFIES (ids 25-31, migration 0039)
-- en fin de timeline (display_order +200) pour que la roadmap se lise
-- "historique livre" -> "futur planifie".
--
-- Idempotent (INSERT IGNORE sur PK) ; reversible :
--   DELETE FROM roadmap_items WHERE id BETWEEN 32 AND 47;
--   UPDATE roadmap_items SET display_order = display_order - 200 WHERE id BETWEEN 25 AND 31;

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- Repousse les items planifies (raciaux) apres l'historique livre.
UPDATE roadmap_items SET display_order = display_order + 200 WHERE id BETWEEN 25 AND 31;

INSERT IGNORE INTO roadmap_items (id, title, description, status, category, display_order) VALUES
(
  32,
  'Systeme de personnages complet',
  'Factions, races et classes jouables avec statistiques derivees recalculees de facon deterministe cote serveur (anti-triche) : points de vie, ressource secondaire, degats, precision, portee, critique, vitesses, endurance, perception, discretion. Feuille de personnage poussee au client a l''entree en monde.',
  'completed',
  'gameplay',
  32
),
(
  33,
  'Systeme de combat',
  'Combat serveur-autoritaire : attaques au corps a corps et a distance, resolution des jets de precision et de critique, degats derives des stats, creatures/monstres avec archetypes data-driven et table de menace repliquee.',
  'completed',
  'combat',
  33
),
(
  34,
  'Sorts et competences par classe',
  'Kits de sorts par profil de classe (data-driven), catalogue strict des competences par classe avec deblocage par palier/niveau, et Grimoire : barre d''action de 10 emplacements remappables par le joueur.',
  'completed',
  'combat',
  34
),
(
  35,
  'Systeme de quetes',
  'Quetes data-driven : acceptation et rendu aupres des PNJ (dialogue), objectifs multiples (collecte, elimination, parler), recompenses (objets, or, experience), persistance du suivi par personnage, et bulles/marqueurs de quete.',
  'completed',
  'gameplay',
  35
),
(
  36,
  'Suivi de quetes HUD et experience',
  'Tracker de quetes affiche sous le radar (bascule clavier), barre d''experience du joueur avec progression de niveau poussee par le serveur, et selection de la quete suivie sur le radar.',
  'completed',
  'ui',
  36
),
(
  37,
  'Metiers : recolte et artisanat',
  'Recolte de ressources sur des nodes repliques (barre de progression serveur), et artisanat : professions connues avec niveaux de competence, recettes, barre de fabrication, gains de competence et paliers de qualite.',
  'completed',
  'gameplay',
  37
),
(
  38,
  'Economie : marchands et monnaie',
  'PNJ marchands avec stock fini, monnaie a trois pieces (or / argent / bronze, ratio 100:1) affichee dans le HUD et la bourse, et porte-monnaie serveur-autoritaire.',
  'completed',
  'economie',
  38
),
(
  39,
  'Commerce entre joueurs et hotel des ventes',
  'Echange direct securise entre deux joueurs (fenetre de troc avec verrou/confirmation et fenetre anti-arnaque), et hotel des ventes : mise en vente, encheres, achat immediat, livraison par courrier.',
  'completed',
  'economie',
  39
),
(
  40,
  'Courrier en jeu',
  'Boite aux lettres par personnage : envoi et reception de messages, pieces jointes (objets, or), livraison des gains d''enchere hors ligne, fusion a la connexion.',
  'completed',
  'social',
  40
),
(
  41,
  'Guildes, groupes et amis',
  'Creation et gestion de guilde (tabard, banque de guilde), formation de groupes avec composition, PV/ressource des membres et mode de butin, liste d''amis et gestion des joueurs ignores.',
  'completed',
  'social',
  41
),
(
  42,
  'Fenetre Personnage unifiee',
  'Une seule fenetre a onglets (touche F1) regroupant l''apercu 3D du personnage, l''inventaire, les caracteristiques, la monnaie, les competences, les techniques (Grimoire) et l''arbre de classe.',
  'completed',
  'ui',
  42
),
(
  43,
  'Meteo et nuages volumetriques',
  'Systeme meteo dynamique avec nuages volumetriques rendus par raymarching, cycle jour/nuit, ombres au sol et ambiance de ciel couvert.',
  'completed',
  'gameplay',
  43
),
(
  44,
  'Anti-occlusion camera',
  'Le decor et le terrain situes entre le joueur et la camera deviennent transparents (fondu trames type screen-door) pour ne jamais boucher la vue du personnage.',
  'completed',
  'ui',
  44
),
(
  45,
  'Choix de la langue au premier lancement',
  'Ecran de selection de la langue au tout premier lancement, proposant une union filtree {langue systeme, langue du pays detecte par geo-IP, anglais}. Geolocalisation differee apres la chauffe du rendu (stabilite au demarrage).',
  'completed',
  'ui',
  45
),
(
  46,
  'Marqueurs de reapparition',
  'Points de reapparition (cimetiere, auberge) materialises en jeu par des labels et des anneaux, avec choix de la destination la plus proche a la mort. Validation serveur de la position de respawn (anti-triche).',
  'completed',
  'gameplay',
  46
),
(
  47,
  'Systeme d''equipement d''objets',
  'Catalogue d''objets equipables sur 10 emplacements (tete, torse, jambes, pieds, mains, arme, bouclier, amulette, deux anneaux). Equiper un objet modifie les statistiques derivees du personnage (calcul serveur-autoritaire : l''emplacement et le bonus proviennent du catalogue serveur, jamais du client). Panneau d''equipement et infobulles de bonus dans la fenetre Personnage. Persistance de l''equipement par personnage.',
  'in_progress',
  'gameplay',
  47
);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
