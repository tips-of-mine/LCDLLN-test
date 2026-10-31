# SND.4 — Ambiances sonores (lieux, heure, météo)

## Objectif

Le monde était silencieux en dehors des pas et de l'interface. SND.4 ajoute un
fond sonore qui dépend de **l'endroit** où se trouve le joueur, de **l'heure**
et de la **météo** : oiseaux en forêt le jour, grillons et chouette la nuit,
grenouilles dans les marais, vent sur les sommets, ressac au bord de la mer,
eau qui court près d'une rivière, rumeur et forge en ville, pluie et tonnerre.

## Principe : des couches qui se mélangent

13 **couches** (boucles de 45 s sans couture) jouent en même temps, chacune à un
volume cible calculé deux fois par seconde. Les changements se font par fondus
de 2,5 à 3 s : on ne « saute » jamais d'une ambiance à l'autre en franchissant
une frontière, et une lisière forêt/plaine mélange naturellement les deux.

| Couche | Son | Quand |
|---|---|---|
| `amb_forest_day` | feuillage, chants d'oiseaux, pic-vert | biome forestier ou arbres denses (≤ 30 m), le jour |
| `amb_forest_night` | souffle léger, grillons épars, chouette | idem, la nuit |
| `amb_plains_day` | vent dans l'herbe, alouettes | ni forêt, ni marais, ni désert, ni haute montagne, le jour |
| `amb_plains_night` | vent léger, grillons | idem, la nuit |
| `amb_swamp` | grenouilles, insectes, bulles | marais (plus fort la nuit) |
| `amb_mountain` | vent fort qui siffle | biome montagne, ou altitude > 280 m (plein à 500 m) |
| `amb_snow` | vent glacé | montagne enneigée ou sol dominant = neige |
| `amb_desert` | vent sec, sable qui crépite | désert / zones arides |
| `amb_coast` | vagues, vent du large | côte, ou mer à moins de ~30 m |
| `amb_river` | eau qui court | rivière ou lac à moins de ~30 m (léger si le biome comporte une rivière) |
| `amb_town` | rumeur des rues, forge lointaine | dans une ville / un village (plus calme la nuit) |
| `amb_rain_light` | pluie fine | pluie (volume = intensité) |
| `amb_rain_heavy` | pluie battante | orage |

Règles de mélange (module pur `src/client/audio/Ambience.{h,cpp}`) :

- **Pluie** : oiseaux et insectes baissent de 70 % à pleine pluie.
- **Ville** : la nature (forêt, plaine, marais, désert) passe à 35 %.
- **Jour/nuit** : nuit avant 5 h et après 21 h, aube et crépuscule progressifs
  sur 1 h 30 (heure du cycle jour/nuit du jeu).
- **Orage** : pluie battante (≥ 60 %) et coups de tonnerre aléatoires toutes les
  12 à 40 s (`amb_thunder`, 3 variantes).
- Chaque volume est borné à [0, 1] puis multiplié par le volume du son dans le
  catalogue et par le bus **Ambience** (0,8).

## D'où viennent les informations

| Donnée | Source |
|---|---|
| Biome du lore | grille 40 × 40 (blocs de 250 m) exportée par `tools/world_gen` dans le `zone.json` de chaque cellule : `audio.biome_grid` / `audio.biome_grid_size` |
| Densité d'arbres | décor de la cellule (`GatherNearSegment`, rayon 30 m, 30 arbres = dense) |
| Eau | 8 sondes à 30 m autour du joueur (`QueryWater`) ; surface au niveau de la mer = mer, sinon rivière/lac |
| Ville | `places.json` (`CityAt`, rayon de l'agglomération) |
| Neige au sol | couche dominante de la carte des sols (SND.2) |
| Heure, météo | cycle jour/nuit et système météo du client |

> **Monde à régénérer** : la grille des biomes n'existe que dans les mondes
> générés après SND.4. La CI régénère le monde avant de construire le paquet,
> donc le nouveau paquet la contient. Un monde ancien fonctionne quand même :
> biome inconnu → plaine par défaut, forêt, eau, ville, altitude et météo
> restent détectées.

## Intégration moteur

`src/client/app/EngineAudio.cpp` :

- `BuildAmbienceInputs()` : rassemble les données ci-dessus pour la position du
  joueur.
- `UpdateAmbienceAudio(dt, inGame)` : recalcul toutes les 0,5 s ; démarre une
  boucle quand sa cible dépasse 0,01 (`PlayLoop`, fondu 3 s), ajuste son volume
  (`SetSoundVolume`, fondu 2,5 s), l'arrête à 0 (fondu 3 s). En quittant le jeu
  (retour aux menus) toutes les ambiances s'éteignent en 0,8 s.
- Appelé chaque image juste après la mise à jour de l'auditeur 3D ; désactivé
  dans l'éditeur monde.

Les boucles sont lues en **streaming** (pas de décodage complet en mémoire) :
≈ 8 Mo d'Ogg Vorbis pour les 16 fichiers.

## Génération des sons

`tools/audio_gen/sfx_ambience.py` (≈ 35 s) → `game/data/audio/ambience/*.ogg`.
Tout est synthétisé, sans échantillon externe :

- vent : bruit rose filtré, rafales lentes (`textures.wind`) ;
- oiseaux : sifflements glissés avec vibrato, trilles ; pic-vert = bloc de bois répété ;
- grillons : salves d'impulsions sur porteuse ~4,5 kHz ; chouette : sinus grave descendant ;
- grenouilles : train d'impulsions filtré par un formant grave ;
- eau : trois bandes de bruit modulées + bulles (glissandos montants très courts) ;
- vagues : houle grave + écume aiguë toutes les 7 à 10 s ;
- ville : voix figurées (bruit filtré modulé en syllabes) + enclume lointaine ;
- pluie : lit de bruit + milliers de gouttes (impulsions très brèves) ;
- tonnerre : craquement + grondement (`textures.thunder`), réverbéré.

Niveaux volontairement bas (−27 à −20 LUFS, crête ≤ −3 dB) : une ambiance
s'entend sous le jeu, elle ne le couvre pas. Les boucles sont « repliées »
(`dsp.wrap_loop`) : la fin se fond dans le début, pas de clic à la reprise.

## Réglages

- `zone_audio.json` : volume de chaque boucle (`volume`), volume du bus
  `Ambience` (0,8).
- Seuils (rayons, altitude, durées de fondu) : constantes en tête de
  `UpdateAmbienceAudio` et dans `AmbienceTargets`.

## Tests

- `ambience_tests` (C++) : familles de biomes, lecture de la grille, lumière du
  jour, couches attendues en forêt (jour/nuit), plaine sous la pluie, orage,
  ville (jour/nuit), neige, altitude, mer/rivière, monde sans grille, bornes.
- `audio_assets_tests` : chaque fichier du catalogue (dont les 16 ambiances)
  existe et se décode.
- `world_gen_tests` : la grille des biomes est exportée (40 × 40).
- `tools/audio_gen/tests` (`AmbienceTests`) : boucle de longueur exacte, pas de
  saturation, sonie < −18 LUFS.

## Limites connues

- Je ne peux pas écouter les sons : niveaux et mélange sont réglés par mesure
  (LUFS, crête). Les retours d'écoute se règlent dans `zone_audio.json`
  (volumes) ou dans `sfx_ambience.py` (contenu).
- Pas encore d'occlusion (intérieur d'une maison, grotte) : les ambiances
  extérieures continuent à l'intérieur.
- Les oiseaux et grillons sont dans la boucle (pas de sources 3D ponctuelles).
