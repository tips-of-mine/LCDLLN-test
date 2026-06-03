# SERVER-CORE.04_Movement_server_authoritative_splines

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : src/shardd/Movement/PathFollowMotion.h
> - Manque : MoveSpline core/flags/init incomplets
> - Resume : Mouvement partiel

## Objectif

Mettre en place un système de mouvement **server-authoritative basé sur
des splines** entre shard et client, inspiré de `src/game/Movement` de
server-core. Bénéfices visés :

1. **Économie de bande passante massive** vs streaming de positions à
   chaque tick. Un déplacement de 50 m en ligne droite = 1 paquet
   (control points + durée) au lieu de 50-100 packets de position. Ratio
   typique ×10 à ×50.
2. **Moins de jitter visuel** côté client : l'interpolation locale entre
   control points est lisse même si quelques paquets sont perdus.
3. **AoI scalable** : avec moins de packets/seconde par entité, le shard
   peut diffuser le mouvement de centaines d'entités sans saturer.
4. **Validation anticheat** : le serveur connaît à l'avance le chemin
   nominal d'une créature, peut détecter facilement un client qui
   essaie de raccourcir.

Ce ticket couvre les 5 patterns clés server-core :

- `MoveSpline` server-side (état, durée, points de contrôle)
- `MoveSplineFlag` bitmask multi-modes (walk/fly/swim/falling/cyclic)
- `packet_builder` isolé (sérialisation pure, testable)
- Math des splines (Catmull-Rom, Bézier) header-only templated
- `typedefs.h` centralisé pour `Vector3`/`Vector4`

## Dépendances

- M00.1 (build base)
- SERVER-CORE.02 (Entities) — `Unit` porte le `MoveSpline`
- SERVER-CORE.03 (Grids) — broadcast du paquet `SMSG_MONSTER_MOVE` via `MessageDistDeliverer`

## Livrables

### Côté shard (`engine/server/shard/movement/`)

- `MoveSpline.{h,cpp}` — état d'une spline en cours d'exécution sur un
  Unit : control points, vélocité, durée, mode (run/walk/fly/swim),
  flags, time elapsed, callback de fin.
- `MoveSplineInit.{h,cpp}` — **builder pattern** : configuration fluente
  d'une nouvelle spline avant `Launch()`.
- `MoveSplineFlag.h` — `enum class` bitmask 32 bits.
- `Spline.h` (header-only templated) — math des splines :
  `template <typename T> class Spline { ... }` avec spécialisations
  Catmull-Rom et Bézier. T = Vector3 le plus souvent.
- `MoveSplinePacketBuilder.{h,cpp}` — sérialisation pure d'un
  `MoveSpline` vers un `ByteBuffer`. Testable sans serveur.

### Côté client (`engine/client/movement/`)

- `MoveSplineInterpolator.{h,cpp}` — reçoit un paquet `SMSG_MONSTER_MOVE`,
  interpole localement la position du WorldObject à chaque frame.
- `MoveSplineState.{h,cpp}` — état local par entité : spline en cours,
  time elapsed, position courante, position attendue (pour smoothing).

### Couche partagée (`engine/network/movement/`)

- `MoveSplinePayloads.{h,cpp}` — schéma binaire du paquet, partagé entre
  shard et client. Encapsule format wire et format CPU.
- `MovementTypedefs.h` — `using Vector3 = ...`, `using Vector4 = ...`,
  `using Quat = ...`. **Single source of truth** : un changement de
  backend (glm, types SIMD custom) se fait dans ce fichier seulement.

### Opcodes

- `kOpcodeMonsterMove` (shard → client) : envoi d'une nouvelle spline.
- `kOpcodeMonsterMoveStop` (shard → client) : interruption immédiate.
- `kOpcodeMoveTeleportAck` (client → shard) : confirmation de téléport.

### Tests

- `engine/network/movement/MoveSplinePayloadsTests.cpp` — round-trip
  serialize/deserialize d'une spline 5 points avec flags.
- `engine/server/shard/movement/SplineMathTests.cpp` — Catmull-Rom passe
  bien par les control points, Bézier convexe.
- `engine/server/shard/movement/MoveSplineInitTests.cpp` — builder ;
  `MoveTo(p1).MoveTo(p2).Launch()` produit la spline attendue.

## Structure & chemins (verrouillé)

- Code moteur : uniquement sous `/engine`
- Contenu : N/A
- Outils offline : N/A
- ❌ Interdit : créer un dossier racine non autorisé

## Spécification technique

### 1. `MoveSpline`

