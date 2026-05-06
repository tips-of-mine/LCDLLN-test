# CHAR-MODEL.28 — FSM IA d'animal : Idle → Wander → Eat → Observe → Flee / Attack

## Dépendances
- CHAR-MODEL.27 (`quadruped_v1` + clips)

## Cadrage

Implémenter la **machine d'états IA** des animaux (mammifères terrestres
de cette release). Tick **indépendant des joueurs** : un animal continue
sa routine même quand aucun joueur ne le regarde (LOD-AI). Les
transitions sont stochastiques avec des durées variables.

Cette FSM produit des **inputs** pour la `LocomotionStateMachine`
existante (vitesse XZ, jump, etc.) plus des **actions** spécifiques
animales (Eat, Observe). Le câblage rendu réutilise `CharacterAnimator`
adapté aux quadrupèdes.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/quadruped_v1.skel
ls game/data/animations/quadruped/{idle,walk,run,eat,observe,attack,flee,die}.anim
ls engine/gameplay/anim/LocomotionStateMachine.h
```

---

## Spécification technique

### États

```cpp
// engine/gameplay/ai/AnimalAI.h
namespace engine::gameplay::ai
{
    enum class AnimalState : uint8_t
    {
        Idle = 0,        // pose au repos, durée 2-6 s
        Wander,          // marche aléatoire, durée 5-15 s
        Eat,             // tête baissée, loop 4-10 s
        Observe,         // tête levée, regarde alentour, 2-5 s
        Flee,            // course paniquée, déclenchée par menace
        Attack,          // approche + frappe (loup, ours)
        Dead,            // verrouillé après mort
    };

    enum class AnimalArchetype : uint8_t
    {
        Herbivore = 0,   // tendance Eat / Wander, fuit toute menace
        Predator,        // chasse, peut Attack
        Neutral,         // ni l'un ni l'autre (ex. cochon)
    };
}
```

### Inputs / outputs

```cpp
struct AnimalAIInput
{
    engine::math::Vec3 worldPos;      // position monde de l'animal
    engine::math::Vec3 nearestPlayerPos;
    float              distanceToPlayer;   // m
    bool               playerVisible;       // ligne de vue
    float              healthRatio;         // [0,1]
    bool               wasHit;              // true si vient d'être frappé ce tick
};

struct AnimalAIOutput
{
    engine::math::Vec2 desiredVelocityXZ;   // m/s, à passer à locomotion
    bool               playEat        = false;
    bool               playObserve    = false;
    bool               playAttack     = false;
    bool               playDie        = false;
};
```

### Paramètres par archétype

```cpp
struct AnimalAIParams
{
    AnimalArchetype archetype     = AnimalArchetype::Herbivore;
    float           wanderSpeed   = 1.0f;     // m/s
    float           fleeSpeed     = 4.0f;
    float           attackSpeed   = 3.0f;
    float           sightRange    = 12.0f;    // m
    float           fleeTriggerDistance = 8.0f;  // herbivore
    float           attackTriggerDistance = 6.0f; // predator
    float           wanderRadius  = 15.0f;    // ne s'éloigne pas trop du spawn
    engine::math::Vec3 spawnPos{0, 0, 0};
};
```

### API

```cpp
class AnimalAI
{
public:
    void Init(const AnimalAIParams& p, uint64_t rngSeed);
    void Tick(const AnimalAIInput& in, float dtSec);

    AnimalState  State() const;
    const AnimalAIOutput& Output() const;
    void         OnDeath();
    bool         IsDead() const;
};
```

### Transitions

```
Idle ─► Wander                      après timer expiré (random 2..6 s)
Wander ─► Idle                      après timer (5..15 s)
                                    OU si distance(spawn, pos) > wanderRadius
Wander ─► Eat                       30 % de chance toutes les 8 s
Eat ─► Idle                         après timer (4..10 s)
any (sauf Dead/Attack/Flee) ─► Observe   si distanceToPlayer < sightRange
                                          ET 20 % de chance / s
Observe ─► Idle                     après timer (2..5 s)

Herbivore :
  any ─► Flee                       si distanceToPlayer < fleeTriggerDistance
                                    OU wasHit
  Flee ─► Idle                      si distanceToPlayer > 1.5 × fleeTrigger
                                    pendant 3 s

Predator :
  Wander/Idle/Observe ─► Attack     si distanceToPlayer < attackTriggerDistance
                                    ET playerVisible
  Attack ─► Idle                    si distanceToPlayer > sightRange

healthRatio < 0.10 ─► Flee (forcée, override sauf Dead)
OnDeath() ─► Dead (verrouillé)
```

### Tick LOD

`AnimalAI::Tick` est **bon marché** (jamais de path-finding ici). Le
système d'orchestration global (CHAR-MODEL.37) tickera chaque animal
à 30 Hz si proche du joueur, 5 Hz si moyen, 0.5 Hz si très loin.

### RNG

`std::mt19937_64` seedé par animal (déterministe pour les tests).

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/ai/AnimalAI.h` + `.cpp`
- `tests/gameplay/AnimalAI_Herbivore_test.cpp`
- `tests/gameplay/AnimalAI_Predator_test.cpp`
- `tests/gameplay/AnimalAI_DeathLock_test.cpp`

**Modifiés :**
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/ai/AnimalAI.h
    engine/gameplay/ai/AnimalAI.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `AnimalAI_Herbivore_test` : injection `distanceToPlayer = 5 m`
      → état passe à `Flee`, `output.desiredVelocityXZ` non nul,
      direction opposée au joueur.
- [ ] Test `AnimalAI_Predator_test` : `distanceToPlayer = 5 m,
      playerVisible=true` → état passe à `Attack`.
- [ ] Test `AnimalAI_DeathLock_test` : `OnDeath` → état `Dead`,
      `playDie=true` une fois, ne ressort jamais.
- [ ] Déterminisme : même seed + même séquence d'inputs → même séquence
      d'états.
- [ ] Tick coût ≤ 1 µs sur la box de référence.

---

## Anti-objectifs

- **Ne pas** intégrer le pathfinding (les animaux marchent en ligne
  droite ; un système de navigation viendra plus tard).
- **Ne pas** intégrer les groupes / meutes.
- **Ne pas** intégrer les cycles jour/nuit.
- **Ne pas** câbler le rendu (`CharacterAnimator` quadrupède est le
  call-site, ce ticket fournit juste les outputs).
- **Ne pas** introduire de réseau / de réplication multijoueur.
