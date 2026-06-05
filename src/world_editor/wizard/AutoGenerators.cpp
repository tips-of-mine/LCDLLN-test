// M100.50 — Implémentation des auto-générateurs (déterministes, seedés).

#include "src/world_editor/wizard/AutoGenerators.h"

#include <random>

namespace engine::editor::world::wizard
{
	namespace
	{
		struct ReliefProfile { int points; float amplitude; float sinuosity; };

		ReliefProfile ProfileFor(const std::string& relief)
		{
			if (relief == "plains")    return { 2, 5.0f,   2.0f };
			if (relief == "hills")     return { 3, 40.0f,  8.0f };
			if (relief == "mountains") return { 5, 220.0f, 20.0f };
			if (relief == "escarped")  return { 5, 480.0f, 35.0f };
			return { 3, 40.0f, 8.0f }; // défaut = hills.
		}
	}

	std::vector<engine::math::Vec3> GenerateMountainPolyline(const std::string& relief, uint32_t seed)
	{
		const ReliefProfile p = ProfileFor(relief);
		std::mt19937_64 rng(static_cast<uint64_t>(seed));
		std::uniform_real_distribution<float> jitterXZ(-1.0f, 1.0f);
		std::uniform_real_distribution<float> heightFrac(0.6f, 1.0f);

		std::vector<engine::math::Vec3> out;
		out.reserve(static_cast<size_t>(p.points));
		const float span = 512.0f;
		const float step = p.points > 1 ? span / static_cast<float>(p.points - 1) : 0.0f;
		for (int i = 0; i < p.points; ++i)
		{
			const float baseX = static_cast<float>(i) * step;
			const float x = baseX + jitterXZ(rng) * p.sinuosity;
			const float z = 256.0f + jitterXZ(rng) * p.sinuosity;
			const float y = p.amplitude * heightFrac(rng);
			out.push_back({ x, y, z });
		}
		return out;
	}
}
