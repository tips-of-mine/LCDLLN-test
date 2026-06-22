# Collision des bâtiments par catalogue de boîtes — Design

**Date** : 2026-06-21
**Statut** : validé (brainstorm), à transformer en plan d'implémentation.

## Problème

Les bâtiments (#908) sont chargés pièce par pièce via
`Engine::BuildPropFromMeshMatrix(gltfPath, matrix, …)`. La collision de chaque
pièce est **un gros cylindre vertical englobant** : `PropCylinder` de rayon égal
à l'empreinte XZ complète de la pièce (ou `collisionRadius` si fourni). Deux
conséquences observées en jeu (auberge) :

- **Mur « fat »** : un mur fin/long devient un large cylindre qui déborde bien
  au-delà du mur réel — il couvre notamment l'**embrasure de la porte**, qu'on ne
  peut donc pas franchir (même avec `PropCylinder.passable` sur la pièce porte, le
  cylindre du mur voisin couvre l'ouverture).
- **« Vol »** au sommet des murs (corrigé séparément par le flag
  `PropCylinder.wall`, PR #919 : capuchon supérieur désactivé pour les pièces de
  bâtiment). Ce design **suppose #919 mergé** (réutilise le sémantique `wall`).

But : permettre de **franchir les portes** proprement et d'avoir des **murs qui
bloquent juste où ils sont**, sans gros cylindre englobant, via un système
**réutilisable** par tous les futurs bâtiments.

## Décisions de cadrage (brainstorm)

1. **Périmètre** : système réutilisable (pas un patch ponctuel auberge).
2. **Source de la collision** : **catalogue par mesh** — on définit la/les
   forme(s) de collision UNE FOIS par TYPE de mesh, dans un fichier de données ;
   réutilisé automatiquement par tout bâtiment référençant ce mesh. Zéro travail
   par instance pour le mappeur.
3. **Primitif** : **boîte orientée** (`PropBox`) à côté de `PropCylinder`.
4. **Adoption incrémentale** : un mesh sans entrée de catalogue retombe sur le
   **cylindre actuel** (rétro-compatibilité totale ; aucune régression sur le
   décor — arbres, coffres — qui n'est pas concerné par ce catalogue).

## Architecture

### 1. Nouveau primitif `PropBox` (boîte orientée)

Dans `src/client/gameplay/CompositeWorldCollider.h`, à côté de `PropCylinder` :

**Choix de simplicité (aligné sur les cylindres existants)** : un mur de bâtiment
est **vertical** (axe Y = haut du monde). On modélise donc la boîte comme un
**rectangle orienté dans le plan XZ** (yaw) **borné en Y** par `[loY, hiY]` —
exactement le même découpage que `PropCylinder` (empreinte XZ + bornes Y), mais
l'empreinte est un rectangle orienté au lieu d'un disque. Cela évite le sweep OBB
3D complet (plus lourd, plus risqué à itérer en CI). Les pièces inclinées
(pitch/roll non nuls, rares pour des murs) sont **approximées** (projection XZ) —
acceptable, cohérent avec la philosophie « léger sur-blocage sans importance ».

```cpp
/// Boîte de collision orientée pour une pièce de bâtiment (mur, jambage, linteau).
/// Empreinte = rectangle dans le plan XZ orienté par (axisX, axisZ) (vecteurs
/// unitaires monde, yaw de la pièce), demi-dimensions (halfX, halfZ) ; bornée en
/// Y par [loY, hiY]. Sweep capsule = test rectangle-orienté-vs-cercle(rayon
/// capsule) dans XZ + recouvrement vertical, calqué sur PropCylinder.
struct PropBox
{
    float cx = 0.0f, cz = 0.0f;       ///< centre XZ monde (m)
    float halfX = 0.5f, halfZ = 0.1f; ///< demi-dimensions du rectangle (m), > 0
    engine::math::Vec3 axisX{ 1, 0, 0 }; ///< axe « largeur » monde (unitaire, XZ)
    engine::math::Vec3 axisZ{ 0, 0, 1 }; ///< axe « épaisseur » monde (unitaire, XZ)
    float loY = 0.0f, hiY = 2.0f;     ///< bornes Y monde [bas, haut]
    bool passable = false; ///< aucune collision (battant de porte)
    bool stair = false;    ///< gravissable (cf. CharacterController)
    bool wall = true;      ///< barrière latérale pure, pas de dessus marchable
};
```

`CompositeWorldCollider` gère **deux listes** (`m_cylinders` existante +
`m_boxes` nouvelle). `SweepCapsule` parcourt les deux et conserve le hit le plus
proche (même logique « plus petite fraction »). API ajoutée :
`AddBox(const PropBox&)`, `ClearBoxes()` (et `ClearCylinders` existant ;
`Clear()` global videra les deux).

**Sweep capsule-vs-`PropBox`** (cœur algorithmique), calqué sur le sweep cylindre :
1. **Recouvrement vertical** (comme `PropCylinder`) : avec `capLo/capHi` (bornes Y
   de la capsule au point d'arrivée, ± `halfH` ± rayon), si `capHi < loY` ou
   `capLo > hiY - kStandSkin` → pas de blocage latéral (passe au-dessus/dessous).
2. **Projection XZ dans le repère du rectangle** : pour le départ `s` et le
   déplacement `d` (XZ), coordonnées locales `u = (p-c)·axisX`, `v = (p-c)·axisZ`.
   La boîte devient l'AABB 2D `[-halfX,halfX]×[-halfZ,halfZ]`, **élargi du rayon
   capsule** → `[-(halfX+r),…]×[-(halfZ+r),…]` (Minkowski cercle-vs-rectangle ;
   coins légèrement carrés, négligeable pour un mur).
3. **Slab test 2D** (ray `(u0,v0) → (u0+du,v0+dv)` vs AABB élargi) → fraction
   d'entrée `t` ∈ [0,1] et **axe de la face d'entrée** (u ou v).
4. **Normale monde** = `±axisX` (face u) ou `±axisZ` (face v), signe selon le côté
   d'approche → glissement correct côté `CharacterController`.
5. Renvoyer `fraction`, `normal`, `stair`. Si `passable` → ignorer (continue),
   comme les cylindres passables. Aucun « capuchon » (les boîtes de mur sont
   `wall=true`) : pas de dessus marchable, cohérent avec #919.

Le flag `wall` désactive de la même façon tout « capuchon marchable » : une boîte
de mur ne propose pas de dessus (cohérent avec #919). Une boîte non-`wall` (sol,
plateforme) pourra exposer une face supérieure marchable (post-MVP si besoin ;
MVP : `wall=true` par défaut pour les pièces de bâtiment).

### 2. Catalogue de collision par mesh

Fichier de données : `game/data/collision/building_pieces.json`.

Clé = **nom de base du mesh** sans dossier ni extension (ex.
`Wall_Plaster_Straight`), insensible à la casse. Valeur = soit `"passable": true`
(aucune collision — battants de porte), soit une **liste de boîtes en espace
LOCAL du mesh** :

```json
{
  "version": 1,
  "pieces": {
    "Door_1_Flat":            { "passable": true },
    "Door_1_Round":           { "passable": true },
    "Wall_Plaster_Straight":  { "boxes": [
        { "center": [0, 1.5, 0], "half": [1.0, 1.5, 0.1] }
    ]},
    "Wall_Plaster_Door_Flat": { "boxes": [
        { "center": [-0.8, 1.5, 0], "half": [0.2, 1.5, 0.1] },   // jambage gauche
        { "center": [ 0.8, 1.5, 0], "half": [0.2, 1.5, 0.1] },   // jambage droit
        { "center": [ 0.0, 2.6, 0], "half": [1.0, 0.4, 0.1] }    // linteau au-dessus
    ]}
  }
}
```

- Les `center`/`half` sont en **mètres, repère local du mesh** (origine = origine
  du glTF, avant transform de la pièce). Le chargement réel des valeurs exactes
  pour chaque mesh de l'auberge se fera en lisant les bounds des glTF concernés
  (étape du plan ; valeurs ci-dessus = illustratives).
- `boxes` omis + pas de `passable` ⇒ entrée invalide ignorée (warn) → fallback.

**Chargeur** : `BuildingCollisionCatalog` (nouveau,
`src/client/world/` ou `src/shared/world/`). API :
`bool LoadFromJson(const std::string& json)` et
`const PieceCollision* Lookup(std::string_view meshBaseName) const` renvoyant
`{passable, vector<LocalBox>}` ou `nullptr` si absent. Header-only ou paire
`.h/.cpp` — s'il devient un `.cpp` partagé, **l'ajouter aussi à `server_app`**
(règle repo) ; mais en pratique client-only (collision rendue côté client).

### 3. Branchement dans `BuildPropFromMeshMatrix`

À l'endroit où le cylindre est aujourd'hui ajouté (`Engine.cpp`, fin de
`BuildPropFromMeshMatrix`) :

1. Calculer `meshBaseName` depuis `meshPath` (basename sans extension).
2. `const auto* pc = m_buildingCollisionCatalog.Lookup(meshBaseName);`
3. Si `pc == nullptr` → **comportement actuel** (cylindre `wall = !stair`, etc.).
   Aucune régression.
4. Si `pc->passable` → ne rien ajouter (battant franchissable).
5. Sinon, pour chaque `LocalBox` (`center[x,y,z]`, `half[x,y,z]` en local) :
   - Transformer `center` local par la **matrice monde de la pièce** (`worldM`)
     → `(wx, wy, wz)` monde.
   - Colonnes 3×3 de `worldM` : `colX, colY, colZ`. Échelles `sx=|colX|`,
     `sy=|colY|`, `sz=|colZ|`. Axes unitaires `axisX = colX/sx`,
     `axisZ = colZ/sz` (composantes Y ignorées → projetées dans XZ ; murs
     verticaux). `halfX = half.x·sx`, `halfZ = half.z·sz`.
   - Bornes Y : `loY = wy - half.y·sy`, `hiY = wy + half.y·sy`.
   - `PropBox{wx, wz, halfX, halfZ, axisX, axisZ, loY, hiY, wall=true, stair,
     passable=false}` → `m_worldCollider.AddBox(...)`.

Le catalogue est chargé une fois au boot (à côté du chargement des bâtiments) ;
si le fichier est absent → catalogue vide → tout retombe sur les cylindres
(rétro-compatible).

## Flux de données

`buildings.bin (pièces : gltfPath + matrice)` → `BuildPropFromMeshMatrix` →
(catalogue mesh ? ) → **boîtes orientées monde** (`AddBox`) **ou** cylindre
fallback → `CompositeWorldCollider` → `CharacterController.SweepCapsule`.

## Gestion d'erreurs / cas limites

- Fichier catalogue absent / JSON invalide → log WARN, catalogue vide, fallback
  cylindre (le jeu reste jouable).
- Mesh non catalogué → fallback cylindre (adoption incrémentale).
- Demi-extent ≤ 0 dans une entrée → boîte ignorée (warn).
- Pièce avec échelle non uniforme → les demi-extents sont mis à l'échelle par axe
  (norme de chaque colonne de la matrice) avant normalisation des axes.

## Tests (ctest, CI Linux — pas de toolchain locale)

1. **`CompositeWorldColliderTests`** (étendu) :
   - capsule traversant une `PropBox` orientée (yaw 0 et yaw 45°) : hit, normale =
     face touchée, fraction plausible ;
   - capsule passant à côté / hors bornes Y : pas de hit ;
   - `PropBox.passable` : aucun hit ;
   - **cas mur-à-porte** : 3 boîtes (jambages + linteau) ; une capsule au niveau
     du sol qui traverse l'EMBRASURE (entre les jambages, sous le linteau) **ne
     touche pas** → porte franchissable ; une capsule visant un jambage **touche**.
2. **`BuildingCollisionCatalogTests`** (nouveau) : parse OK ; `passable` ;
   plusieurs boîtes ; mesh absent → `nullptr` ; JSON invalide → échec propre.

## Découpage / périmètre

Un seul sous-projet cohérent :
- primitif `PropBox` + sweep + intégration `CompositeWorldCollider` ;
- `BuildingCollisionCatalog` + format JSON + chargeur ;
- branchement `BuildPropFromMeshMatrix` (fallback cylindre) ;
- données catalogue pour les meshes mur/porte **de l'auberge** (valeurs réelles
  lues depuis les glTF) ;
- tests.

**Livrable testable** : l'auberge a des murs fins (blocage précis) et une porte
**franchissable**, sans toucher au décor existant.

## Hors périmètre (post-MVP, à ne pas faire ici)

- Éditeur d'ouvertures par instance (on s'appuie sur le catalogue par mesh).
- Collision maillage / enveloppe convexe automatique.
- Faces supérieures marchables sur boîtes (sols/plateformes de bâtiment) — les
  pièces sont `wall=true` au MVP.
- Escaliers fiables (chantier séparé déjà identifié).

## Déploiement

✅ **Client uniquement** (collision rendue côté client, données embarquées). Pas
de redéploiement serveur.
