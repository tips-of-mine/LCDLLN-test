#include "ChunkPackageWriter.h"

#include "engine/core/Log.h"
#include "engine/world/ChunkPackageLayout.h"
#include "engine/world/OutputVersion.h"
#include "engine/world/ProbeData.h"
#include "engine/world/WorldModel.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

namespace tools::zone_builder
{
	namespace
	{
		struct ChunkKey
		{
			int32_t x = 0;
			int32_t z = 0;

			bool operator<(const ChunkKey& other) const
			{
				if (x != other.x)
				{
					return x < other.x;
				}

				return z < other.z;
			}
		};

		struct ChunkedInstanceRecord
		{
			std::array<float, 16> transform{};
			uint32_t assetId = 0;
			uint32_t flags = 0;
		};

		// Match engine/world/PakFormat.h layout for .pak files
		constexpr uint32_t kPakMagic = 0x4348504Bu;
		constexpr uint32_t kPakVersion = 1u;

		/// Return a stable asset identifier derived from the relative glTF content path.
		uint32_t HashAssetPath(std::string_view relativePath)
		{
			uint32_t hash = 2166136261u;
			for (unsigned char c : relativePath)
			{
				hash ^= c;
				hash *= 16777619u;
			}

			return hash;
		}

		/// Build an identity 4x4 matrix with translation in the last column.
		std::array<float, 16> BuildTranslationTransform(double x, double y, double z)
		{
			return std::array<float, 16>{
				1.0f, 0.0f, 0.0f, static_cast<float>(x),
				0.0f, 1.0f, 0.0f, static_cast<float>(y),
				0.0f, 0.0f, 1.0f, static_cast<float>(z),
				0.0f, 0.0f, 0.0f, 1.0f
			};
		}

		/// Write the binary zone metadata: version header + ordered chunk coordinate list.
		bool WriteZoneMeta(const std::filesystem::path& path,
			const std::map<ChunkKey, std::vector<ChunkedInstanceRecord>>& chunkInstances,
			uint64_t contentHash)
		{
			LOG_INFO(Core, "[ZoneBuilder] Writing zone meta {}", path.string());

			std::ofstream stream(path, std::ios::binary);
			if (!stream.is_open())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write zone meta FAILED (path={})", path.string());
				return false;
			}

			const uint32_t chunkCount = static_cast<uint32_t>(chunkInstances.size());
			const engine::world::OutputVersionHeader header{
				engine::world::kZoneMetaMagic,
				engine::world::kZoneMetaVersion,
				engine::world::kZoneBuilderVersion,
				engine::world::kZoneEngineVersion,
				contentHash
			};
			if (!engine::world::WriteOutputVersionHeader(stream, header))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write zone meta FAILED (path={})", path.string());
				return false;
			}

			stream.write(reinterpret_cast<const char*>(&chunkCount), sizeof(chunkCount));
			for (const auto& [chunkKey, _] : chunkInstances)
			{
				stream.write(reinterpret_cast<const char*>(&chunkKey.x), sizeof(chunkKey.x));
				stream.write(reinterpret_cast<const char*>(&chunkKey.z), sizeof(chunkKey.z));
			}

