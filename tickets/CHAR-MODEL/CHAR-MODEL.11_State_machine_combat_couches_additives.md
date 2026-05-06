# CHAR-MODEL.11 — State Machine **combat** + couches additives

## Dépendances
- CHAR-MODEL.8 (`AnimationPlayer`)
- CHAR-MODEL.9 (locomotion en cours sous la couche combat)

## Cadrage

Implémenter la **machine d'états combat** avec **couches additives** :
le haut du corps joue l'attaque/cast tandis que le bas continue à
courir/marcher. Catégories d'armes : 1H, 2H, Bow, Spear, Dagger, Staff,
+ canal `Cast` pour les sorts.

L'additive layer additionne un **delta de pose** (cible − bind pose) au
résultat de la couche locomotion, masqué par un **bone mask** qui isole
le haut du corps (épaules, bras, mains, tête optionnelle).

---

## Pré-requis vérifiables

```bash
git status
ls engine/gameplay/anim/LocomotionStateMachine.h
ls engine/gameplay/anim/ActionStateMachine.h
ls engine/render/AnimationBlender.h
```

---

## Spécification technique

### Bone mask

```cpp
// engine/render/AnimationBlender.h (extension)
namespace engine::render
{
    /// Poids par os pour blending masqué. weight[i] ∈ [0,1].
    /// 0 = la couche additive n'affecte pas cet os ; 1 = pleine influence.
    struct BoneMask
    {
        std::vector<float> weights;  // taille = skeleton.joints.size()
    };

    /// Construit un mask à partir d'une liste de noms d'os "racines additives".
    /// Tous les descendants de ces os reçoivent weight 1, les autres 0.
    /// La transition est binaire (pas de feathering) — option future.
    BoneMask BuildBoneMaskFromRoots(
        const Skeleton& skeleton,
        std::span<const std::string_view> rootJointNames);

    /// Applique un additif masqué : out[i] = base[i] ⊕ (additive[i] − bind[i]) * mask[i]
    /// Pour T/S : ⊕ = somme (weighted lerp depuis base).
    /// Pour R   : applique un quaternion delta = additive * conjugate(bind),
    ///            puis slerp(identité, delta, mask[i]) * base.rotation.
    void ApplyAdditiveLayer(
        std::span<const JointLocalTransform> base,
        std::span<const JointLocalTransform> additive,
        std::span<const JointLocalTransform> bind,
        const BoneMask& mask,
        std::span<JointLocalTransform> out);
}
```

### États combat

```cpp
// engine/gameplay/anim/CombatStateMachine.h
namespace engine::gameplay::anim
{
    enum class WeaponClass : uint8_t
    {
        None = 0,
        OneHand,
        TwoHand,
        Bow,
        Spear,
        Dagger,
        Staff,
    };

    enum class CombatAction : uint8_t
    {
        None = 0,
        Idle,           // pose d'attente combat (arme dégainée)
        Attack,         // un coup
        AttackChain,    // combo (attack puis attack rapide)
        Block,
        Parry,          // fenêtre brève après Block
        Cast,           // incantation (durée variable)
        CastRelease,    // brève, à la sortie de Cast
        Hit,            // réception d'un coup
    };
}
```

### Mapping clips

Un set par classe d'arme :

```cpp
struct CombatClipSet
{
    const AnimationClip* idle        = nullptr;
    const AnimationClip* attack      = nullptr;
    const AnimationClip* attackChain = nullptr;
    const AnimationClip* block       = nullptr;
    const AnimationClip* parry       = nullptr;
    const AnimationClip* hit         = nullptr;
};

struct CastClipSet
{
    const AnimationClip* castStart   = nullptr;
    const AnimationClip* castLoop    = nullptr;   // boucle pendant l'incantation
    const AnimationClip* castRelease = nullptr;
};
```

### API

