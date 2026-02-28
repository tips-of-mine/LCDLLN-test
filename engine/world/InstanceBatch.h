#pragma once

/**
 * @file InstanceBatch.h
 * @brief Batch instancing by (mesh + material) key (M09.3).
 *
 * Group instances by key and draw instanced to reduce drawcalls.
 */

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::world {

/**
 * @brief Key for grouping instanced draws: same mesh + same material.
 */
struct InstanceBatchKey {
    uint32_t meshId    = 0;
    uint32_t materialId = 0;

    [[nodiscard]] bool operator==(const InstanceBatchKey& o) const noexcept {
        return meshId == o.meshId && materialId == o.materialId;
    }
};

/** @brief Hash for InstanceBatchKey. */
struct InstanceBatchKeyHash {
    std::size_t operator()(const InstanceBatchKey& k) const noexcept {
        return static_cast<std::size_t>(k.meshId) ^ (static_cast<std::size_t>(k.materialId) << 16u);
    }
};

/**
 * @brief Per-instance transform (column-major 4x4, 16 floats).
 */
struct InstanceTransform {
    float m[16] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
};

/**
 * @brief Groups instances by (mesh + material) key for instanced drawing.
 *
 * Add instances with AddInstance(); then iterate batches (key + instance count + data)
 * to issue one draw call per batch with vkCmdDraw*Instanced.
 */
class InstanceBatchCollector {
public:
    InstanceBatchCollector() = default;

    /**
     * @brief Adds one instance to the batch for the given key.
     *
     * @param key  Mesh + material key.
     * @param transform Instance world transform (column-major 4x4).
     */
    void AddInstance(const InstanceBatchKey& key, const InstanceTransform& transform);

    /**
     * @brief Clears all batches (call at start of frame).
     */
    void Clear();

    /**
     * @brief Returns the number of batches (one per unique key).
     */
    [[nodiscard]] size_t BatchCount() const noexcept { return m_batches.size(); }

    /**
     * @brief Iterates batches: for each (key, instance count, pointer to instance transforms).
     *
     * @param fn Callable (InstanceBatchKey key, uint32_t instanceCount, const InstanceTransform* transforms).
     */
    template<typename Fn>
    void ForEachBatch(Fn&& fn) const {
        for (const auto& [key, vec] : m_batches)
            if (!vec.empty())
                fn(key, static_cast<uint32_t>(vec.size()), vec.data());
    }

private:
    std::unordered_map<InstanceBatchKey, std::vector<InstanceTransform>, InstanceBatchKeyHash> m_batches;
};

} // namespace engine::world
