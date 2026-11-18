-- Migration 0087 — Roadmap items v6 : rattrapage juillet → novembre 2026
--
-- Suite de 0073 (10/07/2026). La roadmap publique du portail ne suivait plus
-- les livraisons depuis juillet. Cette migration :
--   1. passe à « livré » les items restés « en cours » alors qu'ils sont livrés
--      (équipement d'objets, pages du portail, guildes et amis) ;
--   2. ajoute les systèmes livrés depuis (monde des Royaumes, carte à découvrir,
--      Enfers, PNJ d'ambiance, bateaux, cinématiques, eau, anniversaires,
--      cimetières et lieux secrets, sauvegarde fiable des personnages,
--      hôtel des ventes et courrier en base, commandes de jeu et modération) ;
--   3. ajoute les chantiers en cours et planifiés (monde plus vivant, profondeur
--      de vue, optimisation du moteur).
--
-- Convention (inchangée) : 'completed' = livré en jeu ; 'in_progress' = en
-- cours de livraison ; 'planned' = décidé, pas commencé. La catégorie n'est pas
-- affichée sur le portail (métadonnée de tri).
-- Ordre d'affichage : l'historique livré à la suite de 0073 (48 →), puis les
-- chantiers en cours et planifiés après les capacités raciales planifiées
-- (225-231, cf. 0073) : 240 →.
--
-- IDs NON imposés : l'administration du portail (/admin/roadmap) peut avoir
-- créé des items depuis juillet. Chaque insertion est idempotente PAR TITRE
-- (INSERT … SELECT … WHERE NOT EXISTS), sans risque de collision d'id.
--
-- Réversible :
--   UPDATE roadmap_items SET status = 'in_progress' WHERE id IN (6, 7, 8, 9, 10, 19, 47);
--   DELETE FROM roadmap_items WHERE title IN (… titres ci-dessous …);

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

-- 1. Items restés « en cours » alors qu'ils sont livrés. Garde `status =
-- 'in_progress'` : un statut déjà modifié depuis /admin/roadmap n'est pas écrasé.
--    47      équipement d'objets (persistance inventaire + équipement, 0075) ;
--    6 à 10  portail : espace joueur, refonte, administration, contrôle
--            parental, confidentialité (pages /player/* et /admin/* en place) ;
--    19      guildes et amis (doublon de l'item 41 « Guildes, groupes et amis »,
--            livré en 0073).
UPDATE roadmap_items SET status = 'completed'
WHERE id IN (6, 7, 8, 9, 10, 19, 47) AND status = 'in_progress';

-- 2 et 3. Nouveaux items (idempotents par titre).
INSERT INTO roadmap_items (title, description, status, category, display_order)
SELECT v.title, v.description, v.status, v.category, v.display_order
FROM (
  SELECT 'Monde ouvert des Royaumes' AS title,
         'Le continent des Royaumes en monde continu : cellules de 10 km chargées à la volée autour du joueur, régions, villes, lieux et routes générés depuis les données de lore, départs propres à chaque faction.' AS description,
         'completed' AS status, 'monde' AS category, 48 AS display_order
  UNION ALL SELECT 'Carte du monde à découvrir',
         'Carte du monde couverte d''un brouillard levé au fil de l''exploration, mémorisé par personnage (cases de 62,5 m), avec routes et bâtiments révélés.',
         'completed', 'monde', 49
  UNION ALL SELECT 'Les Enfers',
         'Seconde carte du jeu : la Citadelle de la Légion, ses lieux et ses plans, reliés aux Royaumes par des portails appariés. Un territoire hostile aux humains.',
         'completed', 'monde', 50
  UNION ALL SELECT 'Villes vivantes : PNJ d''ambiance',
         'Villageois, gardes, marchands et voyageurs suivent des routines et un agenda selon l''heure du jour : ils se lèvent, travaillent, font leur ronde et rentrent la nuit.',
         'completed', 'monde', 51
  UNION ALL SELECT 'Bateaux et traversées',
         'Navires sur les routes maritimes : embarquement, traversée avec les autres passagers, débarquement au port d''arrivée.',
         'completed', 'gameplay', 52
  UNION ALL SELECT 'Cinématiques narratives',
         'Séquences de caméra et de sons déclenchées par l''histoire, pour mettre en scène les moments forts des quêtes.',
         'completed', 'gameplay', 53
  UNION ALL SELECT 'Rendu de l''eau',
         'Eau retravaillée : couleur selon la profondeur près des rivages, houle et courant, reflets du ciel et du décor, rives adoucies.',
         'completed', 'graphismes', 54
  UNION ALL SELECT 'Récompenses d''anniversaire',
         'Exploits et cadeaux pour l''anniversaire d''inscription et l''anniversaire du joueur, dont des gâteaux aux effets temporaires.',
         'completed', 'exploits', 55
  UNION ALL SELECT 'Exploits des cimetières et lieux secrets',
         'Un exploit pour qui découvre les 666 cimetières du monde, et des lieux secrets à trouver, suivis par personnage.',
         'completed', 'exploits', 56
  UNION ALL SELECT 'Progression des personnages sécurisée',
         'La base de données est la seule source de vérité des personnages : un seul serveur à la fois peut les modifier, les échanges entre joueurs sont journalisés, et un instantané restaurable est pris à chaque déconnexion.',
         'completed', 'infrastructure', 57
  UNION ALL SELECT 'Hôtel des ventes et courrier fiabilisés',
         'Annonces de l''hôtel des ventes conservées en base de données ; or joint aux courriers transféré de façon sûre, sans perte ni duplication.',
         'completed', 'economie', 58
  UNION ALL SELECT 'Commandes de jeu et modération',
         'Commandes d''aide pour les joueurs, et commandes réservées aux maîtres de jeu et modérateurs (déplacement, informations, personnages, objets, monde, quêtes, modération, serveur), avec droits par rôle et journal d''audit.',
         'completed', 'administration', 59
  UNION ALL SELECT 'Un monde plus peuplé',
         'Bien plus de PNJ et de créatures à l''écran sans perte de fluidité : figurants dans les villes, foule et faune gérées par le jeu lui-même, détail adapté à la distance. Première étape livrée : le classement des habitants du monde par rôle.',
         'in_progress', 'monde', 240
  UNION ALL SELECT 'Optimisation du moteur',
         'Travail continu sur les performances du jeu et des serveurs : processeur, mémoire et rendu graphique, sans perte de fonctionnalité.',
         'in_progress', 'infrastructure', 241
  UNION ALL SELECT 'Voir plus loin',
         'Horizon lointain, silhouettes des villes à grande distance, vie visible au loin (fumées, fenêtres éclairées, oiseaux) et profondeur de champ dans les dialogues, à coût de rendu constant.',
         'planned', 'graphismes', 242
) AS v
WHERE NOT EXISTS (SELECT 1 FROM roadmap_items r WHERE r.title = v.title);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
