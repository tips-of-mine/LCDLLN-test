# Chantier 2 — SP1 : groundwork moteur avatar modulaire — design

Date : 2026-07-09
Statut : direction validée par l'utilisateur (modulaire + groundwork-first + placeholders + commande admin de test).

> **Suite (2026-07-10)** — SP-A « catalogue d'objets + équipement » **livré** (PRs #965 serveur / #966 client) : 10 slots gameplay, équiper/déséquiper, bonus de stats dérivées serveur-autoritaire, persistance. Spec dédiée : `docs/superpowers/specs/2026-07-10-ch2-spA-item-catalog-equipment-design.md`. Le **rendu visuel** des objets équipés sur l'avatar (attache de mesh) reste à faire (SP-D) : bloqué par la limite du rig UE5 (os à l'origine) documentée ici et par les assets modulaires.

## Contexte

Chantier 2 = système d'équipement complet. Décision d'apparence : **personnage
modulaire** (équiper REMPLACE un morceau du corps, pas de superposition).
L'analyse du mesh actuel (cf. [[project_character_window_unified]]) a montré que
l'avatar est **un `.glb` unique baké** : impossible de remplacer une partie. Le
modulaire exige (a) des assets modulaires — travail d'art, **hors code**, plus
tard — et (b) une **capacité moteur** à composer/rendre/échanger des parties.

Décision : **construire d'abord la capacité moteur (SP1), testée avec des parties
placeholder**, pour brancher les vrais assets plus tard sans re-architecturer.

## Objectif SP1

Rendre l'avatar-monde à partir de **plusieurs meshes de parties partageant un
même squelette**, avec **échange de partie en direct**, démontré par des
**placeholders** et une **commande admin de test**. **100% client-rendu.** Aucun
serveur, wire, DB, stats, asset final, ni logique d'équipement (SP suivants).

## Faisabilité (ancrage code)

- `SkinnedRenderer::Record(mesh, finalBoneMatrices, …)` dessine **un** mesh
  skinné avec des matrices d'os fournies (SSBO), en multi-sous-mesh/matériau.
- Le draw avatar actuel : `Engine.cpp` ~5600, `m_skinnedRenderer.Record(
  *m_currentSkinnedMesh, finals, …)`. `finals` = matrices d'os de la frame,
  calculées depuis le squelette + pose d'animation courante.
- **Modulaire = calculer `finals` UNE fois (squelette partagé) puis appeler
  `Record` pour CHAQUE partie active**, même `finals` + même model matrix. Les
  parties doivent être riggées au **même squelette** (même nombre/ordre d'os) →
  les mêmes `finals` s'appliquent à toutes.

Changement moteur **minimal** : boucle de `Record` au lieu d'un seul.

## Architecture

### Nouveau composant : `ModularAvatar` (client)

Fichier `src/client/render/skinned/ModularAvatar.{h,cpp}`. Rôle : représenter un
avatar composé de parties.

- **Slots** : enum `EquipVisualSlot` (SP1 minimal mais extensible) —
  `Body` (corps de base, toujours présent), `Head`, `Chest`, `Legs`, `Feet`,
  `Hands`, `Weapon`, `Offhand`. SP1 démontre le pipeline ; tous les slots ne sont
  pas peuplés d'assets.
- **État** : un pointeur `SkinnedMesh*` par slot occupé (non-owning ; les meshes
  appartiennent à un cache/registre). Le `Body` est le corps de base ; les autres
  slots sont vides par défaut.
- **Squelette partagé** : `ModularAvatar` référence UN squelette (celui du `Body`).
  Invariant SP1 : toute partie ajoutée doit avoir un squelette compatible
  (même `skeleton.bones.size()` et même ordre) — sinon rejet + log (les
  `finals` ne s'appliqueraient pas).
- **API** :
  - `void SetPart(EquipVisualSlot slot, const SkinnedMesh* mesh)` — pose/retire
    (nullptr) une partie ; valide la compatibilité squelette.
  - `void ClearPart(EquipVisualSlot slot)`.
  - `std::vector<const SkinnedMesh*> ActiveParts() const` — parties à dessiner
    (Body + slots occupés).
  - `const Skeleton& SharedSkeleton() const`.

### Rendu

Remplacer le draw mono-mesh de l'avatar local par une **boucle sur
`ActiveParts()`** :

```
finals = ComputeFinalBoneMatrices(sharedSkeleton, currentPose)   // 1x
for (const SkinnedMesh* part : modularAvatar.ActiveParts())
    m_skinnedRenderer.Record(*part, finals, model, submeshMatIdx(part), …)
```

- `finals` inchangé (déjà calculé pour l'avatar). Les parties partagent la pose.
- Chaque partie garde son propre routage matériau (habit/peau via
  `submeshMaterialIndices`, comme aujourd'hui).
- Idem pour l'aperçu 3D (`RacePreviewViewport`) : boucle de rendu sur les parties
  (permet de voir l'équipement dans la fenêtre Personnage). Étape optionnelle
  SP1 (le monde suffit à valider) mais souhaitable.

### Chargement des parties partageant un squelette

- Le loader actuel (`SkinnedMeshLoader`) charge un `.glb` → un `SkinnedMesh`
  (skeleton + vertices + indices + submeshes + clips).
- SP1 : charger des parties depuis des `.glb` **et réutiliser un squelette
  commun**. Deux voies possibles ; SP1 retient la plus simple :
  - Chaque part-`.glb` porte le même squelette (exporté sur le même rig) ; on
    charge le mesh de la partie et on **ignore/valide** son squelette contre
    celui du `Body`. La pose (`finals`) vient du `Body`.
- **Placeholders SP1 (aucun asset)** : générer des parties **procédurales**
  simples (ex. une boîte skinnée à 100% sur un os donné — tête, main…) via un
  petit helper `MakePlaceholderPart(skeleton, boneIndex, dims)`. Cela prouve :
  (a) plusieurs parties rendues avec les os partagés, (b) l'échange live change
  l'avatar. Deux variantes par slot (ex. « casque A » vs « casque B ») pour
  démontrer le SWAP.

### Déclencheur de test (commande admin)

Commande chat **admin-only + loguée serveur** (convention repo, cf.
[[feedback_admin_commands_logging]] / [[reference_slash_commands]]) :
`/modular <slot> <variante>` — pose la partie placeholder `<variante>` sur
`<slot>` de l'avatar local ; `/modular <slot> off` retire. Purement client dans
son effet visuel SP1 (pas de persistance) ; passe par le master pour le gating
admin + le log, comme les autres commandes.

> Alternative si le gating admin complique SP1 : une touche debug derrière un
> flag config `client.debug.modular_test` (défaut false). À trancher au plan.

## Fichiers

- **Create** `src/client/render/skinned/ModularAvatar.{h,cpp}` — composant + slots.
- **Create** `src/client/render/skinned/PlaceholderPart.{h,cpp}` — génération de
  parties procédurales (boîte skinnée à un os) partageant un squelette.
- **Modify** `src/client/app/Engine.cpp` — l'avatar local devient un
  `ModularAvatar` (Body = mesh courant) ; boucle de `Record` sur `ActiveParts()` ;
  commande `/modular`.
- **Modify** `src/client/app/Engine.h` — membre `ModularAvatar`.
- **Modify** (optionnel SP1) `RacePreviewViewport` — rendu multi-parties.
- **Tests** : `ModularAvatar` a une logique pure testable (compat squelette,
  ActiveParts, SetPart/ClearPart) → test unitaire ctest (Linux). `PlaceholderPart`
  génère des données CPU testables (bones weights = 1 sur l'os visé).
- **Modify** `CMakeLists.txt` (+ liste de tests) — nouveaux `.cpp` + test target.

## Hors périmètre SP1 (→ SP suivants)

- Catalogue d'objets (type/slot/stats/apparence par `itemId`).
- Wire equip/unequip + état d'équipement + persistance DB. ⚠️ serveur.
- Recalcul des stats depuis le stuff.
- UI paperdoll : glisser l'inventaire vers les slots d'équipement.
- **Assets modulaires réels** (art) — SP1 n'utilise que des placeholders.

## Vérification

- **Unitaire (ctest/Linux)** : compat squelette (rejet si bone count/ordre
  différent), `ActiveParts` (Body + slots occupés, ordre stable), SetPart/ClearPart,
  `PlaceholderPart` (poids d'os corrects). Ces tests sont **portables** (données
  CPU, pas de Vulkan/ImGui) → tournent sur Linux.
- **En jeu (Windows)** : `/modular head A` fait apparaître une partie placeholder
  sur la tête de l'avatar-monde ; `/modular head B` l'échange ; `/modular head off`
  la retire — le tout en direct, l'avatar continue de s'animer normalement (les
  parties suivent la pose). Aucun crash Vulkan.

## Risques

- **Squelette identique obligatoire** : une partie mal riggée (ordre d'os
  différent) se déformerait. SP1 valide bone count/ordre et rejette sinon. Les
  placeholders procéduraux utilisent le squelette du Body → toujours compatibles.
- **Perf** : N `Record` = N draws + N uploads SSBO de `finals`. SP1 : peu de
  parties, avatar local seul → négligeable. (Optim future : uploader `finals`
  une fois pour toutes les parties.)
- **Convention winding Vulkan** : réutiliser le pipeline `SkinnedRenderer`
  existant tel quel — ne pas toucher aux `frontFace`/`cullMode` (cf. CLAUDE.md).
- **Gating admin de `/modular`** : si le circuit master/admin alourdit SP1,
  repli sur un flag debug config (décision au plan).

## Déploiement

✅ **Client uniquement** pour le rendu et les placeholders. La commande
`/modular`, SI routée admin via master (pour le log), suit le circuit commandes
existant — à préciser au plan si un handler master est nécessaire (sinon repli
flag debug = strictement client).
