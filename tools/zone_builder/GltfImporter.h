#pragma once

#include "engine/core/Config.h"

#include <string>
#include <string_view>
#include <vector>

namespace tools::zone_builder
{
	struct GltfAsset
	{
		std::string relativePath;
		std::vector<std::string> meshNames;
		std::vector<std::string> materialNames;
	};

	/// Load a `.gltf` asset from content and extract mesh/material reference names.
	bool LoadGltfAsset(const engine::core::Config& config, std::string_view relativeGltfPath, GltfAsset& outAsset, std::string& outError);
}
