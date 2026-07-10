#include "src/client/render/skinned/PlaceholderPart.h"

#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/core/Log.h"

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

		// IMPORTANT — placement de la boîte sur ce rig UE5.
		// Deux constats issus du diagnostic en jeu :
		//  1) TOUS les os ont leur bind-globale à l'ORIGINE (os tête ≈ (0,0,0),
		//     Y=0 au lieu de ~1.6 m). La pose debout est portée par les SOMMETS du
		//     maillage, pas par les matrices d'os.
		//  2) La matrice finals[os_tête] appliquée au rendu DÉPLACE un point posé à
		//     hauteur tête loin dans le décor (l'os tête est inexploitable ici).
		//
		// Solution robuste : peser la boîte à l'os RACINE (index 0), dont la matrice
		// finals ≈ identité, et la placer à la hauteur tête MODÈLE (~1.6 m). La boîte
		// suit alors la matrice modèle de l'avatar (position + orientation du corps)
		// et se pose sur la tête, indépendamment de l'os tête cassé. Le suivi fin par
		// os (casque qui tourne avec la tête) reviendra avec un rig correct + vrais
		// assets modulaires. `boneIndex` n'est plus qu'informatif (log).
		const std::uint16_t bone = 0; // os racine : rid la matrice modèle avatar.

		// Hauteur tête (espace modèle) : l'avatar fait ~1.8 m, la tête est vers 1.6 m.
		constexpr float kHeadHeightM = 1.60f;
		const float cx = 0.0f, cy = kHeadHeightM, cz = 0.0f;
		const float h = halfExtentM;

		LOG_INFO(Render,
			"[ModularDiag] boite tete : boneDemande={} pesee_sur_os=0 (racine) "
			"centre=({:.2f},{:.2f},{:.2f}) demiTaille={:.2f} m",
			boneIndex, cx, cy, cz, h);

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
		cpu.indices.reserve(72); // double face (recto + verso) : 6 faces x 2 tris x 2 sens x 3.
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
			// Recto (CCW, normale sortante).
			cpu.indices.push_back(base + 0u);
			cpu.indices.push_back(base + 1u);
			cpu.indices.push_back(base + 2u);
			cpu.indices.push_back(base + 0u);
			cpu.indices.push_back(base + 2u);
			cpu.indices.push_back(base + 3u);
			// Verso (winding inversé) : boîte DOUBLE-FACE. Assurance anti-culling —
			// le repère de dev reste visible quelle que soit la convention de face.
			cpu.indices.push_back(base + 0u);
			cpu.indices.push_back(base + 2u);
			cpu.indices.push_back(base + 1u);
			cpu.indices.push_back(base + 0u);
			cpu.indices.push_back(base + 3u);
			cpu.indices.push_back(base + 2u);
		}

		cpu.submeshes.push_back(
			SkinnedSubMesh{0u, static_cast<std::uint32_t>(cpu.indices.size()), std::string()});
		return cpu;
	}
}