```cpp
class MoveSpline {
public:
  struct Config {
    float                       velocity = 0.0f;     // m/s
    MoveSplineFlag              flags = MoveSplineFlag::None;
    std::vector<Vector3>        points;
    std::optional<Vector3>      finalFacing;
    std::optional<ObjectGuid>   facingTarget;
    int32_t                     animationId = 0;
  };

  bool   IsActive() const;
  bool   Finalized() const;
  Vector3 ComputePosition() const;        // selon time elapsed
  void    Update(int32_t deltaMs);
  void    Initialize(Config&& cfg);
  void    Stop();

private:
  Config              m_cfg;
  Spline<Vector3>     m_spline;            // math
  int32_t             m_timeElapsedMs = 0;
  int32_t             m_durationMs = 0;
};
```

### 2. `MoveSplineFlag` (bitmask 32 bits)

```cpp
enum class MoveSplineFlag : uint32_t {
  None            = 0,
  Walk            = 1u << 0,
  Flying          = 1u << 1,
  Swimming        = 1u << 2,
  Falling         = 1u << 3,
  NoSpline        = 1u << 4,    // ligne droite, pas de smoothing
  Cyclic          = 1u << 5,    // boucle (patrouilles)
  EnterCycle      = 1u << 6,    // démarre puis devient cyclique
  Frozen          = 1u << 7,    // entité immobile (mais a une orientation)
  TransportEnter  = 1u << 8,
  TransportExit   = 1u << 9,
  OrientationFixed= 1u << 10,
  Backward        = 1u << 11,
};
```

`operator|`, `operator&`, `operator~` overloadés pour usage type-safe.

### 3. `MoveSplineInit` (builder)

```cpp
class MoveSplineInit {
public:
  explicit MoveSplineInit(Unit& target);
  MoveSplineInit& MoveTo(Vector3 p);
  MoveSplineInit& MovebyPath(std::vector<Vector3> path);
  MoveSplineInit& SetVelocity(float v);
  MoveSplineInit& SetFlags(MoveSplineFlag f);
  MoveSplineInit& SetFacing(Vector3 dir);
  MoveSplineInit& SetFacing(ObjectGuid target);
  MoveSplineInit& SetCyclic();
  int32_t Launch();   // calcule durée, applique au Unit, broadcast SMSG_MONSTER_MOVE
};
```

Usage :

```cpp
MoveSplineInit(creature)
  .MoveTo({100, 200, 50})
  .MoveTo({110, 220, 50})
  .SetVelocity(5.0f)
  .SetFlags(MoveSplineFlag::Walk)
  .Launch();
```

### 4. Math des splines (`Spline.h`)

Header-only templated :

```cpp
template <typename T>
class Spline {
public:
  enum class EvaluationMode { Linear, CatmullRom, Bezier3 };
  void   Init(std::vector<T> points, EvaluationMode mode);
  T      Evaluate(float t) const;       // t in [0, 1]
  float  Length() const;                 // approximé par sampling
  T      EvaluateDerivative(float t) const;
private:
  std::vector<T>  m_points;
  EvaluationMode  m_mode = EvaluationMode::Linear;
  std::vector<float> m_segmentLengths;
};
```

Spécialisé pour `Vector3`, instanciable pour 2D ou 4D si besoin futur
(animation de skeleton, par ex.).

### 5. `MoveSplinePacketBuilder`

Couche **pure** : prend un `MoveSpline`, retourne un `ByteBuffer`.
Aucune dépendance vers le `Unit` ou le réseau. Testable trivialement.

```cpp
class MoveSplinePacketBuilder {
public:
  static void WriteMonsterMove(ByteBuffer& out,
                                ObjectGuid sourceGuid,
                                Vector3 startPos,
                                MoveSpline const& spline);
  static bool ParseMonsterMove(ByteBuffer& in,
                                ObjectGuid& outGuid,
                                Vector3& outStartPos,
                                MoveSpline::Config& outCfg);
};
```

### 6. Format wire `SMSG_MONSTER_MOVE`

```
[ uint16 opcode = kOpcodeMonsterMove ]
[ ObjectGuid (8 bytes) ]
[ Vector3 startPos (12 bytes) ]
[ uint32 splineId (incrémente à chaque envoi pour le client) ]
[ uint32 flags ]
[ uint32 durationMs ]
[ uint8  mode (0=Linear, 1=CatmullRom, 2=Bezier3) ]
[ uint16 nbPoints ]
[ Vector3 × nbPoints ]
[ optional uint8 facingType + payload ]
```

### 7. Côté client : interpolation locale

```cpp
class MoveSplineInterpolator {
public:
  void OnMonsterMovePacket(ByteBuffer& in);
  Vector3 GetCurrentPosition(ObjectGuid guid, double clientTimeMs);
private:
  std::unordered_map<ObjectGuid, MoveSplineState> m_states;
};
```

À chaque frame, le client interpole la position de chaque entité visible
selon sa spline en cours et le temps écoulé. **Aucune communication
réseau** entre les paquets. Si l'entité change de direction, le serveur
envoie une nouvelle `SMSG_MONSTER_MOVE` qui remplace l'ancienne.

## Étapes d'implémentation

