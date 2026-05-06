# CHAR-MODEL.13 — IK pied (ray-cast terrain, ajustement vertical)

## Dépendances
- CHAR-MODEL.9 (locomotion : IK actif uniquement en états Ground)

## Cadrage

Implémenter une **IK pied à 2 os** (cuisse + jambe + cheville) avec
ray-cast terrain pour ajuster la hauteur des pieds aux irrégularités du
sol. Méthode : **TwoBoneIK analytique** (loi des cosinus), sans solveur
itératif. L'orientation de la cheville suit la normale du sol projetée.

Le système ajuste aussi la **hauteur des hanches** (root vertical) si
un pied est plus bas que le sol prévu par la pose, pour éviter que la
jambe soit hyper-tendue.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/Skeleton.h
ls engine/gameplay/anim/LocomotionStateMachine.h
ls engine/gameplay/CharacterController.h     # interface IWorldCollider potentielle
grep -n "IWorldCollider\|RayCast" engine/gameplay/*.h
```

Si l'interface de ray-cast monde s'appelle différemment, **adapter** le
nom dans la signature ci-dessous.

---

## Spécification technique

### Interface de ray-cast

Dépendance amont : un **`IWorldCollider`** (existant ou à confirmer)
exposant :

```cpp
// pseudo : adapter au nom existant
struct RayHit { engine::math::Vec3 position; engine::math::Vec3 normal; bool hit; };
class IWorldCollider
{
public:
    virtual RayHit RayCastDown(engine::math::Vec3 origin, float maxDistance) const = 0;
};
```

### Configuration FootIK

```cpp
// engine/gameplay/anim/FootIK.h
namespace engine::gameplay::anim
{
    struct FootIKConfig
    {
        // Indices d'os du squelette ciblé (humanoid_v1 par défaut).
        uint16_t hipL, kneeL, footL;
        uint16_t hipR, kneeR, footR;
        uint16_t pelvisRoot;       // racine bassin (pour ajustement vertical)

        float rayUpOffset    = 0.6f;   // m, ray part au-dessus du pied
        float rayDownLength  = 1.2f;   // m
        float maxFootLift    = 0.30f;  // m, plafond d'ajustement vers le haut
        float maxFootDrop    = 0.30f;  // m, plafond vers le bas (avant abaissement bassin)
        float blendInTime    = 0.15f;  // s, fade IK on
        float blendOutTime   = 0.20f;  // s, fade IK off
    };

    enum class IKActivation : uint8_t { Off = 0, On = 1 };

    class FootIK
    {
    public:
        void Init(const Skeleton* skeleton, const FootIKConfig& cfg);

        /// Active/désactive IK selon l'état (off en Jump/Fall/SwimX).
        void SetActivation(IKActivation a);

        /// Ajuste la pose en place. modelMatrix = transform monde de l'entité.
        /// Lecture seule : skeleton, world. Écriture : pose.
        /// Coût O(1).
        void Apply(
            const IWorldCollider& world,
            const engine::math::Mat4& modelMatrix,
            float dtSec,
            std::span<engine::math::Mat4> jointModelMatrices,   // déjà calculées par Compute
            std::span<JointLocalTransform> ioLocalPose);
    };
}
```

### Algorithme

Pour chaque pied (L/R) :
1. Calculer la position monde de la cheville à partir des matrices
   model-space + `modelMatrix`.
2. RayCast vers le bas : origine = position cheville + `rayUpOffset` Y,
   distance max = `rayUpOffset + rayDownLength`.
3. Si hit `pHit`, normale `nHit` :
   - `desiredFootY = pHit.y + bindFootClearance` (ex. 0.02 m).
   - Si `desiredFootY > footY` (sol plus haut que la pose) **et**
     `desiredFootY - footY ≤ maxFootLift` :
     - Repositionner le pied verticalement à `desiredFootY` (espace monde).
     - Résoudre TwoBoneIK pour la chaîne cuisse → genou → cheville :
       analytique via la loi des cosinus, en respectant la longueur
       totale des os.
     - Orienter la cheville pour que sa normale plante = `nHit`
       (rotation autour de l'axe perpendiculaire à la direction de la
       jambe).
4. Si `desiredFootY < footY - maxFootDrop` (sol bien plus bas) :
   - Abaisser le bassin de `(footY - desiredFootY)` (clampé à
     `maxFootLift`) pour que la pose conserve le contact.
5. Blend IK avec un facteur `[0,1]` qui rampe selon `blendInTime/blendOutTime`.

### Activation par état

`IKActivation::On` quand `LocomotionState ∈ {Idle, Walk, WalkSlow, Run,
Land}`. `Off` sinon (Jump, Fall, Swim, action en cours).

### TwoBoneIK : équations

Étant donné `A` (hanche), `B` (genou), `C` (cheville), `T` (target),
longueurs `lAB`, `lBC` :

```
d = ‖T - A‖
cosTheta = clamp((lAB² + d² - lBC²) / (2 * lAB * d), -1, 1)
théta = acos(cosTheta)
    → angle entre AB et AT
cosPhi = clamp((lAB² + lBC² - d²) / (2 * lAB * lBC), -1, 1)
phi = acos(cosPhi)
    → angle au genou (entre BA et BC)
```

Plier le genou autour de l'axe **avant** (axe perpendiculaire à AT,
choisi pour que le genou pointe vers l'avant du personnage — utiliser
le forward du modèle).

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/anim/FootIK.h` + `.cpp`
- `tests/gameplay/FootIK_TwoBoneSolve_test.cpp`
- `tests/gameplay/FootIK_RayAdjust_test.cpp` (avec `IWorldCollider` mock)

**Modifiés :**
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/anim/FootIK.h
    engine/gameplay/anim/FootIK.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `FootIK_TwoBoneSolve_test` : pour 3 cibles connues, vérifie
      que l'angle au genou produit positionne effectivement la cheville
      à la cible (à 1e-3 près).
- [ ] Test `FootIK_RayAdjust_test` : avec un sol mock à `y = 0.1`,
      la pose ajustée a la cheville à `y ≈ 0.12` (clearance 0.02).
- [ ] L'IK est désactivée quand `LocomotionState=Jump/Fall/Swim`.
- [ ] Aucune allocation par appel `Apply`.

---

## Anti-objectifs

- **Ne pas** appliquer l'IK aux mains (pas demandé dans cette release).
- **Ne pas** introduire de solveur CCD/FABRIK (analytique uniquement).
- **Ne pas** câbler `IWorldCollider` réel : le test utilise un mock.
- **Ne pas** ajuster `LocomotionStateMachine` — IK est une post-passe.
- **Ne pas** modifier les clips d'animation.
