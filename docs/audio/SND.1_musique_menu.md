# SND.1 — Musique de l'écran de connexion : « Aux portes de la Lune Noire »

Remplace `Horns_of_the_Fallen_Bastion.mp3` (≈ 30 s, jugée insuffisante). Écrite
d'après le cahier des charges initialement rédigé pour Gemini (rappelé ci-dessous),
générée par `tools/audio_gen/menu_theme.py`.

| | |
|---|---|
| Fichier | `game/data/audio/music/menu_lune_noire.ogg` (Ogg Vorbis q6, ≈ 10,8 Mo) |
| Durée | 8 min 50 s (159 mesures à 72 noires/min), boucle exacte à l'échantillon |
| Format | 48 kHz, stéréo, −15,5 LUFS intégrés, crête ≤ −1 dBFS |
| Tonalité | ré mineur (couleurs phrygiennes : mi bémol sur pédale de ré) |

## Forme

| Section | Temps | Contenu |
|---|---|---|
| A — L'approche | 0:00–1:30 | Taikos lointains (« DOOM — DOOM »), vent, bourdon de contrebasses, note grave unique des trombones, chœur sans paroles qui naît, tambours sur cadre et hochets lointains, tonnerre. |
| B — Le monde se dévoile | 1:30–3:30 | Toutes les percussions (taikos, tambours de bataille, hochets, enclume, tambours sur cadre), nappe de cordes, cors ; thème principal au cor puis aux violons ; contre-chant du hautbois. |
| C — L'appel de l'aventure | 3:30–5:30 | Percussions seules, puis rythmes imbriqués (taikos médiums, tambours de bataille en doubles croches gauche/droite, djembés, petites percussions, bloc de bois, chaînes, enclume) ; ostinato des cordes graves ; thème au cor solo, thème secondaire flûte/violons, tutti. |
| D — Au cœur des ténèbres | 5:30–7:30 | Battement de cœur des taikos, vent, cors lointains, frappes en écho, flûte puis hautbois sur un motif fragile ; reconstruction (trémolos, retour des tambours, roulement de timbales). |
| E — Le retour inéluctable | 7:30–8:50 | Reprise du thème en force (cors + violons + chœur), cadence (si♭–sol–la–ré, gong, impact), puis retour à la pulsation lointaine du début. |

La pulsation des grands taikos ne s'interrompt jamais. Les 8 dernières mesures
reprennent les 8 premières (pédale de ré, tambours lointains) et les queues de
réverbération qui dépassent la fin sont replacées au début : lue en boucle par
miniaudio, la piste est continue.

## Fabrication

- Percussions, vent, tonnerre : synthèse (`tools/audio_gen/lcdlln_audio/percussion.py`,
  `textures.py`).
- Orchestre (cordes, cors, cuivres graves, chœur « ooh », flûte, hautbois, timbales) :
  partition écrite en Python, rendue par FluidSynth avec la banque FluidR3_GM (MIT).
- Mixage, réverbération (salle 3,2 s ; caverne 6,5 s pour les plans lointains),
  sonie et limiteur : numpy/scipy (`dsp.py`).
- Régénérer : `python3 tools/audio_gen/menu_theme.py` (≈ 1 min 30, 8 Go de RAM),
  voir `tools/audio_gen/README.md`.

Limite assumée : c'est une **maquette orchestrale de qualité « General MIDI »** ;
la partition peut être reprise telle quelle avec une banque orchestrale plus riche
(variable `LCDLLN_SF2`) ou par un compositeur.

## Moteur

- Ogg Vorbis activé dans miniaudio (`external/stb/stb_vorbis.c`, domaine public,
  compilé dans `MaMenuMusic.cpp`) : l'Ogg boucle sans le délai d'encodeur du MP3.
- `config.json` (`audio.menu_music_path`) et `zone_audio.json` (`auth_music`) pointent
  sur la nouvelle piste. Si un `config.json` local désigne une piste absente
  (ancien MP3), le client se replie sur la piste livrée (avertissement au journal).
- `audio_assets_tests` (CI) : piste présente, décodable, 48 kHz stéréo, 5 à 10 min.

## Cahier des charges (prompt d'origine, résumé)

Piste instrumentale pour le menu d'un MMORPG dark fantasy, 5 à 10 minutes en boucle
invisible, sans voix. Colonne vertébrale : plusieurs couches de tambours de guerre
(taikos, tambours de bataille, petites percussions rapides, percussions métalliques
— chaînes, enclume, gongs —, tambours du monde — djembé, tambour sur cadre, bodhrán).
Cinq sections (approche, dévoilement, appel de l'aventure, cœur des ténèbres, retour
inéluctable). Cordes graves, cuivres, bois, chœur textural sans syllabes, textures
de vent, tonnerre, résonances métalliques. Émotion visée : « Je me tiens aux portes
d'un monde sombre, immense et dangereux — et j'ai hâte d'y entrer. »
