/// M45.4 — Implémentation du chargement glTF via cgltf.
/// CGLTF_IMPLEMENTATION et STB_IMAGE_IMPLEMENTATION ne sont définis QUE dans ce
/// .cpp (unique TU pour chaque bibliothèque header-only).

#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0) // en-tete tiers : silence les warnings
#endif
#include "cgltf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define STB_IMAGE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0) // en-tete tiers : silence les warnings
#endif
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "GltfMeshLoader.h"

#include <cfloat>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace tools::impostor_builder
{
	namespace
	{
		/// Cherche l'accesseur d'un attribut donné dans une primitive.
		/// \return nullptr si l'attribut est absent.
		const cgltf_accessor* FindAttr(const cgltf_primitive& prim, cgltf_attribute_type type)
		{
			for (cgltf_size a = 0; a < prim.attributes_count; ++a)
				if (prim.attributes[a].type == type)
					return prim.attributes[a].data;
			return nullptr;
		}

		/// Résout l'URI d'une image glTF en chemin absolu sur disque.
		/// \param gltfPath Chemin du .gltf source (sert de base pour l'URI relative).
		/// \param image    Image cgltf (peut avoir image->uri == nullptr si embarquée).
		/// \return Chemin résolu, ou chaîne vide si l'URI est absente/data-uri/buffer.
		std::string ResolveImagePath(const std::string& gltfPath, const cgltf_image* image)
		{
			if (image == nullptr || image->uri == nullptr)
				return {}; // image embarquée (buffer view / data-uri) non gérée ici

			// Les data-URI (base64 inline) commencent par "data:" — non gérées.
			if (std::strncmp(image->uri, "data:", 5) == 0)
				return {};

			// Décode les éventuels %20 etc. sur une copie locale.
			std::string uri = image->uri;
			std::vector<char> buf(uri.begin(), uri.end());
			buf.push_back('\0');
			cgltf_decode_uri(buf.data());
			uri = buf.data();

			const std::filesystem::path base = std::filesystem::path(gltfPath).parent_path();
			return (base / uri).string();
		}

		/// Charge la texture baseColor d'un matériau dans LoadedMaterial.
		/// Non-fatale : en cas de PNG manquante, warne et laisse bcW=bcH=0.
		/// \param gltfPath Chemin du .gltf (base de résolution).
		/// \param src      Matériau cgltf source.
		/// \param dst      Matériau destination (rempli).
		/// Effet de bord : warnings sur stderr ; appelle stbi_load/stbi_image_free.
		void FillMaterial(const std::string& gltfPath, const cgltf_material& src, LoadedMaterial& dst)
		{
			if (src.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = src.pbr_metallic_roughness;
				for (int k = 0; k < 4; ++k)
					dst.baseColorFactor[k] = pbr.base_color_factor[k];
				dst.metallic  = pbr.metallic_factor;
				dst.roughness = pbr.roughness_factor;

				const cgltf_texture* tex = pbr.base_color_texture.texture;
				if (tex != nullptr && tex->image != nullptr)
				{
					const std::string imgPath = ResolveImagePath(gltfPath, tex->image);
					if (!imgPath.empty())
					{
						int w = 0, h = 0, comp = 0;
						// Force RGBA (4 canaux) pour un échantillonnage uniforme.
						stbi_uc* pixels = stbi_load(imgPath.c_str(), &w, &h, &comp, 4);
						if (pixels != nullptr && w > 0 && h > 0)
						{
							dst.bcW = w;
							dst.bcH = h;
							dst.baseColorRGBA.assign(pixels,
								pixels + static_cast<size_t>(w) * h * 4);
							stbi_image_free(pixels);
						}
						else
						{
							std::cerr << "[impostor_builder] WARN: baseColor PNG introuvable/illisible: "
							          << imgPath << " (fallback baseColorFactor)\n";
							if (pixels != nullptr) stbi_image_free(pixels);
						}
					}
				}
			}

			dst.alphaBlendOrMask = (src.alpha_mode == cgltf_alpha_mode_blend ||
			                        src.alpha_mode == cgltf_alpha_mode_mask);
		}
	}

	bool LoadGltf(const std::string& path, LoadedMesh& out, std::string& err)
	{
		out = LoadedMesh{};

		cgltf_options options;
		std::memset(&options, 0, sizeof(options));

		cgltf_data* data = nullptr;
		cgltf_result r = cgltf_parse_file(&options, path.c_str(), &data);
		if (r != cgltf_result_success || data == nullptr)
		{
			err = "LoadGltf: échec parse: " + path;
			return false;
		}

		r = cgltf_load_buffers(&options, data, path.c_str());
		if (r != cgltf_result_success)
		{
			err = "LoadGltf: échec chargement des buffers: " + path;
			cgltf_free(data);
			return false;
		}

		// --- Charge tous les matériaux (index cgltf == index LoadedMesh) -----
		out.materials.resize(data->materials_count);
		for (cgltf_size mi = 0; mi < data->materials_count; ++mi)
			FillMaterial(path, data->materials[mi], out.materials[mi]);

		/// Retrouve l'index global d'un matériau cgltf dans data->materials.
		/// \return index [0,materials_count) ou -1 si nullptr.
		auto materialIndexOf = [&](const cgltf_material* m) -> int {
			if (m == nullptr) return -1;
			const cgltf_size idx = cgltf_material_index(data, m);
			return static_cast<int>(idx);
		};

		float bmin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
		float bmax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

		for (cgltf_size m = 0; m < data->meshes_count; ++m)
		{
			const cgltf_mesh& mesh = data->meshes[m];
			for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
			{
				const cgltf_primitive& prim = mesh.primitives[p];
				if (prim.type != cgltf_primitive_type_triangles)
					continue;

				const cgltf_accessor* posAcc = FindAttr(prim, cgltf_attribute_type_position);
				if (posAcc == nullptr)
					continue; // POSITION obligatoire ; primitive ignorée si absente
				const cgltf_accessor* nrmAcc = FindAttr(prim, cgltf_attribute_type_normal);
				const cgltf_accessor* colAcc = FindAttr(prim, cgltf_attribute_type_color);
				const cgltf_accessor* uvAcc  = FindAttr(prim, cgltf_attribute_type_texcoord);

				const cgltf_size vcount = posAcc->count;
				const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());

				for (cgltf_size v = 0; v < vcount; ++v)
				{
					RasterVertex rv;

					float pos[3] = {0, 0, 0};
					cgltf_accessor_read_float(posAcc, v, pos, 3);
					rv.pos[0] = pos[0]; rv.pos[1] = pos[1]; rv.pos[2] = pos[2];

					if (nrmAcc != nullptr)
					{
						float nrm[3] = {0, 1, 0};
						cgltf_accessor_read_float(nrmAcc, v, nrm, 3);
						rv.normal[0] = nrm[0]; rv.normal[1] = nrm[1]; rv.normal[2] = nrm[2];
					}
					// sinon défaut (0,1,0) déjà dans RasterVertex.

					if (colAcc != nullptr)
					{
						// vec3 ou vec4 ; lecture en 4 composantes (A=1 si vec3).
						float col[4] = {1, 1, 1, 1};
						cgltf_accessor_read_float(colAcc, v, col, 4);
						rv.color[0] = col[0]; rv.color[1] = col[1];
						rv.color[2] = col[2]; rv.color[3] = col[3];
					}
					// sinon défaut blanc opaque déjà dans RasterVertex.

					if (uvAcc != nullptr)
					{
						float uv[2] = {0, 0};
						cgltf_accessor_read_float(uvAcc, v, uv, 2);
						rv.uv[0] = uv[0]; rv.uv[1] = uv[1];
					}
					// sinon défaut (0,0) déjà dans RasterVertex.

					for (int k = 0; k < 3; ++k)
					{
						if (rv.pos[k] < bmin[k]) bmin[k] = rv.pos[k];
						if (rv.pos[k] > bmax[k]) bmax[k] = rv.pos[k];
					}

					out.vertices.push_back(rv);
				}

				// --- Indices du sous-mesh (réindexés avec baseVertex) --------
				const uint32_t firstIndex = static_cast<uint32_t>(out.indices.size());
				if (prim.indices != nullptr)
				{
					const cgltf_size icount = prim.indices->count;
					for (cgltf_size i = 0; i < icount; ++i)
					{
						const cgltf_size idx = cgltf_accessor_read_index(prim.indices, i);
						out.indices.push_back(baseVertex + static_cast<uint32_t>(idx));
					}
				}
				else
				{
					for (cgltf_size i = 0; i < vcount; ++i)
						out.indices.push_back(baseVertex + static_cast<uint32_t>(i));
				}

				LoadedSubMesh sm;
				sm.firstIndex    = firstIndex;
				sm.indexCount    = static_cast<uint32_t>(out.indices.size()) - firstIndex;
				sm.materialIndex = materialIndexOf(prim.material);
				if (sm.indexCount >= 3)
					out.subMeshes.push_back(sm);
			}
		}

		cgltf_free(data);

		if (out.vertices.empty() || out.indices.empty() || out.subMeshes.empty())
		{
			err = "LoadGltf: aucune primitive triangle exploitable dans " + path;
			return false;
		}

		for (int k = 0; k < 3; ++k)
		{
			out.boundsMin[k] = bmin[k];
			out.boundsMax[k] = bmax[k];
		}
		return true;
	}
}
