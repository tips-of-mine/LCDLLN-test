# CMANGOS.05 — vmap (collisions / LOS / height / DynamicTree)

> **Ticket source** : [tickets/CMANGOS/CMANGOS.05_vmap_collisions_los_height_dynamictree.md](../../../../tickets/CMANGOS/CMANGOS.05_vmap_collisions_los_height_dynamictree.md)
> **Priorité** : P1 — squelette
> **Cible** : shard

## 1. Statut implémentation

❌ **Absent** — aucun système de raycast / line-of-sight server-authoritative,
aucun BIH/BVH compilé offline, aucun DynamicTree pour portes/objets
transformables. Pas de tile streaming refcount.

## 2. Preuves dans le code

**Existant (concerne autre chose — physique éditeur) :**
- [tickets/M100/M100.12-CollisionProxySystem.md](../../../../tickets/M100/M100.12-CollisionProxySystem.md) — ⚠️ **collision
  proxies pour l'éditeur** (Capsule/ConvexHull/TriMesh par mesh). C'est un
  système de physique d'éditeur (placement de props), **pas** un système de
  LOS/height server-authoritative.
- [engine/world/](../../../../engine/world/) (WorldModel, ChunkPackageLayout, StreamingScheduler,
  PakReader…) — pipeline de streaming chunks/HLOD côté client/world. Contient
  des notions proches mais ne fournit pas les 3 requêtes vmap canoniques.
- [tools/](../../../../tools/) — répertoires `world/`, `zone_builder/`, `hlod_builder/` mais pas
  de `vmap_extractor/`.

**Manquant (vs spec ticket) :**
- ❌ `engine/server/shard/vmap/` — dossier inexistant
- ❌ `VMapManager` — aucune des 3 requêtes (`IsInLineOfSight`, `GetHeight`,
  `GetObjectHitPos`) n'existe au niveau shard
- ❌ `BIH<T>` (Bounding Interval Hierarchy templated)
- ❌ `VMapTile` / `VMapFormat`
- ❌ `ManagedModel` (refcount + délai release)
- ❌ `DynamicTree` (BSP/BVH pour portes transformables)
- ❌ `VMapStreamer` (câblage avec GridState `Active`)
- ❌ Outil offline `tools/vmap_extractor/` + format `.vmap`
- ❌ Config `vmap.tiles_dir`, `vmap.max_loaded_tiles`, `vmap.release_delay_sec`
- ❌ Contenu `game/data/vmap/<zone>/tile_*.vmap`

## 3. Recouvrement milestones existantes

❌ **Non couvert** — M100.12 (Collision Proxy System) est explicitement un
**système de physique d'éditeur** (proxies capsule/convex/trimesh attachés
aux meshes pour le placement éditeur), **pas** un système de LOS server-side.
Les besoins sont disjoints :
- M100.12 : physique de props éditeur, overlay wireframe, auto-fit V-HACD-like
- CMANGOS.05 : LOS / height / projectile hit côté shard, BIH compilé offline,
  tile streaming refcount, DynamicTree portes

Aucune autre milestone M00-M44 n'aborde le sujet.

## 4. Écart par rapport à la spec CMANGOS

