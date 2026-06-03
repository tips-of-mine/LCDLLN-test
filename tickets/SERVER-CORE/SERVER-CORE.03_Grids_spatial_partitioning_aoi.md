# SERVER-CORE.03_Grids_spatial_partitioning_aoi

## Objectif

Mettre en place le **partitionnement spatial 2D** du monde côté shard
LCDLLN, base de toute simulation MMO scalable :

1. **Grille 2D de taille fixe** sur chaque `Map` du shard. Chaque cellule
   contient les `WorldObject` qui s'y trouvent.
2. **Visitor pattern templated** sur les types contenus (Player, Creature,
   GameObject, …) : dispatch statique compile-time, zéro virtual call dans
   le hot path d'AoI.
3. **Searchers / Workers / Checks composables** : foncteurs paramétrant
   les searchers — on écrit `NearestAttackableUnitInObjectRangeCheck`
   une fois et on le passe à n'importe quel searcher.
4. **GridStates state machine** (`Loaded` / `Active` / `Idle` / `Removal`) :
   chargement/déchargement dynamique selon la présence de joueurs. On
   n'update que les cellules `Active`.
5. **`GridNotifiers` pour broadcast spatial** : `MessageDistDeliverer` ne
   parcourt que les cellules dans le rayon, jamais la map entière.

C'est le ticket **fondateur** pour l'AoI : toute boucle sur les
`WorldObject` (snapshot, broadcast chat de proximité, recherche de
cibles, AoE de sort) doit passer par les grids. Sans ça, dès 100 joueurs
co-localisés on a un O(N²) qui tue le tick.

## Dépendances

- M00.1 (build base)
- SERVER-CORE.02 (Entities) — `WorldObject::AddToWorld()` insère dans la grille
- Pré-requis pour : SERVER-CORE.04 (Movement), SERVER-CORE.05 (vmap), SERVER-CORE.13 (Maps), tout le PvE/PvP

## Livrables

### Côté shard (`engine/server/shard/grid/`)

- `GridDefines.h` — constantes : `kCellSize` (mètres), `kCellsPerMap`,
  enums `GridState` (`Loaded`, `Active`, `Idle`, `Removal`).
- `GridRefManager.h` (templated) — intrusive list reference manager :
  chaque WorldObject porte un `GridReference<T>` qui s'auto-invalide à
  la suppression. Évite les dangling pointers sans coût atomique
  `shared_ptr`.
- `Grid.{h,cpp}` (templated) — une cellule. Contient `GridRefManager` par
  type d'entité présent.
- `GridMap.{h,cpp}` — la grille 2D entière de la Map. API
  `GetGrid(x, y)`, `LoadGrid(x, y)`, `UnloadGrid(x, y)`.
- `GridStateMachine.{h,cpp}` — gestion des transitions Loaded ↔ Active ↔
  Idle ↔ Removal selon présence joueurs et timers.

### Visitor / Searchers / Notifiers (`engine/server/shard/grid/`)

- `GridVisitors.h` — templates `Visitor<T>` qui visitent un type spécifique.
- `GridSearchers.h` — templates `WorldObjectSearcher<Check>`,
  `CreatureListSearcher<Check>`, etc. paramétrés par un Check (foncteur).
- `GridChecks.h` — foncteurs prêts à l'emploi :
  `NearestAttackableUnitInObjectRangeCheck`,
  `AnyPlayerInObjectRangeCheck`,
  `MostHpMissingNotSelfFriendlyCheck`, …
- `GridNotifiers.{h,cpp}` :
  - `MessageDistDeliverer` — broadcast un paquet aux joueurs dans rayon.
  - `VisibleNotifier` — recompute la visibility pour un joueur (qui voit
    qui).

### Configuration (`config.json`)

```json
"grid": {
  "cell_size_meters": 100.0,
  "active_distance_meters": 200.0,
  "idle_timeout_sec": 300,
  "unload_timeout_sec": 1800
}
```

### Tests

- `engine/server/shard/grid/GridMapTests.cpp` — insertion/retrait
  d'entités, voisinage correct.
- `engine/server/shard/grid/GridStateTests.cpp` — transitions, timers.
- `engine/server/shard/grid/GridSearcherTests.cpp` — searcher avec un
  Check synthétique trouve les bonnes entités.
