# Feyhin Lokcthat — SP1 : Terrain & hydrographie (design)

> Date : 2026-06-04 · Statut : design validé en brainstorming, à relire avant plan d'implémentation.
> Sous-projet **SP1** du chantier « Feyhin Lokcthat » (reproduction jouable de la cité des
> gladiateurs sur la carte de lancement). SP1 = **fondation** : relief, eau, surfaces,
> plateformes. Les bâtiments (pont, faubourg, monument, cité) viennent dans SP2→SP5.

## 1. Contexte & objectif

Au lancement, le jeu charge la zone `demo_plains`. On veut, à terme, que le joueur se
promène dans la vallée de **Feyhin Lokcthat** (planche de concept fournie : vallée encaissée
nord-sud, rivière en méandre, pont à arches, faubourg étagé, monument isolé sur éperon, cité
haute perchée sur le massif est). SP1 fabrique **uniquement le terrain et l'eau** de cette
vallée, praticables à pied, et bascule la carte de lancement dessus.

Le découpage complet du chantier :

| # | Sous-projet | Dépend de |
|---|---|---|
| **SP1** | **Terrain & hydrographie** (ce spec) | — |
| SP2 | Pont à arches | SP1 |
| SP3 | Faubourg bas (+ arbre, rochers en objets) | SP1 |
| SP4 | Monument isolé | SP1 |
| SP5 | Cité haute | SP1 |
| SP6 (Vague 2) | Collision mesh + intérieurs clés | moteur + SP3/4/5 |

## 2. Décisions verrouillées (issues du brainstorming)

- **Fidélité** : reproduction fidèle complète, à découper en sous-projets.
- **Walkable** : extérieurs praticables maintenant (Vague 1) ; intérieurs clés plus tard (Vague 2).
- **Géographie** : figée par le croquis utilisateur → plan de masse `docs/feyhin_lokcthat/plan_masse.html` (v7).
- **Pic de la cité** : la cité haute est sur son **propre pic rocheux détaché**, **en avant** (côté rivière)
  du massif est — **PAS** posée sur le massif. Le massif est est un mur séparé, derrière.
- **Génération** : **générateur PowerShell paramétrique** (précédent `Generate-DemoPlainsAssets.ps1`),
  exécutable par Claude → binaires committés. **Pas de `zone_builder` en SP1** (le terrain passe
  par le manifeste legacy single-zone).
- **Échelle** : `world_size = 1536 m`, `height_scale = 512 m`, heightmap **1025×1025**, origine centrée `(-768, -768)`.
- **Zone** : nouvelle zone `feyhin_lokcthat` (on **ne touche pas** `demo_plains`).

## 3. Repère & système de coordonnées

- Monde : **X = est(+) / ouest(−)**, **Z = nord(+) / sud(−)**, Y = altitude (m). 1 unité = 1 m.
- Terrain carré centré : étendue `[-768, +768]` en X et Z (origine `-768`, côté `1536`).
- Heightmap HAMP `1025×1025` u16, rééchantillonné sur l'étendue
  (`vertStepWorld = world_size / 1024 = 1.5 m/texel`).
- Encodage hauteur : `height_m = (u16 / 65535) * height_scale`, soit **0 → 512 m**.
- POV (cavaliers) au **sud**, regard vers le **nord**. Nord = haut du plan de masse.

### Coordonnées monde des éléments (d'après le plan v6 ; valeurs conventionnelles ajustables)

