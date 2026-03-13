#pragma once

#include "engine/core/Config.h"

#include <string>
#include <string_view>
#include <vector>

namespace tools::zone_builder
{
	struct LayoutInstance
	{
		std::string guid;
		std::string gltfPath;
		double positionX = 0.0;
		double positionY = 0.0;
		double positionZ = 0.0;
	};

	struct LayoutDocument
	{
		int version = 0;
		std::string relativePath;
		std::vector<LayoutInstance> instances;
	};

	/// Load and validate the minimal versioned zone layout schema from content.
	bool LoadLayoutDocument(const engine::core::Config& config, std::string_view relativeLayoutPath, LayoutDocument& outDocument, std::string& outError);
}
