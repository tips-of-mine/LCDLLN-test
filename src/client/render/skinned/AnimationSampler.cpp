#include "src/client/render/skinned/AnimationSampler.h"

namespace engine::render::skinned
{

namespace
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
    engine::math::Mat4 ComposeTRS(const engine::math::Vec3& t,
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
}

std::vector<engine::math::Mat4> AnimationSampler::SamplePose(const Skeleton& skeleton,
                                                              const AnimationClip& clip,
                                                              float t)
{
    std::vector<engine::math::Mat4> locals(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& b = skeleton.bones[i];

        // Valeurs de fallback : translation = colonne 3 de bindLocal,
        // rotation = identite, scale = (1,1,1). Les clips Mixamo animent
        // chaque bone a chaque frame donc ce chemin est rarement utilise sur
        // les vrais assets, mais reste correct pour les clips synthetiques.
        const engine::math::Vec3 bindT{b.bindLocal.m[12], b.bindLocal.m[13], b.bindLocal.m[14]};

        engine::math::Vec3 tr = bindT;
        engine::math::Quat ro = engine::math::Quat::Identity();
        engine::math::Vec3 sc{1.0f, 1.0f, 1.0f};

        if (i < clip.tracks.size()) {
            const BoneTracks& trk = clip.tracks[i];
            tr = InterpolateKeyframes(trk.translation, t, bindT);
            ro = InterpolateKeyframes(trk.rotation, t, engine::math::Quat::Identity());
            sc = InterpolateKeyframes(trk.scale, t, engine::math::Vec3{1.0f, 1.0f, 1.0f});
        }

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

}  // namespace engine::render::skinned
