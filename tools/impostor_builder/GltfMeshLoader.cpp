/// M45.4 — Implémentation du chargement glTF via cgltf.
/// CGLTF_IMPLEMENTATION n'est défini QUE dans ce .cpp (unique TU).

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "GltfMeshLoader.h"

#include <cfloat>
#include <cstring>

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

					// Mise à jour des bounds.
					for (int k = 0; k < 3; ++k)
					{
						if (rv.pos[k] < bmin[k]) bmin[k] = rv.pos[k];
						if (rv.pos[k] > bmax[k]) bmax[k] = rv.pos[k];
					}

					out.vertices.push_back(rv);
				}

				// Indices (réindexés avec l'offset baseVertex).
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
					// Pas d'indices : triangles séquentiels.
					for (cgltf_size i = 0; i < vcount; ++i)
						out.indices.push_back(baseVertex + static_cast<uint32_t>(i));
				}
			}
		}

		cgltf_free(data);

		if (out.vertices.empty() || out.indices.empty())
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
