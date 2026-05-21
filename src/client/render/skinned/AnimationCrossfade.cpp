#include "src/client/render/skinned/AnimationCrossfade.h"
#include "src/client/render/skinned/AnimationSampler.h"

#include <algorithm>
#include <cmath>

namespace engine::render::skinned
{

namespace
{
/// B.1 (fix audit §1/§3) — Verrouille la translation horizontale (X,Z) du/des
/// bone(s) racine (parentIndex == -1, typiquement le Hips) sur leur bind pose.
/// La composante verticale (Y) est conservee pour garder le bob naturel de la
/// marche. Voir AnimationCrossfade::SetRootMotionLockXZ pour le pourquoi.
///
/// `pose` contient des matrices LOCALES (column-major) alignees sur
/// skeleton.bones ; pour un bone racine, sa transform locale EST son offset
/// global (ComputeGlobalMatrices : globals[root] = locals[root]).
void LockRootMotionXZ(const Skeleton& skeleton, std::vector<engine::math::Mat4>& pose)
{
    const size_t n = std::min(pose.size(), skeleton.bones.size());
    for (size_t i = 0; i < n; ++i)
    {
        if (skeleton.bones[i].parentIndex == -1)
        {
            // Translation en m[12]/m[13]/m[14] (cf. AnimationSampler::ComposeTRS).
            pose[i].m[12] = skeleton.bones[i].bindLocal.m[12];
            pose[i].m[14] = skeleton.bones[i].bindLocal.m[14];
        }
    }
}
}  // namespace

/// Demarre une nouvelle clip. Voir AnimationCrossfade::Play (header) pour la
/// semantique complete (no-op si meme clip, blend si une autre joue deja).
void AnimationCrossfade::Play(const AnimationClip& newClip, bool loops, float now)
{
    if (m_current.clip == &newClip) {
        // Same clip already playing : no-op (avoid resetting time and visual hitch).
        return;
    }
    if (m_current.clip != nullptr) {
        // Move current to previous and start crossfade.
        m_previous = m_current;
        m_crossfadeStartTime = now;
    }
    m_current.clip = &newClip;
    m_current.startTime = now;
    m_current.loops = loops;
}

/// Echantillonne une seule clip a `now`. Pour les clips loopantes, utilise fmod
/// pour wrapper le temps dans [0, duration). Pour les one-shots, clampe a
/// [0, duration]. Si anim.clip est null ou duration nulle, renvoie un vecteur
/// vide (caller doit gerer).
std::vector<engine::math::Mat4>
AnimationCrossfade::SampleSingle(const Skeleton& skel, const ActiveAnimation& anim, float now)
{
    std::vector<engine::math::Mat4> empty;
    if (!anim.clip || anim.clip->duration <= 0.0f) return empty;

    const float elapsed = now - anim.startTime;
    const float t = anim.loops
                  ? std::fmod(std::max(0.0f, elapsed), anim.clip->duration)
                  : std::min(std::max(0.0f, elapsed), anim.clip->duration);
    return AnimationSampler::SamplePose(skel, *anim.clip, t);
}

/// Echantillonne la pose finale (current seule, ou blend prev<->cur). Voir
/// AnimationCrossfade::Sample (header). Si un crossfade est actif, re-echantillonne
/// les keyframes brutes des deux clips pour pouvoir interpoler en TRS — un lerp
/// sur matrices melangerait incorrectement les rotations (translation OK, mais
/// la composante rotation d'une matrice ne se lerp pas lineairement).
std::vector<engine::math::Mat4>
AnimationCrossfade::Sample(const Skeleton& skeleton, float now) const
{
    // Pas de current : renvoie la bind pose (chaque bone = bindLocal).
    if (!m_current.clip) {
        std::vector<engine::math::Mat4> bind(skeleton.bones.size());
        for (size_t i = 0; i < skeleton.bones.size(); ++i)
            bind[i] = skeleton.bones[i].bindLocal;
        return bind;
    }

    auto curPose = SampleSingle(skeleton, m_current, now);

    // Pas de previous OU crossfade termine : renvoie current.
    const float crossfadeElapsed = now - m_crossfadeStartTime;
    if (!m_previous.has_value() || crossfadeElapsed >= kCrossfadeDuration) {
        if (m_lockRootMotionXZ) LockRootMotionXZ(skeleton, curPose);
        return curPose;
    }

    // Crossfade actif : interpole entre prev et cur par TRS, bone par bone.
    const float alpha = std::clamp(crossfadeElapsed / kCrossfadeDuration, 0.0f, 1.0f);

    // Calcul du temps echantillonne dans chaque clip (loop wrap vs clamp).
    const float curElapsed = now - m_current.startTime;
    const float curT = m_current.loops
                     ? std::fmod(std::max(0.0f, curElapsed), m_current.clip->duration)
                     : std::min(std::max(0.0f, curElapsed), m_current.clip->duration);
    const float prevElapsed = now - m_previous->startTime;
    const float prevT = m_previous->loops
                     ? std::fmod(std::max(0.0f, prevElapsed), m_previous->clip->duration)
                     : std::min(std::max(0.0f, prevElapsed), m_previous->clip->duration);

    std::vector<engine::math::Mat4> blended(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        // Fallback bindLocal pour translation si le track est absent ; identite/1 pour rot/scale.
        const Bone& boneRef = skeleton.bones[i];
        const engine::math::Vec3 bindT{boneRef.bindLocal.m[12], boneRef.bindLocal.m[13], boneRef.bindLocal.m[14]};

        engine::math::Vec3 curTr = bindT, prevTr = bindT;
        engine::math::Quat curRo = engine::math::Quat::Identity(), prevRo = engine::math::Quat::Identity();
        engine::math::Vec3 curSc{1.0f, 1.0f, 1.0f}, prevSc{1.0f, 1.0f, 1.0f};

        if (i < m_current.clip->tracks.size()) {
            const auto& trk = m_current.clip->tracks[i];
            curTr = InterpolateKeyframes(trk.translation, curT, bindT);
            curRo = InterpolateKeyframes(trk.rotation, curT, engine::math::Quat::Identity());
            curSc = InterpolateKeyframes(trk.scale, curT, engine::math::Vec3{1.0f, 1.0f, 1.0f});
        }
        if (i < m_previous->clip->tracks.size()) {
            const auto& trk = m_previous->clip->tracks[i];
            prevTr = InterpolateKeyframes(trk.translation, prevT, bindT);
            prevRo = InterpolateKeyframes(trk.rotation, prevT, engine::math::Quat::Identity());
            prevSc = InterpolateKeyframes(trk.scale, prevT, engine::math::Vec3{1.0f, 1.0f, 1.0f});
        }

        const engine::math::Vec3 blendedT{
            prevTr.x + alpha * (curTr.x - prevTr.x),
            prevTr.y + alpha * (curTr.y - prevTr.y),
            prevTr.z + alpha * (curTr.z - prevTr.z)
        };
        const engine::math::Quat blendedR = engine::math::Quat::Slerp(prevRo, curRo, alpha);
        const engine::math::Vec3 blendedS{
            prevSc.x + alpha * (curSc.x - prevSc.x),
            prevSc.y + alpha * (curSc.y - prevSc.y),
            prevSc.z + alpha * (curSc.z - prevSc.z)
        };
        blended[i] = AnimationSampler::ComposeTRS(blendedT, blendedR, blendedS);
    }
    if (m_lockRootMotionXZ) LockRootMotionXZ(skeleton, blended);
    return blended;
}

}  // namespace engine::render::skinned
