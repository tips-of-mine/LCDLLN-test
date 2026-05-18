#pragma once

#include "src/shared/math/Math.h"

#include <string>
#include <vector>

namespace engine::render::skinned
{

struct Bone
{
    std::string name;             // ex. "mixamorig:LeftArm"
    int parentIndex = -1;         // -1 pour la racine ; toujours < propre index
    engine::math::Mat4 bindLocal; // transform local en bind pose
    engine::math::Mat4 inverseBindGlobal; // inverse de la matrice globale en bind pose
};

struct Skeleton
{
    std::vector<Bone> bones;

    // Returns the bone index by name, or -1 if not found.
    int FindBoneIndex(const std::string& name) const;
};

}  // namespace engine::render::skinned
