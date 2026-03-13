#include "engine/world/ProbeData.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <cstring>
#include <filesystem>

namespace engine::world
{
	namespace
	{
		/// Read one float array entry from a flattened config key prefix like `sun.direction`.
		bool ReadFloat3(const engine::core::Config& cfg, std::string_view prefix, float (&outValues)[3])
		{
			bool foundAny = false;
			for (int index = 0; index < 3; ++index)
			{
				const std::string key = std::string(prefix) + "[" + std::to_string(index) + "]";
				if (cfg.Has(key))
				{
					outValues[index] = static_cast<float>(cfg.GetDouble(key, static_cast<double>(outValues[index])));
					foundAny = true;
				}
			}

			return foundAny;
		}
	}

	bool LoadProbeSet(const engine::core::Config& cfg,
		std::string_view relativePath,
		uint64_t expectedContentHash,
		bool validateContentHash,
		ProbeSet& outProbeSet,
		std::string& outError)
	{
		outProbeSet = ProbeSet{};
		const std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(cfg, relativePath);
		LOG_INFO(Core, "[ZoneProbes] Loading probes {}", fullPath.string());

		const std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(cfg, relativePath);
		if (bytes.empty())
		{
			outError = "probe file missing or empty";
			LOG_WARN(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		OutputVersionHeader header;
		if (!ReadOutputVersionHeader(bytes, header, outError))
		{
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		if (!ValidateOutputVersionHeader(header, kProbeSetMagic, kProbeSetVersion, expectedContentHash, validateContentHash, outError))
		{
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		const size_t headerSize = sizeof(OutputVersionHeader);
		if (bytes.size() < headerSize + sizeof(uint32_t))
		{
			outError = "probe file too small";
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		uint32_t count = 0;
		std::memcpy(&count, bytes.data() + headerSize, sizeof(count));

		const size_t payloadSize = headerSize + sizeof(uint32_t) + static_cast<size_t>(count) * sizeof(ProbeRecord);
		if (bytes.size() != payloadSize)
		{
			outError = "unexpected probe payload size";
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		outProbeSet.probes.resize(count);
		if (count > 0)
		{
			std::memcpy(outProbeSet.probes.data(), bytes.data() + headerSize + sizeof(uint32_t), sizeof(ProbeRecord) * count);
		}

		LOG_INFO(Core, "[ZoneProbes] Load probes OK (path={}, count={}, hash=0x{:016X})",
			fullPath.string(),
			outProbeSet.probes.size(),
			header.contentHash);
		return true;
	}

	bool LoadAtmosphereSettings(const engine::core::Config& cfg, std::string_view relativePath, AtmosphereSettings& outSettings, std::string& outError)
	{
		outSettings = AtmosphereSettings{};
		const std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(cfg, relativePath);
		LOG_INFO(Core, "[ZoneProbes] Loading atmosphere {}", fullPath.string());

		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			outError = "atmosphere file missing";
			LOG_WARN(Core, "[ZoneProbes] Load atmosphere FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		engine::core::Config atmosphereCfg;
		if (!atmosphereCfg.LoadFromFile(fullPath.string()))
		{
			outError = "failed to parse atmosphere json";
			LOG_ERROR(Core, "[ZoneProbes] Load atmosphere FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		ReadFloat3(atmosphereCfg, "sun.direction", outSettings.sunDirection);
		ReadFloat3(atmosphereCfg, "sun.color", outSettings.sunColor);
		ReadFloat3(atmosphereCfg, "ambient.color", outSettings.ambientColor);
		LOG_INFO(Core, "[ZoneProbes] Load atmosphere OK (path={})", fullPath.string());
		return true;
	}
}
