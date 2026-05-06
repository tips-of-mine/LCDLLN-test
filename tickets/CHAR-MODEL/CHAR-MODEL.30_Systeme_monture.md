# CHAR-MODEL.30 — Système de **monture**

## Dépendances
- CHAR-MODEL.12 (sockets / `ComputeSocketWorldMatrix`)
- CHAR-MODEL.26 (`CharacterAnimator` : flag `snap.isMounted`)
- CHAR-MODEL.29 (cheval avec socket `dosCheval`)

## Cadrage

Implémenter le **système de monture** : un personnage peut **monter**
une entité animale possédant `mountable=true`. Quand monté :
- le `CharacterController` du joueur **suit** la transform de la
  monture (le joueur n'a plus de translation propre),
- la pose du joueur est forcée à `sit_object` (déjà géré par
  `CharacterAnimator` quand `isMounted=true`),
- la position du joueur monde = `ComputeSocketWorldMatrix(monture,
  "dosCheval")`,
- la monture reçoit les commandes de mouvement (direction du joueur),
  son IA est suspendue tant qu'elle est montée.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/AttachmentSocket.h
ls engine/gameplay/CharacterAnimator.h
ls engine/gameplay/CharacterController.h
ls game/data/models/cheval/cheval.species.json
```

---

## Spécification technique

### API

```cpp
// engine/gameplay/MountSystem.h
namespace engine::gameplay
{
    struct MountableInfo
    {
        std::string socketName = "dosCheval";
        // …futurs paramètres : maxRiders, accelerationModifier, etc.
    };

    /// Référence faible vers une entité monture (ID stable côté gameplay).
    using MountId = uint64_t;
    constexpr MountId kInvalidMountId = 0;

    class MountSystem
    {
    public:
        /// Enregistre une entité comme monture potentielle.
        MountId Register(uint64_t entityId, const MountableInfo& info);
        void    Unregister(MountId id);

        /// Le joueur identifié par `playerId` monte `mount`. Échoue si la
        /// monture est déjà occupée ou hors de portée (≤ 3 m). Retourne true.
        bool Mount(uint64_t playerId, MountId mount, float distance);

        /// Démonte le joueur.
        void Dismount(uint64_t playerId);

        /// État.
        bool       IsMounted(uint64_t playerId) const;
        MountId    MountOf(uint64_t playerId) const;
        uint64_t   RiderOf(MountId mount) const;   // 0 si pas occupé

        /// À appeler chaque tick : met à jour la transform du joueur monté
        /// = ComputeSocketWorldMatrix(monture, "dosCheval"). La monture
        /// reçoit la commande de direction du joueur (passée en input).
        void Tick(float dtSec, IGameplayWorld& world);
    };
}
```

`IGameplayWorld` (interface mince à introduire si pas existante) expose :
- résolution `entityId → CharacterController*`,
- résolution `entityId → AnimationModelMatrices` (pour le socket monde),
- transmission d'inputs du joueur vers le `CharacterController` de la
  monture.

### Comportement

1. `Mount(player, mount, distance)` :
   - rejet si `distance > 3 m`, déjà monté, ou monture déjà occupée ;
   - met `playerSnapshot.isMounted = true`,
   - copie la position du socket `dosCheval` dans la position du
     `CharacterController` joueur,
   - suspend l'IA de la monture (flag interne dans `AnimalAI`),
   - le **bind** rider↔monture est stocké dans `MountSystem`.
2. `Tick` :
   - calcule la matrice du socket `dosCheval` ; assigne `position
     joueur = socket.translation`, `orientation joueur = socket.rotation`,
   - les inputs de mouvement du joueur sont **forwardés** au
     `CharacterController` de la monture ;
3. `Dismount` :
   - place le joueur 1 m à côté de la monture (gauche),
   - relâche `isMounted=false`,
   - réactive l'IA monture.

### Anim

- Joueur monté : pose `sit_object` (action loopée, déjà géré par
  `ActionStateMachine` quand `CharacterAnimator` reçoit
  `snap.isMounted=true`).
- Monture : continue ses clips quadrupèdes locomotion normaux.

### Persistance / réseau

Hors scope. Mount/Dismount sont des opérations locales gameplay dans
ce ticket.

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/MountSystem.h` + `.cpp`
- `tests/gameplay/MountSystem_MountDismount_test.cpp`
- `tests/gameplay/MountSystem_OutOfRange_test.cpp`

**Modifiés :**
- `engine/gameplay/CharacterAnimator.{h,cpp}` *(uniquement si nécessaire :
  vérifier que `snap.isMounted` est bien forwardé à `ActionSM` pour
  forcer `sit_object` — ajustement minimal)*
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/MountSystem.h
    engine/gameplay/MountSystem.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `MountSystem_MountDismount_test` : Mount valide, IsMounted
      true, Dismount, IsMounted false ; la position joueur après Mount
      = position socket de la monture.
- [ ] Test `MountSystem_OutOfRange_test` : `distance=5 m` → Mount
      échoue, IsMounted reste false.
- [ ] Pendant Mount, l'animator joueur joue `sit_object`.
- [ ] L'IA de la monture est suspendue pendant Mount, reprend après
      Dismount.

---

## Anti-objectifs

- **Ne pas** introduire de combat à dos de monture.
- **Ne pas** introduire de gestion d'écurie / persistance.
- **Ne pas** câbler le réseau (déléguer au futur protocole gameplay).
- **Ne pas** modifier les clips d'animation.
- **Ne pas** modifier `CharacterController`.
