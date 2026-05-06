# CHAR-MODEL.9 — State Machine **locomotion**

## Dépendances
- CHAR-MODEL.8 (`AnimationPlayer`, `BlendLocalPoses`)

## Cadrage

Implémenter la **machine d'états locomotion** d'un personnage :

```
Idle ──► Walk ──► Run
  ▲       │        │
  │       ▼        ▼
  └──── WalkSlow (eau/sable/neige)
  │
  └─► Jump ─► Fall ─► Land ─► Idle
  │
  └─► SwimIdle ◄──► SwimMove
```

Entrées : un `LocomotionInput` mis à jour par le gameplay (vitesse XZ,
isGrounded, surfaceType, mode water/normal). Sortie : un nom de clip à
jouer + un `playRate` (cycle adapté à la vitesse).

Le câblage final `CharacterController → input` est CHAR-MODEL.26.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/AnimationBlender.h
ls engine/gameplay/CharacterController.h
```

---

## Spécification technique

### Entrées

```cpp
// engine/gameplay/anim/LocomotionStateMachine.h
namespace engine::gameplay::anim
{
    enum class SurfaceType : uint8_t { Normal = 0, Water = 1, Sand = 2, Snow = 3 };
    enum class LocomotionMode : uint8_t { Ground = 0, Swim = 1 };

    struct LocomotionInput
    {
        engine::math::Vec2 velocityXZ;     // m/s (norme = vitesse au sol)
        bool               isGrounded   = true;
        bool               jumpRequested = false;
        SurfaceType        surface       = SurfaceType::Normal;
        LocomotionMode     mode          = LocomotionMode::Ground;
    };
}
```

### États

```cpp
enum class LocomotionState : uint8_t
{
    Idle = 0,
    WalkSlow,    // surface ∈ {Water, Sand, Snow}
    Walk,
    Run,
    Jump,        // briève, déclenchée par input
    Fall,        // !isGrounded en mode Ground après Jump ou chute
    Land,        // briève, à la reprise de contact
    SwimIdle,
    SwimMove,
};
```

### Seuils de vitesse (m/s)

| Transition                  | Seuil                     |
|-----------------------------|---------------------------|
| Idle → Walk                 | speed > 0.2               |
| Walk → Run                  | speed > 3.0               |
| Run → Walk                  | speed < 2.5  *(hystérésis)* |
| Walk → Idle                 | speed < 0.1  *(hystérésis)* |
| any Ground → WalkSlow       | surface ∈ {Water,Sand,Snow} **et** speed > 0.1 |
| WalkSlow → Walk             | surface = Normal          |
| Idle/Walk/Run → Jump        | jumpRequested **et** isGrounded |
| Jump → Fall                 | après 0.25 s **ou** vitesse verticale ≤ 0 |
| Fall → Land                 | isGrounded redevient true |
| Land → Idle/Walk/Run        | après 0.15 s              |
| Ground states → SwimIdle    | mode = Swim               |
| SwimIdle → SwimMove         | speed > 0.2               |
| SwimMove → SwimIdle         | speed < 0.1               |

Hystérésis : différencier seuil d'entrée et de sortie pour éviter le
flickering (déjà encodé dans la table).

### Mapping clips & playRate

```cpp
struct LocomotionClipMap
{
    const AnimationClip* idle      = nullptr;
    const AnimationClip* walkSlow  = nullptr;
    const AnimationClip* walk      = nullptr;
    const AnimationClip* run       = nullptr;
    const AnimationClip* jump      = nullptr;
    const AnimationClip* fall      = nullptr;
    const AnimationClip* land      = nullptr;
    const AnimationClip* swimIdle  = nullptr;
    const AnimationClip* swimMove  = nullptr;
};
```

`playRate` :
- `Walk` : `playRate = clamp(speed / referenceWalkSpeed, 0.5, 1.5)`
  avec `referenceWalkSpeed = 1.5 m/s`.
- `Run`  : `playRate = clamp(speed / referenceRunSpeed,  0.7, 1.3)`
  avec `referenceRunSpeed  = 5.0 m/s`.
- Autres états : `playRate = 1.0`.

### API

```cpp
class LocomotionStateMachine
{
public:
    void Init(const LocomotionClipMap& clips);
    void SetCrossfadeTime(float seconds); // défaut : 0.20

    /// Tick : met à jour l'état + push la transition au player.
    void Tick(const LocomotionInput& input, float dtSec, AnimationPlayer& player);

    LocomotionState State() const;
};
```

### Transitions cross-fade

- Toutes les transitions normales utilisent `CrossfadeTo(clip, 0.20s)`.
- Idle ↔ Walk ↔ Run : 0.20 s.
- Toute transition impliquant Jump / Fall / Land : 0.10 s (réactivité).
- Land → Idle/Walk/Run : 0.15 s.

### Transition vers `Land` ne court-circuite pas

`Land` doit jouer son cycle minimum (0.15 s) même si la vitesse passe
> 3 m/s pendant ce temps. À la sortie, l'état cible est calculé d'après
les conditions courantes.

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/anim/LocomotionStateMachine.h` + `.cpp`
- `tests/gameplay/LocomotionSM_IdleWalkRun_test.cpp`
- `tests/gameplay/LocomotionSM_JumpFallLand_test.cpp`
- `tests/gameplay/LocomotionSM_Surface_test.cpp`
- `tests/gameplay/LocomotionSM_Swim_test.cpp`

**Modifiés :**
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/anim/LocomotionStateMachine.h
    engine/gameplay/anim/LocomotionStateMachine.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `LocomotionSM_IdleWalkRun_test` : injection séquentielle de
      vitesses 0 / 1 / 5 / 1 / 0 → suite d'états Idle / Walk / Run / Walk
      / Idle.
- [ ] Test `LocomotionSM_JumpFallLand_test` : Jump → Fall → Land → Idle.
- [ ] Test `LocomotionSM_Surface_test` : `surface=Sand` à 1 m/s →
      `WalkSlow`, retour Normal → `Walk`.
- [ ] Test `LocomotionSM_Swim_test` : `mode=Swim` à 0 m/s → `SwimIdle`,
      à 1 m/s → `SwimMove`.
- [ ] Hystérésis : ping-pong vitesse 2.4 / 2.6 m/s ne provoque **pas**
      d'oscillation Walk↔Run.

---

## Anti-objectifs

- **Ne pas** brancher `CharacterController` (CHAR-MODEL.26).
- **Ne pas** gérer les actions ni le combat (CHAR-MODEL.10, 11).
- **Ne pas** faire d'IK (CHAR-MODEL.13).
- **Ne pas** faire d'I/O fichier (les clips sont fournis en pointeurs
  par `Init`).
- **Ne pas** gérer la fatigue ni les buffs/debuffs.
