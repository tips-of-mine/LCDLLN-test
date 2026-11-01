# EAU.2 — Eau : houle de la mer, lacs calmes, rivières qui coulent

Suite d'EAU.1 (couleur selon la profondeur, rivages). Choix du joueur : lacs et
rivières plus calmes que la mer.

## Trois sortes d'eau (`game/data/shaders/water.frag`)

| Eau | Détection | Rendu |
|---|---|---|
| **Mer** | `flowSpeed < 0` (posé par `WaterPass` pour le lac `isOcean`) | houle de 4 trains de vagues (21, 13, 8, 5 m) autour du vent dominant, vitesse réaliste (eau profonde), moutons d'écume sur les crêtes |
| **Lac** | ni rivière ni mer | rides légères (pente 0,45), eau presque lisse |
| **Rivière** | direction de courant par sommet (`vFlowDir`) | rides étirées dans le sens du courant (filets d'eau), entraînées vers l'aval, traînées d'écume légères |

Convention ajoutée (`WaterPass.h/.cpp`) : pour la mer, `flowSpeed` est NÉGATIF
(|valeur| = force de la houle, 1,0 par défaut) et `flowDirection` = direction
du vent (0,8 ; 0,6). Les push constants restent à 128 octets (aucun champ ajouté).

## Écume affinée

- Bande des rives resserrée : 0,7 m d'eau au lieu de 1,2 m, liseré continu
  d'~10 cm. Retour joueur (captures du lac de Marn) : l'écume peignait en
  blanc les jambes du personnage et le pied des troncs dans l'eau (l'épaisseur
  vue y est faible aussi).
- Mer : moutons sur les crêtes, en taches irrégulières.
- Rivière : traînées qui filent avec le courant.

## Limites

- La nappe de la mer n'a que 4 sommets : la houle se lit par la lumière, les
  reflets et l'écume, la surface ne se soulève pas réellement (il faudrait une
  grille de sommets dense autour de la caméra — étape possible plus tard).
- Réglages (force de la houle, vent) en constantes dans `WaterPass.cpp`
  (`kSeaSwellStrength`, `kSeaWindDirection`).
- Étape suivante : EAU.3, reflets (ciel, arbres, éclat du soleil).
