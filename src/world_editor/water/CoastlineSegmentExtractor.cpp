#include "src/world_editor/water/CoastlineSegmentExtractor.h"

#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		/// Interpole linéairement la position d'un point d'arête où l'iso
		/// `seaLevel` traverse. Retourne `t ∈ [0, 1]` tel que
		/// `lerp(h0, h1, t) == seaLevel`.
		inline float EdgeInterp(float h0, float h1, float seaLevel)
		{
			const float d = h1 - h0;
			if (std::fabs(d) < 1e-6f) return 0.5f;
			const float t = (seaLevel - h0) / d;
			return (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
		}

		/// Convertit (gridX_local, gridZ_local) en (worldX, worldZ).
		inline void GridToWorld(const ConsolidatedHeightGrid& g,
			float gx, float gz, float& wx, float& wz)
		{
			wx = (static_cast<float>(g.originCellX) + gx) * g.cellSizeMeters;
			wz = (static_cast<float>(g.originCellZ) + gz) * g.cellSizeMeters;
		}
	}

	std::vector<CoastlineSegment> ExtractCoastlineSegments(
		const ConsolidatedHeightGrid& grid, float seaLevelMeters)
	{
		std::vector<CoastlineSegment> out;
		const int W = grid.width;
		const int H = grid.height;
		if (W < 2 || H < 2) return out;

		// Convention des coins du carré (x, z) → (x+1, z) → (x+1, z+1) → (x, z+1)
		// notés 0=SW, 1=SE, 2=NE, 3=NW (Z monte vers le N).
		// Code = bit0(SW) | bit1(SE) | bit2(NE) | bit3(NW).
		// Pour chaque code 0..15, table des paires d'arêtes traversées par
		// l'iso-altitude. Arêtes : 0=South(SW-SE), 1=East(SE-NE), 2=North(NE-NW),
		// 3=West(NW-SW). Chaque entrée : jusqu'à 2 paires (e0,e1) ; -1 sentinelle.
		struct EdgePair { int e0; int e1; };
		// Cas non-triviaux : codes 0 et 15 = uniforme → aucun segment.
		// Codes 5 et 10 = selle de cheval, résolus en deux segments.
		static const EdgePair kCases[16][2] = {
			{ {-1,-1}, {-1,-1} },  // 0  0000
			{ { 3, 0}, {-1,-1} },  // 1  0001 SW
			{ { 0, 1}, {-1,-1} },  // 2  0010 SE
			{ { 3, 1}, {-1,-1} },  // 3  0011 SW+SE
			{ { 1, 2}, {-1,-1} },  // 4  0100 NE
			{ { 3, 0}, { 1, 2} },  // 5  0101 SW+NE (selle)
			{ { 0, 2}, {-1,-1} },  // 6  0110 SE+NE
			{ { 3, 2}, {-1,-1} },  // 7  0111 SW+SE+NE
			{ { 2, 3}, {-1,-1} },  // 8  1000 NW
			{ { 2, 0}, {-1,-1} },  // 9  1001 SW+NW
			{ { 0, 1}, { 2, 3} },  // 10 1010 SE+NW (selle)
			{ { 2, 1}, {-1,-1} },  // 11 1011 SW+SE+NW
			{ { 1, 3}, {-1,-1} },  // 12 1100 NE+NW
			{ { 1, 0}, {-1,-1} },  // 13 1101 SW+NE+NW
			{ { 0, 3}, {-1,-1} },  // 14 1110 SE+NE+NW
			{ {-1,-1}, {-1,-1} },  // 15 1111 uniforme
		};

		auto pushSegment = [&](float ax, float az, float bx, float bz)
		{
			CoastlineSegment s;
			s.ax = ax; s.az = az;
			s.bx = bx; s.bz = bz;
			out.push_back(s);
		};

		out.reserve(static_cast<size_t>(W) * H / 8u);

		for (int z = 0; z < H - 1; ++z)
		{
			for (int x = 0; x < W - 1; ++x)
			{
				const float h_sw = grid.Get(x,     z);
				const float h_se = grid.Get(x + 1, z);
				const float h_ne = grid.Get(x + 1, z + 1);
				const float h_nw = grid.Get(x,     z + 1);

				int code = 0;
				if (h_sw > seaLevelMeters) code |= 1;
				if (h_se > seaLevelMeters) code |= 2;
				if (h_ne > seaLevelMeters) code |= 4;
				if (h_nw > seaLevelMeters) code |= 8;
				if (code == 0 || code == 15) continue;

				// Pré-calcule les points d'intersection sur les 4 arêtes.
				// Chaque arête est paramétrée selon (gx, gz) chunk-local
				// au carré.
				float ex[4], ez[4]; // South, East, North, West
				{
					const float tS = EdgeInterp(h_sw, h_se, seaLevelMeters);
					ex[0] = static_cast<float>(x) + tS;
					ez[0] = static_cast<float>(z);
					const float tE = EdgeInterp(h_se, h_ne, seaLevelMeters);
					ex[1] = static_cast<float>(x + 1);
					ez[1] = static_cast<float>(z) + tE;
					const float tN = EdgeInterp(h_nw, h_ne, seaLevelMeters);
					ex[2] = static_cast<float>(x) + tN;
					ez[2] = static_cast<float>(z + 1);
					const float tW = EdgeInterp(h_sw, h_nw, seaLevelMeters);
					ex[3] = static_cast<float>(x);
					ez[3] = static_cast<float>(z) + tW;
				}

				for (int k = 0; k < 2; ++k)
				{
					const EdgePair pair = kCases[code][k];
					if (pair.e0 < 0 || pair.e1 < 0) continue;
					float ax, az, bx, bz;
					GridToWorld(grid, ex[pair.e0], ez[pair.e0], ax, az);
					GridToWorld(grid, ex[pair.e1], ez[pair.e1], bx, bz);
					pushSegment(ax, az, bx, bz);
				}
			}
		}

		return out;
	}
}
