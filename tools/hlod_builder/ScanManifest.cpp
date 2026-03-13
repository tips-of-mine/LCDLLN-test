#include "ScanManifest.h"

#include <cmath>
#include <filesystem>

namespace tools::hlod_builder
{
	namespace
	{
		constexpr float kGridSpacing = 10.0f; ///< Metres between mesh instances on the auto-grid.
	}

	std::vector<ChunkInput> ScanContentDir(const std::string& contentDir)
	{
		namespace fs = std::filesystem;

		// Collect all *.mesh files found anywhere under contentDir/meshes/.
		const fs::path meshRoot = fs::path(contentDir) / "meshes";
		std::vector<std::string> meshPaths; // relative to contentDir
		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(meshRoot, ec))
		{
			if (ec) break;
			if (!entry.is_regular_file()) continue;
			if (entry.path().extension() != ".mesh") continue;

			// Store path relative to contentDir so it matches MeshLoader expectations.
			fs::path rel = fs::relative(entry.path(), fs::path(contentDir), ec);
			if (!ec && !rel.empty())
				meshPaths.push_back(rel.generic_string());
		}

		if (meshPaths.empty())
			return {};

		// Place all found meshes in a uniform grid within chunk (0,0).
		// Grid is square; spacing = kGridSpacing metres.
		const int32_t cols = static_cast<int32_t>(std::ceil(std::sqrt(static_cast<double>(meshPaths.size()))));

		ChunkInput chunk;
		chunk.chunkX = 0;
		chunk.chunkZ = 0;

		for (size_t i = 0; i < meshPaths.size(); ++i)
		{
			Instance inst;
			inst.meshPath   = meshPaths[i];
			inst.materialId = 0;
			inst.x = static_cast<float>(i % cols) * kGridSpacing;
			inst.y = 0.0f;
			inst.z = static_cast<float>(i / cols) * kGridSpacing;
			chunk.instances.push_back(std::move(inst));
		}

		return { std::move(chunk) };
	}
}