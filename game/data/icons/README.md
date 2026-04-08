# assets/icons/

🇫🇷 FRANÇAIS
## Pourquoi ce dossier existe
Ce dossier regroupe toutes les icônes utilisées par l’application, séparées des images UI
afin de faciliter leur remplacement et leur cohérence visuelle.

## Ce qu’il doit contenir
- `app/` : icônes de l’application (exécutable, fenêtre, barre des tâches).
- `ui/` : petites icônes utilisées dans l’interface (boutons, états, alertes).
- À la racine de `icons/` : icônes **Windows (.ico)** liées à la compilation des exécutables :
  - `lcdlln_client.ico` + `lcdlln_client.rc` → **lcdlln.exe** (client jeu).
  - `lcdlln_world_editor.ico` + `lcdlln_world_editor.rc` → **lcdlln_world_editor.exe**.
  - Régénération optionnelle : `powershell -File tools/gen_exe_icons.ps1` (multi-résolution 16–256 px).

---

🇬🇧 ENGLISH
## Why this folder exists
This folder groups all icons used by the application, separated from UI images
to simplify replacement and maintain visual consistency.

## What it must contain
- `app/` : application-level icons (executable, window, taskbar).
- `ui/` : small icons used in the UI (buttons, status, alerts).
- At `icons/` root: **Windows (.ico)** sources for embedded executable icons:
  - `lcdlln_client.ico` + `lcdlln_client.rc` → **lcdlln.exe**.
  - `lcdlln_world_editor.ico` + `lcdlln_world_editor.rc` → **lcdlln_world_editor.exe**.
  - Optional regenerate: `powershell -File tools/gen_exe_icons.ps1` (16–256 px).
