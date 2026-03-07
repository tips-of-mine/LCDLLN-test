/// Zone builder (M10.5 / M11.2): writes chunk packages (chunk.meta + geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin).
/// Usage: zone_builder --output <dir> --chunk <x> <z>
/// Example: zone_builder --output build/zone_0/chunks --chunk 0 0
///   creates build/zone_0/chunks/chunk_0_0/chunk.meta, geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin

#include "ChunkPackageWriter.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	std::string outputDir = "build/zone_0/chunks";
	int32_t chunkX = 0;
	int32_t chunkZ = 0;
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = argv[i];
		if (arg == "--output" && i + 1 < argc)
			outputDir = argv[++i];
		else if (arg == "--chunk" && i + 2 < argc)
		{
			chunkX = static_cast<int32_t>(std::atoi(argv[++i]));
			chunkZ = static_cast<int32_t>(std::atoi(argv[++i]));
		}
	}
	std::string chunkDir = outputDir + "/chunk_" + std::to_string(chunkX) + "_" + std::to_string(chunkZ);
	if (!tools::zone_builder::WriteChunkPackage(chunkDir, chunkX, chunkZ))
	{
		std::cerr << "zone_builder: failed to write chunk package to " << chunkDir << "\n";
		return 1;
	}
	std::cout << "zone_builder: wrote " << chunkDir << " (chunk.meta, geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin)\n";
	return 0;
}
