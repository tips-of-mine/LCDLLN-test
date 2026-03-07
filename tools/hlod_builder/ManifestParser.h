#pragma once

#include "Types.h"

#include <string>
#include <vector>

namespace tools::hlod_builder
{
	/// Parses a simple text manifest: "content <path>", then "chunk <cx> <cz>" followed by "mesh <path> <materialId> <x> <y> <z>" lines.
	/// \param content Path to manifest file (used for resolving relative paths if needed).
	/// \param text Full manifest text.
	/// \param contentDirOut Filled with content root path from manifest.
	/// \return Chunks with instances; empty on parse error.
	std::vector<ChunkInput> ParseManifest(const std::string& contentDirDefault, const std::string& text, std::string& contentDirOut);
}
