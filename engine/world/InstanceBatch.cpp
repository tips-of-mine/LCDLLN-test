/**
 * @file InstanceBatch.cpp
 * @brief Instance batching by (mesh+material) key (M09.3).
 */

#include "engine/world/InstanceBatch.h"

namespace engine::world {

void InstanceBatchCollector::AddInstance(const InstanceBatchKey& key, const InstanceTransform& transform) {
    m_batches[key].push_back(transform);
}

void InstanceBatchCollector::Clear() {
    m_batches.clear();
}

} // namespace engine::world
