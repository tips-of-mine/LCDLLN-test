#pragma once

// M100.18 — Outil de peinture de densité végétale. Rendu ImGui guardé Windows.
// (La résolution densité → positions Poisson-disk vit dans le sampler pur
// PoissonDiskSampler ; le câblage terrain/brush complet est itératif.)

#include <cstdint>
#include <string>

namespace engine::editor::world
{
	enum class FoliagePaintMode : uint8_t { AddDensity = 0, EraseDensity = 1 };

	struct FoliagePaintParams
	{
		std::string activeAssetId;
		float brushRadius = 6.0f;
		float densityTarget = 0.7f;
		float strength = 0.5f;
		float falloff = 0.7f;
		float minRadius = 0.4f; // espacement Poisson-disk par défaut
		FoliagePaintMode mode = FoliagePaintMode::AddDensity;
	};

	class FoliagePaintTool
	{
	public:
		FoliagePaintParams& Params() { return m_params; }
		const FoliagePaintParams& Params() const { return m_params; }
		void Render();

	private:
		FoliagePaintParams m_params;
	};
}
