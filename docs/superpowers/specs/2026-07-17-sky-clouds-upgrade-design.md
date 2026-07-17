# Amélioration du ciel — nuages Perlin-Worley + ciel analytique

Date : 2026-07-17
Statut : validé (suite du comparatif UE 5.8 ; lancement demandé en séance
après le lot polish UI).
Périmètre : 100 % client (rendu Vulkan) — aucun redéploiement serveur.

## Constat (comparatif UE)

Le ciel d'UE vient d'un empilement SkyAtmosphere (diffusion physique) +
VolumetricCloud (bruit Perlin-Worley 3D pré-cuit + érosion). Chez nous :

- `clouds.frag` (PR #853) : ray-march correct mais **bruit value-noise FBM
  calculé in-shader** (hash) → nuages mous/patateux sans bords érodés.
- `sky.frag` : **dégradé 2 couleurs** zénith/horizon (poussé par
  DayNightCycle) → ciel plat, ne réagit pas physiquement au soleil.

Levier n°1 = qualité du bruit des nuages ; levier n°2 = modèle de ciel.
Deux PRs **indépendantes** (fichiers disjoints, mergeables dans n'importe
quel ordre — leçon des piles + squash) :

## PR A — `claude/sky-clouds-noise` : textures 3D Perlin-Worley

- **Générateur CPU pur** `src/client/render/clouds/CloudNoiseGenerator.{h,cpp}`
  (testable ctest Linux) :
  - bruit de **Worley 3D périodique** (points caractéristiques par cellule,
    distance min sur 27 voisins avec wrap, inversé → « cotonneux ») ;
  - **fBm de Perlin 3D périodique** (gradients sur lattice wrap, fade
    quintique) ;
  - `GenerateCloudNoise(seed)` → 2 textures RGBA8 :
    - **base 64³** : R = fBm Perlin, G/B/A = Worley 8/16/32 cellules ;
    - **détail 32³** : R/G/B = Worley 4/8/16 cellules.
  - Déterministe (hash entier maison, pas de rand()), génération au boot
    (< 1 s, loguée).
- **CloudPass** : `Init` reçoit queue + queue family (upload one-shot via
  staging + command pool transitoire) ; 2 images 3D R8G8B8A8 + sampler
  linéaire REPEAT (tuilage) ; descriptor set layout passe à 4 bindings
  (2 = base, 3 = détail), écrits avec 0/1 à chaque Record.
- **clouds.frag** : `cloudDensity` échantillonne les textures — forme de
  base = dilatation Schneider `remap(perlin, worleyFbm-1, 1, 0, 1)`
  (tuile ~5,2 km), seuil piloté par coverage (inchangé côté CPU), érosion
  de détail (tuile ~800 m, wispy en bas / billowy en haut). Push constants
  **inchangés** (192 o) → zéro changement d'API météo/CPU.
- Échec de création des textures → Init false → passe désactivée (même
  passthrough qu'un shader manquant, boot jamais bloqué).

## PR B — `claude/sky-analytic` : modèle de ciel analytique

- **sky.frag** : la couleur du ciel devient une **diffusion simple
  Rayleigh + Mie** calculée par pixel (ray-march léger : 8 échantillons le
  long du rayon vue dans une atmosphère exponentielle, transmittance vers
  le soleil sur 4 échantillons ; constantes physiques standard β_R/β_M,
  H_R=8 km, H_M=1,2 km, planète 6360/6420 km). Résultat : bleu profond au
  zénith, blanchiment à l'horizon, orangés au lever/coucher, nuit qui
  tombe naturellement quand le soleil descend.
- **Bascule sûre** : le float de padding `_pad3[0]` des push constants
  (160 o inchangés) devient `skyModel` (0 = dégradé legacy, 1 = analytique).
  Config `client.sky.analytic` (défaut **true**) lue par Engine au
  remplissage des push constants. Un problème visuel en jeu se contourne
  en 1 clé de config sans rebuild.
- Soleil (disque net + couronne), lune (phases) et bande sous-horizon :
  **conservés tels quels** par-dessus le nouveau fond.
- Les teintes zénith/horizon de DayNightCycle restent envoyées (mode
  legacy + ambiant des nuages) — la météo continue de teinter les nuages.

## Hors périmètre

- `sky_capture.comp` (IBL) : garde le dégradé (la capture ciel sert
  d'ambiance basse fréquence ; à aligner dans un ticket ultérieur).
- Perspective aérienne sur le terrain lointain (ticket futur).
- Étoiles nocturnes.

## Tests

- `cloud_noise_generator_tests` (ctest Linux, pattern
  `lcdlln_add_simple_test`) : déterminisme par seed, périodicité du Worley
  et du Perlin (f(0)==f(période)), tailles/format, distribution non
  dégénérée (min/max/moyenne).
- Shaders : compilés au runtime (glslangValidator) — la CI ne les valide
  pas ; relecture rigoureuse + bascule config comme filet. **Validation
  visuelle utilisateur requise.**

## Déploiement

✅ client uniquement (aucun opcode/migration/config serveur).
