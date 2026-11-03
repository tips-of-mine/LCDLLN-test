# EAU.5 — Rives : mares de mer, écume des rives plates, lacs qui débordent

## Symptômes (captures du 03/11)

1. **Mare blanche dans une plaine côtière** : une flaque ovale à l'écume
   épaisse, d'aspect laiteux, en pleine terre, loin du rivage.
2. **Lac qui déborde** (forêt) : d'un côté, la nappe d'eau dépasse la berge et
   se termine par un bord droit qui flotte au-dessus d'une pente descendante.

## Causes

### 1. « Mares de mer » (`tools/world_gen`)

La nappe de la mer d'une cellule côtière couvre **toute** la cellule, au niveau
de la mer (40 m). Dans les bandes côtières du lore (`,` côte, `_` plage), le
relief oscille autour de ce niveau : chaque creux un peu plus bas faisait
apparaître la mer **à l'intérieur des terres**, avec l'écume des rivages et les
moutons de la houle — d'où la mare blanche. Le fond des étangs creusés près de
la mer passait aussi sous son niveau : la mer apparaissait sous l'eau de
l'étang.

### 2. Lacs et étangs qui débordent (`tools/world_gen`)

- Niveau d'eau = moyenne du relief − 0,8 m : à flanc de pente, le côté aval
  était **plus bas que l'eau** (jusqu'à 40 m) → nappe « perchée ».
- Cuvette creusée selon un **cercle** (rayon moyen) alors que la nappe suit un
  **polygone bruité** (±8 % pour les lacs, ±12 % pour les étangs).
- Rives seulement **abaissées** au niveau de l'eau, jamais relevées.

Mesure sur le monde généré (`main`) : **340 lacs et étangs sur 350** avaient
une partie du contour au-dessus du sol.

### 3. Écume trop large sur les rives plates (`water.frag`)

L'écume suivait l'**épaisseur d'eau** (moins de 0,7 m). Sur une rive presque
plate, l'eau reste peu profonde sur des dizaines de mètres : l'écume couvrait
toute cette bande au lieu d'un liseré au bord.

## Corrections

| Fichier | Changement |
|---|---|
| `tools/world_gen/lib/Terrain.cpp` | `FillLandlockedSea` : après le relief de base, les cuvettes sous la mer qui ne communiquent pas avec la mer sont relevées à 40,5 m. Communiquer = toucher, par des échantillons sous la mer, un biome marin (` ` ou `-`) ou le bord de la cellule (prudence : la mer peut être chez la voisine). |
| `tools/world_gen/lib/Terrain.cpp` | `LakeWaterLevel` : niveau = moyenne − 0,8 m, mais **jamais plus haut que le bas de la rive** (15e centile du relief de base, 10 m au-delà du contour) − 0,3 m. Fonction pure : raccord exact entre cellules. |
| `tools/world_gen/lib/Terrain.cpp` | `CarveLake` : cuvette creusée d'après le **polygone** (`LakeRadiusAt`) ; sur les 6 derniers mètres, le fond remonte jusqu'à 0,3 m au-dessus de l'eau : le bord de la nappe est **enfoui sous la rive**. Dehors : grève élargie quand la rive domine (pente ≤ 22° environ), **digue** quand le relief est plus bas (crête de 8 m puis pente ≈ 19°). Jamais de rehaussement sur un lit de rivière (moins de 12 m) ni sur un plateau urbain. Fond jamais sous la mer + 0,1 m. |
| `tools/world_gen/lib/Places.cpp` | Étangs : même règle de niveau ; emplacement refusé si l'eau serait à moins de 3 m au-dessus de la mer. |
| `tools/world_gen/lib/Terrain.cpp` | `World::IsWater` suit le polygone (plus d'arbres dans les avancées du lac). |
| `game/data/shaders/water.frag` | Profondeur **verticale** reconstruite (fond et surface sont sur le même rayon caméra), pente du fond mesurée d'un pixel à l'autre → **distance au rivage**. Écume limitée à ≈ 1,8 m du rivage (4,5 m pour le ressac de la mer) et 0,7 m de profondeur, quelle que soit la pente ; lignes de ressac parallèles au rivage. Calcul fait avant le `discard` (dérivées écran). |
| `tools/world_gen/main.cpp` | Le rapport affiche le nombre d'échantillons comblés. |

## Tests

`world_gen_tests` (section 7) :

- cuvette isolée comblée, mer et chenal relié conservés, cuvette touchant le
  bord conservée, cas sans biome ;
- rayon du lac interpolé entre les sommets du polygone (angles négatifs
  compris) ;
- lac sur une pente de 60 m → 20 m : fond sous l'eau dans le polygone, rive
  au-dessus de l'eau tout autour (digue côté aval), relief d'origine retrouvé
  au-delà, pas de digue sur un lit de rivière, fond jamais sous la mer.

## Mesure (6 cellules à problèmes, régénérées)

| | avant | après |
|---|---|---|
| Échantillons sous la mer isolés (cuvettes) | 60 987 | 0 |
| Lacs / étangs dont la nappe déborde | 13 / 13 | 0 / 12 |

(Un étang en moins : trop près du niveau de la mer, refusé.)

## Déploiement

Le monde est régénéré par la CI (`lcdlln_world_gen`). Les heightmaps et l'eau
partent avec le client ; les points d'apparition (hauteur) avec le shard.
