# SND.3 — Sons d'interface et d'évènements de jeu

Dépend de SND.2 (lecture réelle des sons). Fusionner **SND.2 → SND.3 → SND.4**.

## Principe

Plutôt que d'ajouter un appel audio dans chacune des dizaines de fenêtres, Engine
prend à chaque frame une **photo compacte de l'état** visible
(`engine::audio::UiAudioSnapshot`) et la compare à la précédente
(`DiffUiAudio`, module pur `src/client/audio/UiAudioCues.{h,cpp}`) :

| Changement observé | Son |
|---|---|
| un élément d'interface devient actif à l'appui de la souris (bouton, case, onglet…) | `ui_click` (bois) |
| une fenêtre s'ouvre / se ferme (courrier, guilde, artisanat, menu Pause, Options…) | `ui_open` / `ui_close` (parchemin, cuir) |
| carte du monde (M) ouverte / fermée | `map_open` / `map_close` (parchemin déroulé / roulé) |
| niveau supérieur | `level_up` (roulement de timbales, accord de cuivres, cloches, gong) |
| quête acceptée | `quest_accept` (arpège de harpe sur un coup de taiko) |
| objectif progressé ou tous remplis | `quest_objective` (glockenspiel) |
| quête terminée / rendue | `quest_complete` (taiko, tuba, cloches, harpe) |
| bannière « Vous entrez à … » | `place_discovered` (ō-daiko, tuba grave, gong) |
| message de discussion / chuchotement (pas les siens) | `chat_message` / `chat_whisper` (harpe) |
| bourse modifiée | `coins` |
| objet ramassé (sans transaction) | `item_pickup` |
| équipement changé | `item_equip` (cuir et métal) |
| fenêtre de butin | `loot_open` |
| invitation de groupe | `ui_notify` (célesta) |

- Au plus 3 sons par frame, les « fanfares » (niveau, quête terminée) d'abord.
- Aucun son pour l'état initial ni pendant 240 frames après l'entrée en jeu (les
  quêtes, l'inventaire et la bourse arrivent en plusieurs paquets).
- Les écrans de connexion n'ont que le clic.

## Sons

18 sons dans `game/data/audio/sfx/ui/` (bus **UI**, réglage « Interface » du menu
Options), générés par `tools/audio_gen/sfx_ui.py` :
- matières synthétisées : bois, parchemin, cuir, pièces, métal ;
- courtes fanfares jouées par des instruments **enregistrés** (FluidR3_GM : harpe,
  cloches, célesta, glockenspiel, tuba, cuivres, timbales, taiko) — les familles
  de la musique du menu, rien d'électronique.

## Code

- `src/client/audio/UiAudioCues.{h,cpp}` : photo, comparaison, détection de clic.
- `src/client/app/EngineAudio.cpp` : `BuildUiAudioSnapshot`, `UpdateUiAudio`
  (appelée juste avant `ImGui::Render()`, écrans de connexion et jeu).
- Tests : `ui_audio_cues_tests` (C++), `audio-gen` (chaque son fini, court, sans saturation).
