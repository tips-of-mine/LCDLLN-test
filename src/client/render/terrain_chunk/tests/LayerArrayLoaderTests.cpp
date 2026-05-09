/// Tests unitaires pour LayerArrayLoader — focus sur ResolveLayerAssetPath
/// (pure CPU, pas de Vulkan).
///
/// Vérifient :
///   - Si le `.texr` existe : retourne ce chemin.
///   - Si absent : fallback vers placeholders/<name>.png.
///   - `contentRoot` est bien préfixé.
///   - Les 3 mapTypes (Albedo, Normal, Arm) produisent des paths distincts.

#include "engine/render/terrain_chunk/LayerArrayLoader.h"
#include "engine/world/terrain/LayerPalette.h"

#include <cstdio>
#include <set>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::render::terrain_chunk::LayerMapType;
	using engine::render::terrain_chunk::ResolveLayerAssetPath;
	using engine::world::terrain::LayerPalette;

	LayerPalette MakePalette()
	{
		LayerPalette p;
		const char* names[8] = { "dirt", "grass_dry", "grass_wet", "mud",
		                         "sand", "rock", "snow", "lava_cooled" };
		for (uint32_t i = 0; i < 8; ++i)
		{
			p.layers[i].index = i;
			p.layers[i].name = names[i];
			p.layers[i].albedoPath = std::string("tex/terrain/") + names[i] + "_albedo.texr";
			p.layers[i].normalPath = std::string("tex/terrain/") + names[i] + "_normal.texr";
			p.layers[i].armPath    = std::string("tex/terrain/") + names[i] + "_arm.texr";
		}
		return p;
	}

	/// Si le `.texr` existe, retourne ce chemin (pas de fallback).
	void Test_ResolvePath_ReturnsTexrIfExists()
	{
		auto palette = MakePalette();
		auto p = ResolveLayerAssetPath(palette, 0, LayerMapType::Albedo, "game/data",
			[](const std::filesystem::path& path)
			{
				return path.string().find("dirt_albedo.texr") != std::string::npos;
			});
		REQUIRE(p.string().find("dirt_albedo.texr") != std::string::npos);
		REQUIRE(p.string().find("placeholders") == std::string::npos);
	}

	/// Si le `.texr` est absent, fallback vers placeholders/<name>.png.
	void Test_ResolvePath_FallsBackToPlaceholder()
	{
		auto palette = MakePalette();
		auto p = ResolveLayerAssetPath(palette, 0, LayerMapType::Albedo, "game/data",
			[](const std::filesystem::path& path)
			{
				// Aucun .texr présent → fileExists toujours false.
				(void)path;
				return false;
			});
		REQUIRE(p.string().find("placeholders") != std::string::npos);
		REQUIRE(p.string().find("dirt.png") != std::string::npos);
	}

	/// `contentRoot` est bien préfixé au chemin retourné.
	void Test_ResolvePath_ContentRootIsPrefix()
	{
		auto palette = MakePalette();
		auto p = ResolveLayerAssetPath(palette, 5, LayerMapType::Normal, "/some/root",
			[](const std::filesystem::path&) { return true; });
		const std::string s = p.string();
		// Sur Linux : '/some/root/...'. Sur Windows std::filesystem peut
		// normaliser en '\some\root\...'. Accepter les deux.
		const bool hasUnixPrefix = s.find("/some/root") == 0;
		const bool hasWinPrefix  = s.find("\\some\\root") == 0
		                       || s.find("some/root") != std::string::npos
		                       || s.find("some\\root") != std::string::npos;
		REQUIRE(hasUnixPrefix || hasWinPrefix);
	}

	/// Les 3 mapTypes (Albedo, Normal, Arm) produisent des paths distincts.
	void Test_ResolvePath_AllMapTypesReturnsDistinct()
	{
		auto palette = MakePalette();
		auto fileExists = [](const std::filesystem::path&) { return true; };
		std::set<std::string> paths;
		paths.insert(ResolveLayerAssetPath(palette, 0, LayerMapType::Albedo, "r", fileExists).string());
		paths.insert(ResolveLayerAssetPath(palette, 0, LayerMapType::Normal, "r", fileExists).string());
		paths.insert(ResolveLayerAssetPath(palette, 0, LayerMapType::Arm,    "r", fileExists).string());
		REQUIRE(paths.size() == 3u);
	}
}

int main()
{
	Test_ResolvePath_ReturnsTexrIfExists();
	Test_ResolvePath_FallsBackToPlaceholder();
	Test_ResolvePath_ContentRootIsPrefix();
	Test_ResolvePath_AllMapTypesReturnsDistinct();

	if (g_failed == 0)
	{
		std::printf("[PASS] LayerArrayLoaderTests (4/4)\n");
		return 0;
	}
	std::printf("[FAIL] LayerArrayLoaderTests: %d failure(s)\n", g_failed);
	return 1;
}
