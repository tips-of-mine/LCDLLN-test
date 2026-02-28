/**
 * @file zone_builder.cpp
 * @brief CLI tool: read glTF (assets) + layout.json (placements) to build a zone (M11.1).
 *
 * - layout.json: versioned; positions in meters; GUID per instance.
 * - glTF: load via TinyGLTF; list meshes/materials; emit instances + assets.
 */

#include <tiny_gltf.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace nlohmann;

/** @brief Minimal layout.json schema: version, instances with guid and position (meters). */
struct LayoutInstance {
    std::string guid;
    float position[3] = { 0.f, 0.f, 0.f };
    std::string meshRef;
    std::string materialRef;
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

/** @brief Prints usage to stderr. */
void PrintUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <layout.json> <scene.gltf|scene.glb>\n";
    std::cerr << "  Loads layout (placements, GUID, positions in meters) and glTF (TinyGLTF); lists instances and assets.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        PrintUsage(argv[0]);
        return 1;
    }
    const std::string layoutPath = argv[1];
    const std::string gltfPath   = argv[2];

    uint32_t layoutVersion = 0;
    std::vector<LayoutInstance> instances;
    if (!LoadLayoutJson(layoutPath, layoutVersion, instances)) return 1;

    std::vector<std::string> meshNames, materialNames;
    if (!LoadGltfWithTinygltf(gltfPath, meshNames, materialNames)) return 1;

    std::cout << "zone_builder: layout version " << layoutVersion << ", instances " << instances.size()
              << ", meshes " << meshNames.size() << ", materials " << materialNames.size() << "\n";

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

    return 0;
}
