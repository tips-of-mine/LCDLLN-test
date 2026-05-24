# Design — Avatar : peau correcte par genre + visible en jeu + aperçu 3D de création

> Sous-chantier **A** du chantier « personnalisation de personnage ».
> Date : 2026-05-24 · Branche : `claude/avatar-peau-genre-apercu`.
> Statut : design validé (à relire avant plan d'implémentation).

## 1. Contexte & problème

Depuis les PR #672 (rendu multi-matériaux peau/habit) et #675 (sélecteur de
genre), quatre symptômes subsistent côté avatar :

1. **#1 — Peau femelle non appliquée** : en jeu, un personnage féminin affiche
   la peau masculine (« c'est toujours celui de l'homme qui est pris pour une
   femme »).
2. **#2 — Peau invisible en jeu** : l'habit (armure) s'affiche, mais la peau
   (zones de peau exposées) ne se voit pas.
3. **A3 — Pas d'aperçu 3D** : l'écran de création ne rend pas l'avatar en 3D.
   [`RacePreviewViewport::Render`](../../../src/client/render/race/RacePreviewViewport.cpp)
   est un stub « Task 11 MVP » qui se contente de remplir le rectangle en bleu
   sombre quand un mesh est attaché. On ne peut donc pas voir le genre/la peau
   avant d'entrer en jeu.
4. **A4 — Vocabulaire** : le sélecteur affiche « GENRE » + boutons
   « Homme »/« Femme » à améliorer.

### Faits établis par l'exploration (2026-05-24)

- Les meshes `Male_Ranger.glb` **et** `Female_Ranger.glb` existent
  (`game/data/models/characters/humains/.../`).
- Les deux portent les matériaux glTF `MI_Ranger` (habit) + `MI_Regular_Male`
  (peau) — noms **confirmés** dans le binaire ; ils matchent le défaut
  `body_material_names = "MI_Regular_Male,MI_Regular_Female"`.
- Les textures de peau déployées sont **distinctes** male vs female (md5
  différents) et correspondent chacune à leur source `_Dark_` du pipeline
  d'assets. **Les assets ne sont pas en cause** pour #1/#2.
- Le `SkinnedMeshLoader` peuple bien `mesh.submeshes` avec `firstIndex`,
  `indexCount` et `materialName` (issu de `prim->material->name`).
- Le draw multi-matériaux de `SkinnedRenderer::Record` est correct **mais** ne
  s'active que si `submeshMaterialIndices.size() == mesh.submeshes.size()` ;
  sinon il retombe en **mono-draw avec le matériau habit** → aucune peau.
- Le routage in-world (`Engine.cpp`) ne construit `submeshMaterialIndices` que
  si `bodyMaterialId != 0` et `submeshes` non vide.

**Conclusion** : #1 et #2 ne sont pas une erreur de conception évidente — ce
sont des **bugs de comportement** à diagnostiquer au runtime. Le code « paraît »
correct ; il faut observer la réalité d'exécution.

### Contrainte de validation (impérative)

- **Aucun build local possible** (pas de toolchain C++ dans les shells).
- Le **build n'a lieu qu'au push de la PR** (CI GitHub, **compile-only**, pas de
  ctest).
- **L'utilisateur juge la qualité de la livraison une fois la PR buildée**
  (validation visuelle / en jeu, client Windows/Vulkan).
- Donc : la vérification runtime est **entièrement déléguée à l'utilisateur**.
  Le design ne « prouve » rien au runtime ; il fournit l'**instrumentation** et
  l'**outil d'observation** (aperçu 3D) pour que l'utilisateur tranche, plus la
  logique testable en compilation.

## 2. Objectifs / critères d'acceptation

À valider **par l'utilisateur en jeu** après build CI :

- **CA1** : un personnage féminin affiche la peau féminine en jeu (et masculin
  la masculine).
