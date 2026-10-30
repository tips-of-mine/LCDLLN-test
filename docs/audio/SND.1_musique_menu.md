# SND.1 — Musique de l'écran de connexion : « Aux portes de la Lune Noire »

Remplace `Horns_of_the_Fallen_Bastion.mp3` (≈ 30 s, jugée insuffisante). Écrite
d'après le cahier des charges initialement rédigé pour Gemini (rappelé ci-dessous),
générée par `tools/audio_gen/menu_theme.py`.

| | |
|---|---|
| Fichier | `game/data/audio/music/menu_lune_noire.ogg` (Ogg Vorbis q6, ≈ 7 Mo) |
| Durée | 8 min 34 s (204 mesures), boucle exacte à l'échantillon |
| Format | 48 kHz, stéréo, −15 LUFS intégrés, crête ≤ −1 dBFS |
| Tonalité | ré mineur |

## Version retenue (v5) et écoutes

Cinq versions écoutées le 30/10/2026 ; la v5 est retenue.

| Version | Retour |
|---|---|
| v1 | trop « batterie » et trop électronique (percussions de synthèse), ouverture trop longue, tambours lointains |
| v2 | tambours enregistrés, mieux ; pas assez grave, un instrument non identifiable (cordes rapides) |
| v3 | tambours graves + cuivres, mieux ; accélération trop tardive ; remplacer le cor par l'hélicon ; le rythme ne fait pas « armée en marche » |
| v4 | hélicons + marche militaire ; saturation à 0:28 (trop de cuivres), il faut un second tambour plus soutenu |
| **v5** | **tambours au premier plan, hélicon en simple basse de marche, tambour soutenu en continu — retenue** |

## Forme (v5)

| Section | Contenu |
|---|---|
| Ouverture (≈ 0:00–0:27) | ō-daiko seul, puis les pas de l'armée et la cadence entrent ; l'accélération commence à la 4e mesure (70 → 96 noires/min, atteint vers 0:50) |
| La troupe se met en marche | marche : un pas par temps, cadence de troupe en boucle de 2 mesures, tambour soutenu en croches |
| L'appel de la conquête | marche complète, roulements de fin de phrase, cymbales aux entrées |
| Au cœur des ténèbres | la marche continue assourdie, pédale grave de l'hélicon, vent |
| La conquête | sommet (98 → 100), roulements à chaque phrase, doubles croches du tambour soutenu |
| Le retour inéluctable | cadence finale (timbales, ō-daiko, cymbale, gong), ralentissement vers 70 et retour à l'ouverture |

Instruments : ō-daiko (taiko joué très grave), taikos (pas, cadence gauche/droite,
tambour soutenu aigu), grosse caisse de concert, timbales, cymbales frappées
(sommets seulement), gong ; hélicon (tuba General MIDI, sonorité la plus proche)
en basse de marche « oum-pa ». Les tambours ne s'arrêtent jamais.

## Fabrication

- Tambours et hélicon : partition écrite en Python (carte de tempo avec
  accélération et ralentissement), rendue par FluidSynth avec la banque FluidR3_GM
  (MIT) — sons enregistrés.
- Gong et vent : synthèse (`tools/audio_gen/lcdlln_audio/percussion.py`, `textures.py`).
- Mixage (grave renforcé et attaque des peaux sur le bus de tambours, réverbération
  de salle 2,4 s), sonie et limiteur : numpy/scipy (`dsp.py`).
- Régénérer : `python3 tools/audio_gen/menu_theme.py` (≈ 1 min, 8 Go de RAM),
  voir `tools/audio_gen/README.md`.

Limite assumée : sons de qualité « General MIDI » ; la partition peut être rendue
avec une banque de percussions plus riche (variable `LCDLLN_SF2`) ou reprise par un
compositeur.

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
