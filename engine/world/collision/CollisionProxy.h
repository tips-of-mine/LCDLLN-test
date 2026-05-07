// engine/world/collision/CollisionProxy.h
#pragma once

#include "engine/math/Math.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::world::collision
{
    enum class ProxyType : uint32_t
    {
        Capsule    = 0,
        ConvexHull = 1,
        TriMesh    = 2,
    };

    constexpr uint32_t kCollisionMagic   = 0x4C4C4F43u;  // "COLL" little-endian
    constexpr uint32_t kCollisionVersion = 1u;

    struct CollisionProxy
    {
        ProxyType type = ProxyType::Capsule;

        // Capsule
        engine::math::Vec3 capsuleA{ 0.0f, -0.5f, 0.0f };
        engine::math::Vec3 capsuleB{ 0.0f,  0.5f, 0.0f };
        float              capsuleRadius = 0.5f;

        // ConvexHull / TriMesh
        std::vector<engine::math::Vec3> vertices;
        std::vector<uint32_t>           indices;     // TriMesh seulement

        bool LoadFromFile(const std::filesystem::path& path, std::string& outError);
        bool SaveToFile(const std::filesystem::path& path, std::string& outError) const;
    };
}
