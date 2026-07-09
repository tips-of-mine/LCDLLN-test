#include "src/client/render/skinned/PlaceholderPart.h"

#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/math/Math.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::render::skinned
{
	SkinnedMeshCpuData MakePlaceholderBoxPart(const Skeleton& skel, int boneIndex, float halfExtentM)
	{
		SkinnedMeshCpuData cpu;
		cpu.skeleton = skel; // squelette partagé (copie) — la pose vient du corps au rendu.

		// Position bind-globale de l'os cible. Même convention que
		// AnimationSampler::ComputeGlobalMatrices : global = parent * bindLocal
		// (bones topo-ordonnés parent-avant-enfant). Translation = colonne 3.
		float cx = 0.0f, cy = 0.0f, cz = 0.0f;
		const bool boneOk = (boneIndex >= 0)
			&& (static_cast<std::size_t>(boneIndex) < skel.bones.size());
		if (boneOk)
		{
			std::vector<engine::math::Mat4> globals(skel.bones.size());
			for (std::size_t i = 0; i < skel.bones.size(); ++i)
			{
				const int parent = skel.bones[i].parentIndex;
				globals[i] = (parent < 0)
					? skel.bones[i].bindLocal
					: globals[static_cast<std::size_t>(parent)] * skel.bones[i].bindLocal;
			}
			const engine::math::Mat4& g = globals[static_cast<std::size_t>(boneIndex)];
			cx = g.m[12];
			cy = g.m[13];
			cz = g.m[14];
		}

		const std::uint16_t bone = static_cast<std::uint16_t>(boneOk ? boneIndex : 0);
		const float h = halfExtentM;

		// 6 faces (normale sortante) ; 4 sommets CCW vus de l'extérieur.
		struct Face { float n[3]; float c[4][3]; };
		const Face faces[6] = {
			{{ 0.f, 0.f, 1.f}, {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}}}, // +Z
			{{ 0.f, 0.f,-1.f}, {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}}}, // -Z
			{{ 1.f, 0.f, 0.f}, {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}}}, // +X
			{{-1.f, 0.f, 0.f}, {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}}}, // -X
			{{ 0.f, 1.f, 0.f}, {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}}}, // +Y
			{{ 0.f,-1.f, 0.f}, {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}}}, // -Y
		};
		const float uvs[4][2] = {{0.f,0.f},{1.f,0.f},{1.f,1.f},{0.f,1.f}};

		cpu.vertices.reserve(24);
		cpu.indices.reserve(36);
		for (int f = 0; f < 6; ++f)
		{
			const std::uint32_t base = static_cast<std::uint32_t>(cpu.vertices.size());
			for (int v = 0; v < 4; ++v)
			{
				SkinnedVertex sv{};
				sv.pos[0] = cx + faces[f].c[v][0];
				sv.pos[1] = cy + faces[f].c[v][1];
				sv.pos[2] = cz + faces[f].c[v][2];
				sv.normal[0] = faces[f].n[0];
				sv.normal[1] = faces[f].n[1];
				sv.normal[2] = faces[f].n[2];
				sv.uv[0] = uvs[v][0];
				sv.uv[1] = uvs[v][1];
				sv.boneIndices[0] = bone;
				sv.boneIndices[1] = 0u;
				sv.boneIndices[2] = 0u;
				sv.boneIndices[3] = 0u;
				sv.weights[0] = 1.0f;
				sv.weights[1] = 0.0f;
				sv.weights[2] = 0.0f;
				sv.weights[3] = 0.0f;
				cpu.vertices.push_back(sv);
			}
			cpu.indices.push_back(base + 0u);
			cpu.indices.push_back(base + 1u);
			cpu.indices.push_back(base + 2u);
			cpu.indices.push_back(base + 0u);
			cpu.indices.push_back(base + 2u);
			cpu.indices.push_back(base + 3u);
		}

		cpu.submeshes.push_back(
			SkinnedSubMesh{0u, static_cast<std::uint32_t>(cpu.indices.size()), std::string()});
		return cpu;
	}
}
