# CHAR-MODEL.26 — Câblage `CharacterController → Animation`

## Dépendances
- CHAR-MODEL.9 (locomotion SM)
- CHAR-MODEL.10 (actions SM)
- CHAR-MODEL.11 (combat SM, additive)
- CHAR-MODEL.12 (sockets — pour attacher arme à `mainR`)
- CHAR-MODEL.13 (FootIK — appliqué uniquement en états Ground)
- CHAR-MODEL.14 (clips placeholder)
- CHAR-MODEL.23 (`MaterialOverride` pour la teinte race)

## Cadrage

Câbler le **runtime gameplay** au **runtime animation** : pour chaque
entité jouable / NPC humanoïde, un `CharacterAnimator` qui :

1. lit l'état du `CharacterController` (vitesse XZ, isGrounded,
   surface, mode water/ground, mounted, dead, etc.),
2. nourrit `LocomotionStateMachine`,
3. arbitre la priorité avec `ActionStateMachine`,
4. additionne la couche combat si arme dégainée,
5. applique `FootIK` (post-passe en états Ground),
6. produit la pose finale + la palette skin,
7. soumet un `SkinnedRenderable` à la queue de rendu chaque frame.

À la fin de ce ticket, un personnage humain charge en jeu, marche /
court / saute / nage / attaque visuellement, avec ombres CSM et IK
pieds.

---

## Pré-requis vérifiables

```bash
git status
ls engine/gameplay/CharacterController.h
grep -n "isGrounded\|velocity\|surfaceType" engine/gameplay/CharacterController.h | head
ls engine/gameplay/anim/LocomotionStateMachine.h
ls engine/gameplay/anim/ActionStateMachine.h
ls engine/gameplay/anim/CombatStateMachine.h
ls engine/gameplay/anim/FootIK.h
ls engine/render/SkinnedRenderable.h
ls engine/render/PreviewRenderer.h
```

---

## Spécification technique

### Composant

```cpp
// engine/gameplay/CharacterAnimator.h
namespace engine::gameplay
{
    /// Une instance par entité humanoïde animée. Possède son AnimationPlayer
    /// (couche locomotion) et sa CombatStateMachine. Pas de allocator dans
    /// le hot path.
    class CharacterAnimator
    {
    public:
        struct InitParams
        {
            std::string                                    raceId;        // "humains" etc.
            engine::render::AssetRegistry*                 assets   = nullptr;
            const engine::render::Skeleton*                skeleton = nullptr;
            engine::render::SkinnedMeshHandle              mesh;
            engine::render::SkeletonHandle                 skeletonHandle;
            const engine::render::SocketCatalog*           sockets  = nullptr;
            const engine::gameplay::anim::LocomotionStateMachine::ClipMap* locoClips = nullptr;
            const engine::gameplay::anim::ActionStateMachine::ClipMap*     actionClips = nullptr;
            const engine::gameplay::anim::CombatStateMachine* combat   = nullptr; // ou clipsets
            const engine::render::BoneMask*                upperBodyMask = nullptr;
            const engine::render::RaceColorTable*          colors  = nullptr;
            engine::render::MaterialOverridePool*          overridePool = nullptr;
            const engine::gameplay::anim::FootIKConfig*    footIkCfg = nullptr;
        };

        bool Init(const InitParams& p);
        void SetMorphology(const MorphologyParams& m);

        struct ControllerSnapshot
        {
            engine::math::Vec2 velocityXZ;
            bool               isGrounded;
            bool               jumpRequested;
            anim::SurfaceType  surface;
            anim::LocomotionMode mode;
            bool               isMounted;
            bool               isDead;
            engine::math::Mat4 modelMatrix;
        };

        /// Un tick = un sub-step gameplay (60 Hz typiquement).
        /// Production : pose finale + palette skin + SkinnedRenderable poussé.
        void Tick(
            const ControllerSnapshot& snap,
            float dtSec,
            const IWorldCollider* world,                  // peut être nullptr
            engine::render::SkinPaletteBuffer& paletteBuffer,
            engine::render::SceneRenderQueue& renderQueue);

        // Actions explicites (input → action SM)
        void RequestAction(anim::ActionRequest req);
        void RequestAttack();
        void RequestBlock(bool held);
        void RequestCast(float durationSec);
        void OnHit();
    };
}
```

