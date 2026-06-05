# Feyhin Lokcthat — SP2 : Pont à arches (design)

> Date : 2026-06-05 · Sous-projet **SP2** du chantier Feyhin Lokcthat. Dépend de **SP1** (terrain,
> PR #821, branche `feyhin-sp1-pr`). Objectif : un **pont en pierre taillée sombre** franchissant la
> rivière à l'ouest-centre de la vallée, **praticable à pied**, qui matérialise la traversée vers la ville.

## 1. Contexte & cahier des charges (issu de l'utilisateur + planche)

- Pont **200 m**, **10 arches × 20 m** (7 visibles, 3 cachées côté ville), tablier **5,75 m** de large,
  arches **~3,73 m** de haut (pont **bas**, arches naissant près de l'eau).
- **Pierre taillée sombre** (pas de bois). Le MegaKit n'a pas de pierre de taille sombre → texture à fournir.
- Franchit la rivière **ouest → est** ; c'est le chemin vers la ville (faubourg/cité, SP3+).
- Géo (plan v7 / zone SP1) : traversée à **Z ≈ 300**, rivière centrée ~X−45 (largeur ~145 m), niveau
  d'eau **y = 60**, lit ~50, berges ~64.

## 2. Blocages identifiés (investigation) & parti pris

| Blocage | Constat | Parti pris SP2 |
|---|---|---|
| **Placement Y** | `world.scenery` snappe tout au sol (`GroundHeightAt`), pas de Y explicite (`Engine.cpp` ~L10789). | **Enabler moteur** : ajouter champs optionnels `y`, `pitch_deg`, `roll_deg` à `world.scenery`. |
| **Texture pierre** | `Floor_*`/`Wall_*` gltf sans `baseColorUri` → rendu blanc/gris (fallback `VertexColorAlbedo`). | **Enabler moteur** : champ optionnel `albedo` (chemin texture) par entrée scenery + **`.texr` pierre sombre générée**. |
| **Mesh d'arche** | `meshes/arches/*.gltf` = placeholders absents. Seule vraie pièce d'arche : `Wall_Arch.gltf`. | **Viaduc** : rangée de `Wall_Arch` mis à l'échelle (~20 m de portée) comme arches, **dalles `Floor_UnevenBrick`** pour le tablier. À valider visuellement en jeu (mise à l'échelle d'une pièce de mur). |

## 3. Enabler moteur (`world.scenery` étendu) — client-only

Dans le loader `Engine.cpp::LoadScenery()` (~L10608-10833), par entrée, lire en plus (tous optionnels,
rétro-compatibles — absent = comportement actuel) :

- `y` (float) : si présent, **position Y monde explicite** (le bas du mesh est posé à `y` au lieu de
  `GroundHeightAt(x,z)`). Indispensable pour un tablier au-dessus de l'eau.
- `pitch_deg` (float) : rotation autour de X (actuellement câblée à 0). Pour rampes/inclinaisons.
- `roll_deg` (float) : rotation autour de Z.
- `albedo` (string) : chemin `.texr`/texture ; si présent, force le base color du prop (sinon
  comportement gltf actuel). Permet de texturer les pièces nues en pierre sombre.

Collision : inchangée (cylindre `collision_radius`/`solid`) — mais avec `y` explicite, la base du
cylindre = `y` (pas `GroundHeightAt`). Le joueur marche **sur** le tablier via la collision existante
(à valider : la collision cylindre suffit-elle pour marcher dessus, ou faut-il un proxy boîte ? cf. note).

> **Note collision tablier** : la collision props actuelle est un **cylindre** vertical. Un tablier plat
> large nécessite idéalement une collision « sol » (marche dessus). Si le cylindre ne permet pas de
> marcher sur le tablier, fallback SP2 : **sculpter une fine bande de terrain** à hauteur du tablier
> (causeway) pour la marchabilité, le pont restant visuel — OU étendre la collision (reporté). À
> trancher après test en jeu de l'enabler.

## 4. Texture pierre taillée sombre

Générer `game/data/meshes/props/textures/stone_dark.texr` (format TEXR, RGBA8) : gris-pierre sombre
(~#3a3a40) avec bruit déterministe (joints/grain), via un petit générateur PowerShell (même technique que
les couches procédurales terrain). Assignée aux pièces du pont via le champ `albedo`.

## 5. Assemblage du pont (data — `world.scenery`)

Généré par un script PowerShell `tools/world/Generate-FeyhinBridge.ps1` qui **émet les entrées
`world.scenery`** (fragment JSON à fusionner dans config.json) :

- **Arches** : 10 × `Wall_Arch.gltf` alignées le long de l'axe (ouest→est, Z≈300), espacées de 20 m, mises
  à l'échelle pour ~20 m de portée et ~3,73 m de haut, pied à ~y 60 (eau), `albedo`=stone_dark,
  `solid=true`. 7 visibles + 3 vers la rive est.
- **Tablier** : dalles `Floor_UnevenBrick.gltf` (5,75 m de large) posées en ligne à **y ≈ 64**,
  `albedo`=stone_dark, `solid=true`, surface praticable.
- **Parapets** (option) : `Wall_*_Straight` bas de chaque côté du tablier.
- **Approches** : raccord aux berges (rampe douce ou marches) côté ouest et côté est.

Coordonnées exactes calculées par le script à partir de l'axe (P0 ouest ≈ (−135, 300), P1 est ≈ (+90, 300)),
pas de 20 m, déduites du heightmap SP1 (lecture des cotes berges/lit le long de l'axe).

## 6. Marchabilité

Le tablier doit être praticable. Dépend du test de l'enabler (collision cylindre vs besoin d'un sol) — cf.
note §3. Plan B documenté (causeway terrain) si la collision props ne porte pas le joueur.

## 7. Tests

- **Numérique** : 10 arches espacées de 20 m sur 200 m ; tablier continu de l'ouest à l'est à y~64 ; pied
  des arches à ~60.
- **En jeu** (utilisateur) : pont visible en **pierre sombre** (pas blanc), franchissable à pied d'une rive
  à l'autre, arches au-dessus de l'eau, proportions crédibles. Ajuster échelle `Wall_Arch` / y tablier.

## 8. Découpage en lots (livrables/PR)

1. **SP2a — Enabler moteur** (`y`/`pitch`/`roll`/`albedo` sur scenery) + test. PR stackée sur #821. **Build CI requise** (C++ client). Client-only.
2. **SP2b — Texture pierre sombre** (`.texr` + générateur) + **layout du pont** (`Generate-FeyhinBridge.ps1` → entrées `world.scenery`). Data, client-only.

## 9. Déploiement

> **Déploiement : ✅ client uniquement, pas de redéploiement serveur.** SP2a = code **client** (loader
> scenery) ; SP2b = assets + config. Aucun opcode/handler/migration/gating. (SP2a nécessite une **build
> client** via CI — pas un redéploiement serveur.)

## 10. Risques / à valider

- **Mise à l'échelle de `Wall_Arch`** en arche de pont de 20 m : rendu à confirmer en jeu (pièce de mur
  étirée). Si moche → envisager d'importer/générer une vraie arche (le MegaKit n'en a pas).
- **Collision du tablier** (cf. §3/§6).
- **Texture pierre** : crédibilité du `.texr` procédural sombre.
- Ces points se valident sur l'artefact CI ; itération comme pour SP1.
