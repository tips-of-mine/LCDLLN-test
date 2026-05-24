#include "src/client/render/skinned/AvatarMaterialRouting.h"

#include <cctype>

namespace engine::render::skinned
{
namespace
{
    /// Supprime les espaces (et caractères blancs) de début et de fin.
    std::string Trim(const std::string& s)
    {
        std::size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }
}  // namespace

std::vector<uint32_t> BuildSubmeshMaterialIndices(
    const std::vector<SkinnedSubMesh>& submeshes,
    const std::vector<std::string>&    bodyMaterialNames,
    uint32_t                           bodyMaterialId,
    uint32_t                           outfitMaterialId)
{
    std::vector<uint32_t> out;
    if (bodyMaterialId == 0u || submeshes.empty())
        return out;  // vide -> l'appelant fait un mono-draw habit

    out.reserve(submeshes.size());
    for (const auto& sub : submeshes)
    {
        const std::string name = Trim(sub.materialName);
        bool isBody = false;
        for (const auto& bn : bodyMaterialNames)
        {
            if (Trim(bn) == name) { isBody = true; break; }
        }
        out.push_back(isBody ? bodyMaterialId : outfitMaterialId);
    }
    return out;
}
}  // namespace engine::render::skinned
