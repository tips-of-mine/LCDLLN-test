-- Migration 0039 — Roadmap items v4 : capacites raciales et systeme de vitesse
--
-- Suite de 0037. Ajoute les items correspondant aux features DESIGN-validees
-- mais NON encore implementees. Permet de les afficher comme "planned" sur
-- le portail web pour visibilite roadmap publique.
--
-- Idempotent (INSERT IGNORE sur PK) ; reversible :
--   DELETE FROM roadmap_items WHERE id BETWEEN 25 AND 31.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;

START TRANSACTION;

INSERT IGNORE INTO roadmap_items (id, title, description, status, category, display_order) VALUES
(
  25,
  'Vitesse de deplacement par race',
  'Chaque race a un multiplicateur de vitesse different (elfes 1.10x, nains 0.85x, orcs 0.95x, morts-vivants 0.90x, demons 1.05x, chevaliers-dragons 1.00x, humains 1.00x). Infrastructure deja livree (cf. OrbitalCameraController::Update parametre speedMultiplier). Reste a migrer la table de multiplicateurs vers la DB races pour que les game-designers puissent tuner sans recompiler.',
  'in_progress',
  'gameplay',
  25
),
(
  26,
  'Vitesse de deplacement par terrain',
  'Le type de terrain affecte la vitesse : herbe 1.0x (defaut), terre/dirt 0.95x, rock 0.90x, sable/snow 0.65x. Hook deja en place dans OrbitalCameraController. Reste a implementer TerrainRenderer::SampleSpeedMultiplierAtWorldXZ qui lit le splat CPU (R=grass G=dirt B=rock A=snow) et retourne une moyenne ponderee.',
  'planned',
  'gameplay',
  26
),
(
  27,
  'Capacite raciale demon : VOL ou TELEPORT (XOR)',
  'A la creation d''un personnage demon, le joueur doit choisir UNE specialisation parmi DEUX (mutuellement exclusives) : (1) VOL : touche dediee F, ailes visibles dans le dos, mouvement vertical libre, gravite desactivee. (2) TELEPORT : touche dediee G, panneau UI HUD listant les zones DEJA visitees par le perso, cliquer pour s''y teleporter (cooldown serveur). Le choix est irreversible (sauf item rare/quete future). Persistance DB : nouvelle colonne characters.demon_specialization ENUM(''flight'',''teleport''). Validation serveur anti-triche : ne pas accepter une touche racial si la race ou la spec ne match pas.',
  'planned',
  'gameplay',
  27
),
(
  28,
  'Capacite raciale chevalier-dragon : MONTURE DRAGON',
  'A tout moment in-game, un chevalier-dragon peut invoquer son dragon comme monture (touche dediee, ex. G). Mesh dragon a creer (plus gros que l''avatar humanoide). Le perso ''monte'' sur le dragon (point d''attache visuel, anim assise). Vitesse de vol monture > vitesse de course du perso a pied. Etat ''mounted'' synchronise serveur. Anim invocation/dismiss du dragon.',
  'planned',
  'gameplay',
  28
),
(
  29,
  'Selecteur specialisation demon dans CharacterCreate',
  'Si le joueur choisit la race ''demons'' lors de CharacterCreate, l''UI affiche un radio-button supplementaire : (o) Ailes (vol) (o) Portail (teleport). Le choix est obligatoire (par defaut Ailes). Migration DB : characters.demon_specialization (NULL pour les non-demons).',
  'planned',
  'gameplay',
  29
),
(
  30,
  'Mesh ailes demon (placeholder puis final)',
  'Mesh ailes attache au dos de l''avatar quand un demon vole. Phase 1 : placeholder (deux cubes verticaux fixes). Phase 2 : vrai mesh ailes anime. Necessite un systeme d''attachements de mesh (point d''ancrage sur l''avatar) qui n''existe pas encore. Bloquant pour la capacite vol demon.',
  'planned',
  'gameplay',
  30
),
(
  31,
  'Tracking des zones visitees par personnage (pour teleport demon)',
  'Nouvelle table DB character_visited_zones (character_id, zone_id, first_visited_at). Marquage automatique cote serveur a chaque entree de zone. Lecture par l''UI HUD demon-teleport pour afficher la liste des destinations possibles. Cooldown serveur pour eviter le spam (ex. 30 s entre deux teleports). Bloquant pour la capacite teleport demon.',
  'planned',
  'gameplay',
  31
);

COMMIT;

SET FOREIGN_KEY_CHECKS = 1;
