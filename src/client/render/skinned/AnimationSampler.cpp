#include "src/client/render/skinned/AnimationSampler.h"

namespace engine::render::skinned
{

/// Compose une matrice TRS (Translation * Rotation * Scale) column-major
/// compatible Vulkan/OpenGL.
///
/// Le scale est applique sur les colonnes 0/1/2 de la matrice de rotation
/// (chaque colonne represente l'axe transforme), la translation occupe
/// la colonne 3 (m[12..14]) et m[15] = 1.
///
/// \param t Translation locale.
/// \param r Rotation locale (quaternion unitaire ; ToMat4 produit une matrice avec colonne 3 nulle).
/// \param s Scale local (applique par axe sur les colonnes de rotation).
engine::math::Mat4 AnimationSampler::ComposeTRS(const engine::math::Vec3& t,
                                                 const engine::math::Quat& r,
                                                 const engine::math::Vec3& s)
{
    engine::math::Mat4 rot = r.ToMat4();
    // Mat4 column-major : colonne k = m[k*4 .. k*4+3]. Scale par axe.
    rot.m[0] *= s.x; rot.m[1] *= s.x; rot.m[2] *= s.x;
    rot.m[4] *= s.y; rot.m[5] *= s.y; rot.m[6] *= s.y;
    rot.m[8] *= s.z; rot.m[9] *= s.z; rot.m[10] *= s.z;
    // Colonne 3 = translation.
    rot.m[12] = t.x; rot.m[13] = t.y; rot.m[14] = t.z;
    rot.m[15] = 1.0f;
    return rot;
}

std::vector<engine::math::Mat4> AnimationSampler::SamplePose(const Skeleton& skeleton,
                                                              const AnimationClip& clip,
                                                              float t)
{
    std::vector<engine::math::Mat4> locals(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& b = skeleton.bones[i];

        // Par defaut, un os garde sa transform de BIND POSE complete (bindLocal).
        // C'est correct pour les os qu'un clip n'anime PAS du tout : avant, on
        // recomposait avec rotation=identite/scale=1 (translation seule conservee),
        // ce qui ECRASAIT la rotation de bind d'un os non keye. Symptome observe :
        // les clips UE5 (ex. Idle_Loop) n'animent pas l'os d'orteil -> la pointe de
        // la botte s'effondrait ("pied coupe") a l'arret, alors que Walk_Loop keye
        // l'orteil (pied normal). Les clips Mixamo animent chaque os a chaque frame,
        // donc ce chemin par defaut ne les concerne pas.
        if (i >= clip.tracks.size()) {
            locals[i] = b.bindLocal;
            continue;
        }

        const BoneTracks& trk = clip.tracks[i];
        if (trk.translation.empty() && trk.rotation.empty() && trk.scale.empty()) {
            // Os non anime par ce clip -> on conserve sa pose de bind exacte.
            locals[i] = b.bindLocal;
            continue;
        }

        // Os anime : on compose depuis les pistes. Fallback par canal vide conserve
        // (translation = bind, rotation = identite, scale = 1) : cas rare d'un clip
        // qui keyerait certains canaux mais pas d'autres pour un meme os.
        const engine::math::Vec3 bindT{b.bindLocal.m[12], b.bindLocal.m[13], b.bindLocal.m[14]};
        const engine::math::Vec3 tr = InterpolateKeyframes(trk.translation, t, bindT);
        const engine::math::Quat ro = InterpolateKeyframes(trk.rotation, t, engine::math::Quat::Identity());
        const engine::math::Vec3 sc = InterpolateKeyframes(trk.scale, t, engine::math::Vec3{1.0f, 1.0f, 1.0f});
        locals[i] = ComposeTRS(tr, ro, sc);
    }
    return locals;
}

std::vector<engine::math::Mat4> AnimationSampler::ComputeGlobalMatrices(const Skeleton& skeleton,
                                                                         const std::vector<engine::math::Mat4>& locals)
{
    std::vector<engine::math::Mat4> globals(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const int parent = skeleton.bones[i].parentIndex;
        if (parent < 0) {
            globals[i] = locals[i];
        } else {
            globals[i] = globals[parent] * locals[i];
        }
    }
    return globals;
}

std::vector<engine::math::Mat4> AnimationSampler::ComputeFinalMatrices(const Skeleton& skeleton,
                                                                        const std::vector<engine::math::Mat4>& globals)
{
    std::vector<engine::math::Mat4> finals(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        finals[i] = globals[i] * skeleton.bones[i].inverseBindGlobal;
    }
    return finals;
}

}  // namespace engine::render::skinned