- `engine/server/shard/grid/GridNotifierTests.cpp` — `MessageDistDeliverer`
  ne livre qu'aux joueurs dans le rayon, ignore les cellules hors range.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : N/A
- Outils offline : N/A
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. Géométrie de la grille

- Chaque `Map` a une grille `kCellsPerMap × kCellsPerMap` cellules.
- Cellule = carré de `kCellSize` mètres (défaut 100 m).
- Coordonnées cellule : `(cx, cy) = (floor(worldX / kCellSize), floor(worldY / kCellSize))`.
- Indexation : `gridIndex = cy * kCellsPerMap + cx` (1D pour cache-friendly).
- Z ignoré au niveau de la grille (toujours 2D ; vmap gère la 3D LOS).

### 2. `GridReference<T>` (intrusive list)

```cpp
template <class T>
class GridReference {
public:
  GridReference() = default;
  ~GridReference() { unlink(); }
  GridReference(const GridReference&) = delete;
  // move ok
  void link(T* obj, GridRefManager<T>* mgr);
  void unlink();
  T*   getTarget() const { return m_target; }
private:
  T*                   m_target = nullptr;
  GridRefManager<T>*   m_mgr    = nullptr;
  GridReference*       m_prev   = nullptr;
  GridReference*       m_next   = nullptr;
};
```

- Quand un `WorldObject` est détruit, son `GridReference` se détache
  automatiquement de la liste — pas de dangling pointer.

### 3. Visitor pattern templated

```cpp
template <class VISITOR, class TYPE_CONTAINER>
struct ContainerVisitor {
  VISITOR& visitor;
  void Visit(GridRefManager<TYPE_CONTAINER>& m) {
    for (auto& ref : m) {
      visitor.Visit(*ref.getTarget());
    }
  }
};
```

Le visitor a une méthode `Visit(Player&)`, `Visit(Creature&)` etc. — le
compilateur sélectionne la bonne au compile-time. Pas de virtual call.

### 4. Searcher composable

```cpp
template <class CHECK>
class CreatureSearcher {
public:
  CreatureSearcher(Creature*& result, CHECK& check)
    : m_result(result), m_check(check) {}
  void Visit(Creature& c) {
    if (m_check(c)) m_result = &c;  // ou conserve le meilleur
  }
private:
  Creature*& m_result;
  CHECK&     m_check;
};
```

Usage :

```cpp
NearestAttackableUnitInObjectRangeCheck check(self, 30.0f);
Creature* nearest = nullptr;
CreatureSearcher searcher(nearest, check);
gridMap.VisitNeighbours(self->GetPosition(), 30.0f, searcher);
```

### 5. State machine `GridState`

| État | Quand | Update | Mémoire |
|---|---|---|---|
| `Loaded` | Cellule en RAM mais aucun joueur ne la regarde | Non | Oui |
| `Active` | Au moins 1 joueur dans `active_distance_meters` | **Oui** | Oui |
| `Idle` | Plus de joueur depuis `idle_timeout_sec` | Non (mais entités gardées) | Oui |
| `Removal` | Idle depuis `unload_timeout_sec` | Non | **Non**, déchargée du DB cache |

Transitions :

- `Loaded → Active` : un joueur entre.
- `Active → Idle` : tous les joueurs partent + délai.
- `Idle → Active` : un joueur revient.
- `Idle → Removal` : timeout dépassé sans joueur.

Le tick de la `Map` (SERVER-CORE.13) ne visite **que** les cellules `Active`
pour mouvements/IA. Économie majeure de CPU sur les zones désertes.

### 6. `MessageDistDeliverer`

```cpp
class MessageDistDeliverer {
public:
  MessageDistDeliverer(WorldObject const& source, ByteBuffer&& packet, float dist);
  void Visit(Player& target);
  void Visit(Creature& target);  // no-op pour broadcast joueur uniquement
private:
  WorldObject const&  m_source;
  ByteBuffer          m_packet;
  float               m_distSq;
};
```

`gridMap.VisitNeighbours(source.pos, dist, deliverer)` envoie le packet à
chaque Player dans le rayon, sans toucher aux cellules hors range.

