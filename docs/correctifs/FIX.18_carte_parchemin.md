# FIX.18 — Carte du monde : parchemin, noms au survol, sons de déroulé

Demande du 23/11/2026 (Atelier LCDLLN, arbitrages Q1 c, Q2 b sans le skin du dialogue PNJ, Q3 b, Q4 oui).

## Symptômes

- La carte (touche M) affichait en permanence les noms des villes et des hameaux :
  la carte s'ouvre sur environ 2 km visibles (FIX.11), donc toujours au-delà du
  seuil « zoom 2 » qui affichait tous les libellés.
- La fenêtre gardait l'aspect ImGui par défaut (barre de titre bleue, fond gris
  sombre) : une fenêtre système plutôt qu'un objet du jeu.
- Le son d'ouverture existant (SND.3) n'évoquait pas assez un parchemin qu'on déroule.

## Correction

**Noms au survol** (`src/client/world/places/WorldMapPresentation.h`, pur) :
- villes, hameaux, lieux remarquables, lieux secrets : plus aucun libellé dessiné
  sur la carte ; le nom (et les détails) apparaît dans l'info-bulle au survol,
  inchangée ;
- régions : le nom en filigrane n'apparaît que lorsque le curseur est dans la
  région, en vue éloignée (zoom ≤ 2,5) et si son centre est découvert.

**Habillage parchemin** (`src/client/render/WorldMapImGuiRenderer.cpp`) :
- fond parchemin clair, encre brune, bords vieillis, double filet autour de la
  carte, boutons et info-bulles assortis ;
- rouleaux de bois avec pommeaux sur les bords haut et bas, dessinés par la draw
  list (aucune texture ajoutée) ;
- pas de barre de titre ImGui : en-tête dessiné (nom de la carte centré, bouton X).
  Fenêtre toujours déplaçable (glisser l'en-tête ou le cadre) et redimensionnable.

**Déroulé** : à chaque ouverture, la frame 0 (invisible) relève la taille pleine
mémorisée par ImGui, puis la hauteur passe de l'en-tête à la taille pleine en
0,35 s (ease-out cubique) ; le rouleau du bas descend. La mise en page se fait
sur la taille pleine : la carte ne change pas d'échelle pendant le déroulé.
La taille intermédiaire n'est pas enregistrée dans l'.ini (`NoSavedSettings`
pendant le déroulé).

**Sons** (`tools/audio_gen/sfx_ui.py`, nouveau `paper_roll`) :
- `map_open` (0,88 s) : lien de cuir, feuille qui se déroule en ralentissant
  (un craquement par tour), rouleau de bois qui se pose ;
- `map_close` (0,85 s) : feuille qui s'enroule en accélérant, rouleau, lien serré.
Déclenchement inchangé (`UiAudioCues`, ouverture / fermeture de la carte).

## Inchangé

Zoom, glisser, suivi du joueur, « Centrer », « Vue d'ensemble », voile des zones
non découvertes, marqueurs, info-bulles, touche M (ouverture / fermeture, filtre
de l'hôtel des ventes), bannière « Vous entrez à … ».

## Tests

`world_map_presentation_tests` (nouveau) ; `world_map_zoom_tests`,
`ui_audio_cues_tests` et tests du générateur audio relancés.

## Déploiement

Client (rendu, interface, sons) + migration roadmap `0097` → redéploiement master
pour appliquer la migration du portail.
