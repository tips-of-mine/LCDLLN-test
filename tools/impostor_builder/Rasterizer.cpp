/// M45.4 — Implémentation du mini-rasterizer CPU (barycentrique + z-buffer).

#include "Rasterizer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tools::impostor_builder
{
	namespace
	{
		/// Multiplie un point (x,y,z,1) par une matrice column-major 16 floats.
		/// Renvoie les 4 composantes homogènes dans out[4].
		void TransformPoint(const float m[16], float x, float y, float z, float out[4])
		{
			out[0] = m[0] * x + m[4] * y + m[8]  * z + m[12];
			out[1] = m[1] * x + m[5] * y + m[9]  * z + m[13];
			out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
			out[3] = m[3] * x + m[7] * y + m[11] * z + m[15];
		}

		/// Convertit un float [0,1] (clampé) en octet [0,255].
		uint8_t ToU8(float v)
		{
			v = std::clamp(v, 0.0f, 1.0f);
			return static_cast<uint8_t>(v * 255.0f + 0.5f);
		}
	}

	void RasterizeTile(const std::vector<RasterVertex>& verts,
	                   const std::vector<uint32_t>& indices,
	                   const float viewProj[16],
	                   const RasterTarget& target,
	                   std::vector<float>& zbuf)
	{
		const uint32_t ts = target.tileSize;
		if (ts == 0 || target.albedo == nullptr || target.normal == nullptr)
			return;

		// Réinitialise le z-buffer de la tile et efface la tile (transparent).
		zbuf.assign(static_cast<size_t>(ts) * ts, std::numeric_limits<float>::infinity());
		const uint32_t baseX = target.tileX * ts;
		const uint32_t baseY = target.tileY * ts;
		for (uint32_t y = 0; y < ts; ++y)
		{
			for (uint32_t x = 0; x < ts; ++x)
			{
				const size_t px = (static_cast<size_t>(baseY + y) * target.atlasWidth + (baseX + x)) * 4;
				target.albedo[px + 0] = 0; target.albedo[px + 1] = 0;
				target.albedo[px + 2] = 0; target.albedo[px + 3] = 0;
				target.normal[px + 0] = 0; target.normal[px + 1] = 0;
				target.normal[px + 2] = 0; target.normal[px + 3] = 0;
			}
		}

		const float fts = static_cast<float>(ts);

		for (size_t t = 0; t + 2 < indices.size(); t += 3)
		{
			const uint32_t i0 = indices[t + 0];
			const uint32_t i1 = indices[t + 1];
			const uint32_t i2 = indices[t + 2];
			if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
				continue;

			const RasterVertex& v0 = verts[i0];
			const RasterVertex& v1 = verts[i1];
			const RasterVertex& v2 = verts[i2];

			// Projection en clip space puis NDC.
			float c0[4], c1[4], c2[4];
			TransformPoint(viewProj, v0.pos[0], v0.pos[1], v0.pos[2], c0);
			TransformPoint(viewProj, v1.pos[0], v1.pos[1], v1.pos[2], c1);
			TransformPoint(viewProj, v2.pos[0], v2.pos[1], v2.pos[2], c2);

			// Ortho : w == 1 (pas de division perspective réelle), mais on
			// divise quand même par w par robustesse.
			const float w0 = (c0[3] != 0.0f) ? c0[3] : 1.0f;
			const float w1 = (c1[3] != 0.0f) ? c1[3] : 1.0f;
			const float w2 = (c2[3] != 0.0f) ? c2[3] : 1.0f;

			// NDC -> coordonnées écran tile [0,ts]. NDC x,y dans [-1,1].
			const float sx0 = (c0[0] / w0 * 0.5f + 0.5f) * fts;
			const float sy0 = (c0[1] / w0 * 0.5f + 0.5f) * fts;
			const float sx1 = (c1[0] / w1 * 0.5f + 0.5f) * fts;
			const float sy1 = (c1[1] / w1 * 0.5f + 0.5f) * fts;
			const float sx2 = (c2[0] / w2 * 0.5f + 0.5f) * fts;
			const float sy2 = (c2[1] / w2 * 0.5f + 0.5f) * fts;

			const float z0 = c0[2] / w0;
			const float z1 = c1[2] / w1;
			const float z2 = c2[2] / w2;

			// Bounding box écran clippée aux bords de la tile.
			int minX = static_cast<int>(std::floor(std::min({sx0, sx1, sx2})));
			int maxX = static_cast<int>(std::ceil (std::max({sx0, sx1, sx2})));
			int minY = static_cast<int>(std::floor(std::min({sy0, sy1, sy2})));
			int maxY = static_cast<int>(std::ceil (std::max({sy0, sy1, sy2})));
			minX = std::max(minX, 0);
			minY = std::max(minY, 0);
			maxX = std::min(maxX, static_cast<int>(ts) - 1);
			maxY = std::min(maxY, static_cast<int>(ts) - 1);
			if (minX > maxX || minY > maxY)
				continue;

			// Aire signée (×2) pour les coordonnées barycentriques.
			const float area = (sx1 - sx0) * (sy2 - sy0) - (sx2 - sx0) * (sy1 - sy0);
			if (std::fabs(area) < 1e-8f)
				continue; // triangle dégénéré
			const float invArea = 1.0f / area;

			for (int py = minY; py <= maxY; ++py)
			{
				for (int px = minX; px <= maxX; ++px)
				{
					// Centre du pixel.
					const float fx = static_cast<float>(px) + 0.5f;
					const float fy = static_cast<float>(py) + 0.5f;

					// Poids barycentriques.
					float w0b = ((sx1 - fx) * (sy2 - fy) - (sx2 - fx) * (sy1 - fy)) * invArea;
					float w1b = ((sx2 - fx) * (sy0 - fy) - (sx0 - fx) * (sy2 - fy)) * invArea;
					float w2b = 1.0f - w0b - w1b;

					// Couverture : tous les poids du même signe que l'aire.
					if (w0b < 0.0f || w1b < 0.0f || w2b < 0.0f)
						continue;

					const float depth = w0b * z0 + w1b * z1 + w2b * z2;

					const size_t zi = static_cast<size_t>(py) * ts + px;
					if (depth >= zbuf[zi])
						continue; // déjà un fragment plus proche
					zbuf[zi] = depth;

					// Interpolation couleur.
					float r = w0b * v0.color[0] + w1b * v1.color[0] + w2b * v2.color[0];
					float g = w0b * v0.color[1] + w1b * v1.color[1] + w2b * v2.color[1];
					float b = w0b * v0.color[2] + w1b * v1.color[2] + w2b * v2.color[2];
					float a = w0b * v0.color[3] + w1b * v1.color[3] + w2b * v2.color[3];

					// Interpolation normale + renormalisation.
					float nx = w0b * v0.normal[0] + w1b * v1.normal[0] + w2b * v2.normal[0];
					float ny = w0b * v0.normal[1] + w1b * v1.normal[1] + w2b * v2.normal[1];
					float nz = w0b * v0.normal[2] + w1b * v1.normal[2] + w2b * v2.normal[2];
					const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
					if (nlen > 1e-6f) { nx /= nlen; ny /= nlen; nz /= nlen; }

					const uint32_t ax = baseX + static_cast<uint32_t>(px);
					const uint32_t ay = baseY + static_cast<uint32_t>(py);
					const size_t off = (static_cast<size_t>(ay) * target.atlasWidth + ax) * 4;

					target.albedo[off + 0] = ToU8(r);
					target.albedo[off + 1] = ToU8(g);
					target.albedo[off + 2] = ToU8(b);
					target.albedo[off + 3] = ToU8(a);

					// Encodage monde -> [0,1]. Alpha = masque de couverture.
					target.normal[off + 0] = ToU8(nx * 0.5f + 0.5f);
					target.normal[off + 1] = ToU8(ny * 0.5f + 0.5f);
					target.normal[off + 2] = ToU8(nz * 0.5f + 0.5f);
					target.normal[off + 3] = 255;
				}
			}
		}
	}
}
