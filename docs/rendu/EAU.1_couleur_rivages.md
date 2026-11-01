# EAU.1 — Eau : couleur selon la profondeur et écume des rivages

## Objectif

Rapprocher l'eau (mer, lacs, rivières) du rendu de référence fourni (Valheim) :
eau claire et turquoise sur les hauts-fonds, bleu profond au large, rives
fondues avec une écume blanche, surface vivante. Premier palier d'un chantier
en plusieurs étapes (choix du joueur : couleur + rivages d'abord).

| Étape | Contenu | État |
|---|---|---|
| EAU.1 | couleur selon la profondeur, rives, écume, rides animées | ce ticket |
| EAU.2 | houle de la mer (surface qui se soulève), écume des crêtes ; lacs calmes, rivières qui coulent | à faire |
| EAU.3 | reflets (ciel, arbres, éclat du soleil) | à faire |

## Ce qui change (`game/data/shaders/water.frag`)

- **Absorption par canal (Beer-Lambert)** : le rouge s'éteint d'abord, puis le
  vert. 0–1 m d'eau : on voit le fond, teinté turquoise ; quelques mètres :
  turquoise opaque ; au large : bleu sombre. Remplace l'ancienne opacité
  minimale de 80 % qui rendait toute l'eau uniformément bleue.
- **Écume des rivages** : bande blanche là où l'eau fait moins d'~1,2 m,
  découpée par du bruit, parcourue de lignes de ressac animées, plus un liseré
  continu au ras du bord. Dans les rivières, les taches d'écume suivent le
  courant.
- **Rides procédurales** : les textures de normales de l'eau sont encore des
  1×1 vides ; la surface était plate. Des rides en espace monde (stables quand
  la caméra bouge), animées et entraînées par le courant, donnent du relief
  aux reflets et à la réfraction. Plus marquées dans les rivières.
- **Anti-scintillement** : rides et lignes de ressac s'atténuent avec la
  distance (sinon crénelage au loin).
- **Réfraction atténuée au bord** : le sable de la rive ne « nage » plus.

`water.frag.spv` est régénéré (la CI le recompile de toute façon au paquet).

## Réglages (constantes en tête de section dans `water.frag`)

| Constante | Rôle | Valeur |
|---|---|---|
| `absorb` | absorption par mètre (R, V, B) | 0,45 / 0,10 / 0,07 |
| `shallowTint` | couleur des hauts-fonds | turquoise (0,04 ; 0,32 ; 0,34) |
| `deepTint` | couleur du large | bleu (0,01 ; 0,06 ; 0,14) |
| `smoothstep(0.0, 1.2, …)` | largeur de la bande d'écume (m d'eau) | 1,2 |
| `0.025` (distFade) | atténuation des rides avec la distance | — |

Réglage par captures du joueur (je ne vois pas le rendu) : même endroit,
même heure, avant / après.

## Limites connues

- La houle de la mer et les reflets nets viennent aux étapes suivantes.
- L'épaisseur d'eau est mesurée le long du regard : vue rasante, une eau peu
  profonde paraît plus foncée qu'à la verticale (comportement réaliste).