- **CA2** : les zones de peau exposées sont **visibles** en jeu pour les deux
  genres (la peau n'est plus masquée/absente).
- **CA3** : l'écran de création **rend l'avatar en 3D** ; basculer
  Masculin/Féminin change l'aperçu **en live** (mesh + peau).
- **CA4** : le sélecteur affiche « GENRE » + boutons « Masculin »/« Féminin ».

Vérifiable en **compilation/CI** :

- **CA5** : la logique de routage de matériaux est isolée en fonction pure et
  couverte par un test unitaire (compile en CI ; exécuté par l'utilisateur, la
  CI étant compile-only).

### Hors périmètre

- Persistance **serveur** du genre (migration DB + payload) — différée, traitée
  plus tard avec redéploiement serveur.
- Sous-chantiers **B** (rendu des props/coffre) et **C** (feedback
  d'interaction) — specs séparées ultérieures.

## 3. Architecture

### 3.1 Pièce centrale : fonction pure de routage (partagée + testable)

Extraire le routage actuellement inline dans `Engine.cpp` vers un nouveau module
sans dépendance Vulkan :

```
// src/client/render/skinned/AvatarMaterialRouting.{h,cpp}  (PascalCase)
namespace engine::render::skinned {
  // Retourne un vecteur parallèle à `submeshes` : chaque sous-maillage dont le
  // materialName figure dans bodyMaterialNames reçoit bodyMaterialId, les autres
  // outfitMaterialId. Retourne un vecteur VIDE si bodyMaterialId == 0 ou si
  // submeshes est vide (l'appelant retombe alors sur le mono-draw habit).
  std::vector<uint32_t> BuildSubmeshMaterialIndices(
      const std::vector<SkinnedSubMesh>& submeshes,
      const std::vector<std::string>&    bodyMaterialNames,
      uint32_t bodyMaterialId,
      uint32_t outfitMaterialId);
}
```

Appelée **à la fois** par le rendu in-world (`Engine.cpp`) et par l'aperçu
(`RacePreviewViewport`) → un seul comportement, un seul endroit à corriger.

> Note build : ajouter `AvatarMaterialRouting.cpp` aux sources du client **et**
> le test à la cible de tests. Module **client-only** (rendu) — ne concerne pas
> `server_app`.

### 3.2 A1/A2 — diagnostic + correctif in-world

1. Remplacer le routage inline ([`Engine.cpp` ~4745](../../../src/client/app/Engine.cpp))
   par `BuildSubmeshMaterialIndices`.
2. **Instrumentation de diagnostic** (loggée une fois au boot/à l'EnterWorld ou
   au changement de genre — **jamais par frame**) :
   - genre actif (`m_avatarGender`),
   - `m_avatarBodyMaterialIdMale` / `...Female` résolus, `bodyMaterialId` choisi,
   - nombre de sous-maillages classés peau vs habit,
   - la **liste des `materialName`** réellement présents dans le mesh chargé.
   Ce log révèle laquelle des hypothèses ci-dessous est vraie.
3. **Hypothèses classées** (à confirmer via le log + l'aperçu 3D) :
   - **H1** — `m_avatarGender` vaut « male » au draw malgré le choix féminin
     (SetAvatarGender non appliqué, ou EnterWorld réinitialise depuis
     `config.gender`). *Correctif* : garantir l'application + persistance du
     genre avant l'EnterWorld ; vérifier l'ordre de chargement.
   - **H2** — `m_avatarBodyMaterialIdFemale == 0` (texture féminine non chargée
     au runtime malgré le fichier présent). *Correctif* : chemin/chargement.
   - **H3** — peau masquée géométriquement par l'habit, ou `skin_depth_bias_*`
     trop fort. *Correctif* : ajuster le bias (réglable à chaud) / confirmer que
     la peau exposée se limite à certaines zones.
   - **H4** — `materialName` ne matche pas (casse/espaces parasites).
     *Correctif* : matching robuste (trim). *(Priorité basse : noms confirmés
     OK dans le binaire.)*
4. Le correctif réel est choisi selon ce que révèlent log + aperçu. La
   **vérification finale CA1/CA2 est faite en jeu par l'utilisateur** après
   build CI.

### 3.3 A3 — aperçu 3D forward dédié (Approche 1 retenue)

Remplacer le stub de
[`RacePreviewViewport::Render`](../../../src/client/render/race/RacePreviewViewport.cpp)
par un vrai rendu, **isolé** du `SkinnedRenderer` in-world (qu'on stabilise en
parallèle — on ne veut pas le déstabiliser) :

- **Nouveau pipeline forward skinné** dans (ou à côté de) `RacePreviewViewport` :
  vertex skinning + fragment éclairage simple, écrivant dans le `VkImage`
  couleur **existant** + un **nouveau depth buffer** (image + memory à allouer/
  libérer dans Init/Shutdown du viewport).
- **Réutilise** : les matrices d'os déjà calculées par le viewport ; les buffers
  vertex/index du `SkinnedMesh` ; le cache descripteur bindless pour les
  textures ; et `BuildSubmeshMaterialIndices` pour router peau/habit par
  sous-maillage.
- **Nouveaux shaders** : `skinned_preview.vert` + `skinned_preview.frag`
  (une seule sortie couleur R8G8B8A8_UNORM ; pas de GBuffer / velocity /
  prev-frame). Le frag applique le **même depth bias peau** que l'in-world pour
  éviter le z-fighting peau/habit.
- **Caméra + lumière fixes** cadrant l'avatar (buste ou pieds-tête à décider à
  l'implémentation) ; le clip Idle est déjà samplé par le viewport.
- **Bascule de genre live** : `AuthImGuiCharacterCreate` passe au viewport, en
  plus du mesh (déjà via `SetMesh`), le **genre actif + l'id matériau peau**
  correspondant, pour router la peau exactement comme en jeu.

> Bénéfice transverse : l'aperçu est aussi l'**outil de diagnostic** de #1/#2 —
> vue rapprochée, éclairée, contrôlée où l'utilisateur voit immédiatement la peau
> par genre.

### 3.4 A4 — vocabulaire

[`AuthImGuiCharacterCreate.cpp` ~142](../../../src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp) :
garder le titre **« GENRE »**, renommer les boutons
**« Homme » → « Masculin »** et **« Femme » → « Féminin »**.
Le mapping interne reste `0 = male`, `1 = female`.

## 4. Tests & vérification

| Critère | Comment | Par qui |
|---------|---------|---------|
| CA5 (routage) | Test unitaire `BuildSubmeshMaterialIndices` (noms qui matchent → bons ids ; aucun match → tout habit ; submeshes vides → vide ; casse/trim). **Compile en CI ; exécuté par l'utilisateur** (CI compile-only). | CI (compile) + utilisateur |
| CA1 peau femelle | Créer un perso féminin, entrer en jeu | Utilisateur, post-build |
| CA2 peau visible | Observer les deux genres en jeu | Utilisateur, post-build |
| CA3 aperçu 3D live | Basculer Masculin/Féminin dans la création | Utilisateur, post-build |
| CA4 vocabulaire | Écran de création | Utilisateur, post-build |

Mise à jour **`CODEBASE_MAP.md`** : nouveau module `AvatarMaterialRouting`,
shaders d'aperçu, passage du stub `RacePreviewViewport` au rendu forward réel.

## 5. Documentation (convention repo)

- Commentaires **en français**, clarté > brièveté.
- Les fonctions ajoutées/modifiées dans le périmètre éditeur monde doivent être
  documentées (`///` Doxygen) — **non concerné ici** (périmètre client de jeu),
  mais on documente quand même la fonction pure et les nouvelles méthodes du
  viewport pour la lisibilité.
- Convention winding Vulkan : le pipeline d'aperçu rend un mesh **issu de
  fichier** (Male_/Female_Ranger) — vérifier le winding réel du maillage avant
  de figer `frontFace`/`cullMode` ; ne pas « uniformiser » aveuglément sur le
  terrain.

## 6. Déploiement

> **Déploiement** : ✅ **client uniquement** (rendu, UI, shaders) — **pas de
> redéploiement serveur**. La persistance serveur du genre (DB) reste hors
> périmètre, différée.

## 7. Risques

- **Régression terrain (winding)** : nouveau pipeline → respecter la garde
  anti-régression `frontFace`/`cullMode` du `CLAUDE.md`.
- **Diagnostic dépendant du runtime** : #1/#2 ne pourront être confirmés
  corrigés qu'après build CI + observation utilisateur ; le design réduit le
  risque via l'instrumentation et l'aperçu, mais ne garantit pas la cause exacte
  avant exécution.
- **Coût GPU de l'aperçu** : un depth buffer + un pipeline supplémentaires ;
  négligeable pour une vignette, mais à libérer proprement (Shutdown).
