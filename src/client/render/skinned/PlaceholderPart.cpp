#include "src/client/render/skinned/PlaceholderPart.h"

#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/core/Log.h"
#include "src/shared/math/Math.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
	/// Inverse générale d'une matrice 4x4 col-major (méthode des cofacteurs 2x2).
	/// \param m  matrice d'entrée (16 floats, col-major : élément (r,c) = m[c*4+r]).
	/// \param out matrice inverse en sortie (16 floats). Non écrite si singulière.
	/// \return false si la matrice est singulière (det ~ 0), true sinon.
	bool InvertMat4(const float* m, float* out)
	{
		const float a00 = m[0], a10 = m[1], a20 = m[2], a30 = m[3];
		const float a01 = m[4], a11 = m[5], a21 = m[6], a31 = m[7];
		const float a02 = m[8], a12 = m[9], a22 = m[10], a32 = m[11];
		const float a03 = m[12], a13 = m[13], a23 = m[14], a33 = m[15];

		const float b00 = a00 * a11 - a10 * a01;
		const float b01 = a00 * a21 - a20 * a01;
		const float b02 = a00 * a31 - a30 * a01;
		const float b03 = a10 * a21 - a20 * a11;
		const float b04 = a10 * a31 - a30 * a11;
		const float b05 = a20 * a31 - a30 * a21;
		const float b06 = a02 * a13 - a12 * a03;
		const float b07 = a02 * a23 - a22 * a03;
		const float b08 = a02 * a33 - a32 * a03;
		const float b09 = a12 * a23 - a22 * a13;
		const float b10 = a12 * a33 - a32 * a13;
		const float b11 = a22 * a33 - a32 * a23;

		const float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
		if (det > -1e-7f && det < 1e-7f)
			return false; // singulière

		const float inv = 1.0f / det;
		out[0]  = ( a11 * b11 - a21 * b10 + a31 * b09) * inv;
		out[1]  = (-a10 * b11 + a20 * b10 - a30 * b09) * inv;
		out[2]  = ( a13 * b05 - a23 * b04 + a33 * b03) * inv;
		out[3]  = (-a12 * b05 + a22 * b04 - a32 * b03) * inv;
		out[4]  = (-a01 * b11 + a21 * b08 - a31 * b07) * inv;
		out[5]  = ( a00 * b11 - a20 * b08 + a30 * b07) * inv;
		out[6]  = (-a03 * b05 + a23 * b02 - a33 * b01) * inv;
		out[7]  = ( a02 * b05 - a22 * b02 + a32 * b01) * inv;
		out[8]  = ( a01 * b10 - a11 * b08 + a31 * b06) * inv;
		out[9]  = (-a00 * b10 + a10 * b08 - a30 * b06) * inv;
		out[10] = ( a03 * b04 - a13 * b02 + a33 * b00) * inv;
		out[11] = (-a02 * b04 + a12 * b02 - a32 * b00) * inv;
		out[12] = (-a01 * b09 + a11 * b07 - a21 * b06) * inv;
		out[13] = ( a00 * b09 - a10 * b07 + a20 * b06) * inv;
		out[14] = (-a03 * b03 + a13 * b01 - a23 * b00) * inv;
		out[15] = ( a02 * b03 - a12 * b01 + a22 * b00) * inv;
		return true;
	}
}

