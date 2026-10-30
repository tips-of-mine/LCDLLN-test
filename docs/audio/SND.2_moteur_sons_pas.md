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
- **Sol sous le pied** : la couche dominante du splat de la cellule (herbe, terre,
  roche, neige) est gardée côté CPU (`src/client/world/cells/CellSurfaceMap`,
  ≈ 4 Mo par cellule affichée, construite sur le thread de chargement). Au-dessus du
  terrain (plancher de bâtiment, pont) : bois. Nage : brasse.
- **Sons** : 12 surfaces × 4 variantes synthétisées (`tools/audio_gen/sfx_footsteps.py`)
  dans `game/data/audio/sfx/footsteps/` : herbe, terre, gravier, roche, sable, neige,
  boue, bois, eau, nage, blé, maïs. Gravier, sable, boue, blé et maïs sont prêts pour
  les futurs sols (routes, champs) : la carte des sols ne les distingue pas encore.
- Intégration : `Engine::UpdateFootstepAudio` et `Engine::UpdateAudioListener`
  (`src/client/app/EngineAudio.cpp`), appelés après le contrôleur de personnage et
  avec la caméra.

## Tests

- `sound_tests` : lecture réelle en mode sans périphérique (son audible dans le mixage,
  bus à 0 = silence, voix libérée en fin de son, limite de voix, fichier absent),
  variantes jamais répétées, cadence des pas (foulée, arrêt, réception, nage),
  surface → son.
- `cell_surface_map_tests` : lecture SLAP, couche dominante, coordonnées, rejets.
- `audio_assets_tests` : chaque fichier déclaré dans `zone_audio.json` existe et se décode.
- `audio-gen` (Python) : chaque surface produit un pas fini, court, sans clic.

## Configuration (`config.json`, section `audio`)

| Clé | Défaut | Rôle |
|---|---|---|
| `sfx_device_enabled` | true | jouer réellement les sons |
| `max_voices` | 64 | sons simultanés (8–256) |
