# CMANGOS.05_vmap_collisions_los_height_dynamictree

## Objectif

Mettre en place un système de **collisions 3D server-authoritative** côté
shard LCDLLN, inspiré de `src/game/vmap` cmangos, pour répondre aux
besoins de gameplay :

1. **Line-of-sight (LOS)** : « le joueur A peut-il voir le joueur B
   malgré ce mur ? » — pré-requis pour combat à distance crédible et
   sorts ciblés.
2. **Height query** : « quelle est la hauteur du sol à (x, y) ? » —
   pré-requis pour spawn de loot, téléport, pathfinding 2D, validation
   anticheat de chute.
3. **Projectile / object hit** : « le projectile lancé de A vers B
   touche-t-il un obstacle avant ? » — pré-requis pour flèches, sorts
   directionnels.
4. **DynamicTree pour portes / objets transformables** : ouvrir/fermer
   une porte sans rebuild du tile.
5. **Tile streaming + refcount** : ne charger en RAM que les tiles avec
   présence joueur, scaler à des mondes massifs.

C'est le module avec le plus gros **ROI réutilisable directement** selon
l'analyse cmangos. Sans ça, pas de combat distance crédible, pas de
portes vraiment fermées, anticheat de mouvement très limité.

## Dépendances

- M00.1 (build base)
- CMANGOS.02 (Entities) — `WorldObject::CanSeeObject(target)` câble vmap
- CMANGOS.03 (Grids) — déclenche le streaming des tiles vmap selon les
  cellules `Active`

## Livrables

### Côté shard (`engine/server/shard/vmap/`)

- `VMapManager.{h,cpp}` — singleton qui gère le cache de tiles chargés et
  les 3 requêtes principales :
  - `bool IsInLineOfSight(MapId, Vector3 from, Vector3 to)`
  - `float GetHeight(MapId, Vector3 from, float maxSearchDist)`
  - `bool GetObjectHitPos(MapId, Vector3 from, Vector3 to, Vector3& outHit)`
- `VMapTile.{h,cpp}` — contient la géométrie d'un tile (16 km × 16 km
  par exemple) chargée depuis disque, indexée par BIH.
- `BIH.{h,cpp}` — Bounding Interval Hierarchy templated. Plus compact
  qu'un BVH classique, optimisé pour scènes statiques.
- `ManagedModel.{h,cpp}` — wrapper refcount autour d'un modèle
  géométrique chargé en RAM. `acquire()` incrémente le refcount,
  `release()` décrémente. Décharge si refcount tombe à 0 + délai.
