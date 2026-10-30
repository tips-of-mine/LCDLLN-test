# tools/audio_gen — générateur audio LCDLLN

Musiques et sons du jeu fabriqués par programme (synthèse + partition rendue par
FluidSynth), pour être régénérés, modifiés et relus comme du code.

## Prérequis (Linux / CI)

```bash
sudo apt-get install -y fluidsynth fluid-soundfont-gm ffmpeg
pip install numpy scipy mido soundfile pyloudnorm
```

## Contenu

| Fichier | Rôle |
|---|---|
| `lcdlln_audio/dsp.py` | filtres, bruits, panoramique, réverbération par convolution, limiteur, sonie (LUFS), boucle sans couture, export Ogg/WAV |
| `lcdlln_audio/percussion.py` | taiko, tambour de bataille, tambour sur cadre, djembé, petites percussions, hochet, bloc de bois, enclume, gong, chaînes, impact |
| `lcdlln_audio/textures.py` | vent, tonnerre lointain, souffle grave |
| `lcdlln_audio/orchestra.py` | partitions (pupitres), écriture MIDI, rendu FluidSynth |
| `menu_theme.py` | SND.1 — musique de l'écran de connexion (`docs/audio/SND.1_musique_menu.md`) |
| `sfx_footsteps.py` | SND.2 — sons de pas par surface (`docs/audio/SND.2_moteur_sons_pas.md`) |
| `sfx_ui.py` | SND.3 — sons d'interface et d'évènements (`docs/audio/SND.3_sons_interface.md`) |
| `sfx_combat.py` | SND.5 — combat et sorts (`docs/audio/SND.5_sons_combat.md`) |
| `sfx_interactions.py` | SND.6 — récolte et artisanat (`docs/audio/SND.6_sons_recolte_artisanat.md`) |
| `sfx_ambience.py` | SND.4 — boucles d'ambiance et tonnerre (`docs/audio/SND.4_ambiances.md`) |
| `tests/` | tests unitaires (workflow `audio-gen`) |

## Régénérer la musique du menu

```bash
python3 tools/audio_gen/menu_theme.py --out game/data/audio/music/menu_lune_noire.ogg
```

Tout est déterministe (graines fixes) : deux exécutions produisent le même son.
Banque de sons : `LCDLLN_SF2=/chemin/banque.sf2` pour en utiliser une autre.

## Régénérer les sons

```bash
python3 tools/audio_gen/sfx_footsteps.py   # game/data/audio/sfx/footsteps/
python3 tools/audio_gen/sfx_ui.py          # game/data/audio/sfx/ui/
python3 tools/audio_gen/sfx_ambience.py    # game/data/audio/ambience/ (≈ 35 s)
python3 tools/audio_gen/sfx_combat.py      # game/data/audio/sfx/combat/
python3 tools/audio_gen/sfx_interactions.py  # game/data/audio/sfx/interactions/
```
