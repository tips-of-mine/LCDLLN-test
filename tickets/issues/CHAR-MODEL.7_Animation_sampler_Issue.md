# Issue: CHAR-MODEL.7

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/client/render/skinned/AnimationSampler.cpp
- src/client/render/skinned/tests/AnimationSamplerTests.cpp

## Note
Sampler lerp/slerp + tests

---

## Contenu du ticket (CHAR-MODEL.7)

# CHAR-MODEL.7 — Animation sampler (évaluation d'un clip à temps t)

## Dépendances
- CHAR-MODEL.2 (`Skeleton`)
- CHAR-MODEL.6 (`AnimationClip`)

## Cadrage

Implémenter le **sampler** : étant donné un clip, un squelette et un
temps `t`, produire la **pose locale** (un `JointLocalTransform` par os)
prête à être passée à `ComputeModelMatrices` (CHAR-MODEL.2).

Sans état (fonction pure), thread-safe en lecture, allocation-free dans
le hot path : le caller fournit le `std::span` de sortie déjà alloué.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/Skeleton.h
ls engine/render/AnimationClip.h
```

---

## Spécification technique

### API

```cpp
// engine/render/AnimationSampler.h
namespace engine::render
{
    /// Évalue un clip au temps `timeSec` sur un squelette donné.
    /// `outLocalPose.size()` doit valoir `skeleton.joints.size()`.
    /// Pour chaque os :
    ///   - si une piste T/R/S existe pour ce joint : interpolation
    ///     (lerp pour T/S, slerp pour R) entre les deux keyframes
    ///     encadrant `timeSec` ;
    ///   - sinon : copie de `skeleton.joints[i].bindLocal`.
    /// Si `clip.loop` : `timeSec = fmod(timeSec, clip.duration)`.
    /// Sinon : `timeSec` clampé à `[0, clip.duration]`.
    /// Aucune allocation. Coût : O(joints + tracks).
    void SampleClip(
        const Skeleton& skeleton,
        const AnimationClip& clip,
        float timeSec,
        std::span<JointLocalTransform> outLocalPose);
}
```

### Algorithme

1. Initialiser `outLocalPose[i] = skeleton.joints[i].bindLocal` pour
   tous les os.
2. Pour chaque `track` du clip :
   - Trouver l'intervalle `[k, k+1]` tel que
     `timestamps[k] ≤ t < timestamps[k+1]` (recherche dichotomique
     `std::upper_bound` sur le `std::vector<float>`).
   - Si `t ≤ timestamps[0]` → valeur de la première keyframe.
   - Si `t ≥ timestamps[last]` → valeur de la dernière keyframe.
   - Sinon : `alpha = (t - timestamps[k]) / (timestamps[k+1] - timestamps[k])`.
   - Selon `track.channel` :
     - `Translation`/`Scale` → `lerp(values[k*3], values[(k+1)*3], alpha)`
       (composante par composante).
     - `Rotation` → `slerp(quat[k], quat[k+1], alpha)`. **Attention au
       sens** : si `dot(qk, qk+1) < 0`, négativer `qk+1` avant slerp.
3. Écrire dans `outLocalPose[track.jointIndex].translation/rotation/scale`.

### Convention timestamps & looping

- `timeSec` peut être négatif → en mode loop : `t = fmod(t,
  duration); if (t < 0) t += duration;`. En mode non-loop : clamp à 0.
- Tolérance : un clip `duration = 0` est accepté (mais ne devrait jamais
  exister) → renvoie la pose à `t = 0`.

### Performance

Cible : 60 squelettes × 60 os à 60 Hz = 3 600 évaluations/seconde, **≤ 1 ms
CPU** par frame total sur une seule thread. Allocations-free, pas de
`std::function` ni de capture de lambda dans le hot path.

---

## Liste des fichiers

**Créés :**
- `engine/render/AnimationSampler.h` + `.cpp`
- `tests/render/AnimationSampler_Lerp_test.cpp`
- `tests/render/AnimationSampler_Slerp_test.cpp`
- `tests/render/AnimationSampler_LoopWrap_test.cpp`

**Modifiés :**
- `CMakeLists.txt` (ajout des sources `engine_core`)

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/AnimationSampler.h
    engine/render/AnimationSampler.cpp
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Test `AnimationSampler_Lerp_test` : clip avec 2 keyframes T = (0,0,0)
      à t=0 et T = (10,0,0) à t=1 → à t=0.5, translation = (5,0,0) ± 1e-5.
- [ ] Test `AnimationSampler_Slerp_test` : clip avec 2 keyframes R = identité
      et R = quat 180° autour de Y → à t=0.5, R ≈ quat 90° autour de Y.
- [ ] Test `AnimationSampler_LoopWrap_test` : clip de durée 1.0 en loop,
      `t=2.5` → résultat identique à `t=0.5`.
- [ ] Aucune allocation visible (tester avec un allocator-tracking custom
      ou `valgrind --tool=massif` si CI le permet).
- [ ] Bench : 100 squelettes × 60 os évalués en ≤ 0.5 ms CPU sur la box
      de référence.

---

## Anti-objectifs

- **Ne pas** faire de blending (CHAR-MODEL.8).
- **Ne pas** appliquer la pose au GPU (responsabilité du callsite via
  `ComputeModelMatrices` + `SkinPaletteBuffer`).
- **Ne pas** introduire de cache de poses (le sampler est sans état).
- **Ne pas** introduire de step/cubic/hermite : interpolation linéaire
  uniquement (suffisant pour LBS placeholder).