1. **Créer `engine/network/movement/`** + `MovementTypedefs.h` (centralise `Vector3`).
2. **Implémenter `MoveSplineFlag`** + tests basiques d'opérateurs bitmask.
3. **Implémenter `Spline<T>`** header-only avec Linear + CatmullRom + Bezier3 ; tests math.
4. **Implémenter `MoveSpline`** côté shard (état runtime).
5. **Implémenter `MoveSplineInit`** (builder).
6. **Implémenter `MoveSplinePacketBuilder`** + tests round-trip.
7. **Allouer l'opcode `kOpcodeMonsterMove`** côté wire (master+shard+client) ; bumper `kProtocolVersion`.
8. **Câbler côté shard** : `Unit::Update()` appelle `MoveSpline::Update(dt)`, applique la position calculée. Au `Launch()`, broadcast `MessageDistDeliverer` (SERVER-CORE.03).
9. **Implémenter `MoveSplineInterpolator`** côté client.
10. **Smoke test end-to-end** : un Creature côté shard se déplace en ligne droite ; le client affiche un mouvement lisse sans envoi continu de positions.
11. **Tests** : 3 fichiers listés.
12. **Doc** : section « Movement / splines » dans `CODEBASE_MAP.md`.

## Definition of Done (DoD)

- [ ] Build Linux OK (shard) et Windows OK (client) via presets existants
- [ ] Tests `MoveSplinePayloadsTests`, `SplineMathTests`, `MoveSplineInitTests` passent
- [ ] Smoke test : 1 Creature traverse 100 m en 10 s, le client reçoit **1 seul** paquet `SMSG_MONSTER_MOVE` (vs 100+ avec snapshot positions actuel) — vérifié via `LogFilter::PacketIo` activé
- [ ] L'interpolation client est lisse à 60 Hz sans nouveaux paquets pendant 10 s
- [ ] Une nouvelle `SMSG_MONSTER_MOVE` interrompt proprement l'ancienne (pas de saut visuel)
- [ ] `kProtocolVersion` bumpé, master + shard + client redéployés en lock-step
- [ ] Aucun nouveau dossier racine non autorisé créé
- [ ] Rapport final : fichiers modifiés + commandes + résultats + DoD

## Notes / pièges à éviter

- **Cohérence horloge client/serveur** : l'interpolation client utilise un temps local. Si le RTT varie, le client peut être en avance ou en retard. Stratégie standard : envoyer un timestamp serveur dans chaque packet, le client maintient un offset (`clientClock - serverTime`) lissé. **Ne pas** ignorer cette synchro, sinon mouvements saccadés en cas de jitter réseau.
- **Spline ID** : chaque `MoveSpline` a un ID unique (incrément monotone). Le client, en recevant un paquet avec un ID < à celui en cours, l'ignore (out-of-order). Important sur UDP.
- **NoSpline** : pour les téléports ou les forces brusques (knockback), utiliser le flag `NoSpline` qui désactive l'interpolation et force le client à snap. Sinon le client met 50ms à arriver et le combat est désynchro.
- **Cyclic / EnterCycle** : pour les patrouilles infinies, ne **pas** envoyer un nouveau paquet à chaque cycle — le client doit savoir boucler localement. Le flag `Cyclic` indique « rejoue indéfiniment ».
- **Falling** : la chute libre n'est pas une spline. Quand un joueur saute d'une falaise, envoyer un paquet `Falling` avec la vélocité initiale ; le client applique la gravité localement. Le serveur revérifie périodiquement la position pour anticheat.
- **`packet_builder` côté serveur ≠ côté client** : on a deux implémentations qui doivent **rester d'accord** sur le format wire. Le test round-trip dans `MoveSplinePayloadsTests` doit être lancé dans la CI sur les **deux** côtés (côté shard linux et côté client windows). Toute désync = bug critique.
- **Floats vs fixed-point sur le wire** : 12 bytes par Vector3 (3 × float32) est généreux. server-core optimise parfois en fixed-point 16 bits par axe. **Démarrer avec floats**, profiler la bande passante en charge avant d'optimiser.
- **Interaction avec MotionGenerators (futur ticket)** : `MoveSpline` est la **couche transport** ; `MotionGenerator` est la **couche IA** qui décide où aller. Ne pas mélanger. Un `MotionGenerator` produit un nouveau `MoveSplineInit` quand il veut bouger, point.

## Références

- `SERVER-CORE_ANALYSIS.md` § Movement (P1 cross master+shard)
- server-core `src/game/Movement/MoveSpline.cpp`,
  `MoveSplineInit.cpp`, `MoveSplineFlag.h`,
  `spline.h`/`spline.impl.h`, `packet_builder.cpp`,
  `typedefs.h`
- À coordonner avec : SERVER-CORE.13 (Maps tick), SERVER-CORE.14 (MotionGenerators)
