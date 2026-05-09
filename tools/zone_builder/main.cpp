/// Zone builder (M11.1 / M11.2) : import d’un layout versionné, glTF de référence, découpage par chunk ;
/// mode legacy mono-chunk conservé.
/// Voir `zone_builder --help` et `docs/world_editor_zone_pipeline.md` §3.

#include <zone_builder/ChunkPackageWriter.h>
#include <zone_builder/GltfImporter.h>
#include <zone_builder/LayoutImporter.h>
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
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
			/// Optionnel (mode layout) : doit correspondre au dossier résolu `zones/<id>/` (même règle que Sanitize côté WE).
			std::string zoneIdCheck;
			int32_t chunkX = 0;
			int32_t chunkZ = 0;
			bool runLayoutImport = false;
			bool runChunkPackaging = false;
			bool showHelp = false;
			bool hasChunkCoordinates = false;
		};

		std::string SanitizeZoneIdCli(std::string_view raw)
		{
			std::string o;
			o.reserve(raw.size());
			for (char c : raw)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (std::isalnum(u) != 0)
				{
					o.push_back(static_cast<char>(std::tolower(u)));
				}
				else if (c == '_' || c == '-')
				{
					o.push_back('_');
				}
			}
			if (o.empty())
			{
				o = "zone";
			}
			return o;
		}

		void NormalizePathSlashesInPlace(std::string& s)
		{
			for (char& c : s)
			{
				if (c == '\\')
				{
					c = '/';
				}
			}
		}

		/// Racine d’écriture mode layout : défaut `<cwd>/build/zone_0` ; si `--output` commence par `zones/`, résolution via
		/// `paths.content` + `engine::platform::FileSystem::ResolveContentPath` ; sinon chemin relatif/absolu au cwd.
		std::filesystem::path ResolveLayoutOutputRoot(const engine::core::Config& cfg, const std::string& outputOpt)
		{
			if (outputOpt.empty())
			{
				return std::filesystem::current_path() / "build" / "zone_0";
			}

			std::string norm = outputOpt;
			NormalizePathSlashesInPlace(norm);
			if (norm.starts_with("zones/"))
			{
				return engine::platform::FileSystem::ResolveContentPath(cfg, norm);
			}

			std::filesystem::path p(outputOpt);
			if (p.is_absolute())
			{
				return p.lexically_normal();
			}

			return (std::filesystem::current_path() / p).lexically_normal();
		}

		/// Aide CLI (stdout via le logger : console=true au démarrage).
		void LogUsage()
		{
			LOG_INFO(Core, "[ZoneBuilder] zone_builder — outil offline (chemins layout/gltf relatifs à paths.content du config).");
			LOG_INFO(Core, "[ZoneBuilder]");
			LOG_INFO(Core, "[ZoneBuilder] Mode layout (recommandé) :");
			LOG_INFO(Core, "[ZoneBuilder]   zone_builder --layout <chemin/layout.json> [--output <dir>] [--config config.json] [--zone-id <id>]");
			LOG_INFO(Core, "[ZoneBuilder]   --layout   JSON relatif au content, ex. zones/_templates/layout_minimal.json");
			LOG_INFO(Core, "[ZoneBuilder]   --output   défaut : build/zone_0 (sous le répertoire courant) ; pour aligner le jeu :");
			LOG_INFO(Core, "[ZoneBuilder]              zones/<zone_id> → résolu avec paths.content (ex. game/data/zones/<zone_id>)");
			LOG_INFO(Core, "[ZoneBuilder]   --config   défaut : config.json (à la racine du dépôt ou cwd)");
			LOG_INFO(Core, "[ZoneBuilder]   --zone-id  optionnel : le dossier résolu par --output doit être zones/<id> avec id sanitizé.");
			LOG_INFO(Core, "[ZoneBuilder]");
			LOG_INFO(Core, "[ZoneBuilder] Mode legacy (un seul chunk, chemins fichiers au cwd) :");
			LOG_INFO(Core, "[ZoneBuilder]   zone_builder --output <dossier> --chunk <x> <z>");
			LOG_INFO(Core, "[ZoneBuilder]   x,z ≥ 0 (coordonnées chunk négatives → erreur, code de sortie 1).");
			LOG_INFO(Core, "[ZoneBuilder]");
			LOG_INFO(Core, "[ZoneBuilder] Positions layout : X et Z dans [0, kZoneSize) mètres (10 km zone) ; Y libre.");
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

				if (argument == "--zone-id" && index + 1 < argc)
				{
					outOptions.zoneIdCheck = argv[++index];
					continue;
				}

				outError = "argument inconnu ou incomplet : " + argument;
				return false;
			}

			if (!outOptions.runLayoutImport && !outOptions.runChunkPackaging)
			{
				outError = "aucun mode sélectionné (--layout ou --output/--chunk)";
				return false;
			}

			if (outOptions.runLayoutImport && outOptions.hasChunkCoordinates)
			{
				outError = "--chunk est réservé au mode legacy (--output sans --layout)";
				return false;
			}

			if (!outOptions.zoneIdCheck.empty() && !outOptions.runLayoutImport)
			{
				outError = "--zone-id n’est utilisable qu’avec --layout";
				return false;
			}

			return true;
		}

		/// Execute the legacy single-chunk package writer kept from the previous ticket flow.
		int RunChunkPackagingMode(const CliOptions& options)
		{
			if (options.chunkX < 0 || options.chunkZ < 0)
			{
				LOG_ERROR(Core, "[ZoneBuilder] Mode legacy : coordonnées chunk négatives non supportées (chunk=({},{}))", options.chunkX, options.chunkZ);
				return 1;
			}

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

			const std::filesystem::path outputRootPath = ResolveLayoutOutputRoot(config, options.outputPath);
			if (!options.zoneIdCheck.empty())
			{
				const std::string want = SanitizeZoneIdCli(options.zoneIdCheck);
				const auto expectedRoot =
					engine::platform::FileSystem::ResolveContentPath(config, std::string("zones/") + want).lexically_normal();
				if (outputRootPath.lexically_normal() != expectedRoot)
				{
					LOG_ERROR(Core,
						"[ZoneBuilder] --zone-id '{}' (attendu dossier zones/{}) ne correspond pas à --output résolu : obtenu {}",
						options.zoneIdCheck,
						want,
						outputRootPath.string());
					return 1;
				}
			}

			const std::string outputRootDir = outputRootPath.string();
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
		LOG_ERROR(Core, "[ZoneBuilder] Échec arguments : {}", error);
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
