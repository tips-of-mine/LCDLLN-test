/**
 * @file zone_builder.cpp
 * @brief CLI tool: read glTF + layout.json, chunk zone, write build/zone_x/chunks/chunk_i_j/* (M11.1, M11.2).
 *
 * - layout.json: versioned; positions in meters; GUID per instance.
 * - glTF: load via TinyGLTF; list meshes/materials.
 * - M11.2: chunk coord = floor(x/256), floor(z/256); write chunk.meta (bounds, flags), instances.bin, zone.meta.
 * - M11.4: one global zone probe at zone center (probes.bin) + zone_atmosphere.json.
 * - M11.5: versioned header (builderVersion, engineVersion, contentHash xxhash) in each bin/meta; layout + asset mtime for hash.
 */

#include <tiny_gltf.h>

#include <nlohmann/json.hpp>

#include "engine/world/VersionedHeader.h"

#include <xxhash.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace nlohmann;

/** @brief Minimal layout.json schema: version, instances with guid and position (meters). M12.4: optional assetId from editor export. */
struct LayoutInstance {
    std::string guid;
    float position[3] = { 0.f, 0.f, 0.f };
    std::string meshRef;
    std::string materialRef;
    bool useExplicitAssetId = false;
    uint32_t explicitAssetId = 0;
};

/** @brief Loads and validates layout.json; fills instances. Returns true on success. */
bool LoadLayoutJson(const std::string& path, uint32_t& outVersion, std::vector<LayoutInstance>& instances) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "zone_builder: cannot open layout " << path << "\n";
        return false;
    }
    json j;
    try {
        j = json::parse(f);
    } catch (const json::exception& e) {
        std::cerr << "zone_builder: layout.json parse error " << e.what() << "\n";
        return false;
    }
    if (!j.contains("version") || !j["version"].is_number_unsigned()) {
        std::cerr << "zone_builder: layout.json missing or invalid 'version'\n";
        return false;
    }
    outVersion = j["version"].get<uint32_t>();
    instances.clear();
    if (!j.contains("instances") || !j["instances"].is_array())
        return true;
    for (const auto& inst : j["instances"]) {
        LayoutInstance li;
        li.guid = inst.value("guid", "");
        if (inst.contains("position") && inst["position"].is_array() && inst["position"].size() >= 3) {
            li.position[0] = inst["position"][0].get<float>();
            li.position[1] = inst["position"][1].get<float>();
            li.position[2] = inst["position"][2].get<float>();
        }
        li.meshRef = inst.value("mesh", std::string(""));
        li.materialRef = inst.value("material", std::string(""));
        if (inst.contains("assetId") && inst["assetId"].is_number_unsigned()) {
            li.useExplicitAssetId = true;
            li.explicitAssetId = inst["assetId"].get<uint32_t>();
        }
        instances.push_back(std::move(li));
    }
    return true;
}

/** @brief Loads glTF via TinyGLTF and fills mesh/material names. Returns true on success. */
bool LoadGltfWithTinygltf(const std::string& path, std::vector<std::string>& meshNames, std::vector<std::string>& materialNames) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    bool ok = false;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".glb") == 0)
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    else
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!ok) {
        std::cerr << "zone_builder: glTF load failed " << path << ": " << err << "\n";
        return false;
    }
    if (!warn.empty())
        std::cerr << "zone_builder: glTF warn: " << warn << "\n";
    meshNames.clear();
    materialNames.clear();
    for (const auto& m : model.meshes)
        meshNames.push_back(m.name.empty() ? "(unnamed)" : m.name);
    for (const auto& mat : model.materials)
        materialNames.push_back(mat.name.empty() ? "(unnamed)" : mat.name);
    return true;
}

