# CHAR-MODEL.10 — State Machine **actions**

## Dépendances
- CHAR-MODEL.8 (`AnimationPlayer`)

## Cadrage

Implémenter la **machine d'états des actions non-combat** : Greet (saluer),
Sit (assis sol/objet), LieDown (couché sol/lit), Eat (debout/assis),
Drink (debout/assis), OpenContainer, UseObject (générique), Die.

Cette machine est **complémentaire** à la locomotion (CHAR-MODEL.9). En
général, déclencher une action **interrompt** la locomotion : le tick
de la machine d'actions a la priorité tant qu'une action est active.

---

## Pré-requis vérifiables

```bash
git status
ls engine/gameplay/anim/LocomotionStateMachine.h
ls engine/render/AnimationBlender.h
```

---

## Spécification technique

### Actions

```cpp
// engine/gameplay/anim/ActionStateMachine.h
namespace engine::gameplay::anim
{
    enum class Action : uint8_t
    {
        None = 0,
        Greet,
        SitGround,
        SitObject,
        LieDownGround,
        LieDownBed,
        EatStanding,
        EatSitting,
        DrinkStanding,
        DrinkSitting,
        OpenContainer,
        UseObject,
        Die,
    };

    struct ActionRequest
    {
        Action action = Action::None;
        // Méta optionnelle : ID d'objet ciblé, transform du point d'ancrage…
        uint32_t targetObjectId = 0;
    };
}
```

### Catégories de comportement

| Action            | Loopée | Interruptible | Priorité |
|-------------------|--------|---------------|----------|
| Greet             | Non    | Oui           | Basse    |
| SitGround/Object  | Oui    | Oui           | Moyenne  |
| LieDownGround/Bed | Oui    | Oui           | Moyenne  |
| EatX / DrinkX     | Oui    | Oui           | Moyenne  |
| OpenContainer     | Non    | Non           | Moyenne  |
| UseObject         | Non    | Non           | Moyenne  |
| Die               | Non    | **Non**       | Critique |

`Die` ne peut **pas** être interrompue : une fois entrée, la SM y reste
verrouillée jusqu'à un `Reset()` explicite.

### API

```cpp
class ActionStateMachine
{
public:
    struct ClipMap
    {
        const AnimationClip* greet           = nullptr;
        const AnimationClip* sitGround       = nullptr;
        const AnimationClip* sitObject       = nullptr;
        const AnimationClip* lieDownGround   = nullptr;
        const AnimationClip* lieDownBed      = nullptr;
        const AnimationClip* eatStanding     = nullptr;
        const AnimationClip* eatSitting      = nullptr;
        const AnimationClip* drinkStanding   = nullptr;
        const AnimationClip* drinkSitting    = nullptr;
        const AnimationClip* openContainer   = nullptr;
        const AnimationClip* useObject       = nullptr;
        const AnimationClip* die             = nullptr;
    };

    void Init(const ClipMap& clips);
    void SetCrossfadeTime(float seconds); // défaut : 0.15

    /// Demande de jouer une action ; ignorée si la SM est verrouillée.
    void Request(const ActionRequest& req);

    /// Annule l'action en cours (no-op si action non-interruptible).
    void Cancel();

    /// Tick : avance le temps interne, push la transition au player.
    /// Retourne `true` si la SM est active (l'extérieur ne doit alors
    /// pas tick la locomotion).
    bool Tick(float dtSec, AnimationPlayer& player);

    Action Current() const;
    bool   IsLocked() const;   // true en Die
    bool   IsPlaying() const;
};
```

### Sémantique du tick

- Si `Current() == None` → `Tick` retourne `false` (locomotion peut
  jouer).
- Sinon :
  - avance le temps interne ;
  - si action **non-loopée** et `time ≥ clip.duration` : auto-retour à
    `None`, retourne `false` au tick suivant ;
  - si action **loopée** : reste active tant qu'aucune nouvelle requête
    ni `Cancel()` n'arrive ;
  - retourne `true` tant que active.

### Coordination avec la locomotion (informatif)

Le call-site (CHAR-MODEL.26) :
1. `ActionSM.Tick(...)` ;
2. si false → `LocomotionSM.Tick(...)`.

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/anim/ActionStateMachine.h` + `.cpp`
- `tests/gameplay/ActionSM_PlayOneShot_test.cpp`
- `tests/gameplay/ActionSM_LoopAndCancel_test.cpp`
- `tests/gameplay/ActionSM_DieLocked_test.cpp`

**Modifiés :**
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/anim/ActionStateMachine.h
    engine/gameplay/anim/ActionStateMachine.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `ActionSM_PlayOneShot_test` : `Request(Greet)` → IsPlaying
      pendant la durée du clip, retourne `None` ensuite.
- [ ] Test `ActionSM_LoopAndCancel_test` : `Request(SitGround)` reste
      active après la durée du clip ; `Cancel()` repasse à `None`.
- [ ] Test `ActionSM_DieLocked_test` : après `Request(Die)`, toute
      requête ultérieure et `Cancel` sont ignorées ; `IsLocked()` true.
- [ ] Cross-fade visuel correct (validé par test combiné en CHAR-MODEL.26).

---

## Anti-objectifs

- **Ne pas** gérer le combat (CHAR-MODEL.11).
- **Ne pas** gérer les sockets / objets attachés (CHAR-MODEL.12).
- **Ne pas** déclencher d'événements gameplay (sons, particules) — la
  SM est purement animation.
- **Ne pas** appeler la locomotion : le call-site arbitre la priorité.
