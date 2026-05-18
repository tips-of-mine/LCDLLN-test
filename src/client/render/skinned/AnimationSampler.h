#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/math/Math.h"

#include <vector>

namespace engine::render::skinned
{

/// Echantillonneur de pose : combine un Skeleton et un AnimationClip pour
/// produire les matrices locales (puis globales, puis finales) a un instant t.
///
/// Conception en trois etapes pour faciliter le debug et permettre des
/// optimisations futures (ex. cache des matrices globales) :
///   1) SamplePose          : echantillonne les keyframes -> matrices locales TRS.
///   2) ComputeGlobalMatrices (tache 6) : applique la hierarchie parent->enfant.
///   3) ComputeFinalMatrices  (tache 7) : multiplie par inverseBindGlobal pour
///      obtenir les matrices a uploader au shader de skinning.
class AnimationSampler
{
public:
    /// Echantillonne le clip a l'instant t (secondes) et renvoie une matrice
    /// locale 4x4 par bone (taille = skeleton.bones.size()).
    ///
    /// Pour chaque bone, compose une matrice TRS a partir des keyframes des trois
    /// canaux (translation / rotation / scale). Si un canal est absent (track
    /// vide), retombe sur une valeur par defaut :
    ///   - translation : colonne 3 de bindLocal (translation de la bind pose)
    ///   - rotation    : identite
    ///   - scale       : (1, 1, 1)
    ///
    /// \param skeleton Squelette de reference (definit le nombre de bones et la bind pose).
    /// \param clip     Clip d'animation. Si clip.tracks.size() < bones.size(), les bones
    ///                 sans piste prennent la valeur par defaut decrite ci-dessus.
    /// \param t        Instant d'echantillonnage en secondes. Hors-plage : InterpolateKeyframes clampe.
    /// \return Vecteur de matrices locales 4x4 (column-major), aligne sur skeleton.bones.
    static std::vector<engine::math::Mat4> SamplePose(const Skeleton& skeleton,
                                                       const AnimationClip& clip,
                                                       float t);

    // Walks the bone hierarchy (parent always before child) and returns global matrices.
    // Requires locals.size() == skeleton.bones.size().
    static std::vector<engine::math::Mat4> ComputeGlobalMatrices(const Skeleton& skeleton,
                                                                  const std::vector<engine::math::Mat4>& locals);
};

}  // namespace engine::render::skinned
