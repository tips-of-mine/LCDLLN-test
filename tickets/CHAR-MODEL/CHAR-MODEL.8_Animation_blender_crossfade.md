# CHAR-MODEL.8 — Cross-fade entre 2 clips, gestion des transitions

## Dépendances
- CHAR-MODEL.7 (`AnimationSampler::SampleClip`)

## Cadrage

Implémenter le **blender** :
- mélange linéaire de **deux poses locales** par `alpha ∈ [0,1]` ;
- machine de transition simple `CurrentClip → NextClip` avec un
  `fadeTimeSec` configurable ;
- gestion des **temps locaux indépendants** par clip (le sampling de
  chaque clip avance à sa vitesse propre).

À l'issue de ce ticket, on peut faire jouer `Idle → Walk` avec une
transition de 0.2 s sans saut visible.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/AnimationSampler.h
```

---

## Spécification technique

### Blend de pose locale

```cpp
// engine/render/AnimationBlender.h
namespace engine::render
{
    /// Blend deux poses locales : out[i] = lerp(a[i], b[i], alpha).
    /// Translation/Scale : lerp composante par composante.
    /// Rotation : slerp (gère le double-cover quaternion).
    /// alpha = 0 → out = a ; alpha = 1 → out = b.
    /// Allocation-free.
    void BlendLocalPoses(
        std::span<const JointLocalTransform> a,
        std::span<const JointLocalTransform> b,
        float alpha,
        std::span<JointLocalTransform> out);
}
```

### Player + transition

```cpp
namespace engine::render
{
    /// Joueur d'animation à 1 ou 2 couches (current + next pendant fade).
    /// État léger (pas de buffers internes) — le scratch des poses est passé
    /// par le caller.
    class AnimationPlayer
    {
    public:
        struct PlayState
        {
            const AnimationClip* clip      = nullptr;
            float                timeSec   = 0.0f;
            float                playRate  = 1.0f;   // 1 = vitesse normale
        };

        /// Démarre un nouveau clip avec fade depuis l'état courant.
        /// Si fadeTimeSec ≤ 0 : transition instantanée.
        /// Si clip == m_current.clip : ignoré (option : restart bool param).
        void CrossfadeTo(const AnimationClip* clip, float fadeTimeSec, bool restart = false);

        /// Avance les temps de chaque couche par dtSec et progresse la
        /// transition. À appeler 1× par tick (frame ou sub-tick).
        void Tick(float dtSec);

        /// Évalue la pose finale : sample current + (si fade actif) sample next,
        /// blend par alpha. `scratchA/scratchB` doivent avoir la taille
        /// `skeleton.joints.size()` ; ils sont écrasés à chaque appel.
        void Evaluate(
            const Skeleton& skeleton,
            std::span<JointLocalTransform> scratchA,
            std::span<JointLocalTransform> scratchB,
            std::span<JointLocalTransform> outPose) const;

        const PlayState& Current() const { return m_current; }
        bool             IsFading() const;
        float            FadeAlpha() const; // 0..1

    private:
        PlayState m_current;
        PlayState m_next;
        float     m_fadeTimeRemaining = 0.0f;
        float     m_fadeTimeTotal     = 0.0f;
    };
}
```

### Comportement détaillé

- `CrossfadeTo(newClip, fade)` :
  - si `m_current.clip == nullptr` : assigne `m_current = {newClip, 0, 1}`,
    pas de fade ;
  - sinon : décale `m_current → m_next` côté `next`, … en pratique on
    veut **conserver `m_current` en couche A** et faire entrer `newClip`
    en couche B avec `alpha = 0 → 1` ; à la fin du fade on remplace
    `m_current` par `m_next` et on libère `m_next`.
- `Tick(dt)` :
  - `m_current.timeSec += dt * m_current.playRate` ;
  - si fade actif : `m_next.timeSec += dt * m_next.playRate` ; décrémenter
    `m_fadeTimeRemaining` ; si ≤ 0 : terminer la transition.
- `Evaluate` :
  - sample `m_current.clip` à `m_current.timeSec` → `scratchA` ;
  - si fade actif : sample `m_next.clip` à `m_next.timeSec` → `scratchB`,
    `BlendLocalPoses(scratchA, scratchB, alpha, outPose)` ;
  - sinon : copie `scratchA → outPose`.

### Edge cases

- `clip == nullptr` à l'`Evaluate` : `outPose = bind pose` du squelette
  (chaque os à `bindLocal`).
- `CrossfadeTo(samePtr, fade, restart=false)` : no-op.
- `CrossfadeTo(samePtr, fade, restart=true)` : redémarre `timeSec = 0`
  sans fade.
- `dt < 0` : refusé (assert en debug, no-op en release).

---

## Liste des fichiers

**Créés :**
- `engine/render/AnimationBlender.h` + `.cpp`
- `tests/render/AnimationBlender_BlendIdentity_test.cpp`
- `tests/render/AnimationPlayer_Crossfade_test.cpp`

**Modifiés :**
- `CMakeLists.txt`

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/AnimationBlender.h
    engine/render/AnimationBlender.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `AnimationBlender_BlendIdentity_test` : `alpha=0` → `out == a`,
      `alpha=1` → `out == b`, `alpha=0.5` sur deux poses identiques →
      `out == a == b`.
- [ ] Test `AnimationPlayer_Crossfade_test` :
      - démarre avec clip Idle (rotation tête = 0°),
      - `CrossfadeTo(Walk, 0.5s)` (rotation tête = 30°),
      - tick 0.0s : pose ≈ idle (alpha=0),
      - tick 0.25s : pose ≈ rotation tête = 15° (alpha=0.5),
      - tick 0.50s : pose ≈ walk (alpha=1, fade terminé).
- [ ] `IsFading()` cohérent avec l'état avant/pendant/après transition.
- [ ] Aucune allocation dans `Tick` / `Evaluate`.

---

## Anti-objectifs

- **Ne pas** introduire de state machine (CHAR-MODEL.9-11).
- **Ne pas** ajouter de couche additive (réservé à CHAR-MODEL.11).
- **Ne pas** gérer plus de 2 clips simultanément (couche unique +
  transition).
- **Ne pas** allouer dynamiquement.