### Pipeline interne par Tick

```
1. Si snap.isDead :
     ActionSM.Request(Die) ; ne plus tick locomotion / combat.
2. ActionSM.Tick(...) → si actif, scratch = pose actionnelle ; sinon :
3. Locomotion.Tick(input) → AnimationPlayer.Evaluate → scratchA
4. Combat.Tick(scratchA, scratchB, scratchC, scratchD, outFinal)
     - si arme rangée et état combat=None : outFinal = scratchA
5. ApplyHumanoidMorphology(outFinal, morpho)
6. ComputeModelMatrices(skeleton, outFinal, jointModel)
7. Si snap.isGrounded et mode=Ground :
     FootIK.Apply(world, modelMatrix, dt, jointModel, outFinal) →
       recalcul jointModel.
8. ComputeSkinPalette(skeleton, jointModel, palette)
9. offset = paletteBuffer.Append(palette)
10. SkinnedRenderable r = { mesh, skeleton, modelMatrix, offset,
                            joints.size(), materialOverrideId, lod };
    renderQueue.AddSkinned(r);
```

Tous les `scratch` sont **pré-alloués** au `Init` (taille = `joints.
size()`) ; pas d'allocation par tick.

### Resolution `MaterialOverride`

À l'`Init`, lookup `colors->Find(raceId)` → `tint` →
`overridePool.Register({tint, 0.5})` → cache l'id dans `m_materialOverrideId`.

### Mounted

Si `snap.isMounted == true` : la machine de locomotion **n'est pas
tickée** ; à la place, la pose `sit_object` (action) est forcée.
Le couplage `CharacterController` → transform monture est délégué à
`MountSystem` (CHAR-MODEL.30).

### Sockets armes

Pas câblé dans ce ticket : le rendu de l'arme attachée est traité
ailleurs (system inventaire ; le ticket fournit `Socket` pour qu'il
puisse calculer la matrice via `ComputeSocketWorldMatrix`). Documentation
en commentaire.

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/CharacterAnimator.h` + `.cpp`
- `tests/gameplay/CharacterAnimator_LocomotionEnd2End_test.cpp`
  (mock CharacterController, mock IWorldCollider)
- `tests/gameplay/CharacterAnimator_DieLocked_test.cpp`

**Modifiés :**
- `CMakeLists.txt`
- *(peut nécessiter)* `engine/gameplay/CharacterController.h` pour
  exposer un getter `surfaceType()` et `mode()` si non présents — à
  faire **a minima**, sans casser les utilisateurs existants.

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/CharacterAnimator.h
    engine/gameplay/CharacterAnimator.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `CharacterAnimator_LocomotionEnd2End_test` :
      - injecte vitesses 0 / 1.5 / 5 m/s,
      - vérifie que l'`AnimationPlayer` sous-jacent passe Idle / Walk
        / Run dans les transitions attendues,
      - vérifie qu'un `SkinnedRenderable` est poussé chaque tick avec
        un `skinPaletteOffset` valide.
- [ ] Test `CharacterAnimator_DieLocked_test` : `isDead=true` → pose
      `Die` jouée et verrouillée.
- [ ] FootIK actif uniquement en états Ground (vérifié via flag interne
      observable ou via comparaison de poses avec/sans IK).
- [ ] Aucun gain mémoire / fuite VRAM en boucle de 1 000 ticks.
- [ ] Performance : 100 entités × tick ≤ 2 ms CPU sur la box de
      référence.

---

## Anti-objectifs

- **Ne pas** modifier la logique du `CharacterController` (capsule,
  marche/course/saut/nage/vol) — uniquement ajouter des getters si
  manquants.
- **Ne pas** introduire de nouvelle SM (toutes existent en Phase 1).
- **Ne pas** câbler `MountSystem` (CHAR-MODEL.30).
- **Ne pas** câbler le rendu d'armes attachées.
- **Ne pas** allouer dans le hot path.
