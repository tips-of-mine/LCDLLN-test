# assets/

🇫🇷 FRANÇAIS
## Pourquoi ce dossier existe
Ce dossier contient l’ensemble des ressources non-code utilisées par l’application cliente :
images, icônes, thèmes, ressources localisées.

Il permet :
- de séparer clairement le code et les ressources visuelles,
- de faciliter le packaging de l’application,
- de permettre l’évolution graphique (skins, thèmes) sans modifier le code.

## Ce qu’il doit contenir
- `icons/` : icônes de l’application et de l’interface.
- `ui/` : images utilisées par les écrans de l’interface.
- `themes/` : styles Qt (QSS) et configurations de thèmes.
- `localization/` : ressources dépendantes de la langue.
- `placeholders/` : images de secours (fallback).

---

🇬🇧 ENGLISH
## Why this folder exists
This folder contains all non-code resources used by the client application:
images, icons, themes, and localized assets.

It allows:
- a clean separation between code and visuals,
- easy application packaging,
- future graphical evolution (themes/skins) without code changes.

## What it must contain
- `icons/` : application and UI icons.
- `ui/` : images used by UI screens.
- `themes/` : Qt stylesheets and theme configuration.
- `localization/` : language-dependent resources.
- `placeholders/` : fallback images.