| Élément | Empreinte XZ (m) | Altitude cible (m) |
|---|---|---|
| Niveau d'eau | — | **60** |
| Lit de rivière (sous l'eau) | le long du méandre | ~50 |
| Spline méandre (centerline, [Z,X]) | (640,30) (560,85) (470,55) (380,5) (300,−45) (210,5) (120,55) (40,20) (−60,−15) | — |
| Largeur rivière | ~120 m (nord) → ~145 m au pont → ~180 m (sud) | — |
| Pont (emplacement, plateforme) | Z≈300, X −135..+90 (~225 m) | tablier ~ niveau berges |
| Versant ouest (boisé) | X ≤ −120 | monte à **~270** (≈210 de relief) |
| **Pic de la cité (DÉTACHÉ, assise SP5)** | X +90..+175, Z +200..+400 | crag **~470**, plateau sommital **~410** (plat) |
| Selle (entre pic cité et massif) | X +175..+235 | creux **~180** (détache le pic) |
| Massif est (mur de fond, séparé) | X ≥ +235 | monte à **~480** |
| Éperon monument (assise SP4) | X≈+55, Z≈+345 | sommet **~180** |
| Faubourg (assise SP3, étagée) | X +40..+92, Z +205..+290 | 70 → 140 (descend à l'eau) |
| Arbre isolé (SP3) | X≈+78, Z≈+150 | sol |
| Rochers amont / aval (SP3) | amont X≈−40 Z≈+355 ; aval X≈+25 Z≈+110 | émergés ~+8 |
| Crête de fond (nord) | Z ≥ +600 | **~250** |
| Spawn joueur (POV) | X≈−30, Z≈−40 | `GroundHeightAt + hauteur œil`, face +Z |

> Les altitudes sont des **valeurs de départ** ; elles seront affinées via l'aperçu HTML (§8)
> et la validation en jeu (§11). Contrainte dure : tout reste dans `[0, 512]`.

## 4. Livrables

Nouvelle zone `game/data/zones/feyhin_lokcthat/` :

- `terrain_height.r16h` — HAMP, 1025×1025 u16.
- `terrain_splat.slap` — SLAP RGBA8 (4 couches : grass / dirt-sable / rock / snow).
- `terrain_grass.grms` — GRMS R8 (densité herbe détail).
- `atmosphere.json` — soleil + ambiant.
- `zone.meta`, `runtime_manifest.json` — manifeste legacy v3 (cohérence avec `demo_plains`).

Eau (config, pas de fichier) :

- L'eau de SP1 est une **nappe plate** posée via `config.json` `world.test_water` (niveau ~60 m,
  grande emprise couvrant la vallée), branchée au collider (`BindWater`) → gué/nage. On **n'écrit
  pas** `instances/water.bin` : son `LoadWaterBin` valide un **xxHash64** du payload (pénible à
  réimplémenter fidèlement en PowerShell). La rivière maillée WATR (largeur/profondeur/flux) et un
  vrai `water.bin` par-zone sont **reportés** à un raffinement ultérieur — même résultat visuel ici
  (eau plate au niveau 60, le `water.frag` clippe la nappe au rivage réel du relief).

Outillage & docs :

- `tools/world/Generate-FeyhinLokcthatAssets.ps1` — le générateur paramétrique.
- `docs/feyhin_lokcthat/apercu_relief.html` — aperçu ombrage émis par le générateur.

Config :

- `config.json` : repointage `render.terrain.*` + bloc `terrain.*` (échelle) + spawn (§9).

## 5. Le générateur — `Generate-FeyhinLokcthatAssets.ps1`

Structure du script :

1. **Bloc paramètres** en tête : échelle (`worldSize`, `heightScale`, `origin`, `res`),
   `waterLevel`, et la **géométrie** (spline du méandre + demi-largeurs, bords des versants,
   pente/raideur, plateau cité, éperon monument, profil d'entonnoir, crête de fond, altitudes
   cibles du §3). Tout paramétré → un seul endroit à régler.
2. **Helpers** : conversions monde↔texel, clamp hauteur, distance point→polyligne (pour la
   spline du méandre), encodage u16.
3. **Synthèse du champ de hauteurs** (buffer `byte[]` unique, écrit en une passe pour la perf) :
   chaque texel = combinaison de contributions :
   - **Sol de vallée** : plancher bas près de l'eau, remontant doucement vers les bords.
   - **Carve rivière** : `d = distance(texel, spline)` ; si `d < demiLargeur(Z)` → abaisser sous
     `waterLevel` (lit), puis berges qui remontent en `smoothstep` au-delà.
   - **Versant ouest** : pour `X < bordOuest`, rampe jusqu'à ~270 m (profil boisé, pente moyenne).
   - **Pic de la cité (DÉTACHÉ)** : crag rocheux **autonome** sur l'empreinte cité, flancs raides,
     **plateau sommital aplani ~410 m** (assise SP5). Il s'élève de la vallée **en avant** du massif,
     PAS dessus.
   - **Selle** : creux (~180 m) entre le pic de la cité et le massif est, pour bien **détacher** le pic.
   - **Massif est** (mur de fond séparé) : pour `X > +235`, rampe jusqu'à ~480 m, **derrière** le pic.
   - **Éperon monument** : bosse rocheuse localisée (~180 m) détachée du massif, côté rivière.
   - **Entonnoir** : la largeur du plancher/rivière décroît vers le nord.
   - **Crête de fond** (nord) : montée ~250 m.
   - **Passe de lissage** légère (moyenne 3×3 sur quelques itérations) anti-marches, hors falaises voulues.
4. **Splat** (règles altitude/pente, 4 couches) : pente forte → **rock** ; sommets > ~450 m →
   **snow** ; bas près de l'eau / lit → **dirt-sable** ; mi-pente douce → **grass** (défaut).
   Mapping canal RGBA→palette confirmé contre `game/data/terrain/layer_palette.json` à l'impl.
   Invariant : chaque texel a une couche dominante non nulle.
5. **Masque herbe** (GRMS) : densité élevée sur grass + pente douce + proche du plancher ; nul
   sur rock / eau / plateau.
6. **Atmosphère** : soleil chaud bas (fin d'après-midi de la planche), p.ex.
   `direction ≈ [-0.40, 0.45, 0.80]` (lumière venant du nord, rasante), `color ≈ [1.0, 0.93, 0.80]`,
   `ambient ≈ [0.06, 0.07, 0.10]`. Schéma `{version, sun:{direction,color}, ambient:{color}}`.
7. **Eau** : configurée dans `config.json` (`world.test_water`), pas générée par le script — voir §4.
8. **zone.meta / runtime_manifest.json** : repris du modèle `demo_plains` (manifeste v3,
   `terrain_world_size_m = 1536`).
9. **Aperçu HTML** : voir §8.

**Performance** : 1025² ≈ 1,05 M texels. On écrit un `byte[]` unique et on minimise le coût par
texel (pas d'appels lourds dans la boucle). Cible : génération en quelques minutes max, acceptable
pour un run hors-ligne. Si trop lent, repli documenté : baisser la résolution du splat/grass à 513²
(le heightmap reste 1025²).

## 6. Pourquoi pas de `zone_builder` en SP1

Le client lit le terrain de lancement **directement** depuis `render.terrain.heightmap/splatmap/
grass_mask` (cf. `Engine.cpp` boot + `TerrainRenderer::Init`), pas depuis les chunks. Le terrain,
le splat, l'herbe, l'eau et l'atmosphère sont donc tous des fichiers lus tels quels. `zone_builder`
ne devient nécessaire que pour les **instances** (props/bâtiments) → SP2 et suivants.

## 7. Bascule carte de lancement + spawn

Dans `config.json` :

- `render.terrain.heightmap` → `zones/feyhin_lokcthat/terrain_height.r16h`
- `render.terrain.splatmap`  → `zones/feyhin_lokcthat/terrain_splat.slap`
- `render.terrain.grass_mask`→ `zones/feyhin_lokcthat/terrain_grass.grms`
- `terrain.world_size = 1536`, `terrain.height_scale = 512`, `terrain.origin_x = -768`, `terrain.origin_z = -768`
- Spawn / caméra « Jouer » → POV sud `(X≈-30, Z≈-40)`, Y = `GroundHeightAt + hauteur œil`, regard +Z.

*(L'emplacement exact de ces clés dans l'arborescence `config.json` est confirmé à l'édition ;
les noms de clés ci-dessus sont ceux lus par le code.)*

## 8. Aperçu avant le jeu (HTML)

Le générateur émet `docs/feyhin_lokcthat/apercu_relief.html` : une grille de hauteurs
**sous-échantillonnée** (p.ex. 192×192) **embarquée en JSON** dans le HTML, rendue en
**ombrage (hillshade)** + colorisation par altitude + courbes de niveau, avec une légende
d'échelle et les repères des éléments du §3. Permet de **valider le relief sans lancer le jeu** :
Claude exécute le générateur, ouvre l'aperçu, le montre à l'utilisateur, on itère sur les
paramètres avant tout test en jeu.

## 9. Marche & collision (état du moteur)

- Terrain heightmap **marchable automatiquement** : `TerrainCollider.BindTerrain` +
  `GroundHeightAt` (spawn posé sur le sol).
- Eau : `BindWater` → test point-dans-polygone du lac → gué / nage.
- Pas de collision props en SP1 (aucun objet encore). Pentes raides du massif = barrière naturelle
  à l'est ; le passage vers la ville se fera par le pont (SP2).

## 10. Robustesse & validation

- **Idempotent** : la regénération écrase proprement (pas d'empilement).
- **Vérifications d'écriture** : magies (`HAMP/SLAP/GRMS/WATR`), résolutions, tailles attendues.
- **Clamp** : toutes les hauteurs dans `[0, height_scale]`.
- **Splat non nul** : chaque texel a une couche dominante (jamais 4 zéros).
- **Garde d'étendue** : `assert` que l'empreinte de la scène (§3) tient dans `[-768, 768]`.
- **Anti-régression terrain** : SP1 ne touche **aucun** pipeline de rendu / `frontFace` /
  `cullMode` (cf. CLAUDE.md « convention winding »). On ne fait que produire des données.

## 11. Tests

- **Aperçu HTML** : forme du relief conforme au plan v6 (vallée, méandre, versants, massif,
  plateau cité, éperon monument).
- **En jeu** : terrain visible (pas le bug terrain-invisible, qui est côté winding — non touché) ;
  eau présente au bon niveau ; le perso spawn au sud, marche, descend à l'eau (gué/nage), gravit
  les pentes, atteint l'emplacement du pont ; le massif barre l'est.
- **Contrôles numériques** (via aperçu/log) : largeur rivière ~145 m au pont ; emprise pont ~200 m ;
  massif ~500 m ; versant ouest ~270 m ; plateau cité ~410 m ; éperon ~180 m.

## 12. Hors-scope SP1

Pont (SP2) ; faubourg, arbre, rochers en tant qu'**objets** (SP3) ; monument bâti (SP4) ; cité
bâtie (SP5) ; **forêt** en instances d'arbres (passe foliage ultérieure — SP1 ne fait que suggérer
la végétation via le splat/herbe). Collision mesh & intérieurs (SP6, Vague 2).

## 12bis. Notes d'implémentation (écarts assumés vs plan)

- **Modèle `HeightAt`** : plancher → carve rivière **dans le plancher seulement** → **MAX** des reliefs
  (versant/massif/pic/éperon/crête). Garantit que le monument et le pic ne sont jamais noyés par la
  rivière (rocher au bord de l'eau). Diffère du plan initial (max puis min) qui noyait le monument.
- **Perf SLAP/GRMS** : échantillonnent la grille `$heightsM` déjà calculée (lookups + pente par voisins)
  au lieu de rappeler `HeightAt` ~2,6 M fois. Génération complète ~12 min (dominée par la synthèse 1025²).
- **Mapping canal SLAP confirmé** (terrain.frag legacy) : **R=grass, G=dirt, B=rock, A=snow**
  (`$CH_GRASS=0, $CH_DIRT=1, $CH_ROCK=2, $CH_SNOW=3`).
- **Items mineurs connus** (revue qualité, non bloquants SP1) : (I1) pente surestimée ×2 sur la frange de
  bord du splat → légère frange « roche » en bordure de carte (hors scène) ; (I2) pic plafonné à 410 m
  (pas de pointe à 470 m — hauteur ajoutée par les bâtiments en SP5) ; (M1) seuil neige 430 m (spec ~450) ;
  (M2) ombrage HTML : constante 16 non paramétrée ; à revisiter si besoin en SP2+.
- **À régler en jeu (Task 7)** : `world.default_spawn.yaw_deg` (convention d'orientation à confirmer pour
  regarder le nord) et `world.test_water.depth_m` (si le niveau lu ≠ ~60 m).

## 13. Déploiement

> **Déploiement : ✅ client uniquement, pas de redéploiement serveur.** SP1 = génération d'assets
> de zone + repointage `config.json` (terrain/eau/atmosphère/spawn). Aucun opcode, handler,
> migration DB ni gating sécurité. Le terrain et l'eau sont lus côté client.
