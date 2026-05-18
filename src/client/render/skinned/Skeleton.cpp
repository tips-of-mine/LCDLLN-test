#include "src/client/render/skinned/Skeleton.h"

namespace engine::render::skinned
{

int Skeleton::FindBoneIndex(const std::string& name) const
{
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace engine::render::skinned