/** @brief Computes content hash (xxhash) from layout file content + referenced asset mtime (M11.5). Avoids hashing large files. */
uint64_t ComputeContentHash(const std::string& layoutPath, const std::string& gltfPath) {
    std::ifstream layoutFile(layoutPath, std::ios::binary);
    std::string layoutContent((std::istreambuf_iterator<char>(layoutFile)), std::istreambuf_iterator<char>());
    layoutFile.close();
    XXH64_state_t* state = XXH64_createState();
    if (!state) return 0;
    XXH64_reset(state, 0);
    XXH64_update(state, layoutContent.data(), layoutContent.size());
    std::error_code ec;
    auto gltfMtime = std::filesystem::last_write_time(gltfPath, ec);
    uint64_t mtimeVal = ec ? 0 : static_cast<uint64_t>(gltfMtime.time_since_epoch().count());
    XXH64_update(state, &mtimeVal, sizeof(mtimeVal));
    uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

/** @brief Prints usage to stderr. */
void PrintUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <layout.json> <scene.gltf|scene.glb> [outputDir]\n";
    std::cerr << "  If outputDir (e.g. build/zone_0) is given, writes zone.meta, chunks/chunk_i_j/chunk.meta, instances.bin, probes.bin, zone_atmosphere.json (M11.5 versioned).\n";
}

/* M11.2: zone builder output format constants (must match engine reader). */
constexpr uint32_t kZoneMetaMagic = 0x4D4E4F5Au; /* "ZONM" */
constexpr uint32_t kZoneMetaVersion = 1u;
constexpr uint32_t kChunkMetaMagic = 0x4D4E4843u; /* "CHNM" */
constexpr uint32_t kChunkMetaVersion = 1u;
constexpr int kChunkSize = 256;

/** @brief Resolves mesh ref (name or index) to asset id (0-based). */
uint32_t ResolveMeshToAssetId(const std::string& meshRef, const std::vector<std::string>& meshNames) {
    if (meshRef.empty()) return 0u;
    for (size_t i = 0; i < meshNames.size(); ++i)
        if (meshNames[i] == meshRef) return static_cast<uint32_t>(i);
    return 0u;
}

/** @brief Builds column-major 4x4 transform from position (translation only). */
void MakeTransformFromPosition(const float pos[3], float transform[16]) {
    for (int i = 0; i < 16; ++i) transform[i] = (i % 5 == 0) ? 1.f : 0.f;
    transform[12] = pos[0]; transform[13] = pos[1]; transform[14] = pos[2];
}

/** @brief Writes versioned header (magic + VersionedHeader) then payload. */
static bool WriteVersionedHeader(std::ofstream& out, uint32_t magic, const engine::world::VersionedHeader& vh) {
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&vh.formatVersion), 4);
    out.write(reinterpret_cast<const char*>(&vh.builderVersion), 4);
    out.write(reinterpret_cast<const char*>(&vh.engineVersion), 4);
    out.write(reinterpret_cast<const char*>(&vh.contentHash), 8);
    return out.good();
}

/** @brief Writes zone.meta (list of chunk coords) with versioned header (M11.5). */
bool WriteZoneMeta(const std::string& path, int32_t zoneId, const std::vector<std::pair<int32_t, int32_t>>& chunkCoords, const engine::world::VersionedHeader& vh) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "zone_builder: cannot write " << path << "\n"; return false; }
    if (!WriteVersionedHeader(out, kZoneMetaMagic, vh)) return false;
    out.write(reinterpret_cast<const char*>(&zoneId), 4);
    uint32_t n = static_cast<uint32_t>(chunkCoords.size());
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const auto& p : chunkCoords) {
        out.write(reinterpret_cast<const char*>(&p.first), 4);
        out.write(reinterpret_cast<const char*>(&p.second), 4);
    }
    return true;
}

/** @brief Writes chunk.meta (bounds + flags) with versioned header (M11.5). */
bool WriteChunkMeta(const std::string& path, float minX, float minY, float minZ, float maxX, float maxY, float maxZ, uint32_t flags, const engine::world::VersionedHeader& vh) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "zone_builder: cannot write " << path << "\n"; return false; }
    if (!WriteVersionedHeader(out, kChunkMetaMagic, vh)) return false;
    out.write(reinterpret_cast<const char*>(&minX), 4);
    out.write(reinterpret_cast<const char*>(&minY), 4);
    out.write(reinterpret_cast<const char*>(&minZ), 4);
    out.write(reinterpret_cast<const char*>(&maxX), 4);
    out.write(reinterpret_cast<const char*>(&maxY), 4);
    out.write(reinterpret_cast<const char*>(&maxZ), 4);
    out.write(reinterpret_cast<const char*>(&flags), 4);
    return true;
}

