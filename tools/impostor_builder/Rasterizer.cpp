/// M45.4 — Implémentation du mini-rasterizer CPU (barycentrique + z-buffer).
/// FORMAT v2 : trois atlas (albedo/normal/orm), échantillonnage de texture
/// baseColor bilinéaire (wrap REPEAT), alpha cutout, profondeur relative.

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

		/// Replie une coordonnée de texture continue en mode REPEAT vers [0,dim).
		/// \param c   Coordonnée en texels (peut être négative ou > dim).
		/// \param dim Dimension de l'axe (largeur ou hauteur).
		/// \return Index entier dans [0,dim-1].
		int WrapRepeat(int c, int dim)
		{
			if (dim <= 0) return 0;
			int r = c % dim;
			if (r < 0) r += dim;
			return r;
		}

		/// Échantillonne une texture RGBA8 en bilinéaire avec wrap REPEAT.
		/// \param tex  Pixels RGBA8 (origine haut-gauche).
		/// \param w    Largeur en texels.
		/// \param h    Hauteur en texels.
		/// \param u    Coordonnée U (0=gauche, 1=droite ; non bornée -> REPEAT).
		/// \param v    Coordonnée V (0=haut, 1=bas selon convention glTF).
		/// \param out  Couleur échantillonnée RGBA en [0,1].
		void SampleBilinear(const uint8_t* tex, int w, int h, float u, float v, float out[4])
		{
			// Espace texel centré (-0.5 pour le centre du texel).
			const float fx = u * static_cast<float>(w) - 0.5f;
			const float fy = v * static_cast<float>(h) - 0.5f;
			const int x0 = static_cast<int>(std::floor(fx));
			const int y0 = static_cast<int>(std::floor(fy));
			const float tx = fx - static_cast<float>(x0);
			const float ty = fy - static_cast<float>(y0);

			const int xa = WrapRepeat(x0, w);
			const int xb = WrapRepeat(x0 + 1, w);
			const int ya = WrapRepeat(y0, h);
			const int yb = WrapRepeat(y0 + 1, h);

			auto texel = [&](int x, int y, int c) -> float {
				return static_cast<float>(tex[(static_cast<size_t>(y) * w + x) * 4 + c]) / 255.0f;
			};

			for (int c = 0; c < 4; ++c)
			{
				const float top    = texel(xa, ya, c) * (1.0f - tx) + texel(xb, ya, c) * tx;
				const float bottom = texel(xa, yb, c) * (1.0f - tx) + texel(xb, yb, c) * tx;
				out[c] = top * (1.0f - ty) + bottom * ty;
			}
		}
	}

	void ClearTile(const RasterTarget& target, std::vector<float>& zbuf)
	{
		const uint32_t ts = target.tileSize;
		if (ts == 0 || target.albedo == nullptr || target.normal == nullptr ||
		    target.orm == nullptr)
			return;

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
				target.orm[px + 0] = 0; target.orm[px + 1] = 0;
				target.orm[px + 2] = 0; target.orm[px + 3] = 0;
			}
		}
	}

	void RasterizeSubMesh(const std::vector<RasterVertex>& verts,
	                      const std::vector<uint32_t>& indices,
	                      const float viewProj[16],
	                      const RasterTarget& target,
	                      std::vector<float>& zbuf,
	                      const RasterMaterial& mat,
	                      float depthNear,
	                      float depthFar)
	{
		const uint32_t ts = target.tileSize;
		if (ts == 0 || target.albedo == nullptr || target.normal == nullptr ||
		    target.orm == nullptr)
			return;

		const uint32_t baseX = target.tileX * ts;
		const uint32_t baseY = target.tileY * ts;
		const float fts = static_cast<float>(ts);

		// Étendue de profondeur pour normaliser la depth relative (atlas normal.a).
		float depthRange = depthFar - depthNear;
		if (std::fabs(depthRange) < 1e-8f) depthRange = 1.0f;

		const bool hasTex = (mat.baseColorRGBA != nullptr && mat.bcW > 0 && mat.bcH > 0);
		// ORM constant par sous-mesh (pas de texture ORM échantillonnée en v2).
		const uint8_t ormAo = 255;
		const uint8_t ormR  = ToU8(mat.roughness);
		const uint8_t ormM  = ToU8(mat.metallic);

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

			// Ortho : w == 1, mais on divise par w par robustesse.
			const float w0 = (c0[3] != 0.0f) ? c0[3] : 1.0f;
			const float w1 = (c1[3] != 0.0f) ? c1[3] : 1.0f;
			const float w2 = (c2[3] != 0.0f) ? c2[3] : 1.0f;

			const float sx0 = (c0[0] / w0 * 0.5f + 0.5f) * fts;
			const float sy0 = (c0[1] / w0 * 0.5f + 0.5f) * fts;
			const float sx1 = (c1[0] / w1 * 0.5f + 0.5f) * fts;
			const float sy1 = (c1[1] / w1 * 0.5f + 0.5f) * fts;
			const float sx2 = (c2[0] / w2 * 0.5f + 0.5f) * fts;
			const float sy2 = (c2[1] / w2 * 0.5f + 0.5f) * fts;

			const float z0 = c0[2] / w0;
			const float z1 = c1[2] / w1;
			const float z2 = c2[2] / w2;

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

			const float area = (sx1 - sx0) * (sy2 - sy0) - (sx2 - sx0) * (sy1 - sy0);
			if (std::fabs(area) < 1e-8f)
				continue; // triangle dégénéré
			const float invArea = 1.0f / area;

			for (int py = minY; py <= maxY; ++py)
			{
				for (int px = minX; px <= maxX; ++px)
				{
					const float fx = static_cast<float>(px) + 0.5f;
					const float fy = static_cast<float>(py) + 0.5f;

					float w0b = ((sx1 - fx) * (sy2 - fy) - (sx2 - fx) * (sy1 - fy)) * invArea;
					float w1b = ((sx2 - fx) * (sy0 - fy) - (sx0 - fx) * (sy2 - fy)) * invArea;
					float w2b = 1.0f - w0b - w1b;

					if (w0b < 0.0f || w1b < 0.0f || w2b < 0.0f)
						continue;

					const float depth = w0b * z0 + w1b * z1 + w2b * z2;

					const size_t zi = static_cast<size_t>(py) * ts + px;
					if (depth >= zbuf[zi])
						continue; // déjà un fragment plus proche

					// --- Albedo : couleur sommet × facteur, optionnellement texturé.
					float baseR = w0b * v0.color[0] + w1b * v1.color[0] + w2b * v2.color[0];
					float baseG = w0b * v0.color[1] + w1b * v1.color[1] + w2b * v2.color[1];
					float baseB = w0b * v0.color[2] + w1b * v1.color[2] + w2b * v2.color[2];
					float baseA = w0b * v0.color[3] + w1b * v1.color[3] + w2b * v2.color[3];

					float texA = 1.0f;
					if (hasTex)
					{
						const float u = w0b * v0.uv[0] + w1b * v1.uv[0] + w2b * v2.uv[0];
						const float vv = w0b * v0.uv[1] + w1b * v1.uv[1] + w2b * v2.uv[1];
						float tex[4];
						SampleBilinear(mat.baseColorRGBA, mat.bcW, mat.bcH, u, vv, tex);
						// Combine texture × couleur sommet (le feuillage encode souvent
						// la teinte dans COLOR_0). L'alpha de couverture vient de la
						// texture (canal alpha du PNG) modulé par la couleur sommet.
						baseR *= tex[0]; baseG *= tex[1]; baseB *= tex[2];
						texA = tex[3];
					}

					// albedo.rgb = base × baseColorFactor.rgb
					float albR = baseR * mat.baseColorFactor[0];
					float albG = baseG * mat.baseColorFactor[1];
					float albB = baseB * mat.baseColorFactor[2];
					// coverage = texAlpha × colorAlpha × baseColorFactor.a
					const float coverage = texA * baseA * mat.baseColorFactor[3];

					// Alpha cutout (feuillage BLEND/MASK) : sous le seuil -> non couvert.
					if (mat.alphaCutout && coverage < 0.5f)
						continue;

					// Le fragment couvre : on engage le z-buffer maintenant.
					zbuf[zi] = depth;

					// Normale monde interpolée + renormalisation.
					float nx = w0b * v0.normal[0] + w1b * v1.normal[0] + w2b * v2.normal[0];
					float ny = w0b * v0.normal[1] + w1b * v1.normal[1] + w2b * v2.normal[1];
					float nz = w0b * v0.normal[2] + w1b * v1.normal[2] + w2b * v2.normal[2];
					const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
					if (nlen > 1e-6f) { nx /= nlen; ny /= nlen; nz /= nlen; }

					// Profondeur relative normalisée [0,1] sur l'étendue de la vue.
					const float depthRel = (depth - depthNear) / depthRange;

					const uint32_t ax = baseX + static_cast<uint32_t>(px);
					const uint32_t ay = baseY + static_cast<uint32_t>(py);
					const size_t off = (static_cast<size_t>(ay) * target.atlasWidth + ax) * 4;

					// Atlas albedo : rgb = albedo (sRGB), a = couverture (255).
					target.albedo[off + 0] = ToU8(albR);
					target.albedo[off + 1] = ToU8(albG);
					target.albedo[off + 2] = ToU8(albB);
					target.albedo[off + 3] = 255;

					// Atlas normal : rgb = n*0.5+0.5, a = depth relative.
					target.normal[off + 0] = ToU8(nx * 0.5f + 0.5f);
					target.normal[off + 1] = ToU8(ny * 0.5f + 0.5f);
					target.normal[off + 2] = ToU8(nz * 0.5f + 0.5f);
					target.normal[off + 3] = ToU8(depthRel);

					// Atlas ORM : r = AO, g = roughness, b = metallic, a = 255.
					target.orm[off + 0] = ormAo;
					target.orm[off + 1] = ormR;
					target.orm[off + 2] = ormM;
					target.orm[off + 3] = 255;
				}
			}
		}
	}
}