- `DynamicTree.{h,cpp}` — BSP/BVH séparé pour les `GameObject`
  transformables (portes, ponts, blocages d'évents). Fusionné aux
  requêtes du tile statique.
- `VMapStreamer.{h,cpp}` — réagit aux transitions `Loaded ↔ Active`
  des cellules grid (CMANGOS.03) pour `acquire()` / `release()` les
  tiles.

### Outil offline (`tools/vmap_extractor/`)

- `tools/vmap_extractor/vmap_extractor.cpp` — outil `lcdlln_vmap_extractor`
  qui prend des assets sources (mesh exports OBJ/GLTF + heightmaps déjà
  utilisées côté client) et produit des `.vmap` optimisés (BIH bake).
  Conversion **offline**, pas runtime.
- `tools/vmap_extractor/CMakeLists.txt` — build standalone.

### Format `.vmap` (`engine/server/shard/vmap/`)

- `VMapFormat.{h,cpp}` — schéma binaire d'un tile compilé : header,
  table de modèles, BIH compactée, index spatial.

### Configuration (`config.json`)

```json
"vmap": {
  "tiles_dir": "vmap",
  "max_loaded_tiles": 64,
  "release_delay_sec": 60,
  "los_ray_thickness": 0.0,
  "height_search_dist_default": 50.0
}
```

`tiles_dir` est relatif à `paths.content`. Aucun chemin absolu.

### Tests

- `engine/server/shard/vmap/BIHTests.cpp` — raycast contre une scène
  synthétique (cubes), résultats attendus.
- `engine/server/shard/vmap/VMapManagerTests.cpp` — `IsInLineOfSight`
  avec un mur entre A et B → false ; sans mur → true.
- `engine/server/shard/vmap/DynamicTreeTests.cpp` — porte fermée bloque,
  ouverte ne bloque pas, rebuild en O(1) sur état.
- `engine/server/shard/vmap/ManagedModelTests.cpp` — refcount monte et
  descend, décharge après délai.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : tiles `.vmap` sous `/game/data/vmap/`
- Outils offline : uniquement sous `/tools` (`tools/vmap_extractor/`)
- Tous les chemins de tiles relatifs à `paths.content` (config.json)
- ❌ Interdit : créer un dossier racine non autorisé (`/vmaps`, `/collision`, etc.)

## Spécification technique

### 1. BIH (Bounding Interval Hierarchy)

Structure compacte pour raycast static :

- Chaque nœud interne contient 2 plans parallèles à un axe (x ou y ou z)
  et 2 indices de fils (left, right).
- Feuilles : indices d'objets (triangles ou modèles).

Avantages vs BVH classique :
- ~30% moins de mémoire (pas besoin de stocker des bounding boxes
  complètes par nœud).
- Cache-friendly (les plans tiennent dans 2 floats par axe).

```cpp
struct BIHNode {
  uint32_t axis : 2;      // 0=x, 1=y, 2=z, 3=leaf
  uint32_t firstChild : 30;
  float    leftMax;       // plan max du fils gauche
  float    rightMin;      // plan min du fils droit
};
```

### 2. Trois requêtes canoniques

```cpp
class VMapManager {
public:
  // True si la ligne entre from et to ne croise aucun obstacle.
  // Inclut DynamicTree (portes ouvertes/fermées).
  bool IsInLineOfSight(MapId mapId, Vector3 from, Vector3 to) const;

  // Height du terrain au point (from.x, from.y), en cherchant
  // dans une fenêtre verticale de from.z ± maxSearchDist.
  // Retourne -FLT_MAX si rien trouvé.
  float GetHeight(MapId mapId, Vector3 from, float maxSearchDist) const;

  // Si le ray from→to touche un obstacle, retourne true et remplit
  // outHit avec le point d'impact. Sinon false.
  bool GetObjectHitPos(MapId mapId, Vector3 from, Vector3 to,
                        Vector3& outHit) const;
};
```

Ces 3 requêtes couvrent 95% des besoins gameplay (cf. cmangos).

### 3. `ManagedModel` (refcount)

```cpp
class ManagedModel {
public:
  void Acquire();
  void Release();   // décrémente, schedule unload si = 0
  bool IsLoaded() const;
private:
  std::atomic<int> m_refCount{0};
  std::unique_ptr<VMapTile> m_tile;
  int64_t m_releaseTime = 0;
};
```

Au `Release()` avec refcount = 0, on enregistre le timestamp. Le
`VMapStreamer` parcourt périodiquement ses ManagedModel et libère ceux
dont le `releaseTime` est dépassé de `release_delay_sec`. Le délai évite
le yo-yo charge/décharge si un joueur passe rapidement.

### 4. `DynamicTree`

Stockage des GameObject transformables (portes, ponts) :

```cpp
class DynamicTree {
public:
  void Insert(ObjectGuid guid, BoundingBox bbox, GeometryHandle geom);
  void Remove(ObjectGuid guid);
  void SetActive(ObjectGuid guid, bool active);   // porte ouverte/fermée
  bool IsInLineOfSight(Vector3 from, Vector3 to) const;
};
```

Quand `SetActive(guid, false)` est appelé (porte ouverte), le node est
**désactivé** mais pas supprimé — préserve le BSP. Une porte fermée
réactive simplement le node.

### 5. `VMapStreamer`

Câblé sur les transitions de `GridState` (CMANGOS.03) :

- `Grid → Active` : `streamer.AcquireTilesAround(grid.center, range)`.
- `Grid → Idle` : `streamer.ReleaseTilesAround(grid.center, range)` (avec
  délai).

`range` est calculé pour couvrir la distance LOS max d'un joueur
(typiquement 100-150 m selon le jeu).

### 6. Format `.vmap` (offline-baked)

- Header : magic number, version, mapId, tile coords (cx, cy), AABB
  global du tile.
- Table de modèles : pour chaque mesh distinct référencé, une entrée
  contenant ses sommets et triangles.
- BIH : structure d'accélération sérialisée (nodes + indices).
- DynamicGOs initiaux : positions + AABB + handle vers leur géométrie
  source (utilisé au load par DynamicTree).

Le format est **read-only au runtime** : les modifs runtime (porte
ouverte) ne touchent pas le fichier, juste le DynamicTree en RAM.

### 7. Outil offline `lcdlln_vmap_extractor`

Inputs :

- Une liste de meshes sources (OBJ ou GLTF) déjà utilisés par le client
  pour le rendu (à coordonner avec ce qui existe dans `game/data/`).
- Heightmap `.r16h` du terrain (déjà existant dans
  `game/data/zones/<zone>/terrain_height.r16h`).

Process :

1. Charge tous les meshes sources.
2. Pour chaque tile (découpage X×Y selon paramètre), extrait les
   triangles intersectant l'AABB du tile.
3. Construit le BIH du tile.
4. Sérialise en `.vmap` dans `game/data/vmap/<zone>/tile_<cx>_<cy>.vmap`.

Le terrain (heightmap) peut être inclus comme un mesh dégénéré (chaque
quad → 2 triangles) ou stocké séparément avec un raycast spécifique
(plus efficace mais double système). **Démarrer avec mesh dégénéré**,
optimiser si nécessaire.

## Étapes d'implémentation

1. **Créer `engine/server/shard/vmap/`**.
2. **Implémenter `BIH`** templated + tests math (raycast contre cubes synthétiques).
3. **Implémenter `VMapTile`** (chargement format binaire + accès au BIH).
4. **Implémenter `VMapFormat`** (header + sérialisation, pour bootstrap des tests).
5. **Implémenter `VMapManager`** : les 3 requêtes principales.
6. **Implémenter `ManagedModel`** + `VMapStreamer` (refcount + délai release).
7. **Implémenter `DynamicTree`** + intégrer dans `IsInLineOfSight`.
8. **Câbler avec Grids** : transition `Active` → acquire tiles voisins.
9. **Créer `tools/vmap_extractor/`** : version minimale qui prend un OBJ + heightmap → produit un `.vmap`.
10. **Smoke test** : LOS entre 2 entités séparées par un mur (mesh OBJ simple) → false ; LOS sans obstacle → true.
11. **Tests** : 4 fichiers listés.
12. **Doc** : section « Collisions vmap » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard) et build outil `lcdlln_vmap_extractor` OK
- [ ] Tests `BIHTests`, `VMapManagerTests`, `DynamicTreeTests`, `ManagedModelTests` passent
- [ ] Smoke test : 2 entités séparées par un mur (mesh OBJ simple converti en `.vmap`) → `IsInLineOfSight()` retourne `false`. Sans mur → `true`.
- [ ] `GetHeight` retourne la bonne altitude au sol pour 5 points de test (heightmap zone demo_plains)
- [ ] Une porte (DynamicTree) bloque LOS quand fermée, ne bloque pas quand ouverte
- [ ] Refcount `ManagedModel` : tile chargé à l'entrée joueur, déchargé après timeout
- [ ] Aucun chemin absolu dans le code (tout passe par `paths.content`)
- [ ] Aucun nouveau dossier racine non autorisé créé (sauf `tools/vmap_extractor/` qui est sous `/tools`)
- [ ] Rapport final : fichiers modifiés + commandes + résultats + DoD

