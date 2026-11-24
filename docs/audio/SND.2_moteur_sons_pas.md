# SND.2 — Lecture réelle des sons et pas du personnage

Avant SND.2, seule la musique du menu sortait des haut-parleurs : `AudioEngine`
calculait une atténuation pour chaque son « joué » sans rien envoyer à la carte son.

## Moteur de lecture (`src/client/audio/SoundDevice.{h,cpp}`)

- Ouvre le périphérique audio avec miniaudio (`ma_engine`), déjà compilé dans
  `MaMenuMusic.cpp` (avec le décodeur Ogg Vorbis).
- **Bus** : un groupe miniaudio par bus (`SFX`, `UI`, `Music`, `Weather`, `Ambience`…) ;
  les volumes du menu Options (général, musique, effets, interface) s'y appliquent.
- **Sons ponctuels** : décodés une fois puis gardés en cache ; 2D (interface) ou 3D
  (atténuation linéaire entre `minDistance` et `maxDistance`, panoramique selon
  l'orientation de la caméra).
- **Boucles** (ambiances) : lues en flux, avec fondus d'entrée, de volume et de sortie.
- **Limite de voix** (`audio.max_voices`, défaut 64) : au-delà, un son ponctuel de plus
  est ignoré (une boucle passe toujours).
- **Sans carte son** (ou `audio.sfx_device_enabled = false`) : le catalogue est chargé,
  les appels ne font rien, le jeu reste jouable.

## Catalogue (`game/data/audio/zone_audio.json`)

Nouveaux champs d'un son :

| Champ | Rôle |
|---|---|
| `variants` | fichiers alternatifs, tirés au hasard sans répéter le précédent |
| `volume` | gain de base |
| `pitchJitter` / `volumeJitter` | variation aléatoire de hauteur / volume (ex. 0,06 = ±6 %) |
| `stream` | lecture en flux (fichiers longs) |

Le son de test `zone_0_sfx_test` et l'ambiance `zone_0_ambience` (fichiers jamais
livrés) sont retirés ; les ambiances arrivent avec SND.4.

## Pas du personnage

- **Cadence** (`src/client/audio/Footsteps.{h,cpp}`, `FootstepCadence`) : un pas à
  chaque foulée parcourue ; la foulée s'allonge avec la vitesse (marche ≈ 0,75 m,
  course ≈ 1,3 m, sprint ≈ 1,6 m). Réception audible après ≥ 0,35 s en l'air ; à la
  nage, une brasse toutes les 1,1 s. Les pas de marche sont plus discrets que ceux
  de course.
- **Sol sous le pied** (SND.2b) : la NATURE du sol de chaque texel du splat est
  écrite par `tools/world_gen` dans `zones/<c>/terrain_ground.gndm` (format
  `src/shared/world/GroundMap.h`, zone.json `render.terrain.ground_map`) et gardée
  côté CPU (`src/client/world/cells/CellSurfaceMap`, ≈ 4 Mo par cellule affichée,
  construite sur le thread de chargement). Sans ce fichier (monde généré avant
  SND.2b, zone de l'éditeur), repli sur la couche dominante du splat (herbe, terre,
  roche, neige). Au-dessus du terrain (> 0,3 m : plancher, pont) : bois. Pied dans
  l'eau sans nager (profondeur ≥ 4 cm) : eau. Nage : brasse.
- **Sons** : voir « SND.2b » ci-dessous.
- Intégration : `Engine::UpdateFootstepAudio` et `Engine::UpdateAudioListener`
  (`src/client/app/EngineAudio.cpp`), appelés après le contrôleur de personnage et
  avec la caméra.

## SND.2b — sons refaits et nature du sol

**Défaut corrigé** : en SND.2, l'appui du talon était un sinus grave (≈ 100 Hz)
amorti, joué deux fois (talon puis pointe) : sur la terre (donc sur les routes et
les rues, peintes en « terre » dans le splat), la roche et le bois, 75 à 85 % de
l'énergie du son était sous 200 Hz et le pas sonnait comme un tambour.

