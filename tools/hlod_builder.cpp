/**
 * @file hlod_builder.cpp
 * @brief Offline tool: build HLOD per chunk — clustering, mesh merge; output chunk packages (M09.4, M10.5).
 *
 * Input: JSON with chunk instances (meshId, materialId, transform, bounds) and mesh/material data.
 * Output (M10.5): outputDir/chunk.meta, geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin.
 */

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace nlohmann;

constexpr uint32_t kHlodMagic = 0x444F4C48u; /* "HLOD" */
constexpr uint32_t kHlodVersion = 1u;
/* Generic .pak format (M10.5) — same layout as engine/streaming/PakReader.h */
constexpr uint32_t kGenericPakMagic = 0x004B4150u; /* "PAK\0" */
constexpr uint32_t kGenericPakVersion = 1u;
constexpr size_t   kPakEntryNameSize = 64u;
/* chunk.meta format (M10.5) */
constexpr uint32_t kChunkMetaMagic = 0x4154454Du; /* "META" */
constexpr uint32_t kChunkMetaVersion = 1u;
constexpr size_t   kChunkMetaSlotCount = 5u;
constexpr unsigned kMinClustersPerChunk = 20u;
constexpr unsigned kMaxClustersPerChunk = 80u;

struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };
struct Bounds {
    float minX = 0.f, minY = 0.f, minZ = 0.f;
    float maxX = 0.f, maxY = 0.f, maxZ = 0.f;
};
struct MeshData {
    std::vector<Vec3> vertices;
    std::vector<uint32_t> indices;
};
struct Instance {
    uint32_t meshId = 0;
    uint32_t materialId = 0;
    float transform[16] = {};
    Bounds bounds;
};
struct ClusterKey {
    int32_t cx = 0, cy = 0, cz = 0;
    uint32_t materialId = 0;
    bool operator==(const ClusterKey& o) const noexcept {
        return cx == o.cx && cy == o.cy && cz == o.cz && materialId == o.materialId;
    }
};
struct ClusterKeyHash {
    size_t operator()(const ClusterKey& k) const noexcept {
        return static_cast<size_t>(k.cx) ^ (static_cast<size_t>(k.cy) << 8u) ^
               (static_cast<size_t>(k.cz) << 16u) ^ (static_cast<size_t>(k.materialId) << 24u);
    }
};

/** Applies 4x4 column-major transform to vertex v, writes result to out. */
void ApplyTransform(const float t[16], const Vec3& v, Vec3& out) {
    out.x = t[0] * v.x + t[4] * v.y + t[8] * v.z + t[12];
    out.y = t[1] * v.x + t[5] * v.y + t[9] * v.z + t[13];
    out.z = t[2] * v.x + t[6] * v.y + t[10] * v.z + t[14];
}

/** Merges mesh instances (by index) into a single vertex/index buffer and recomputes AABB bounds. */
void MergeInstancesIntoCluster(
    const std::vector<Instance>& instances,
    const std::vector<MeshData>& meshes,
    const std::vector<uint32_t>& instanceIndices,
    Bounds& outBounds,
    std::vector<Vec3>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    outVertices.clear();
    outIndices.clear();
    outBounds = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    for (uint32_t idx : instanceIndices) {
        const Instance& inst = instances[idx];
        if (inst.meshId >= meshes.size()) continue;
        const MeshData& mesh = meshes[inst.meshId];
        const uint32_t vertexOffset = static_cast<uint32_t>(outVertices.size());
        for (const Vec3& v : mesh.vertices) {
            Vec3 tv;
            ApplyTransform(inst.transform, v, tv);
            outVertices.push_back(tv);
            outBounds.minX = std::min(outBounds.minX, tv.x);
            outBounds.minY = std::min(outBounds.minY, tv.y);
            outBounds.minZ = std::min(outBounds.minZ, tv.z);
            outBounds.maxX = std::max(outBounds.maxX, tv.x);
            outBounds.maxY = std::max(outBounds.maxY, tv.y);
            outBounds.maxZ = std::max(outBounds.maxZ, tv.z);
        }
        for (uint32_t i : mesh.indices)
            outIndices.push_back(vertexOffset + i);
    }
}

