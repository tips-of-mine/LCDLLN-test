# FIX.7 — CI Windows : textures de repli du terrain et échecs de build masqués

## Sol magenta dans le client empaqueté

Log client du 28/10/2026 : `Couldn't load PBR layer … ('game/data\terrain\placeholders\snow.png')
— fallback magenta` pour les 8 couches. Ces PNG 4×4 sont **générés au build** par
`gen_terrain_placeholders` (ignorés par git, `.gitignore`). Seul le job « Serveur + outils »
construisait ce générateur : les paquets **client** et **éditeur** n'avaient donc pas les
textures.

Correction : les jobs client et éditeur construisent `gen_terrain_placeholders` avant
l'empaquetage de `game/data` ; le job client échoue si `snow.png` est absent.

## Échecs de build masqués

Les étapes `Build` enchaînent plusieurs `cmake --build` dans un bloc pwsh : pwsh ne retient
que le code de sortie de la dernière commande. Un échec au milieu passait inaperçu (cas de
la PR #1061). Les trois étapes activent maintenant `$ErrorActionPreference = "Stop"` et
`$PSNativeCommandUseErrorActionPreference = $true` (pwsh 7.3+) : la première commande en
échec arrête l'étape.

## Restent hors CI (assets absents du dépôt)

- `game/data/models/avatars/nains/nains.glb` : référencé par `creatures/archetypes.json`
  (`"mesh": "nains"`) mais absent du dépôt (cgltf 6 = fichier introuvable).
- `game/data/zone.meta`, `audio/zone_0_ambience.wav`, `audio/zone_0_sfx_test.wav`.
- `game/data/chunks/chunk_*/terrain.bin` (ancienne zone autour de l'origine).