namespace engine::render::skinned
{
	SkinnedMeshCpuData MakePlaceholderBoxPart(const Skeleton& skel, int boneIndex, float halfExtentM)
	{
		SkinnedMeshCpuData cpu;
		cpu.skeleton = skel; // squelette partagé (copie) — la pose vient du corps au rendu.

		// Attache RIGIDE de la boîte à l'os cible. Le skinning applique
		// finals[i] = globals[i] * inverseBindGlobal[i] (IBM). Pour qu'un sommet
		// suive l'os SANS distorsion, on l'exprime en espace OS-LOCAL puis on le
		// ramène en espace modèle bind par bg = inverse(IBM) :
		//     pos_modele = bg * pos_osLocal
		//   → world = globals * IBM * bg * pos_osLocal = globals * pos_osLocal
		// (rigide). Bug précédent : on ajoutait l'offset des coins en espace
		// modèle brut (sans bg) → boîte géante et décalée derrière le perso, car
		// l'offset traversait globals*IBM ≠ identité.
		//
		// bg = inverse(IBM) ; si l'os est hors bornes → identité (repli).
		float bg[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
		const bool boneOk = (boneIndex >= 0)
			&& (static_cast<std::size_t>(boneIndex) < skel.bones.size());
		if (boneOk)
			(void)InvertMat4(skel.bones[static_cast<std::size_t>(boneIndex)].inverseBindGlobal.m, bg);

		// Échelle du rig pour la taille MONDE : elle DOIT venir de la bind-globale
		// COMPOSÉE (chaîne bindLocal) et NON de inverse(IBM). En effet, au rendu la
		// boîte est positionnée par le tableau `globals` (= composition des bindLocal,
		// cf. AnimationSampler::ComputeGlobalMatrices), pas par inverse(IBM). Sur un
		// rig où l'IBM porte une échelle différente (nœud armature), utiliser
		// scale(inverse IBM) donnait un localH démesuré → boîte géante hors champ.
		// Taille monde = scale(globals) * localH ; on veut halfExtentM, donc
		// localH = halfExtentM / scale(globals).
		float gcol0[3] = {1.0f, 0.0f, 0.0f};
		float gtrans[3] = {0.0f, 0.0f, 0.0f};
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
			gcol0[0] = g.m[0]; gcol0[1] = g.m[1]; gcol0[2] = g.m[2];
			gtrans[0] = g.m[12]; gtrans[1] = g.m[13]; gtrans[2] = g.m[14];
		}
		float rigScale = std::sqrt(gcol0[0] * gcol0[0] + gcol0[1] * gcol0[1] + gcol0[2] * gcol0[2]);
		if (rigScale < 1e-6f)
			rigScale = 1.0f;

		// Diagnostic (une ligne par variante) : où atterrit la boîte et à quelle
		// échelle. bgTrans = centre via inverse(IBM) ; gTrans = tête via bindLocal
		// composé (= position réelle au rendu). S'ils divergent, le rig est
		// incohérent. invIbmScale vs rigScale explique une éventuelle taille folle.
		const float invIbmScale = std::sqrt(bg[0] * bg[0] + bg[1] * bg[1] + bg[2] * bg[2]);
		LOG_INFO(Render,
			"[ModularDiag] bone={} bgTrans=({:.3f},{:.3f},{:.3f}) gTrans=({:.3f},{:.3f},{:.3f}) "
			"invIbmScale={:.4f} rigScale(bindLocal)={:.4f} halfExtentM={:.3f} localH={:.4f}",
			boneIndex, bg[12], bg[13], bg[14], gtrans[0], gtrans[1], gtrans[2],
			invIbmScale, rigScale, halfExtentM, halfExtentM / rigScale);

		const std::uint16_t bone = static_cast<std::uint16_t>(boneOk ? boneIndex : 0);
		const float h = halfExtentM / rigScale; // demi-extension en espace OS-LOCAL.

		// Transforme un point/vecteur os-local -> modèle bind via bg (col-major).
		auto xformPoint = [&](float x, float y, float z, float w, float out[3]) {
			out[0] = bg[0] * x + bg[4] * y + bg[8]  * z + bg[12] * w;
			out[1] = bg[1] * x + bg[5] * y + bg[9]  * z + bg[13] * w;
			out[2] = bg[2] * x + bg[6] * y + bg[10] * z + bg[14] * w;
		};

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
				// Coin os-local -> espace modèle bind (point : w=1).
				float p[3];
				xformPoint(faces[f].c[v][0], faces[f].c[v][1], faces[f].c[v][2], 1.0f, p);
				sv.pos[0] = p[0];
				sv.pos[1] = p[1];
				sv.pos[2] = p[2];
				// Normale os-local -> modèle (vecteur : w=0), renormalisée.
				float n[3];
				xformPoint(faces[f].n[0], faces[f].n[1], faces[f].n[2], 0.0f, n);
				const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				const float inv = (nl > 1e-6f) ? (1.0f / nl) : 1.0f;
				sv.normal[0] = n[0] * inv;
				sv.normal[1] = n[1] * inv;
				sv.normal[2] = n[2] * inv;
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
			// le repère de dev reste visible quelle que soit la convention réelle
			// de face (cf. historique winding CLAUDE.md). Coût négligeable.
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
