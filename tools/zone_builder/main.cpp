/// Zone builder (M11.1 / M11.2): imports a versioned layout.json, loads referenced glTF assets,
/// chunks instances into `build/zone_x/chunks/chunk_i_j/*`, and keeps legacy chunk packaging mode.
/// Usage:
///   zone_builder --layout zones/layout.json [--config config.json]
///   zone_builder --output build/zone_0/chunks --chunk 0 0

#include "ChunkPackageWriter.h"
#include "GltfImporter.h"
#include "LayoutImporter.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

namespace tools::zone_builder
{
	namespace
	{
		struct CliOptions
		{
			std::string configPath = "config.json";
			std::string layoutPath;
			std::string outputPath;
			int32_t chunkX = 0;
			int32_t chunkZ = 0;
			bool runLayoutImport = false;
			bool runChunkPackaging = false;
			bool showHelp = false;
			bool hasChunkCoordinates = false;
		};

		/// Emit CLI usage for the layout import mode and the legacy chunk packaging mode.
		void LogUsage()
		{
			LOG_INFO(Core, "[ZoneBuilder] Usage: zone_builder --layout <relative-layout.json> [--output <build/zone_x>] [--config <config.json>]");
			LOG_INFO(Core, "[ZoneBuilder] Legacy: zone_builder --output <dir> --chunk <x> <z>");
		}

		/// Parse CLI arguments into one of the supported execution modes.
		bool ParseArguments(int argc, char** argv, CliOptions& outOptions, std::string& outError)
		{
			for (int index = 1; index < argc; ++index)
			{
				const std::string argument = argv[index] != nullptr ? argv[index] : "";
				if (argument == "--help" || argument == "-h")
				{
					outOptions.showHelp = true;
					return true;
				}

				if (argument == "--config" && index + 1 < argc)
				{
					outOptions.configPath = argv[++index];
					continue;
				}

				if (argument == "--layout" && index + 1 < argc)
				{
					outOptions.layoutPath = argv[++index];
					outOptions.runLayoutImport = true;
					continue;
				}

				if (argument == "--output" && index + 1 < argc)
				{
					outOptions.outputPath = argv[++index];
					continue;
				}

				if (argument == "--chunk" && index + 2 < argc)
				{
					outOptions.chunkX = static_cast<int32_t>(std::atoi(argv[++index]));
					outOptions.chunkZ = static_cast<int32_t>(std::atoi(argv[++index]));
					outOptions.hasChunkCoordinates = true;
					outOptions.runChunkPackaging = true;
					continue;
				}

				outError = "unknown or incomplete argument: " + argument;
				return false;
			}

			if (!outOptions.runLayoutImport && !outOptions.runChunkPackaging)
			{
				outError = "no command selected";
				return false;
			}

			if (outOptions.runLayoutImport && outOptions.hasChunkCoordinates)
			{
				outError = "--chunk is only valid for legacy chunk packaging mode";
				return false;
			}

			return true;
		}

		/// Execute the legacy single-chunk package writer kept from the previous ticket flow.
		int RunChunkPackagingMode(const CliOptions& options)
		{
			const std::string outputDir = options.outputPath.empty() ? "build/zone_0/chunks" : options.outputPath;
			const std::string chunkDirectory = outputDir + "/chunk_" + std::to_string(options.chunkX) + "_" + std::to_string(options.chunkZ);
			LOG_INFO(Core, "[ZoneBuilder] Writing chunk package {}", chunkDirectory);
			if (!WriteChunkPackage(chunkDirectory, options.chunkX, options.chunkZ))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Chunk packaging FAILED (dir={}, chunk=({},{}))",
					chunkDirectory,
					options.chunkX,
					options.chunkZ);
				return 1;
			}

