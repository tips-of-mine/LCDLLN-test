#include "engine/world/ZoneDescriptor.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Fail(const char* msg)
{
	std::cerr << "FAIL: " << msg << '\n';
	std::exit(1);
}

bool WriteMinimalR16h(const std::filesystem::path& path, uint32_t w, uint32_t h)
{
	std::ofstream f(path, std::ios::binary);
	if (!f)
		return false;
	const uint32_t magic = 0x504D4148u;
	f.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
	f.write(reinterpret_cast<const char*>(&w), sizeof(w));
	f.write(reinterpret_cast<const char*>(&h), sizeof(h));
	const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
	std::vector<uint16_t> zeros(n, 0);
	f.write(reinterpret_cast<const char*>(zeros.data()), static_cast<std::streamsize>(zeros.size() * sizeof(uint16_t)));
	return static_cast<bool>(f);
}

} // namespace

int main()
{
	engine::world::ZoneDescriptorV1 z{};
	std::string err;

	const char* goodV1 = R"({
  "world_editor_format": 1,
  "zone_id": "lhynn_plains_01",
  "heightmap_r16h": "terrain/flat_2x2.r16h",
  "seed": 42,
  "texture_layers": ["grass", "rock"]
})";
	if (!engine::world::ParseZoneDescriptorJson(goodV1, z, err))
	{
		std::cerr << err << '\n';
		Fail("parse good v1");
	}
	if (z.zone_id != "lhynn_plains_01" || !z.has_seed || z.seed != 42 || z.texture_layers.size() != 2u)
		Fail("fields v1");
	if (z.has_heightmap_dims)
		Fail("v1 no dims by default");

	const std::filesystem::path zoneJson = std::filesystem::path("zones") / "demo" / "zone.json";
	const auto hm = engine::world::ResolveZoneHeightmapPath(zoneJson, z);
	const auto expect = std::filesystem::path("zones") / "demo" / "terrain" / "flat_2x2.r16h";
	if (hm != expect)
		Fail("ResolveZoneHeightmapPath");

	if (engine::world::ParseZoneDescriptorJson("{}", z, err))
		Fail("reject empty");

	if (engine::world::ParseZoneDescriptorJson(
	        R"({"world_editor_format":2,"zone_id":"x","heightmap_r16h":"a.r16h"})", z, err))
		Fail("reject format 2 without schema/dims");

	const char* goodV2 = R"({
  "world_editor_format": 2,
  "zone_schema": { "name": "lcdlln.zone", "version": 1 },
  "zone_id": "z2",
  "heightmap_r16h": "terrain/t.r16h",
  "heightmap_width": 2,
  "heightmap_height": 2
})";
	if (!engine::world::ParseZoneDescriptorJson(goodV2, z, err))
	{
		std::cerr << err << '\n';
		Fail("parse good v2");
	}
	if (!z.has_zone_schema || !z.has_heightmap_dims || z.heightmap_width != 2u || z.heightmap_height != 2u)
		Fail("v2 fields");

	const std::filesystem::path tmpRoot = std::filesystem::temp_directory_path() / "lcdlln_zone_descriptor_test";
	std::error_code ec;
	std::filesystem::remove_all(tmpRoot, ec);
	std::filesystem::create_directories(tmpRoot / "terrain");
	const std::filesystem::path zonePath = tmpRoot / "zone.json";
	if (!WriteMinimalR16h(tmpRoot / "terrain" / "t.r16h", 2u, 2u))
		Fail("write r16h");
	{
		std::ofstream zf(zonePath);
		zf << goodV2;
	}
	if (!engine::world::ValidateZoneHeightmapAgainstFile(zonePath, z, err))
	{
		std::cerr << err << '\n';
		Fail("validate v2 against file");
	}

	if (!WriteMinimalR16h(tmpRoot / "terrain" / "t.r16h", 3u, 3u))
		Fail("rewrite r16h 3x3");
	if (engine::world::ValidateZoneHeightmapAgainstFile(zonePath, z, err))
		Fail("expect mismatch 2x2 vs 3x3 file");

	std::cerr << "zone_descriptor_tests: OK\n";
	return 0;
}