/** @brief Per-instance data for instances.bin (transform + assetId + flags). */
struct ChunkInstanceRecord {
    float transform[16] = {};
    uint32_t assetId = 0;
    uint32_t flags = 0;
};

constexpr uint32_t kInstancesBinMagic = 0x54534E49u; /* "INST" */

/** @brief Writes instances.bin with versioned header (M11.5). */
bool WriteInstancesBin(const std::string& path, const std::vector<ChunkInstanceRecord>& instances, const engine::world::VersionedHeader& vh) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "zone_builder: cannot write " << path << "\n"; return false; }
    if (!WriteVersionedHeader(out, kInstancesBinMagic, vh)) return false;
    uint32_t n = static_cast<uint32_t>(instances.size());
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const auto& inst : instances) {
        out.write(reinterpret_cast<const char*>(inst.transform), 16 * 4);
        out.write(reinterpret_cast<const char*>(&inst.assetId), 4);
        out.write(reinterpret_cast<const char*>(&inst.flags), 4);
    }
    return true;
}

/* M11.4: probes.bin format (must match engine ProbesFormat.h). */
constexpr uint32_t kProbesBinMagic = 0x424F5250u; /* "PROB" */
constexpr uint32_t kProbesBinVersion = 1u;
struct ProbeRecord { float position[3] = {}; float radius = 1000.f; float intensity = 1.f; };

/** @brief Writes probes.bin with versioned header (M11.5). */
bool WriteProbesBin(const std::string& path, const std::vector<ProbeRecord>& probes, const engine::world::VersionedHeader& vh) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "zone_builder: cannot write " << path << "\n"; return false; }
    if (!WriteVersionedHeader(out, kProbesBinMagic, vh)) return false;
    uint32_t n = static_cast<uint32_t>(probes.size());
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const auto& p : probes) {
        out.write(reinterpret_cast<const char*>(p.position), 12);
        out.write(reinterpret_cast<const char*>(&p.radius), 4);
        out.write(reinterpret_cast<const char*>(&p.intensity), 4);
    }
    return true;
}

