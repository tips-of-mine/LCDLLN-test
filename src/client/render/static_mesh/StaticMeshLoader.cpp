#include "src/client/render/static_mesh/StaticMeshLoader.h"

// cgltf est défini (CGLTF_IMPLEMENTATION) une seule fois dans le projet, dans
// SkinnedMeshLoader.cpp. Ici on n'inclut que les déclarations.
#include "cgltf.h"

#include "src/shared/core/Log.h"

namespace engine::render::staticmesh
{

namespace
{
    /// Extrait l'URI d'image d'une texture_view glTF (ou chaîne vide si absente).
    std::string UriOf(const cgltf_texture_view& view)
    {
        if (view.texture && view.texture->image && view.texture->image->uri)
            return view.texture->image->uri;
        return {};
    }
}  // namespace

std::optional<StaticMeshCpuData> StaticMeshLoader::LoadCpuOnlyForTests(const std::string& path)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success)
    {
        LOG_WARN(Render, "[StaticMeshLoader] Parse failed for '{}' (cgltf result={})", path, static_cast<int>(result));
        if (data) cgltf_free(data);
        return std::nullopt;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
    {
        LOG_WARN(Render, "[StaticMeshLoader] Load buffers failed for '{}'", path);
        cgltf_free(data);
        return std::nullopt;
    }

    StaticMeshCpuData out;
    bool meshLoaded = false;

    // Fusionne toutes les primitives de tous les meshes en un seul buffer
    // (vertices/indices), en réindexant chaque primitive sur l'offset courant.
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi)
    {
        const cgltf_mesh* mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi)
        {
            const cgltf_primitive* prim = &mesh->primitives[pi];
            const cgltf_accessor *aPos = nullptr, *aNor = nullptr, *aUv = nullptr, *aCol = nullptr;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai)
            {
                const cgltf_attribute& at = prim->attributes[ai];
                if (at.type == cgltf_attribute_type_position) aPos = at.data;
                else if (at.type == cgltf_attribute_type_normal) aNor = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) aUv = at.data;
                else if (at.type == cgltf_attribute_type_color && at.index == 0) aCol = at.data;
            }
            if (!aPos) continue;  // primitive sans position : ignorée

            const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());
            const size_t nv = aPos->count;
            out.vertices.resize(baseVertex + nv);
            // Nombre de composantes de COLOR_0 (vec3 ou vec4) ; l'alpha reste à 1 si vec3.
            const int colComps = aCol ? static_cast<int>(cgltf_num_components(aCol->type)) : 0;
            for (size_t v = 0; v < nv; ++v)
            {
                StaticVertex& sv = out.vertices[baseVertex + v];
                cgltf_accessor_read_float(aPos, v, sv.pos, 3);
                if (aNor) cgltf_accessor_read_float(aNor, v, sv.normal, 3);
                if (aUv)  cgltf_accessor_read_float(aUv, v, sv.uv, 2);
                if (aCol) cgltf_accessor_read_float(aCol, v, sv.color, colComps > 4 ? 4 : colComps);
            }

            const uint32_t subFirstIndex = static_cast<uint32_t>(out.indices.size());
            if (prim->indices)
            {
                const size_t ni = prim->indices->count;
                const size_t base = out.indices.size();
                out.indices.resize(base + ni);
                for (size_t i = 0; i < ni; ++i)
                    out.indices[base + i] = baseVertex + static_cast<uint32_t>(cgltf_accessor_read_index(prim->indices, i));
            }
            else
            {
                const size_t base = out.indices.size();
                out.indices.resize(base + nv);
                for (size_t i = 0; i < nv; ++i)
                    out.indices[base + i] = baseVertex + static_cast<uint32_t>(i);
            }

            StaticSubMesh sub;
            sub.firstIndex = subFirstIndex;
            sub.indexCount = static_cast<uint32_t>(out.indices.size()) - subFirstIndex;
            if (prim->material)
            {
                if (prim->material->name) sub.materialName = prim->material->name;
                if (prim->material->has_pbr_metallic_roughness)
                {
                    const cgltf_pbr_metallic_roughness& pbr = prim->material->pbr_metallic_roughness;
                    sub.baseColorUri = UriOf(pbr.base_color_texture);
                    sub.ormUri       = UriOf(pbr.metallic_roughness_texture);
                }
                sub.normalUri = UriOf(prim->material->normal_texture);
                // alphaMode MASK/BLEND -> decoupe alpha (feuillages). En differe on traite
                // les deux comme un cutout (discard sous le seuil dans le fragment shader).
                sub.alphaCutout = (prim->material->alpha_mode != cgltf_alpha_mode_opaque);
            }
            out.submeshes.push_back(std::move(sub));
            meshLoaded = true;
        }
    }

    cgltf_free(data);

    if (!meshLoaded)
    {
        LOG_WARN(Render, "[StaticMeshLoader] No primitive with POSITION in '{}'", path);
        return std::nullopt;
    }

    // Bornes verticales locales (pour le topY des cylindres de collision des props).
    if (!out.vertices.empty())
    {
        float mn = out.vertices[0].pos[1];
        float mx = out.vertices[0].pos[1];
        for (const auto& v : out.vertices)
        {
            if (v.pos[1] < mn) mn = v.pos[1];
            if (v.pos[1] > mx) mx = v.pos[1];
        }
        out.localMinY = mn;
        out.localMaxY = mx;
    }
    return out;
}

}  // namespace engine::render::staticmesh