## Notes / pièges à éviter

- **Précision float** : les requêtes LOS sur de longues distances (100+ m) avec des coordonnées world (peuvent être en dizaines de milliers) accumulent des erreurs float. **Toujours** travailler en coordonnées **locales au tile** dans le BIH ; convertir en local au début de la requête, traduire à la fin.
- **Threading** : `VMapManager` doit être **thread-safe pour les requêtes** (lectures concurrentes ok), mais le **streaming** (acquire/release) doit être sériallisé. Un `std::shared_mutex` autour du cache de tiles suffit. **Ne pas** mettre un mutex sur chaque requête (perf).
- **Edge cases LOS** :
  - Ray exactement parallèle à un plan : test avec epsilon, pas exact.
  - Ray partant d'un point exactement sur la surface : décaler de epsilon dans la direction.
  - Ray length 0 : retourner `true` (pas d'obstacle).
- **DynamicTree memory** : si on a 10 000 GameObject sur une map (raid), le BSP peut devenir gros. **Ne pas** rebuild à chaque insertion ; utiliser une rebuild différée (batch) ou un BVH dynamique (Bullet, Box2D ont des refs).
- **Coordination avec terrain client** : le client a sa propre représentation du terrain (`engine/render/terrain/`). Le tile vmap **doit** rester cohérent avec ce que le client voit. Si le designer modifie une heightmap, le `.vmap` correspondant doit être rebake — ajouter dans `tools/vmap_extractor/` un mode **incrémental** qui ne rebake que les tiles affectés.
- **Format .vmap : versioning** : prévoir un magic number et un champ `version` dès le début. À chaque évolution du format, bumper la version + le `lcdlln_vmap_extractor` produit le nouveau format ; le shard refuse les vieux fichiers avec un log explicite.
- **Mesh dégénéré pour terrain** : 65×65 = 4096 quads × 2 = 8192 triangles par tile heightmap. C'est gros mais OK pour le BIH. Optimisation future : raycast direct sur heightmap (3 multiplications par cellule au lieu de tester N triangles).
- **Pas de glm en dépendance shard** : le client utilise probablement glm pour Vector3, mais le shard n'a pas besoin de la lib complète. Réutiliser `MovementTypedefs.h` (CMANGOS.04) qui définit `Vector3` localement, ou créer un `engine/math/Geom.h` minimal côté serveur.

## Références

- `CMANGOS_ANALYSIS.md` § vmap (P1 shard)
- cmangos `src/game/vmap/VMapManager2.cpp`, `BIH.h`,
  `ManagedModel.h`, `DynamicTree.cpp`, `WorldModel.cpp`,
  `TileAssembler.cpp` (outil)
- À coordonner avec : `engine/render/terrain/` côté client (cohérence du
  terrain), CMANGOS.13 (Maps), CMANGOS.03 (Grids streaming)
