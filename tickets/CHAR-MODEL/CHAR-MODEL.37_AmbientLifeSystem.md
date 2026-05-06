# CHAR-MODEL.37 — Tick AI global / `AmbientLifeSystem`

## Dépendances
- CHAR-MODEL.28 (`AnimalAI`)
- Tous les modèles concernés (CHAR-MODEL.29 à 36)

## Cadrage

Implémenter le **système de vie indépendante** : tous les PNJ et animaux
ont une routine qui tourne **sans qu'aucun joueur ne les regarde**. Les
animaux marchent, mangent, s'observent, fuient ou attaquent même hors
champ. Budget CPU géré par **LOD AI** (3 paliers selon distance au
joueur le plus proche).

Garantit que le monde **vit en permanence**, indépendamment des
caméras.

---

## Pré-requis vérifiables

```bash
git status
ls engine/gameplay/ai/AnimalAI.h
ls engine/gameplay/CharacterAnimator.h
ls game/data/models/                    # toutes les espèces livrées
```

---

## Spécification technique

### Paliers LOD AI

| Palier        | Distance | Fréquence tick AI | Fréquence anim/render |
|---------------|----------|-------------------|------------------------|
| `Near`        | 0–30 m   | 30 Hz             | 60 Hz (full anim)      |
| `Medium`      | 30–80 m  | 5 Hz              | 30 Hz (anim + render)  |
| `Far`         | 80–200 m | 0.5 Hz            | 5 Hz (anim figée, render) |
| `Hibernate`   | > 200 m  | 0.1 Hz (état seulement, pas de render anim) | 0 |

### API

```cpp
// engine/gameplay/ai/AmbientLifeSystem.h
namespace engine::gameplay::ai
{
    using EntityId = uint64_t;

    struct AmbientEntity
    {
        EntityId           id;
        std::string        speciesId;       // "vache", "loup", "dragon_adulte", etc.
        AnimalAI           ai;
        engine::math::Vec3 worldPos;
        engine::math::Vec3 facingForward;
    };

    enum class AiLod : uint8_t { Near = 0, Medium, Far, Hibernate };

    class AmbientLifeSystem
    {
    public:
        void Init(uint32_t maxEntities = 4096);

        EntityId Spawn(std::string_view speciesId,
                       const engine::math::Vec3& spawnPos);
        void     Despawn(EntityId id);

        /// Met à jour les positions des joueurs (sources de LOD).
        void UpdatePlayerPositions(std::span<const engine::math::Vec3> positions);

        /// Tick global. dtSec accumule entre appels selon LOD.
        /// Distribue les ticks selon paliers ; ce tick est appelé chaque frame
        /// par le moteur, mais l'AI individuelle est tickée à fréquence réduite.
        void Tick(float frameDtSec);

        /// Pour chaque entité visible (Near/Medium/Far), produit le
        /// SkinnedRenderable correspondant via le CharacterAnimator dédié.
        /// Hibernate → aucun renderable.
        void Render(
            engine::render::SkinPaletteBuffer& palettes,
            engine::render::SceneRenderQueue& queue);

        /// Statistiques pour debug / profilage.
        struct Stats
        {
            uint32_t countNear = 0, countMedium = 0, countFar = 0, countHibernate = 0;
            float    avgTickMicros = 0.0f;
        };
        Stats DebugStats() const;
    };
}
```

### Algorithme de tick

```cpp
void AmbientLifeSystem::Tick(float dt)
{
    accumulators.framesSinceLastTickMedium += 1;
    accumulators.framesSinceLastTickFar    += 1;
    accumulators.framesSinceLastTickHib    += 1;

    pour chaque entité {
        lod = ComputeLod(distanceToNearestPlayer(entity.worldPos));
        switch (lod) {
        case Near:     entity.ai.Tick(input, dt);                 break;
        case Medium:   if (frame % 12 == 0) entity.ai.Tick(input, 12*dt); break;
        case Far:      if (frame % 120 == 0) entity.ai.Tick(input, 120*dt); break;
        case Hibernate:if (frame % 600 == 0) entity.ai.Tick(input, 600*dt); break;
        }
        // Mettre à jour worldPos selon ai.Output().desiredVelocity
    }
}
```

### Spawn par biome

`AmbientLifeSystem::Init` peut éventuellement charger depuis
`game/data/world/ambient_spawns.json` une liste de zones avec densité
par espèce. **Hors scope** de ce ticket : la tabulation par biome
viendra avec un ticket monde dédié. Ici, l'API `Spawn` explicite suffit.

### Persistance

Aucune. Les entités ambiantes sont régénérées au boot.

### Réseau / multijoueur

Hors scope. Système purement local-client à ce stade. Une couche
"replication ambient" arriverait plus tard — non couverte par cette
release.

### Performance

Cible : 4 000 entités totales, **≤ 4 ms CPU par frame** :
- Near : ≤ 50 entités × 30 Hz = bon marché ;
- Medium : ≤ 200 × 5 Hz ;
- Far : ≤ 800 × 0.5 Hz ;
- Hibernate : 3 000 × 0.1 Hz.

Tester via `DebugStats()`.

---

## Liste des fichiers

**Créés :**
- `engine/gameplay/ai/AmbientLifeSystem.h` + `.cpp`
- `tests/gameplay/AmbientLifeSystem_LodDistribution_test.cpp`
- `tests/gameplay/AmbientLifeSystem_TickFrequency_test.cpp`
- `tests/gameplay/AmbientLifeSystem_HibernateNoRender_test.cpp`

**Modifiés :**
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/gameplay/ai/AmbientLifeSystem.h
    engine/gameplay/ai/AmbientLifeSystem.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `AmbientLifeSystem_LodDistribution_test` :
      - 1 joueur en (0,0,0), 4 entités à distances 10, 50, 100, 300 m,
      - vérifie LOD respectifs `Near`, `Medium`, `Far`, `Hibernate`.
- [ ] Test `AmbientLifeSystem_TickFrequency_test` : sur 600 frames,
      vérifie qu'une entité `Far` est tickée ≈ 5 fois (600 / 120).
- [ ] Test `AmbientLifeSystem_HibernateNoRender_test` : entité en
      `Hibernate` n'apparaît **pas** dans la queue de rendu.
- [ ] Performance : 4 000 entités → ≤ 4 ms par frame sur la box de
      référence.
- [ ] `DebugStats` cohérent avec les distributions injectées.

---

## Anti-objectifs

- **Ne pas** introduire de pathfinding global.
- **Ne pas** introduire de spawning par biome (futur).
- **Ne pas** introduire de réplication réseau.
- **Ne pas** persister l'état (tout est régénéré au boot).
- **Ne pas** faire d'IA cognitive (groupes, mémoire, etc.).
- **Ne pas** modifier `AnimalAI` ni les modèles ; ce système les
  orchestre uniquement.
