#include "src/world_editor/terrain/PolylineMacroCore.h"

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/world_editor/terrain/TerrainBrush.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::editor::world
{
	namespace
	{
		/// Côté d'un chunk de heightmap en mètres (256 m, cf. M100.5). Le bord
		/// `localX = 256` est partagé avec le chunk suivant à `localX = 0`.
		constexpr float kChunkSpanMeters =
			(engine::world::terrain::kTerrainResolution - 1u) *
			 engine::world::terrain::kTerrainCellSizeMeters;

		/// Dernier index de cellule valide en X ou Z (256 quand
		/// `kTerrainResolution = 257`).
		constexpr int kLastCell =
			static_cast<int>(engine::world::terrain::kTerrainResolution) - 1;

		/// Petit Vec2 local au .cpp (pour éviter de dépendre d'un header de
		/// math externe et de garder la rasterisation autonome).
		struct V2 { float x; float z; };

		/// Soustrait deux V2 composante à composante.
		inline V2 Sub(V2 a, V2 b) { return { a.x - b.x, a.z - b.z }; }

		/// Produit scalaire 2D dans le plan XZ.
		inline float Dot(V2 a, V2 b) { return a.x * b.x + a.z * b.z; }

		/// Composante Y du produit vectoriel 3D des deux vecteurs étendus au
		/// plan XZ : retourne `a.x * b.z - a.z * b.x`. Signe utilisé pour
		/// détecter de quel côté de l'axe se trouve le point (asymétrie).
		inline float Cross(V2 a, V2 b) { return a.x * b.z - a.z * b.x; }

		/// Norme L2 d'un vecteur XZ.
		inline float Length(V2 a) { return std::sqrt(a.x * a.x + a.z * a.z); }

		/// Projette `p` sur le segment `[s0, s1]`. Retourne le paramètre
		/// `t ∈ [0, 1]` (avec clamp aux bornes du segment) et la distance
		/// perpendiculaire `distLat ≥ 0`.
		void ClosestPointOnSegment(V2 p, V2 s0, V2 s1,
			float& outT, float& outDistLat)
		{
			const V2 d = Sub(s1, s0);
			const float len2 = Dot(d, d);
			if (len2 <= 0.0f)
			{
				// Segment dégénéré : t = 0, distance = |p - s0|.
				outT = 0.0f;
				outDistLat = Length(Sub(p, s0));
				return;
			}
			const float t = std::clamp(Dot(Sub(p, s0), d) / len2, 0.0f, 1.0f);
			outT = t;
			const V2 proj = { s0.x + t * d.x, s0.z + t * d.z };
			outDistLat = Length(Sub(p, proj));
		}

		/// Évalue le profil radial unidimensionnel pour `u ∈ [0, 1]`. Renvoie
		/// un poids dans `[0, 1]` (1 sur l'axe, 0 à la base). Voir doc
		/// `FlankProfile` dans le header.
		float EvalProfile(FlankProfile p, float u)
		{
			const float uc = std::clamp(u, 0.0f, 1.0f);
			switch (p)
			{
				case FlankProfile::Smoothstep:
				{
					// 1 - smoothstep(0, 1, u) = (1 - uc) * (1 - uc) * (1 + 2*uc)
					// non, on veut décroissance monotone : 1 - (uc² * (3 - 2uc))
					const float ss = uc * uc * (3.0f - 2.0f * uc);
					return 1.0f - ss;
				}
				case FlankProfile::Linear:
					return 1.0f - uc;
				case FlankProfile::Exp:
					return std::exp(-3.0f * uc * uc);
			}
			return 0.0f;
		}

		/// Données pré-calculées par segment (deux vertices consécutifs)
		/// pour accélérer la boucle de rasterisation.
		struct SegmentPrecomp
		{
			V2 s0;
			V2 s1;
			float widthMaxLocal;   // max(widthMeters de v0, widthMeters de v1)
		};

		/// Interpole linéairement entre deux scalaires.
		inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
	}

	SparseChunkDeltas RasterizeMacroPolyline(const MacroPolylineParams& params,
		bool invert)
	{
		SparseChunkDeltas out;
		if (params.vertices.size() < 2u) return out;

		// 1) Construire la liste de segments (avec segment de fermeture si Loop).
		const size_t vCount = params.vertices.size();
		const size_t segCount = (params.mode == PolylineMode::Loop)
			? vCount
			: (vCount - 1u);

		std::vector<SegmentPrecomp> segs;
		segs.reserve(segCount);

		// Borne max de largeur (pour étendre le bounding box global).
		float maxWidthGlobal = 0.0f;
		// Bounding box en X/Z des centres de vertex (sera étendu par width*1.5).
		float bbMinX = +1e30f, bbMaxX = -1e30f;
		float bbMinZ = +1e30f, bbMaxZ = -1e30f;

		for (size_t i = 0; i < segCount; ++i)
		{
			const PolylineVertex& va = params.vertices[i];
			const PolylineVertex& vb = params.vertices[(i + 1u) % vCount];
			SegmentPrecomp seg;
			seg.s0 = { va.worldX, va.worldZ };
			seg.s1 = { vb.worldX, vb.worldZ };
			seg.widthMaxLocal = std::max(va.widthMeters, vb.widthMeters);
			segs.push_back(seg);

			maxWidthGlobal = std::max(maxWidthGlobal, seg.widthMaxLocal);
			bbMinX = std::min({ bbMinX, va.worldX, vb.worldX });
			bbMaxX = std::max({ bbMaxX, va.worldX, vb.worldX });
			bbMinZ = std::min({ bbMinZ, va.worldZ, vb.worldZ });
			bbMaxZ = std::max({ bbMaxZ, va.worldZ, vb.worldZ });
		}

		// 2) Étendre le bounding box par maxWidthGlobal * 1.5 (sécurité : couvre
		//    aussi l'asymétrie qui peut décaler le profil de quelques mètres).
		const float pad = maxWidthGlobal * 1.5f;
		bbMinX -= pad; bbMaxX += pad;
		bbMinZ -= pad; bbMaxZ += pad;

		// 3) Convertir le BB en plage de chunks.
		const int chunkXMin = static_cast<int>(std::floor(bbMinX / kChunkSpanMeters));
		const int chunkXMax = static_cast<int>(std::floor(bbMaxX / kChunkSpanMeters));
		const int chunkZMin = static_cast<int>(std::floor(bbMinZ / kChunkSpanMeters));
		const int chunkZMax = static_cast<int>(std::floor(bbMaxZ / kChunkSpanMeters));

		const int resInt = static_cast<int>(engine::world::terrain::kTerrainResolution);
		const float invertSign = invert ? -1.0f : +1.0f;

		// 4) Itérer chunk par chunk.
		for (int cz = chunkZMin; cz <= chunkZMax; ++cz)
		{
			for (int cx = chunkXMin; cx <= chunkXMax; ++cx)
			{
				const float chunkOriginX = static_cast<float>(cx) * kChunkSpanMeters;
				const float chunkOriginZ = static_cast<float>(cz) * kChunkSpanMeters;

				// Détermine la plage locale [xL..xH] et [zL..zH] qui intersecte
				// le BB élargi de la polyline. Cellules hors plage : skip.
				const int xL = std::max(0,
					static_cast<int>(std::floor(bbMinX - chunkOriginX)));
				const int xH = std::min(kLastCell,
					static_cast<int>(std::ceil(bbMaxX - chunkOriginX)));
				const int zL = std::max(0,
					static_cast<int>(std::floor(bbMinZ - chunkOriginZ)));
				const int zH = std::min(kLastCell,
					static_cast<int>(std::ceil(bbMaxZ - chunkOriginZ)));
				if (xL > xH || zL > zH) continue;

				const engine::world::GlobalChunkCoord coord{ cx, cz };
				std::unordered_map<uint32_t, float>* cellMap = nullptr;

				for (int iz = zL; iz <= zH; ++iz)
				{
					const float worldZ = chunkOriginZ +
						static_cast<float>(iz) * engine::world::terrain::kTerrainCellSizeMeters;
					for (int ix = xL; ix <= xH; ++ix)
					{
						const float worldX = chunkOriginX +
							static_cast<float>(ix) * engine::world::terrain::kTerrainCellSizeMeters;
						const V2 p{ worldX, worldZ };

						// 5) Trouver le segment dont la distance latérale est min.
						float bestT = 0.0f, bestDist = 0.0f;
						int   bestSeg = -1;
						for (size_t si = 0; si < segs.size(); ++si)
						{
							float t, distLat;
							ClosestPointOnSegment(p, segs[si].s0, segs[si].s1, t, distLat);
							if (bestSeg < 0 || distLat < bestDist)
							{
								bestSeg = static_cast<int>(si);
								bestT = t;
								bestDist = distLat;
							}
						}
						if (bestSeg < 0) continue;

						const PolylineVertex& va = params.vertices[
							static_cast<size_t>(bestSeg)];
						const PolylineVertex& vb = params.vertices[
							(static_cast<size_t>(bestSeg) + 1u) % vCount];

						// 6) Interpolation locale des paramètres.
						const float widthLocal  = std::max(0.0f,
							Lerp(va.widthMeters, vb.widthMeters, bestT));
						const float heightLocal = Lerp(va.heightMeters, vb.heightMeters, bestT);
						const float noiseLocal  = std::max(0.0f,
							Lerp(va.noiseAmplitude, vb.noiseAmplitude, bestT));
						const float asymLocal   = std::clamp(
							Lerp(va.asymmetry, vb.asymmetry, bestT), -1.0f, 1.0f);

						const float halfWidth = widthLocal * 0.5f;
						if (halfWidth <= 0.0f) continue;
						if (bestDist > halfWidth) continue;

						// 7) Poids radial selon le profil choisi.
						const float u = bestDist / halfWidth;
						float w = EvalProfile(params.profile, u) * heightLocal;

						// 8) Asymétrie via signe du cross-product tangent vs (p - s0).
						const SegmentPrecomp& seg = segs[static_cast<size_t>(bestSeg)];
						const V2 tangent = Sub(seg.s1, seg.s0);
						const V2 toPoint = Sub(p, seg.s0);
						const float crossY = Cross(tangent, toPoint);
						const float sign   = (crossY >= 0.0f) ? +1.0f : -1.0f;
						w *= (1.0f + asymLocal * sign);

						// 9) Bruit Simplex 2D (déterministe : seed encodé dans
						//    un décalage de coord pour éviter de modifier
						//    EvalSimplex2D qui n'a pas de paramètre seed).
						//    Avec un seed différent, on translate (x, z) ce qui
						//    décorrèle visuellement les motifs.
						float noise = 0.0f;
						if (noiseLocal > 0.0f)
						{
							const float seedShiftX = static_cast<float>(
								(params.noiseSeed * 1664525u) & 0xFFFFu) * 0.5f;
							const float seedShiftZ = static_cast<float>(
								(params.noiseSeed * 1013904223u) & 0xFFFFu) * 0.5f;
							noise = EvalSimplex2D(
								worldX + seedShiftX,
								worldZ + seedShiftZ,
								std::max(1e-6f, params.noiseFrequency),
								/*octaves=*/2);
							noise *= noiseLocal;
						}

						const float delta = (w + noise) * invertSign;
						if (delta == 0.0f) continue;

						if (cellMap == nullptr)
						{
							cellMap = &out[coord];
							// Reserve approximative pour limiter rehash.
							cellMap->reserve(64);
						}

						const uint32_t cellIndex = static_cast<uint32_t>(
							iz * resInt + ix);
						(*cellMap)[cellIndex] += delta;
					}
				}
			}
		}

		return out;
	}
}