**Synthèse** (`tools/audio_gen/sfx_footsteps.py`) : l'appui est un impact de bruit
filtré (aucune hauteur), suivi du déroulé du pied et de la pointe, plus la texture
du sol. 6 variantes par sol refait (4 inchangées pour la nage, le blé et le maïs,
identiques octet pour octet).

| Nature du sol | Où | Son | Fichiers |
|---|---|---|---|
| Terre battue | routes, rues, places | appui sec et compact, un peu de grit | `packed_earth_1..6` |
| Terre | taches de terre, sous-bois clairs | appui étouffé, mottes qui s'effritent | `dirt_1..6` |
| Herbe | prairies, forêts | froissement des brins | `grass_1..6` |
| Gravier | terres arides, montagnes, volcans (terre au splat) | crissement de cailloux | `gravel_1..6` |
| Pierre | roche, falaises de craie, roche de l'Oracle, croûte de lave | claquement sec | `rock_1..6` |
| Marbre | pierre blanche du Camp de l'Oracle | éclat net et brillant | `marble_1..6` |
| Neige | neige des sommets | crissement de compression | `snow_crunch_1..6` |
| Sable | déserts, plages | glissement granuleux feutré | `sand_1..6` |
| Boue | marais, berges, rivages hors plage | claque mouillée, succion | `mud_squelch_1..6` |
| Eau | pied dans l'eau sans nager | claque, remous, gouttes | `water_1..6` |
| Bois | plancher, pont, quai (> 0,3 m) | planche creuse, grincement une fois sur trois | `wood_1..6` |

Classification (`ClassifyGround`, `tools/world_gen/lib/Output.cpp`), par priorité :
marbre et roche peints (Oracle), rues et routes, couche dominante neige ou roche
(craie → pierre), rivage (sable sur les côtes, boue ailleurs), berge de rivière
(boue), désert et plage (sable), puis selon la couverture quand la terre domine
(marais → boue ; terres arides, volcans, montagnes → gravier ; sinon terre),
herbe sinon. Aux Enfers : terre battue, terre ou pierre.

Les rues et places des villes sont rendues en terre (splat) : elles sonnent en
terre battue comme les routes ; un son de pavés n'a de sens qu'avec un rendu
pavé.

## Tests

- `sound_tests` : lecture réelle en mode sans périphérique (son audible dans le mixage,
  bus à 0 = silence, voix libérée en fin de son, limite de voix, fichier absent),
  variantes jamais répétées, cadence des pas (foulée, arrêt, réception, nage),
  surface → son.
- `cell_surface_map_tests` : lecture SLAP, couche dominante, coordonnées, rejets ;
  carte des sols (aller-retour `.gndm`, priorité sur le splat, emprise, classe
  future → inconnue, version future refusée, repli sur le splat).
- `sound_tests` (SND.2b) : chaque nature de sol → son, priorités eau / plancher.
- `world_gen_tests` (SND.2b) : `.gndm` sur la grille du splat, rues et routes de
  Kendrhir en terre battue, références dans zone.json et runtime_manifest ; Enfers
  en terre, terre battue et pierre seulement.
- `audio_assets_tests` : chaque fichier déclaré dans `zone_audio.json` existe et se décode.
- `audio-gen` (Python) : chaque surface produit un pas fini, court, sans clic ;
  SND.2b : aucun appui tonal (énergie 60–400 Hz non concentrée), moins de 50 %
  d'énergie sous 200 Hz, 6 variantes distinctes.

## Configuration (`config.json`, section `audio`)

| Clé | Défaut | Rôle |
|---|---|---|
| `sfx_device_enabled` | true | jouer réellement les sons |
| `max_voices` | 64 | sons simultanés (8–256) |
