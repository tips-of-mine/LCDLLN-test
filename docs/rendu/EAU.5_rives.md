# EAU.5 — Rives : mares de mer, écume des rives plates, lacs qui débordent

## Symptômes (captures du 03/11)

1. **Mare blanche dans une plaine côtière** : une flaque ovale à l'écume
   épaisse, d'aspect laiteux, en pleine terre, loin du rivage.
2. **Lac qui déborde** (forêt) : d'un côté, la nappe d'eau dépasse la berge et
   se termine par un bord droit qui flotte au-dessus d'une pente descendante.

## Causes

### 1. « Mares de mer » (`tools/world_gen`)

La nappe de la mer d'une cellule côtière couvre **toute** la cellule, au niveau
de la mer (40 m). Dans les bandes côtières du lore (`,` côte, `_` plage),
le relief oscille autour de ce niveau : chaque creux un peu plus bas faisait
apparaître la mer **à l'intérieur des terres**, avec l'écume des rivages et
les moutons de la houle — d'où la mare blanche.

### 2. Écume trop large sur les rives plates (`water.frag`)

L'écume suivait l'**épaisseur d'eau** (moins de 0,7 m). Sur une rive presque
plate, l'eau reste peu profonde sur des dizaines de mètres : l'écume couvrait
toute cette bande au lieu d'un liseré au bord.

### 3. Lacs qui débordent (`tools/world_gen`)

- La cuvette était creusée selon un **cercle** (rayon moyen), alors que la
  nappe suit un **polygone bruité** (±8 %) : par endroits, la nappe dépassait
  la zone creusée (et ailleurs, une fosse sèche restait sous le niveau de
  l'eau).
- Les rives n'étaient qu'**abaissées** jusqu'au niveau de l'eau. Un lac posé à
  flanc de pente gardait, côté aval, un sol plus bas que l'eau : la nappe
  flottait au-dessus du vide.

## Corrections

| Fichier | Changement |
|---|---|
| `tools/world_gen/lib/Terrain.cpp` | `FillLandlockedSea` : après le relief de base, les cuvettes sous la mer qui ne communiquent pas avec la mer sont relevées à 40,5 m. Communiquer = toucher, par des échantillons sous la mer, un biome marin (` ` ou `-`) ou le bord de la cellule (prudence : la mer peut être chez la voisine). |
| `tools/world_gen/lib/Terrain.cpp` | `CarveLake` : cuvette creusée d'après le **polygone** (`LakeRadiusAt`) ; au bord, sol à 0,3 m au-dessus de l'eau ; relief plus haut → grève douce sur 40 m (inchangé) ; relief plus bas → **digue naturelle** (crête de 8 m puis pente ≈ 19°), sauf sur un lit de rivière (moins de 12 m). |
| `tools/world_gen/lib/Terrain.cpp` | `World::IsWater` suit aussi le polygone (plus d'arbres plantés dans les avancées du lac). |
| `game/data/shaders/water.frag` | Profondeur **verticale** reconstruite (fond et surface sont sur le même rayon caméra), pente du fond mesurée d'un pixel à l'autre → **distance au rivage**. L'écume est limitée à ≈ 1,8 m du rivage (et 0,7 m de profondeur), quelle que soit la pente ; les lignes de ressac suivent cette distance (parallèles au rivage). |
| `tools/world_gen/main.cpp` | Le rapport affiche le nombre d'échantillons comblés. |

## Tests

`world_gen_tests` (section 7) :

- cuvette isolée comblée, mer et chenal relié conservés, cuvette touchant le
  bord conservée, cas sans biome ;
- rayon du lac interpolé entre les sommets du polygone (angles négatifs
  compris) ;
- lac sur une pente de 60 m → 20 m : fond sous l'eau dans tout le polygone,
  rive au-dessus de l'eau tout autour (digue côté aval), relief d'origine
  retrouvé au-delà, pas de digue sur un lit de rivière.

## Mesure sur le monde généré

Voir la description de la PR (avant / après, mêmes cellules).

## Déploiement

Le monde est régénéré par la CI (`lcdlln_world_gen`) : les nouvelles
heightmaps partent avec le client **et** avec le shard (hauteur du sol
côté serveur). Déployer les deux ensemble.
