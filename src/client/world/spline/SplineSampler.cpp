// M100.29 — Implémentation de l'échantillonnage spline + helpers (purs).

#include "src/client/world/spline/SplineSampler.h"

namespace engine::world::spline
{
	using engine::math::Vec3;

	namespace
	{
		Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
		{
			const float t2 = t * t, t3 = t2 * t;
			// 0.5 * (2p1 + (-p0+p2)t + (2p0-5p1+4p2-p3)t² + (-p0+3p1-3p2+p3)t³)
			Vec3 a = p1 * 2.0f;
			Vec3 b = (p2 - p0) * t;
			Vec3 c = (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2;
			Vec3 d = (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3;
			return (a + b + c + d) * 0.5f;
		}
	} // namespace

	std::vector<Vec3> SampleCatmullRom(const std::vector<SplineNode>& nodes, bool closed, int samplesPerSegment)
	{
		std::vector<Vec3> out;
		const int m = static_cast<int>(nodes.size());
		if (m == 0) return out;
		if (m == 1) { out.push_back(nodes[0].position); return out; }
		if (samplesPerSegment < 1) samplesPerSegment = 1;

		auto nodeAt = [&](int i) -> Vec3
		{
			if (closed) { i = ((i % m) + m) % m; return nodes[static_cast<size_t>(i)].position; }
			if (i < 0) i = 0; if (i > m - 1) i = m - 1;
			return nodes[static_cast<size_t>(i)].position;
		};

		const int segs = closed ? m : (m - 1);
		for (int i = 0; i < segs; ++i)
		{
			const Vec3 p0 = nodeAt(i - 1), p1 = nodeAt(i), p2 = nodeAt(i + 1), p3 = nodeAt(i + 2);
			for (int j = 0; j < samplesPerSegment; ++j)
			{
				const float t = static_cast<float>(j) / static_cast<float>(samplesPerSegment);
				out.push_back(CatmullRom(p0, p1, p2, p3, t));
			}
		}
		if (!closed) out.push_back(nodes[static_cast<size_t>(m - 1)].position);
		return out;
	}

	std::vector<Vec3> GroundFit(const std::vector<Vec3>& pts, const HeightSampler& sampler)
	{
		std::vector<Vec3> out = pts;
		if (sampler)
			for (auto& p : out) p.y = sampler(p.x, p.z);
		return out;
	}

	void ApplyRoadWeight(std::array<uint8_t, 8>& weights, int roadLayer, uint8_t target)
	{
		if (roadLayer < 0 || roadLayer >= 8) return;
		const int t = (target > 255) ? 255 : static_cast<int>(target);
		const int rem = 255 - t;

		int otherSum = 0;
		for (int i = 0; i < 8; ++i) if (i != roadLayer) otherSum += weights[static_cast<size_t>(i)];

		if (otherSum > 0)
		{
			int acc = 0;
			for (int i = 0; i < 8; ++i)
			{
				if (i == roadLayer) continue;
				const int w = weights[static_cast<size_t>(i)] * rem / otherSum;
				weights[static_cast<size_t>(i)] = static_cast<uint8_t>(w);
				acc += w;
			}
			// La couche route absorbe le reliquat d'arrondi → somme exacte = 255.
			weights[static_cast<size_t>(roadLayer)] = static_cast<uint8_t>(255 - acc);
		}
		else
		{
			for (int i = 0; i < 8; ++i) weights[static_cast<size_t>(i)] = 0;
			weights[static_cast<size_t>(roadLayer)] = 255;
		}
	}
}
