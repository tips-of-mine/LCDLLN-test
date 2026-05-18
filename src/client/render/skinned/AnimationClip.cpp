#include "src/client/render/skinned/AnimationClip.h"

#include <algorithm>
#include <utility>

namespace engine::render::skinned
{

namespace
{
    // Find consecutive keyframes (i, i+1) such that kfs[i].time <= t < kfs[i+1].time.
    // Returns {-1, -1} if kfs.empty(). Returns {0, 0} if t < kfs[0].time. Returns {last, last} if t >= kfs[last].time.
    template <typename T>
    std::pair<int, int> FindBracket(const std::vector<Keyframe<T>>& kfs, float t)
    {
        if (kfs.empty()) return {-1, -1};
        if (t <= kfs.front().time) return {0, 0};
        if (t >= kfs.back().time) return {static_cast<int>(kfs.size()) - 1, static_cast<int>(kfs.size()) - 1};
        for (size_t i = 0; i + 1 < kfs.size(); ++i) {
            if (t < kfs[i + 1].time) return {static_cast<int>(i), static_cast<int>(i) + 1};
        }
        return {static_cast<int>(kfs.size()) - 1, static_cast<int>(kfs.size()) - 1};
    }

    float ComputeAlpha(float t, float t0, float t1)
    {
        const float dt = t1 - t0;
        return (dt > 0.0f) ? (t - t0) / dt : 0.0f;
    }
}

engine::math::Vec3 InterpolateKeyframes(const std::vector<Keyframe<engine::math::Vec3>>& kfs,
                                        float t,
                                        const engine::math::Vec3& fallback)
{
    auto [i0, i1] = FindBracket(kfs, t);
    if (i0 < 0) return fallback;
    if (i0 == i1) return kfs[i0].value;
    const float a = ComputeAlpha(t, kfs[i0].time, kfs[i1].time);
    return engine::math::Vec3{
        kfs[i0].value.x + a * (kfs[i1].value.x - kfs[i0].value.x),
        kfs[i0].value.y + a * (kfs[i1].value.y - kfs[i0].value.y),
        kfs[i0].value.z + a * (kfs[i1].value.z - kfs[i0].value.z)
    };
}

engine::math::Quat InterpolateKeyframes(const std::vector<Keyframe<engine::math::Quat>>& kfs,
                                        float t,
                                        const engine::math::Quat& fallback)
{
    auto [i0, i1] = FindBracket(kfs, t);
    if (i0 < 0) return fallback;
    if (i0 == i1) return kfs[i0].value;
    const float a = ComputeAlpha(t, kfs[i0].time, kfs[i1].time);
    return engine::math::Quat::Slerp(kfs[i0].value, kfs[i1].value, a);
}

}  // namespace engine::render::skinned