Utilisé par : chat de proximité (`say`/`yell`/`emote` côté shard, cf.
SERVER-CORE.01), broadcast d'animation, snapshot AoI.

## Étapes d'implémentation

1. **Créer `engine/server/shard/grid/`**.
2. **Implémenter `GridReference` + `GridRefManager`** (templated, intrusive list).
3. **Implémenter `Grid<T>` + `GridMap`** : insertion/retrait, lookup par coord, voisinage 3×3 cellules.
4. **Implémenter `ContainerVisitor`** + un visitor de test trivial.
5. **Implémenter le 1er searcher (`CreatureSearcher`)** + 1er check (`NearestAttackableUnitInObjectRangeCheck`).
6. **Implémenter `MessageDistDeliverer`** + test : broadcast à 3 joueurs dont 1 hors range → 2 reçoivent.
7. **Implémenter `GridStateMachine`** : transitions selon présence joueurs + timers.
8. **Câbler `WorldObject::AddToWorld()`** (SERVER-CORE.02) : insère le WorldObject dans la grille appropriée.
9. **Câbler `Map::Update()`** : ne visite que les cellules `Active`.
10. **Tests** : 4 fichiers listés.
11. **Doc** : section « Grid spatial / AoI » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard) via presets existants
- [ ] Tests `Grid*Tests` passent
- [ ] Smoke test : 100 entités spawned aléatoirement sur une map ; un searcher avec rayon 50 m ne visite que les cellules pertinentes (mesurable via compteur)
- [ ] `MessageDistDeliverer` envoie à exactement les joueurs dans le rayon (test paramétré)
- [ ] State machine : 1 cellule passe `Loaded → Active` en entrée joueur, `Active → Idle` après timeout, `Idle → Removal` après timeout
- [ ] Aucun nouveau dossier racine non autorisé créé
- [ ] Rapport final : fichiers modifiés + commandes + résultats + DoD

## Notes / pièges à éviter

- **Cell size** : 100 m est un défaut raisonnable pour un MMO (joueur voit ~50-80 m, donc voisinage 3×3 cellules ≈ 300×300 m ≈ couvre 99% des AoI). Trop petit = trop de cellules à visiter (overhead). Trop grand = chaque cellule trop chargée. **Profilage avant ajustement.**
- **Voisinage 3×3 vs 5×5** : pour la plupart des recherches AoI, 3×3 cellules autour du source suffit (rayon < 1.5 × cellSize). Les recherches à grand rayon (broadcast world chat, AoE sort epic) doivent demander 5×5 ou plus — exposer un paramètre `radius` à `VisitNeighbours`, pas une constante.
- **Dispatch statique** : ne **jamais** introduire de virtual call dans le visitor. Si on a besoin d'un dispatch dynamique (rare), utiliser `std::variant` ou un visitor concret par type — pas une classe abstraite avec `virtual Visit()`.
- **Z ignoré** : la grille est 2D. Un dragon volant à 100 m d'altitude est dans la même cellule que le joueur au sol. Pour les requêtes 3D précises (LOS, height), passer par vmap (SERVER-CORE.05). La grille est uniquement un filtre grossier.
- **GridReference ≠ pointeur partagé** : si du code conserve un `WorldObject*` brut au-delà du tick, c'est un bug. Toute référence longue durée doit utiliser un `ObjectGuid` (SERVER-CORE.02) + lookup à chaque utilisation.
- **State machine timers** : utiliser une horloge monotone (`std::chrono::steady_clock`), pas wall clock — sinon un changement d'heure système peut faire decharger toute la map d'un coup.
- **Persistance des entités à l'unload** : à l'`Idle → Removal`, les Creatures non-spawned-pool doivent être sauvegardées en DB si elles ont un état modifié (HP bas, position non-default). À détailler dans un futur ticket Map (SERVER-CORE.13).

## Références

- `SERVER-CORE_ANALYSIS.md` § Grids (P1 shard)
- server-core `src/game/Grids/GridDefines.h`, `GridRefManager.h`,
  `GridNotifiers.cpp`, `GridSearchers.h`, `GridStates.cpp`
- server-core `src/game/Maps/Map.cpp` (utilisateur principal des grids)
