# `game/data/icons/app/`

## FR
Icônes **fenêtre / barre des tâches** (PNG) pour le client jeu et l’éditeur, chargées au runtime sous Windows.

| Fichier | Usage |
|--------|--------|
| `lcdlln_game.png` | Client `lcdlln` — clé `window.icon_png` dans `config.json` |
| `lcdlln_editor.png` | `lcdlln_editor` — clé `editor.window_icon_png` |

Les chemins sont relatifs à `paths.content` (souvent `game/data`). Le moteur utilise `FileSystem::ReadAllBytesContent` (fichier disque ou `.texr` monté).

Pour l’icône **de l’exécutable** Windows (explorateur, raccourci), il faut en plus un `.ico` dans les ressources du linker (hors périmètre de ces PNG).

## EN
**Window / taskbar** PNG icons for the game client and the world editor, loaded at runtime on Windows.

| File | Used by |
|------|---------|
| `lcdlln_game.png` | `lcdlln` — `window.icon_png` in `config.json` |
| `lcdlln_editor.png` | `lcdlln_editor` — `editor.window_icon_png` |

Paths are relative to `paths.content` (typically `game/data`). The engine uses `FileSystem::ReadAllBytesContent` (disk or mounted `.texr`).

For the **executable** icon in Explorer, add a `.ico` to the linker resources (separate from these PNGs).
