# SND.6 — Sons de récolte et d'artisanat

## Objectif

Récolter un filon, couper un arbre ou forger une épée se faisait en silence.
SND.6 joue le geste en boucle tant que la barre avance, puis un son de fin.

## Ce qu'on entend

| Action | Boucle | Durée du cycle |
|---|---|---|
| filon de minerai (type 1) | `harvest_mining` : pioche sur la roche, éclats | 2 s (2 coups) |
| plante (type 2) | `harvest_herb` : feuillage froissé, tiges cassées | 3 s |
| arbre (type 3) | `harvest_wood` : hache, échardes | 2,4 s (2 coups) |
| dépouille d'animal (type 4) | `harvest_skinning` : couteau qui tranche | 2 s |
| forge (`blacksmithing`, `mining`, `engineering`) | `craft_forge` : marteau et enclume, braises | 2 s (fort-faible-fort-faible) |
| alchimie (`alchemy`, `cooking`) | `craft_alchemy` : bulles, frémissement, fioles | 4 s |
| cuir, couture (`leatherworking`, `tailoring`, `skinning`) | `craft_leather` : cuir étiré, alêne | 3 s |
| autre métier | `craft_generic` : petits coups de bois | 2 s |

Fins :

- récolte terminée → `harvest_complete` ;
- objet fabriqué → `craft_complete` ; qualité rare ou épique →
  `craft_complete_rare` (scintillement en plus) ;
- action annulée (déplacement, interruption) → la boucle s'arrête, pas de son de fin.

Le type de nœud vient de l'archétype répliqué (`kGatheringNodeArchetypeBase`
+ type de `game/data/gathering/resource_nodes.json`) ; le métier, de l'onglet
d'artisanat ouvert. Type inconnu → feuillage ; métier inconnu → coups de bois.

## Intégration

- Module pur `src/client/audio/InteractionAudio.{h,cpp}` : `PlanInteractionAudio`
  compare deux photos (récolte / artisanat) et renvoie la boucle voulue et les
  sons de fin. La récolte prime si les deux barres sont actives. Une fin ne
  sonne que sur une vraie transition (jamais à l'entrée en jeu).
- `src/client/app/EngineAudio.cpp` : `BuildInteractionAudioSnapshot` /
  `UpdateInteractionAudio` ; la boucle n'est relancée que si le geste change
  (fondu d'entrée 0,15 s, de sortie 0,25 s). Appelé chaque image ; tout se tait
  en quittant le jeu ; désactivé dans l'éditeur monde.
- Catalogue `zone_audio.json` : bus `SFX`, boucles à 0,7.

## Génération

`tools/audio_gen/sfx_interactions.py` → `game/data/audio/sfx/interactions/*.wav`
(11 fichiers mono). Boucles « repliées » (`dsp.wrap_loop`) : la réverbération
de fin de cycle revient au début, pas de clic à la reprise.

## Tests

- `interaction_audio_tests` (C++) : boucle par type de nœud et par métier,
  récolte en cours / terminée / annulée, objet normal / épique, priorité de la
  récolte, photo initiale ou invalide.
- `audio_assets_tests` : fichiers du catalogue présents et décodables.
- `tools/audio_gen/tests` (`InteractionSoundTests`) : mono, boucles de longueur
  exacte, pas de saturation.

## Limites connues

- Sons non écoutés par moi : réglages par mesure ; ajuster volumes
  (`zone_audio.json`) ou contenu (`sfx_interactions.py`) après écoute.
- Seul le joueur local est entendu (pas la récolte des autres joueurs).