			LOG_INFO(Core, "[ZoneBuilder] Chunk packaging OK (dir={}, chunk=({},{}))",
				chunkDirectory,
				options.chunkX,
				options.chunkZ);
			return 0;
		}

		/// Load a layout, validate referenced glTF assets, then write chunked zone outputs.
		int RunLayoutImportMode(const engine::core::Config& config, const CliOptions& options)
		{
			LOG_WARN(Core, "[ZoneBuilder] tinygltf is unavailable in this repository; using a minimal built-in glTF JSON importer");

			LayoutDocument layout;
			std::string error;
			if (!LoadLayoutDocument(config, options.layoutPath, layout, error))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Layout import FAILED (layout={}, reason={})", options.layoutPath, error);
				return 1;
			}

			std::vector<std::string> uniqueAssetPaths;
			std::unordered_set<std::string> visitedPaths;
			uniqueAssetPaths.reserve(layout.instances.size());
			for (const LayoutInstance& instance : layout.instances)
			{
				if (visitedPaths.insert(instance.gltfPath).second)
				{
					uniqueAssetPaths.push_back(instance.gltfPath);
				}
			}

			std::vector<GltfAsset> assets;
			assets.reserve(uniqueAssetPaths.size());
			for (const std::string& assetPath : uniqueAssetPaths)
			{
				GltfAsset asset;
				if (!LoadGltfAsset(config, assetPath, asset, error))
				{
					LOG_ERROR(Core, "[ZoneBuilder] Layout import FAILED (layout={}, asset={}, reason={})",
						options.layoutPath,
						assetPath,
						error);
					return 1;
				}

				assets.emplace_back(std::move(asset));
			}

			LOG_INFO(Core, "[ZoneBuilder] Zone summary (layout={}, instances={}, uniqueAssets={})",
				options.layoutPath,
				layout.instances.size(),
				assets.size());

			for (const LayoutInstance& instance : layout.instances)
			{
				LOG_INFO(Core, "[ZoneBuilder] Instance guid={} gltf={} pos=({:.3f}, {:.3f}, {:.3f})",
					instance.guid,
					instance.gltfPath,
					instance.positionX,
					instance.positionY,
					instance.positionZ);
			}

			for (const GltfAsset& asset : assets)
			{
				LOG_INFO(Core, "[ZoneBuilder] Asset {} meshes={} materials={}",
					asset.relativePath,
					asset.meshNames.size(),
					asset.materialNames.size());

				for (const std::string& meshName : asset.meshNames)
				{
					LOG_INFO(Core, "[ZoneBuilder]   mesh={}", meshName);
				}

				for (const std::string& materialName : asset.materialNames)
				{
					LOG_INFO(Core, "[ZoneBuilder]   material={}", materialName);
				}
			}

			const std::string outputRootDir = options.outputPath.empty() ? "build/zone_0" : options.outputPath;
			if (!WriteChunkedZoneOutputs(outputRootDir, config, layout, error))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Layout import FAILED (layout={}, output={}, reason={})",
					options.layoutPath,
					outputRootDir,
					error);
				return 1;
			}

			LOG_INFO(Core, "[ZoneBuilder] Layout import OK (layout={}, output={})", options.layoutPath, outputRootDir);
			return 0;
		}
	}
}

int main(int argc, char** argv)
{
	engine::core::LogSettings logSettings;
	logSettings.filePath = "zone_builder.log";
	logSettings.console = true;
	engine::core::Log::Init(logSettings);
	LOG_INFO(Core, "[ZoneBuilder] Init OK");

	tools::zone_builder::CliOptions options;
	std::string error;
	if (!tools::zone_builder::ParseArguments(argc, argv, options, error))
	{
		LOG_ERROR(Core, "[ZoneBuilder] Init FAILED: {}", error);
		tools::zone_builder::LogUsage();
		LOG_INFO(Core, "[ZoneBuilder] Destroyed");
		engine::core::Log::Shutdown();
		return 1;
	}

	if (options.showHelp)
	{
		tools::zone_builder::LogUsage();
		LOG_INFO(Core, "[ZoneBuilder] Destroyed");
		engine::core::Log::Shutdown();
		return 0;
	}

	const engine::core::Config config = engine::core::Config::Load(options.configPath, argc, argv);
	LOG_INFO(Core, "[ZoneBuilder] Config loaded (path={})", options.configPath);

	const int exitCode = options.runLayoutImport
		? tools::zone_builder::RunLayoutImportMode(config, options)
		: tools::zone_builder::RunChunkPackagingMode(options);

	LOG_INFO(Core, "[ZoneBuilder] Destroyed");
	engine::core::Log::Shutdown();
	return exitCode;
}
