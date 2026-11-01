# EAU.3 — Eau : reflets du ciel et lumière selon l'heure

Suite d'EAU.1 (couleur, rivages) et EAU.2 (houle, courant).

## Constat (captures du lac de Marn, 01/11, de nuit)

- L'eau restait turquoise vif en pleine nuit : sa couleur était constante.
- Les reflets venaient d'un cubemap de repli **uni bleu foncé** (1 × 1) :
  le ciel réel (nuages, soleil, lune) ne se reflétait jamais.

## Correction

- `WaterPass::SetSkyboxCube` + `Engine.cpp` : chaque image, l'eau lit le
  **ciel capturé et préfiltré** du jeu (`SpecularPrefilterPass`, le même que
  l'éclairage des objets), qui suit l'heure et la météo. Repli sur l'ancien
  cubemap uni si la capture n'est pas prête.
- `water.frag` :
  - **lumière ambiante** = mip la plus floue du ciel vers le zénith, rapportée
    à un ciel de jour (luminance 0,35) : la couleur d'eau, l'écume et le
    liseré rasant s'assombrissent au crépuscule et la nuit (plancher 6 %) ;
  - **reflet du ciel** légèrement flou (mip 0,6, les rides le brouillent),
    Fresnel de l'eau (≈ 2 % d'aplomb, fort en vue rasante) ; le disque solaire
    du ciel (HDR) donne les **éclats de soleil** sur l'eau ;
  - arbres et rives proches : reflet écran (SSR) inchangé.

## Limites

- Les reflets d'objets hors écran (arbre derrière la caméra) ne peuvent pas
  apparaître (limite du SSR) : le ciel prend le relais.
- Référence de luminance du ciel (0,35) et flou du reflet (0,6) sont des
  constantes du shader, à ajuster sur captures.