/** Loads chunk input JSON: meshes (vertices/indices), instances (meshId, materialId, transform, bounds), materials list. */
bool LoadInput(const std::string& path,
               std::vector<Instance>& instances,
               std::vector<MeshData>& meshes,
               std::vector<std::string>& materialPaths)
{
    std::ifstream f(path);
    if (!f) { std::cerr << "hlod_builder: cannot open " << path << "\n"; return false; }
    json j;
    try { j = json::parse(f); } catch (const json::exception& e) { std::cerr << "hlod_builder: JSON parse error " << e.what() << "\n"; return false; }
    if (!j.contains("meshes") || !j["meshes"].is_array()) { std::cerr << "hlod_builder: missing meshes array\n"; return false; }
    if (!j.contains("instances") || !j["instances"].is_array()) { std::cerr << "hlod_builder: missing instances array\n"; return false; }
    meshes.clear();
    for (const auto& m : j["meshes"]) {
        MeshData md;
        if (m.contains("vertices") && m["vertices"].is_array())
            for (const auto& v : m["vertices"])
                if (v.is_array() && v.size() >= 3)
                    md.vertices.push_back({ v[0].get<float>(), v[1].get<float>(), v[2].get<float>() });
        if (m.contains("indices") && m["indices"].is_array())
            for (const auto& i : m["indices"]) md.indices.push_back(i.get<uint32_t>());
        meshes.push_back(std::move(md));
    }
    instances.clear();
    for (const auto& in : j["instances"]) {
        Instance inst;
        inst.meshId = in.value("meshId", 0u);
        inst.materialId = in.value("materialId", 0u);
        if (in.contains("transform") && in["transform"].is_array() && in["transform"].size() >= 16)
            for (size_t i = 0; i < 16; ++i) inst.transform[i] = in["transform"][i].get<float>();
        if (in.contains("bounds") && in["bounds"].is_array() && in["bounds"].size() >= 6) {
            inst.bounds.minX = in["bounds"][0].get<float>();
            inst.bounds.minY = in["bounds"][1].get<float>();
            inst.bounds.minZ = in["bounds"][2].get<float>();
            inst.bounds.maxX = in["bounds"][3].get<float>();
            inst.bounds.maxY = in["bounds"][4].get<float>();
            inst.bounds.maxZ = in["bounds"][5].get<float>();
        }
        instances.push_back(inst);
    }
    materialPaths.clear();
    if (j.contains("materials") && j["materials"].is_array())
        for (const auto& mat : j["materials"])
            materialPaths.push_back(mat.value("path", std::string("")));
    return true;
}

/** Returns grid resolution (one dimension) so that grid^3 is close to targetClusters, capped 1..64. */
unsigned ComputeGridSize(unsigned targetClusters, unsigned numMaterials) {
    unsigned cells = (targetClusters + numMaterials - 1) / (numMaterials ? numMaterials : 1u);
    if (cells < 1u) cells = 1u;
    if (cells > 64u) cells = 64u;
    unsigned g = 1u;
    while (g * g * g < cells) ++g;
    return g;
}

/** Assigns each instance to a (grid cell, materialId) cluster key using chunk AABB. */
void ClusterInstances(
    const std::vector<Instance>& instances,
    const Bounds& chunkBounds,
    unsigned gridSize,
    std::unordered_map<ClusterKey, std::vector<uint32_t>, ClusterKeyHash>& clusters)
{
    clusters.clear();
    if (instances.empty()) return;
    float sx = (chunkBounds.maxX - chunkBounds.minX) > 0.001f ? (chunkBounds.maxX - chunkBounds.minX) : 1.f;
    float sy = (chunkBounds.maxY - chunkBounds.minY) > 0.001f ? (chunkBounds.maxY - chunkBounds.minY) : 1.f;
    float sz = (chunkBounds.maxZ - chunkBounds.minZ) > 0.001f ? (chunkBounds.maxZ - chunkBounds.minZ) : 1.f;
    for (size_t i = 0; i < instances.size(); ++i) {
        const Instance& inst = instances[i];
        float cx = (inst.bounds.minX + inst.bounds.maxX) * 0.5f;
        float cy = (inst.bounds.minY + inst.bounds.maxY) * 0.5f;
        float cz = (inst.bounds.minZ + inst.bounds.maxZ) * 0.5f;
        int gx = static_cast<int>((cx - chunkBounds.minX) / sx * static_cast<float>(gridSize));
        int gy = static_cast<int>((cy - chunkBounds.minY) / sy * static_cast<float>(gridSize));
        int gz = static_cast<int>((cz - chunkBounds.minZ) / sz * static_cast<float>(gridSize));
        gx = std::clamp(gx, 0, static_cast<int>(gridSize) - 1);
        gy = std::clamp(gy, 0, static_cast<int>(gridSize) - 1);
        gz = std::clamp(gz, 0, static_cast<int>(gridSize) - 1);
        ClusterKey key;
        key.cx = gx; key.cy = gy; key.cz = gz;
        key.materialId = inst.materialId;
        clusters[key].push_back(static_cast<uint32_t>(i));
    }
}