```cpp
class CombatStateMachine
{
public:
    void Init(
        const std::array<CombatClipSet, 7>& weaponSets, // index = WeaponClass
        const CastClipSet&                  castSet,
        const BoneMask&                     upperBodyMask,
        const Skeleton*                     skeleton);

    void SetWeaponClass(WeaponClass w);
    void RequestAttack();
    void RequestBlock(bool held);    // true = appuyé, false = relâché
    void RequestCast(float durationSec);
    void OnHit();                    // déclenche Hit court (interrompt sauf Cast)

    /// Tick combat. La pose locomotion est passée en `lowerBase` ; la SM
    /// produit la pose finale (locomotion bas du corps + additive haut du corps).
    /// `scratch{A,B,C}` doivent avoir la taille du squelette.
    void Tick(
        float dtSec,
        std::span<const JointLocalTransform> lowerBase,
        std::span<JointLocalTransform> scratchA,
        std::span<JointLocalTransform> scratchB,
        std::span<JointLocalTransform> scratchC,
        std::span<JointLocalTransform> outFinal);

    CombatAction State() const;
    WeaponClass  Weapon() const;
};
```

### Comportement

- `Idle` (combat idle) joue dès `SetWeaponClass(non-None)`.
- `Attack` interrompt `Idle/Block`, joue le clip une fois, retourne à
  `Idle`.
- Si `RequestAttack` arrive **pendant** la fenêtre `[0.5×duration,
  duration]` de `Attack` → enchaîne `AttackChain`.
- `Block(true)` → état `Block` (loop) ; `Block(false)` → fenêtre `Parry`
  de 0.20 s puis `Idle`.
- `Cast(duration)` :
  - joue `castStart` (durée fixe) ;
  - puis loop `castLoop` jusqu'à `duration - castRelease.duration` ;
  - puis joue `castRelease`.
- `Hit` interrompt `Idle/Attack/Block` (pas `Cast` — la table de
  fragilité Cast est gameplay, pas anim).

### Transitions & blending

- Cross-fade 0.10 s entre clips combat ;
- Le résultat additive est appliqué **par-dessus** la pose locomotion
  fournie par le caller, masqué `upperBodyMask`.

### Bone mask par défaut "haut du corps"

Roots : `spine_2`, `clavicle_l`, `clavicle_r`. Ajuster selon le rig
humanoid_v1 (CHAR-MODEL.14).

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/anim/CombatStateMachine.h` + `.cpp`
- *(extension)* `engine/render/AnimationBlender.h` (BoneMask + ApplyAdditive)
- `tests/gameplay/CombatSM_AttackChain_test.cpp`
- `tests/gameplay/CombatSM_BlockParry_test.cpp`
- `tests/gameplay/CombatSM_Cast_test.cpp`
- `tests/render/AnimationBlender_AdditiveMask_test.cpp`

**Modifiés :**
- `engine/render/AnimationBlender.cpp` (ajout `BuildBoneMaskFromRoots`,
  `ApplyAdditiveLayer`)
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/anim/CombatStateMachine.h
    engine/gameplay/anim/CombatStateMachine.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `CombatSM_AttackChain_test` : Attack → 2nd RequestAttack
      pendant fenêtre → AttackChain joué.
- [ ] Test `CombatSM_BlockParry_test` : Block(true) → Block ; Block(false)
      → Parry pendant 0.20 s → Idle.
- [ ] Test `CombatSM_Cast_test` : Cast(2 s) → castStart + castLoop +
      castRelease, durée totale ≈ 2 s ± 0.05 s.
- [ ] Test `AnimationBlender_AdditiveMask_test` : un mask qui isole un
      seul os fait que les autres os ont une pose strictement égale à
      `base`.

---

## Anti-objectifs

- **Ne pas** gérer les hitboxes ni les dégâts.
- **Ne pas** déclencher de sons/particules.
- **Ne pas** introduire de feathering du bone mask (binaire à ce stade).
- **Ne pas** brancher au gameplay (CHAR-MODEL.26 fait le câblage).
