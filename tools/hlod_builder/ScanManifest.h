#pragma once

#include "Types.h"

#include <string>
#include <vector>

namespace tools::hlod_builder
{
	/// Scans contentDir/meshes/ for *.mesh files and builds ChunkInput entries.
	/// Each mesh is placed on a uniform grid within chunk (0,0) at y=0.
	/// Grid spacing is kGridSpacing metres. Used by --scan mode.
	std::vector<ChunkInput> ScanContentDir(const std::string& contentDir);
}