/**
 * Merges clusters (same material) until cluster count is at most maxClusters.
 */
void MergeClustersToLimit(
    std::unordered_map<ClusterKey, std::vector<uint32_t>, ClusterKeyHash>& clusters,
    unsigned maxClusters)
{
    while (clusters.size() > maxClusters) {
        auto it = clusters.begin();
        if (it == clusters.end()) break;
        ClusterKey k = it->first;
        std::vector<uint32_t> merged = std::move(it->second);
        clusters.erase(it);
        auto best = clusters.end();
        size_t bestSize = 0;
        for (auto j = clusters.begin(); j != clusters.end(); ++j) {
            if (j->first.materialId != k.materialId) continue;
            size_t s = j->second.size();
            if (s > bestSize) { bestSize = s; best = j; }
        }
        if (best != clusters.end())
            for (uint32_t idx : merged) best->second.push_back(idx);
        else
            clusters[k] = std::move(merged);
    }
}

/** Writes HLOD payload (magic, version, meshes, materials) to a buffer for embedding in geo.pak. */
static void AppendHlodPayload(std::vector<char>& out,
    const std::vector<Bounds>& mergedBounds,
    const std::vector<uint32_t>& mergedMaterialIds,
    const std::vector<std::vector<Vec3>>& mergedVertices,
    const std::vector<std::vector<uint32_t>>& mergedIndices,
    const std::vector<std::string>& materialPaths)
{
    auto append32 = [&out](uint32_t v) { out.insert(out.end(), reinterpret_cast<const char*>(&v), reinterpret_cast<const char*>(&v) + 4); };
    auto appendF = [&out](float v) { out.insert(out.end(), reinterpret_cast<const char*>(&v), reinterpret_cast<const char*>(&v) + 4); };
    append32(kHlodMagic);
    append32(kHlodVersion);
    append32(static_cast<uint32_t>(mergedBounds.size()));
    append32(static_cast<uint32_t>(materialPaths.size()));
    for (size_t i = 0; i < mergedBounds.size(); ++i) {
        const Bounds& b = mergedBounds[i];
        appendF(b.minX); appendF(b.minY); appendF(b.minZ);
        appendF(b.maxX); appendF(b.maxY); appendF(b.maxZ);
        append32(mergedMaterialIds[i]);
        append32(static_cast<uint32_t>(mergedVertices[i].size()));
        append32(static_cast<uint32_t>(mergedIndices[i].size()));
    }
    for (size_t i = 0; i < mergedVertices.size(); ++i) {
        const char* vp = reinterpret_cast<const char*>(mergedVertices[i].data());
        out.insert(out.end(), vp, vp + mergedVertices[i].size() * sizeof(Vec3));
        const char* ip = reinterpret_cast<const char*>(mergedIndices[i].data());
        out.insert(out.end(), ip, ip + mergedIndices[i].size() * sizeof(uint32_t));
    }
    for (const std::string& s : materialPaths) {
        append32(static_cast<uint32_t>(s.size()));
        out.insert(out.end(), s.data(), s.data() + s.size());
    }
}

/** Writes generic .pak (header + entries + payload): one entry "hlod" with given payload. */
static bool WriteGenericPak(const std::string& path, const char* entryName, const void* payload, size_t payloadSize) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "hlod_builder: cannot write " << path << "\n"; return false; }
    const uint32_t numEntries = payloadSize > 0 ? 1u : 0u;
    const uint64_t dataOffset = 12u + 80u * numEntries;
    const uint64_t dataSize = payloadSize;
    out.write(reinterpret_cast<const char*>(&kGenericPakMagic), 4);
    out.write(reinterpret_cast<const char*>(&kGenericPakVersion), 4);
    out.write(reinterpret_cast<const char*>(&numEntries), 4);
    if (numEntries > 0) {
        char name[kPakEntryNameSize] = {};
        size_t nlen = std::strlen(entryName);
        if (nlen >= kPakEntryNameSize) nlen = kPakEntryNameSize - 1;
        std::memcpy(name, entryName, nlen);
        out.write(name, kPakEntryNameSize);
        out.write(reinterpret_cast<const char*>(&dataOffset), 8);
        out.write(reinterpret_cast<const char*>(&dataSize), 8);
        out.write(static_cast<const char*>(payload), static_cast<std::streamsize>(payloadSize));
    }
    return true;
}