100% des livrables sont absents. C'est l'un des **plus gros gaps fonctionnels**
identifiés dans `CMANGOS_ANALYSIS.md` (le ticket le qualifie de "module avec
le plus gros ROI réutilisable").

Implication directe : sans LOS server-authoritative,
- pas de combat à distance crédible (un joueur peut tirer à travers un mur)
- pas de validation anticheat de chute/fly hack robuste
- pas de portes vraiment fermées (gameplay donjon limité)
- pas de spawn de loot/teleport au sol fiable (pas de `GetHeight`)

## 5. Effort estimé

**L** (1 sprint complet) — module complet à créer :
- Math BIH templated + tests
- VMapManager + 3 requêtes
- Format `.vmap` binaire versionné + tests round-trip
- Outil offline `lcdlln_vmap_extractor` (binaire indépendant)
- ManagedModel refcount + VMapStreamer câblé sur Grids
- DynamicTree BSP/BVH pour GameObjects transformables
- Smoke test mur/porte LOS true/false

Pas de wire-breaking côté protocole (les requêtes sont serveur-internes).

## 6. Valeur joueur/serveur

**Élevée → Critique** — déblocant pour plusieurs systèmes downstream :
- **Combat à distance** (sans LOS, projectiles traversent les murs)
- **Sorts ciblés** (besoin de validation cible visible)
- **Anticheat mouvement** (validation chute, no-clip)
- **Donjons** (portes fermées qui bloquent vraiment)
- **Spawn de loot / téléport** (besoin de `GetHeight` au sol)

Ticket explicitement marqué "P1 — squelette" et "ROI réutilisable directement"
dans `CMANGOS_ANALYSIS.md`.

## 7. Dépendances bloquantes

- **CMANGOS.02 Entities** — `WorldObject::CanSeeObject()` doit câbler `VMapManager`.
  Sans hiérarchie OOP, on peut câbler sur `EntityId` opaque.
- **CMANGOS.03 Grids** — `VMapStreamer` réagit aux transitions
  `Loaded ↔ Active` pour acquire/release des tiles. Sans GridState machine
  (manquante actuellement), on peut commencer en chargement statique global
  (acceptable pour la v1, pas scalable).
- **Cohérence avec terrain client** — le client a sa propre représentation
  terrain dans `engine/render/terrain/`. Les `.vmap` doivent rester en sync
  (rebake si heightmap change). À coordonner avec les pipelines de zone
  (`tools/zone_builder/`).
- **CMANGOS.04 Movement** ou `MovementTypedefs.h` — `Vector3` partagé
  client/serveur. À défaut, créer `engine/math/Geom.h` minimal côté serveur.

## 8. Risque / piège ⚠️

- ⚠️ **Nouvelle dépendance ?** — le ticket n'introduit pas de lib externe
  (BIH/BVH custom), donc OK avec AGENTS.md. Mais si on veut un BVH
  industriel (Bullet, Embree), c'est interdit sans accord explicite.
- ⚠️ **Précision float** — coordonnées world (dizaines de milliers) +
  longues distances → erreurs accumulées. **Toujours travailler en local
  au tile** (cf. ticket §Notes).
- ⚠️ **Threading** — `VMapManager` doit être thread-safe lectures
  concurrentes (`shared_mutex`), sériaaliser le streaming. Mauvais design =
  contention massive.
- ⚠️ **Format .vmap versioning** — magic number + version dès le début ;
  bump à chaque évolution + outil regénère + shard refuse les vieux fichiers
  avec log explicite.
- ⚠️ **Coordination terrain client** — heightmap éditée → `.vmap` à rebake.
  Mode incrémental dans `vmap_extractor` à prévoir, sinon rebake complet
  coûte des heures.
- ⚠️ **Mesh dégénéré pour terrain** — 65×65 quads = 8192 triangles par tile
  heightmap. OK mais gros. Optimisation future : raycast direct sur
  heightmap (3 multiplications par cellule).
- ⚠️ **Config `vmap.tiles_dir`** — relatif à `paths.content`, pas absolu.
- ⚠️ **DynamicTree memory** — 10 000+ GameObject sur une map raid → BSP
  gros. Rebuild différée ou BVH dynamique.
- ⚠️ **Redéploiement** — nouveau handler/manager côté shard ; redéploiement
  serveur shard requis (mais pas wire-breaking côté protocole).
- Pas de migration DB.

## 9. Recommandation finale

✅ **Faire en l'état** — gros chantier mais bien découpé en couches isolées
(BIH math → VMapTile → VMapManager → ManagedModel → DynamicTree → outil
offline → câblage Grids).

**Ordre d'attaque suggéré :**

1. **Étape 0** : valider que les meshes sources sont disponibles dans un
   format que l'outil offline peut consommer (OBJ/GLTF dans
   `game/data/zones/`). Si non, commencer par un export depuis
   `tools/zone_builder/`.
2. **Étape 1** : implémenter `BIH<T>` math + tests raycast contre cubes
   synthétiques (le plus isolé, le plus testable).
3. **Étape 2** : implémenter `VMapFormat` + `VMapTile` (parser binaire
   testable).
4. **Étape 3** : créer `tools/vmap_extractor/` minimal (OBJ + heightmap →
   `.vmap`).
5. **Étape 4** : implémenter `VMapManager` avec les 3 requêtes canoniques
   + tests LOS sur mur synthétique.
6. **Étape 5** : implémenter `ManagedModel` + `VMapStreamer` (chargement
   global d'abord, dynamique ensuite quand GridState `Active` arrivera).
7. **Étape 6** : implémenter `DynamicTree` pour portes/objets transformables.
8. **Étape 7** : intégration smoke test bout en bout (LOS server-side dans
   le combat à distance).

À planifier **après** CMANGOS.02 (a minima `Vector3` typedef partagé) et
**en parallèle** de CMANGOS.03 (GridState peut arriver après, le streaming
peut être global au début).

Pour la cohérence avec le pipeline éditeur LCDLLN existant, **vérifier que
M100.12 (Collision Proxy System éditeur) et CMANGOS.05 (vmap shard)
restent disjoints** — deux systèmes complémentaires, pas conflictuels.

---

*Audit du 2026-05-08. Mises à jour : —*
