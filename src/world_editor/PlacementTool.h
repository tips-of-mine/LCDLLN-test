#pragma once

// M100.17 — Outil de placement universel (ghost, snapping, modes). La logique
// de génération d'instances (BuildInstances) est PURE et testable ; le rendu
// ImGui de la palette de propriétés est guardé Windows.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "src/client/world/instances/PropInstances.h"
#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	class PlacementDocument;

	enum class PlacementMode : uint8_t { Single = 0, DragLine = 1, Scatter = 2 };
	enum class PlacementSnap : uint8_t { Ground = 0, Grid = 1, Face = 2 };
	enum class PlacementAlign : uint8_t { TerrainNormal = 0, WorldUp = 1 };

	struct PlacementParams
	{
		std::string assetPath;
		PlacementMode mode = PlacementMode::Single;
		PlacementSnap snap = PlacementSnap::Ground;
		PlacementAlign align = PlacementAlign::TerrainNormal;
		float gridSizeMeters = 1.0f;
		float dragLineSpacing = 2.0f;
		float scatterRadius = 5.0f;
		uint32_t scatterCount = 12;
		float rotMinDeg = 0.0f, rotMaxDeg = 360.0f;
		float scaleMin = 0.95f, scaleMax = 1.05f;
		uint64_t rngSeed = 1;
		uint32_t layerTag = static_cast<uint32_t>(engine::world::instances::PlacementLayer::Props);
	};

	/// Hash FNV-1a 32 bits d'un chemin d'asset (stable, identique zone_builder).
	uint32_t HashAssetPath(const std::string& path);

	class PlacementTool
	{
	public:
		void SetParams(const PlacementParams& p) { m_params = p; }
		PlacementParams& Params() { return m_params; }
		const PlacementParams& Params() const { return m_params; }

		/// Génère les instances à poser selon le mode, depuis `start` (et `end`
		/// pour drag-line). `terrainNormal` sert au snap d'orientation. Les
		/// instanceId sont alloués sur `doc`. PURE et déterministe (seed).
		std::vector<engine::world::instances::PropInstance> BuildInstances(
			const engine::math::Vec3& start, const engine::math::Vec3& end,
			const engine::math::Vec3& terrainNormal, PlacementDocument& doc) const;

		/// Rend le panneau de propriétés (ImGui, Windows uniquement).
		void Render();

	private:
		PlacementParams m_params;
	};
}
