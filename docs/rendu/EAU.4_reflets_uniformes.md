# EAU.4 — Reflets uniformes (fin des reflets « par plaques »)

## Symptôme (captures du 02/11, lac de Marn)

Les arbres se reflètent par plaques : des morceaux de reflet nets, des bords
coupés à la verticale, d'autres zones où le même arbre ne se reflète pas.

## Causes (`game/data/shaders/water.frag`, `ssrTrace`)

1. **Profondeur mal comparée** : la profondeur NDC de Vulkan est déjà dans
   [0, 1] ; l'ancien code la remappait (`z × 0,5 + 0,5`) avant de la comparer
   à la profondeur de la scène. Certains rayons « touchaient » à tort, leurs
   voisins non → plaques.
2. **Bascule nette** : un rayon qui touche affichait la scène, un rayon qui
   rate affichait le ciel, sans transition → bords francs.
3. **Portée de 16 m** (32 pas fixes de 0,5 m) : les arbres plus lointains
   n'étaient jamais atteints.
4. **Rides** : chaque ride déviait le rayon réfléchi → reflet en lambeaux.

## Correction

- Comparaison en **distances linéaires** (m), pas croissants (×1,12,
  ≈ 200 m de portée), **affinage par dichotomie** du point touché.
- **Confiance** du reflet (bords de l'écran, bout de la portée) → fondu
  continu entre le reflet de la scène et celui du ciel.
- Le ciel lointain n'est plus pris pour un « toucher ».
- Rayon réfléchi calculé avec une normale adoucie (rides × 0,5).
- Reflet plafonné à 65 % (miroir moins dur ; l'eau garde sa couleur).

## Limite

Ce qui n'est pas à l'écran (au-dessus du haut de l'image, derrière la
caméra) ne peut pas se refléter : le reflet s'estompe alors vers le ciel au
lieu d'être coupé net.