			if (!stream.good())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write zone meta FAILED (path={})", path.string());
				return false;
			}

			LOG_INFO(Core, "[ZoneBuilder] Zone meta OK (path={}, chunks={})", path.string(), chunkCount);
			return true;
		}

		/// Build the MVP global zone probe centered in the current zone extents.
		engine::world::ProbeRecord BuildGlobalProbe(const LayoutDocument& layout)
		{
			engine::world::ProbeRecord probe{};
			probe.position[0] = static_cast<float>(engine::world::kZoneSize) * 0.5f;
			probe.position[1] = 2.0f;
			probe.position[2] = static_cast<float>(engine::world::kZoneSize) * 0.5f;
			probe.radius = static_cast<float>(engine::world::kZoneSize) * 0.5f;
			probe.extents[0] = static_cast<float>(engine::world::kZoneSize) * 0.5f;
			probe.extents[1] = 256.0f;
			probe.extents[2] = static_cast<float>(engine::world::kZoneSize) * 0.5f;
			probe.params[0] = 1.0f; // intensity

			if (!layout.instances.empty())
			{
				double sumY = 0.0;
				for (const LayoutInstance& instance : layout.instances)
				{
					sumY += instance.positionY;
				}

				probe.position[1] = static_cast<float>(sumY / static_cast<double>(layout.instances.size()));
			}

			return probe;
		}

		/// Write the MVP `probes.bin` payload with one global probe.
		bool WriteProbesBin(const std::filesystem::path& path, const LayoutDocument& layout, uint64_t contentHash)
		{
			LOG_INFO(Core, "[ZoneBuilder] Writing probes {}", path.string());

			std::ofstream stream(path, std::ios::binary);
			if (!stream.is_open())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write probes FAILED (path={})", path.string());
				return false;
			}

			const uint32_t probeCount = 1u;
			const engine::world::ProbeRecord probe = BuildGlobalProbe(layout);
			const engine::world::OutputVersionHeader header{
				engine::world::kProbeSetMagic,
				engine::world::kProbeSetVersion,
				engine::world::kZoneBuilderVersion,
				engine::world::kZoneEngineVersion,
				contentHash
			};
			if (!engine::world::WriteOutputVersionHeader(stream, header))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write probes FAILED (path={})", path.string());
				return false;
			}

			stream.write(reinterpret_cast<const char*>(&probeCount), sizeof(probeCount));
			stream.write(reinterpret_cast<const char*>(&probe), sizeof(probe));
			if (!stream.good())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write probes FAILED (path={})", path.string());
				return false;
			}

			LOG_INFO(Core, "[ZoneBuilder] Probes OK (path={}, count={})", path.string(), probeCount);
			return true;
		}

		/// Write a minimal zone atmosphere JSON used by the runtime lighting fallback.
		bool WriteAtmosphereJson(const std::filesystem::path& path)
		{
			LOG_INFO(Core, "[ZoneBuilder] Writing atmosphere {}", path.string());

			std::ofstream stream(path, std::ios::binary);
			if (!stream.is_open())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write atmosphere FAILED (path={})", path.string());
				return false;
			}

			stream <<
				"{\n"
				"  \"version\": 1,\n"
				"  \"sun\": {\n"
				"    \"direction\": [0.5774, 0.5774, 0.5774],\n"
				"    \"color\": [1.0, 0.95, 0.85]\n"
				"  },\n"
				"  \"ambient\": {\n"
				"    \"color\": [0.03, 0.03, 0.05]\n"
				"  }\n"
				"}\n";
			if (!stream.good())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write atmosphere FAILED (path={})", path.string());
				return false;
			}

			LOG_INFO(Core, "[ZoneBuilder] Atmosphere OK (path={})", path.string());
			return true;
		}

		/// Write an empty `.pak` file header for legacy chunk packaging outputs.
		bool WriteEmptyPak(const std::string& path)
		{
			LOG_INFO(Core, "[ZoneBuilder] Writing empty pak {}", path);
			std::ofstream f(path, std::ios::binary);
			if (!f)
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write empty pak FAILED (path={})", path);
				return false;
			}
			uint32_t magic = kPakMagic;
			uint32_t version = kPakVersion;
			uint32_t numEntries = 0;
			f.write(reinterpret_cast<const char*>(&magic), 4);
			f.write(reinterpret_cast<const char*>(&version), 4);
			f.write(reinterpret_cast<const char*>(&numEntries), 4);
			if (!f.good())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write empty pak FAILED (path={})", path);
				return false;
			}

			LOG_INFO(Core, "[ZoneBuilder] Empty pak OK (path={})", path);
			return true;
		}

		/// Write one binary `chunk.meta` payload preceded by the common version header.
		bool WriteChunkMeta(const std::filesystem::path& path, int32_t cx, int32_t cz, uint32_t flags, uint64_t contentHash)
		{
			LOG_INFO(Core, "[ZoneBuilder] Writing chunk meta {} for chunk ({},{})", path.string(), cx, cz);

			std::ofstream f(path, std::ios::binary);
			if (!f)
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write chunk meta FAILED (path={})", path.string());
				return false;
			}

			if (cx < 0 || cz < 0)
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write chunk meta FAILED (chunk=({},{}), reason=negative chunk coordinates are unsupported)", cx, cz);
				return false;
			}

			const engine::world::OutputVersionHeader header{
				engine::world::kChunkMetaMagic,
				engine::world::kChunkMetaVersion,
				engine::world::kZoneBuilderVersion,
				engine::world::kZoneEngineVersion,
				contentHash
			};
			if (!engine::world::WriteOutputVersionHeader(f, header))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write chunk meta FAILED (path={})", path.string());
				return false;
			}

			f.write(reinterpret_cast<const char*>(&cx), sizeof(cx));
			f.write(reinterpret_cast<const char*>(&cz), sizeof(cz));
			uint32_t boundsMinX = static_cast<uint32_t>(cx * 256);
			uint32_t boundsMinZ = static_cast<uint32_t>(cz * 256);
			uint32_t boundsMaxX = boundsMinX + 256;
			uint32_t boundsMaxZ = boundsMinZ + 256;
			f.write(reinterpret_cast<const char*>(&boundsMinX), sizeof(boundsMinX));
			f.write(reinterpret_cast<const char*>(&boundsMinZ), sizeof(boundsMinZ));
			f.write(reinterpret_cast<const char*>(&boundsMaxX), sizeof(boundsMaxX));
			f.write(reinterpret_cast<const char*>(&boundsMaxZ), sizeof(boundsMaxZ));
			f.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
			if (!f.good())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write chunk meta FAILED (path={})", path.string());
				return false;
			}

			LOG_INFO(Core, "[ZoneBuilder] Chunk meta OK (path={}, flags={})", path.string(), flags);
			return true;
		}

		/// Write binary instance records: version header + count + transform + assetId + flags.
		bool WriteInstancesBin(const std::filesystem::path& path, const std::vector<ChunkedInstanceRecord>& instances, uint64_t contentHash)
		{
			LOG_INFO(Core, "[ZoneBuilder] Writing instances {}", path.string());

			std::ofstream stream(path, std::ios::binary);
			if (!stream.is_open())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write instances FAILED (path={})", path.string());
				return false;
			}

			const uint32_t instanceCount = static_cast<uint32_t>(instances.size());
			const engine::world::OutputVersionHeader header{
				engine::world::kInstancesMagic,
				engine::world::kInstancesVersion,
				engine::world::kZoneBuilderVersion,
				engine::world::kZoneEngineVersion,
				contentHash
			};
			if (!engine::world::WriteOutputVersionHeader(stream, header))
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write instances FAILED (path={})", path.string());
				return false;
			}

			stream.write(reinterpret_cast<const char*>(&instanceCount), sizeof(instanceCount));
			for (const ChunkedInstanceRecord& instance : instances)
			{
				stream.write(reinterpret_cast<const char*>(instance.transform.data()),
					static_cast<std::streamsize>(sizeof(float) * instance.transform.size()));
				stream.write(reinterpret_cast<const char*>(&instance.assetId), sizeof(instance.assetId));
				stream.write(reinterpret_cast<const char*>(&instance.flags), sizeof(instance.flags));
			}

			if (!stream.good())
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write instances FAILED (path={})", path.string());
				return false;
			}

			LOG_INFO(Core, "[ZoneBuilder] Instances OK (path={}, count={})", path.string(), instanceCount);
			return true;
		}

		/// Write an empty binary file for legacy segments that are out of scope for M11.2.
		bool WriteEmptyBin(const std::string& path)
		{
			LOG_INFO(Core, "[ZoneBuilder] Writing empty binary {}", path);
			std::ofstream f(path, std::ios::binary);
			if (!f)
			{
				LOG_ERROR(Core, "[ZoneBuilder] Write empty binary FAILED (path={})", path);
				return false;
			}

			LOG_INFO(Core, "[ZoneBuilder] Empty binary OK (path={})", path);
			return true;
		}
	}

	bool WriteChunkPackage(const std::string& outputDir, int32_t chunkX, int32_t chunkZ)
	{
		LOG_INFO(Core, "[ZoneBuilder] Legacy chunk package create requested (dir={}, chunk=({},{}))", outputDir, chunkX, chunkZ);

		std::error_code ec;
		std::filesystem::create_directories(outputDir, ec);
		if (ec)
		{
			LOG_ERROR(Core, "[ZoneBuilder] Legacy chunk package FAILED (dir={}, reason={})", outputDir, ec.message());
			return false;
		}

		const uint32_t legacyFlags = engine::world::kChunkMetaHasGeo |
			engine::world::kChunkMetaHasTex |
			engine::world::kChunkMetaHasInstances |
			engine::world::kChunkMetaHasNav |
			engine::world::kChunkMetaHasProbes;

		std::string metaPath = outputDir + "/chunk.meta";
		if (!WriteChunkMeta(metaPath, chunkX, chunkZ, legacyFlags, 0))
			return false;
		if (!WriteEmptyPak(outputDir + "/geo.pak"))
			return false;
		if (!WriteEmptyPak(outputDir + "/tex.pak"))
			return false;
		if (!WriteEmptyBin(outputDir + "/instances.bin"))
			return false;
		if (!WriteEmptyBin(outputDir + "/navmesh.bin"))
			return false;
		if (!WriteEmptyBin(outputDir + "/probes.bin"))
			return false;

		LOG_INFO(Core, "[ZoneBuilder] Legacy chunk package OK (dir={})", outputDir);
		return true;
	}

	bool WriteChunkedZoneOutputs(std::string_view outputRootDir, const engine::core::Config& config, const LayoutDocument& layout, std::string& outError)
	{
		LOG_INFO(Core, "[ZoneBuilder] Build chunked zone requested (output={}, instances={})",
			std::string(outputRootDir),
			layout.instances.size());

		std::map<ChunkKey, std::vector<ChunkedInstanceRecord>> chunkInstances;
		std::vector<std::string> referencedAssetPaths;
		referencedAssetPaths.reserve(layout.instances.size());
		for (const LayoutInstance& instance : layout.instances)
		{
			const int32_t chunkX = static_cast<int32_t>(std::floor(instance.positionX / static_cast<double>(engine::world::kChunkSize)));
			const int32_t chunkZ = static_cast<int32_t>(std::floor(instance.positionZ / static_cast<double>(engine::world::kChunkSize)));
			if (chunkX < 0 || chunkZ < 0)
			{
				outError = "negative chunk coordinates are unsupported by the current chunk.meta bounds layout";
				LOG_ERROR(Core, "[ZoneBuilder] Build chunked zone FAILED (guid={}, reason={})", instance.guid, outError);
				return false;
			}

			ChunkedInstanceRecord record;
			record.transform = BuildTranslationTransform(instance.positionX, instance.positionY, instance.positionZ);
			record.assetId = HashAssetPath(instance.gltfPath);
			record.flags = 0u;
			chunkInstances[ChunkKey{ chunkX, chunkZ }].push_back(record);
			referencedAssetPaths.push_back(instance.gltfPath);
		}

		uint64_t contentHash = 0;
		if (!engine::world::ComputeZoneContentHash(config, layout.relativePath, referencedAssetPaths, contentHash, outError))
		{
			LOG_ERROR(Core, "[ZoneBuilder] Build chunked zone FAILED (reason={})", outError);
			return false;
		}

		const std::filesystem::path rootPath(outputRootDir);
		const std::filesystem::path chunksRoot = rootPath / "chunks";
		std::error_code ec;
		std::filesystem::create_directories(chunksRoot, ec);
		if (ec)
		{
			outError = ec.message();
			LOG_ERROR(Core, "[ZoneBuilder] Build chunked zone FAILED (path={}, reason={})", chunksRoot.string(), outError);
			return false;
		}

		if (!WriteZoneMeta(rootPath / "zone.meta", chunkInstances, contentHash))
		{
			outError = "failed to write zone.meta";
			return false;
		}

		if (!WriteProbesBin(rootPath / "probes.bin", layout, contentHash))
		{
			outError = "failed to write probes.bin";
			return false;
		}

		if (!WriteAtmosphereJson(rootPath / "atmosphere.json"))
		{
			outError = "failed to write atmosphere.json";
			return false;
		}

		for (const auto& [chunkKey, instances] : chunkInstances)
		{
			const std::filesystem::path chunkDir = chunksRoot /
				("chunk_" + std::to_string(chunkKey.x) + "_" + std::to_string(chunkKey.z));
			std::filesystem::create_directories(chunkDir, ec);
			if (ec)
			{
				outError = ec.message();
				LOG_ERROR(Core, "[ZoneBuilder] Build chunked zone FAILED (path={}, reason={})", chunkDir.string(), outError);
				return false;
			}

			if (!WriteChunkMeta(chunkDir / "chunk.meta", chunkKey.x, chunkKey.z, engine::world::kChunkMetaHasInstances, contentHash))
			{
				outError = "failed to write chunk.meta";
				return false;
			}

			if (!WriteInstancesBin(chunkDir / "instances.bin", instances, contentHash))
			{
				outError = "failed to write instances.bin";
				return false;
			}
		}

		LOG_INFO(Core, "[ZoneBuilder] Build chunked zone OK (output={}, chunks={}, hash=0x{:016X})",
			rootPath.string(),
			chunkInstances.size(),
			contentHash);
		return true;
	}
}