/** @brief Writes zone_atmosphere.json (MVP: sky color, horizon exponent). */
bool WriteZoneAtmosphereJson(const std::string& path) {
    std::ofstream out(path);
    if (!out) { std::cerr << "zone_builder: cannot write " << path << "\n"; return false; }
    out << "{\"version\":1,\"skyColor\":[0.53,0.81,1.0],\"horizonExponent\":1.0}\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        PrintUsage(argv[0]);
        return 1;
    }
    const std::string layoutPath = argv[1];
    const std::string gltfPath   = argv[2];
    const std::string outputDir  = (argc >= 4) ? argv[3] : "";

    uint32_t layoutVersion = 0;
    std::vector<LayoutInstance> instances;
    if (!LoadLayoutJson(layoutPath, layoutVersion, instances)) return 1;

    std::vector<std::string> meshNames, materialNames;
    if (!LoadGltfWithTinygltf(gltfPath, meshNames, materialNames)) return 1;

    std::cout << "zone_builder: layout version " << layoutVersion << ", instances " << instances.size()
              << ", meshes " << meshNames.size() << ", materials " << materialNames.size() << "\n";

    if (!outputDir.empty()) {
        /* M11.5: content hash from layout + asset mtime for versioned headers. */
        uint64_t contentHash = ComputeContentHash(layoutPath, gltfPath);
        engine::world::VersionedHeader vh;
        vh.formatVersion = engine::world::kZoneBuildFormatVersion;
        vh.builderVersion = engine::world::kCurrentBuilderVersion;
        vh.engineVersion = engine::world::kCurrentEngineVersion;
        vh.contentHash = contentHash;
        /* M11.2: chunk assignation floor(x/256), floor(z/256); write build/zone_x/chunks/chunk_i_j/chunk.meta + instances.bin, zone.meta. */
        std::map<std::pair<int32_t, int32_t>, std::vector<ChunkInstanceRecord>> chunks;
        for (const LayoutInstance& inst : instances) {
            int32_t ci = static_cast<int32_t>(std::floor(inst.position[0] / static_cast<float>(kChunkSize)));
            int32_t cj = static_cast<int32_t>(std::floor(inst.position[2] / static_cast<float>(kChunkSize)));
            ChunkInstanceRecord rec;
            MakeTransformFromPosition(inst.position, rec.transform);
            rec.assetId = inst.useExplicitAssetId ? inst.explicitAssetId : ResolveMeshToAssetId(inst.meshRef, meshNames);
            rec.flags = 0;
            chunks[{ci, cj}].push_back(rec);
        }
        std::vector<std::pair<int32_t, int32_t>> chunkCoords;
        for (const auto& kv : chunks) chunkCoords.push_back(kv.first);
        std::string base = outputDir;
        if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
        const int32_t zoneId = 0;
        for (const auto& kv : chunks) {
            int32_t ci = kv.first.first, cj = kv.first.second;
            float minX = static_cast<float>(ci * kChunkSize);
            float minZ = static_cast<float>(cj * kChunkSize);
            float maxX = static_cast<float>((ci + 1) * kChunkSize);
            float maxZ = static_cast<float>((cj + 1) * kChunkSize);
            std::string chunkDir = base + "chunks/chunk_" + std::to_string(ci) + "_" + std::to_string(cj);
            std::filesystem::create_directories(chunkDir);
            if (!WriteChunkMeta(chunkDir + "/chunk.meta", minX, 0.f, minZ, maxX, 256.f, maxZ, 0u, vh)) return 1;
            if (!WriteInstancesBin(chunkDir + "/instances.bin", kv.second, vh)) return 1;
        }
        if (!WriteZoneMeta(base + "zone.meta", zoneId, chunkCoords, vh)) return 1;
        /* M11.4: one global zone probe at zone center + zone_atmosphere.json. */
        int32_t minCi = chunkCoords.empty() ? 0 : chunkCoords[0].first;
        int32_t maxCi = minCi, minCj = chunkCoords.empty() ? 0 : chunkCoords[0].second, maxCj = minCj;
        for (const auto& p : chunkCoords) {
            if (p.first < minCi) minCi = p.first;
            if (p.first > maxCi) maxCi = p.first;
            if (p.second < minCj) minCj = p.second;
            if (p.second > maxCj) maxCj = p.second;
        }
        float zoneCenterX = 0.5f * (static_cast<float>(minCi + maxCi + 1) * static_cast<float>(kChunkSize));
        float zoneCenterZ = 0.5f * (static_cast<float>(minCj + maxCj + 1) * static_cast<float>(kChunkSize));
        ProbeRecord globalProbe;
        globalProbe.position[0] = zoneCenterX;
        globalProbe.position[1] = 0.f;
        globalProbe.position[2] = zoneCenterZ;
        globalProbe.radius = 2000.f;
        globalProbe.intensity = 1.f;
        if (!WriteProbesBin(base + "probes.bin", { globalProbe }, vh)) return 1;
        if (!WriteZoneAtmosphereJson(base + "zone_atmosphere.json")) return 1;
        std::cout << "zone_builder: wrote zone.meta + " << chunkCoords.size() << " chunks, probes.bin, zone_atmosphere.json to " << outputDir << "\n";
    } else {
        for (size_t i = 0; i < instances.size(); ++i) {
            const LayoutInstance& inst = instances[i];
            std::cout << "  instance[" << i << "] guid=" << inst.guid
                      << " pos=(" << inst.position[0] << "," << inst.position[1] << "," << inst.position[2] << ")m"
                      << " mesh=" << inst.meshRef << " material=" << inst.materialRef << "\n";
        }
        for (size_t i = 0; i < meshNames.size(); ++i)
            std::cout << "  mesh[" << i << "] " << meshNames[i] << "\n";
        for (size_t i = 0; i < materialNames.size(); ++i)
            std::cout << "  material[" << i << "] " << materialNames[i] << "\n";
    }

    return 0;
}
