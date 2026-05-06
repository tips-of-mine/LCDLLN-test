# CHAR-MODEL.25 — Customisation morphologique (sliders bornés par race)

## Dépendances
- CHAR-MODEL.24 (`PreviewRenderer` + cache race)

## Cadrage

Permettre au joueur d'ajuster, via des sliders ImGui dans le wizard,
**3 paramètres morphologiques** :
- `globalScale`        — taille globale,
- `corpulence`         — épaisseur du torse / proportions,
- `limbsLength`        — longueur des membres.

Bornes lues depuis le manifest race (`morphologyBounds`). Application :
modification de la **bind pose** du squelette **par instance** (pas par
asset) — le squelette est partagé en RAM, mais une couche de scaling
appliquée par-dessus la pose locale produite par le sampler.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/PreviewRenderer.h
ls engine/client/CharacterCreationUi.{h,cpp}
ls game/data/models/humain/humain.race.json
```

---

## Spécification technique

### Paramètres

```cpp
// engine/gameplay/MorphologyParams.h (nouveau)
namespace engine::gameplay
{
    struct MorphologyParams
    {
        float globalScale  = 1.0f;
        float corpulence   = 1.0f;
        float limbsLength  = 1.0f;
    };

    /// Bornes lues depuis le manifest race.
    struct MorphologyBounds
    {
        engine::math::Vec2 globalScale  = {0.9f, 1.1f};
        engine::math::Vec2 corpulence   = {0.85f, 1.2f};
        engine::math::Vec2 limbsLength  = {0.95f, 1.05f};
    };

    MorphologyParams ClampToBounds(const MorphologyParams& p,
                                   const MorphologyBounds& b);
}
```

### Application sur la pose

Une **post-passe** sur la pose locale, après le sampler/blender :

```cpp
// engine/render/MorphologyApplicator.h
namespace engine::render
{
    /// Applique des scales par-os à la pose locale en place.
    /// `limbsScale` s'applique aux os longs (upperarm, forearm, thigh, shin).
    /// `torsoScale` s'applique à pelvis/spine_1/spine_2/spine_3.
    /// `globalScale` multiplie la translation root.
    /// Conventions os = humanoid_v1.
    void ApplyHumanoidMorphology(
        const Skeleton& skeleton,
        float globalScale,
        float corpulenceScale,
        float limbsScale,
        std::span<JointLocalTransform> ioPose);
}
```

Détail :
- `globalScale` : `pose[root].scale *= globalScale` (le scale se
  propage par hiérarchie aux enfants).
- `corpulenceScale` : pour chaque os du torse listé,
  `pose[i].scale.x *= corpulenceScale`, `pose[i].scale.z *=
  corpulenceScale` (pas Y pour ne pas allonger verticalement).
- `limbsScale` : pour chaque os de membre listé, `pose[i].scale.y *=
  limbsScale` (Y = axe long de l'os si convention respectée).

### Liste d'os par catégorie (humanoid_v1)

```cpp
constexpr std::array<std::string_view, 4> kTorsoBones = {
    "pelvis", "spine_1", "spine_2", "spine_3"
};
constexpr std::array<std::string_view, 8> kLimbBones = {
    "upperarm_l", "forearm_l", "upperarm_r", "forearm_r",
    "thigh_l",    "shin_l",    "thigh_r",    "shin_r"
};
```

### Étendre `CharacterCreationState`

```cpp
struct CharacterCreationState
{
    // Existant inchangé…
    float previewRotationDeg = 0.0f;

    // Nouveau (CHAR-MODEL.25)
    engine::gameplay::MorphologyParams morphology;
};
```

### UI ImGui

Trois sliders dans la section dédiée du wizard, intitulés "Taille",
"Corpulence", "Longueur des membres". Bornes lues du manifest race
courant et passées à `ImGui::SliderFloat`. Appliquer les valeurs au
`PreviewRenderer` via une nouvelle méthode `SetMorphology(params)`.

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/MorphologyParams.h` + `.cpp`
- `engine/render/MorphologyApplicator.h` + `.cpp`
- `tests/render/MorphologyApplicator_TorsoLimbs_test.cpp`

**Modifiés :**
- `engine/client/CharacterCreationUi.h` (ajout `morphology` dans
  `CharacterCreationState`)
- `engine/client/CharacterCreationUi.cpp` (sliders + propagation)
- `engine/render/PreviewRenderer.{h,cpp}` (méthode `SetMorphology`,
  appel `ApplyHumanoidMorphology` après sample/blend)
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/MorphologyParams.h
    engine/gameplay/MorphologyParams.cpp
    engine/render/MorphologyApplicator.h
    engine/render/MorphologyApplicator.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `MorphologyApplicator_TorsoLimbs_test` :
      - pose bind initiale : taille 1.80 m,
      - `ApplyHumanoidMorphology(globalScale=0.9)` → taille ≈ 1.62 m,
      - `limbsScale=1.10` → bras + jambes plus longs (mesure des
        positions monde des extrémités).
- [ ] `ClampToBounds` rejette les valeurs hors bornes (clamp).
- [ ] Les sliders du wizard mettent à jour le preview en temps réel.
- [ ] Aucun saut visuel quand on change de race avec morphologie
      personnalisée (la morpho est ré-appliquée aux nouvelles bornes).

---

## Anti-objectifs

- **Ne pas** modifier les `.skinmesh` ni `.skel` (la morpho est runtime).
- **Ne pas** introduire de blendshape (réservé à une future release).
- **Ne pas** ajouter d'autres axes morphologiques (visage, mains…).
- **Ne pas** persister la morphologie côté serveur dans ce ticket
  (la persistance vient avec la création de personnage finale).
