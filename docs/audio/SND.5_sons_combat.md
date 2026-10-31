# SND.5 — Sons de combat et de sorts

## Objectif

Le combat était muet. SND.5 fait entendre chaque coup porté ou reçu, les
ratés, les critiques, les morts, les incantations et les auras, plus un
battement de cœur quand la vie du joueur est basse.

## Principe : comparer deux « photos » de l'état de combat

Comme les sons d'interface (SND.3), aucun code réseau n'est touché. À chaque
image, Engine prend une photo de l'état de combat répliqué par le serveur et la
compare à la précédente (`src/client/audio/CombatAudioCues.{h,cpp}`, module pur) :

| Changement observé | Son | Où |
|---|---|---|
| nouvelle entrée du journal de combat, le joueur frappe | impact selon sa classe (ci-dessous) | position de la cible |
| … coup critique | + `combat_crit` (impact lourd) | idem |
| … raté (du joueur ou sur lui) | `combat_whoosh` | 2D (joueur) / attaquant |
| le joueur encaisse | `combat_player_hurt` (+ `combat_hit_claw` si créature) | 2D |
| combat entre tiers (autre joueur contre un mob) | impact discret (60 %) | cible |
| une entité passe à 0 PV | `combat_death` (chute d'un corps) | position de l'entité |
| le joueur passe à 0 PV | `player_death` (gong grave) | 2D |
| début d'incantation | `spell_cast` (charge, 1,4 s) | 2D |
| fin d'incantation (≥ 85 % de la durée) | `spell_release` | 2D |
| incantation coupée avant | `spell_fizzle` | 2D |
| nouvelle aura sur le joueur | `aura_buff` ou `aura_debuff` | 2D |
| PV < 25 % | `combat_heartbeat` (boucle) | 2D |

Impact selon le profil de classe du joueur :

| Profil | Son |
|---|---|
| `melee`, `tank`, `voleur` | `combat_hit_blade` (tintement + chair) |
| `distance`, `pisteur` | `combat_hit_arrow` (sifflement + flèche qui se plante) |
| `lanceur`, `healer`, `sacre` | `combat_hit_magic` (décharge + étincelles) |
| autre / inconnu | `combat_hit_blunt` (coup sourd) |

Auras néfastes : dégâts sur la durée, dégâts subis augmentés, ralentissement
(`SpellEffectType` 1, 5, 7). Toutes les autres sont des bienfaits.

Garde-fous :

- la première photo (entrée en jeu, changement de personnage) ne sonne pas ;
- une entrée de journal déjà vue ne rejoue jamais (comparaison des numéros de
  séquence, insensible à l'ordre ou à la troncature du journal) ;
- une entité qui apparaît déjà morte ne sonne pas ;
- au plus 6 sons par image, les plus importants d'abord (mort du joueur, coup
  encaissé, critique, mort, sorts, impacts, auras, ratés) : une grosse attaque
  de zone ne fait pas un mur de son.

## Intégration moteur

`src/client/app/EngineAudio.cpp` :

- `BuildCombatAudioSnapshot()` : journal de combat, PV et type (créature ou
  non) des entités visibles, barre d'incantation, auras du joueur.
- `UpdateCombatAudio(inGame)` : joue les sons ; position 3D = position lissée
  de l'entité (+ 1 m, hauteur du torse), sinon position du snapshot ; entité hors
  de vue → son 2D atténué. Gère la boucle du cœur (fondu 0,4 s / 0,8 s). Tout se
  tait en quittant le jeu.
- Appelé chaque image après l'ambiance (SND.4) ; désactivé dans l'éditeur monde.

Catalogue `zone_audio.json` : bus `SFX`, distance 2 m (plein) → 40 m (muet),
3 variantes par impact, petites variations de hauteur et de volume à chaque
coup.

## Génération des sons

`tools/audio_gen/sfx_combat.py` → `game/data/audio/sfx/combat/*.wav`
(30 fichiers mono 48 kHz, ≈ 2,5 Mo). Tout est synthétisé :

- chair / cuir : bruit grave très amorti + petit corps sinusoïdal ;
- lame : air fendu + chair + tintement métallique bref ;
- flèche : sifflement aigu + claquement + vibration de la hampe ;
- magie : glissando descendant + étincelles ;
- griffes : trois déchirures de bruit aigu ;
- critique : impact sub-grave + métal ;
- mort : deux chocs de corps + gravier + équipement (chaînes) ;
- mort du joueur : gong grave + impact ;
- incantation : souffle qui monte + accord tremblé qui s'élève ;
- auras : arpège clair montant (bienfait), descente mineure et souffle grave
  (malédiction) ;
- cœur : deux battements graves, boucle exacte d'1 s (60 bpm).

## Tests

- `combat_audio_cues_tests` (C++) : silence initial, impacts par profil,
  critique prioritaire, raté, coup encaissé (créature), combat entre tiers,
  journal réordonné, morts (entité, joueur, déjà morte), incantation
  (début / lâché / interrompue), auras, plafond par image, changement de
  personnage, battement de cœur.
- `audio_assets_tests` : chaque fichier du catalogue existe et se décode.
- `tools/audio_gen/tests` (`CombatSoundTests`) : mono, court, sans saturation,
  cœur = boucle exacte.

## Limites connues

- Je ne peux pas écouter les sons : réglages par mesure ; retours d'écoute à
  appliquer dans `zone_audio.json` (volumes) ou `sfx_combat.py` (contenu).
- Pas de son propre par sort ni par arme équipée (le profil de classe décide) ;
  pas de cris ni de voix (créatures, personnages).
- Les sorts instantanés (sans barre d'incantation) n'ont que l'impact.
