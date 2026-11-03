# PERF.1 — Textures en mémoire GPU, tuilage optimal et mips

## Symptôme

Log client du 02/11 (cellule n003_w009, RTX 3080, écran 5760 × 1080) : la
passe GPU **Geometry** prend 20 à 30 ms dès qu'une cinquantaine d'arbres est
visible, jusqu'à **150 ms** avec ~450 arbres (≈ 7 à 40 images/s). Sans décor
visible, elle tombe à 2–3 ms. Les maillages sont légers (≤ 25 000
triangles) : ce n'est pas la géométrie.

## Cause

`AssetRegistry::createSampledRgba8Texture` (toutes les textures PNG : écorces,
feuillages, props, UI…) créait les images en :

- tuilage **LINEAR** (rangées brutes, très mauvais pour le cache de textures),
- mémoire **HOST_VISIBLE** (lue par le GPU à travers le bus PCIe),
- **un seul niveau de mip** : un feuillage à 50 m lit encore sa texture
  1024 × 1024 pleine résolution, texel par texel.

Sur 6 mégapixels de feuillage en alpha-test, c'est catastrophique.

## Correction

`createOptimalMippedTexture` : image **DEVICE_LOCAL**, tuilage **OPTIMAL**,
**chaîne de mips complète** générée sur le GPU (blits linéaires), copie par
tampon de staging, image laissée en `SHADER_READ_ONLY_OPTIMAL`. Utilisée dès
que la queue d'upload est fournie (c'est le cas en jeu) ; l'ancien chemin
LINEAR reste le repli. Les matériaux (props, décor) ont déjà des samplers
qui exploitent les mips (`maxLod` illimité) ; les passes plein écran gardent
`maxLod = 0` (inchangées).

Le log indique désormais `loaded PNG texture … (OPTIMAL+mips)`.

## À vérifier

Nouveau log `[Perf]` au même endroit : la ligne `gpu: Geometry=…` devrait
tomber à quelques ms.
