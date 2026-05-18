#pragma once

#include "src/shared/math/Math.h"
#include "src/shared/math/Quat.h"

#include <string>
#include <vector>

namespace engine::render::skinned
{

template <typename T>
struct Keyframe
{
    float time;  // seconds since clip start
    T value;
};

struct BoneTracks
{
    std::vector<Keyframe<engine::math::Vec3>> translation;
    std::vector<Keyframe<engine::math::Quat>> rotation;
    std::vector<Keyframe<engine::math::Vec3>> scale;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<BoneTracks> tracks;  // aligned on Skeleton::bones (index = bone index)
};

// Interpolates between consecutive keyframes at time t (linear for Vec3, slerp for Quat).
// Returns fallback if the track is empty. Clamps to first/last keyframe if t is out of range.
engine::math::Vec3 InterpolateKeyframes(const std::vector<Keyframe<engine::math::Vec3>>& kfs,
                                        float t,
                                        const engine::math::Vec3& fallback);
engine::math::Quat InterpolateKeyframes(const std::vector<Keyframe<engine::math::Quat>>& kfs,
                                        float t,
                                        const engine::math::Quat& fallback);

}  // namespace engine::render::skinned