/** Writes chunk.meta: magic, version, 5 slots (present + size). */
static bool WriteChunkMeta(const std::string& path,
    uint64_t geoSize, uint64_t texSize, uint64_t instancesSize, uint64_t navSize, uint64_t probesSize) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "hlod_builder: cannot write " << path << "\n"; return false; }
    out.write(reinterpret_cast<const char*>(&kChunkMetaMagic), 4);
    out.write(reinterpret_cast<const char*>(&kChunkMetaVersion), 4);
    auto slot = [&out](uint8_t present, uint64_t size) {
        out.write(reinterpret_cast<const char*>(&present), 1);
        char pad[7] = {};
        out.write(pad, 7);
        out.write(reinterpret_cast<const char*>(&size), 8);
    };
    slot(geoSize > 0 ? 1u : 0u, geoSize);
    slot(texSize > 0 ? 1u : 0u, texSize);
    slot(instancesSize > 0 ? 1u : 0u, instancesSize);
    slot(navSize > 0 ? 1u : 0u, navSize);
    slot(probesSize > 0 ? 1u : 0u, probesSize);
    return true;
}

/** Writes instances.bin: count then per-instance meshId, materialId, transform[16], bounds[6]. */
static bool WriteInstancesBin(const std::string& path, const std::vector<Instance>& instances) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "hlod_builder: cannot write " << path << "\n"; return false; }
    uint32_t n = static_cast<uint32_t>(instances.size());
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const Instance& i : instances) {
        out.write(reinterpret_cast<const char*>(&i.meshId), 4);
        out.write(reinterpret_cast<const char*>(&i.materialId), 4);
        out.write(reinterpret_cast<const char*>(i.transform), 16 * sizeof(float));
        out.write(reinterpret_cast<const char*>(&i.bounds.minX), 6 * sizeof(float));
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string inputPath = "chunk_input.json";
    std::string outputDir = ".";
    if (argc >= 2) inputPath = argv[1];
    if (argc >= 3) outputDir = argv[2];
    if (outputDir.empty()) outputDir = ".";

    std::vector<Instance> instances;
    std::vector<MeshData> meshes;
    std::vector<std::string> materialPaths;
    if (!LoadInput(inputPath, instances, meshes, materialPaths)) return 1;

    Bounds chunkBounds = { 0.f, 0.f, 0.f, 256.f, 256.f, 256.f };
    unsigned numMats = materialPaths.empty() ? 1u : static_cast<unsigned>(materialPaths.size());
    unsigned gridSize = ComputeGridSize(kMaxClustersPerChunk, numMats);

    std::unordered_map<ClusterKey, std::vector<uint32_t>, ClusterKeyHash> clusters;
    ClusterInstances(instances, chunkBounds, gridSize, clusters);
    MergeClustersToLimit(clusters, kMaxClustersPerChunk);

    std::vector<Bounds> mergedBounds;
    std::vector<uint32_t> mergedMaterialIds;
    std::vector<std::vector<Vec3>> mergedVertices;
    std::vector<std::vector<uint32_t>> mergedIndices;

    for (const auto& [key, inds] : clusters) {
        if (inds.empty()) continue;
        Bounds b;
        std::vector<Vec3> verts;
        std::vector<uint32_t> indsOut;
        MergeInstancesIntoCluster(instances, meshes, inds, b, verts, indsOut);
        mergedBounds.push_back(b);
        mergedMaterialIds.push_back(key.materialId);
        mergedVertices.push_back(std::move(verts));
        mergedIndices.push_back(std::move(indsOut));
    }

    /* M10.5: write chunk packages (chunk.meta + geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin). */
    std::string base = outputDir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';

    std::vector<char> hlodPayload;
    AppendHlodPayload(hlodPayload, mergedBounds, mergedMaterialIds, mergedVertices, mergedIndices, materialPaths);
    uint64_t geoSize = 12 + 80 + hlodPayload.size();
    if (!WriteGenericPak(base + "geo.pak", "hlod", hlodPayload.data(), hlodPayload.size())) return 1;

    uint64_t texSize = 12u;
    if (!WriteGenericPak(base + "tex.pak", nullptr, nullptr, 0)) return 1;

    if (!WriteInstancesBin(base + "instances.bin", instances)) return 1;
    uint64_t instancesSize = 4u + static_cast<uint64_t>(instances.size()) * (4u + 4u + 16u * 4u + 6u * 4u);

    std::ofstream(base + "navmesh.bin", std::ios::binary).close();
    std::ofstream(base + "probes.bin", std::ios::binary).close();
    uint64_t navSize = 0, probesSize = 0;

    if (!WriteChunkMeta(base + "chunk.meta", geoSize, texSize, instancesSize, navSize, probesSize)) return 1;

    std::cout << "hlod_builder: wrote chunk.meta, geo.pak (" << mergedBounds.size() << " meshes), tex.pak, instances.bin, navmesh.bin, probes.bin to " << outputDir << "\n";
    return 0;
}
