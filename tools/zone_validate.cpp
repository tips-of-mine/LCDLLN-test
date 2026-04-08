#include "engine/world/ZoneDescriptor.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::cerr << "usage: zone_validate <path/to/zone.json>\n";
		return 2;
	}

	const std::string zonePathStr = argv[1];
	std::ifstream in(zonePathStr, std::ios::binary);
	if (!in)
	{
		std::cerr << "zone_validate: cannot open file: " << zonePathStr << '\n';
		return 1;
	}
	std::ostringstream oss;
	oss << in.rdbuf();
	const std::string json = oss.str();

	engine::world::ZoneDescriptorV1 desc{};
	std::string err;
	if (!engine::world::ParseZoneDescriptorJson(json, desc, err))
	{
		std::cerr << "zone_validate: parse error: " << err << '\n';
		return 1;
	}

	std::string verr;
	if (!engine::world::ValidateZoneHeightmapAgainstFile(std::filesystem::path(zonePathStr), desc, verr))
	{
		std::cerr << "zone_validate: heightmap: " << verr << '\n';
		return 1;
	}

	std::cout << "zone_validate: OK zone_id=" << desc.zone_id;
	if (desc.has_heightmap_dims)
		std::cout << " heightmap=" << desc.heightmap_width << 'x' << desc.heightmap_height;
	std::cout << '\n';
	return 0;
}
