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

	bool LoadProbeSet(const engine::core::Config& cfg, std::string_view relativePath, ProbeSet& outProbeSet, std::string& outError)
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

		if (bytes.size() < sizeof(uint32_t) * 3u)
		{
			outError = "probe file too small";
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		const uint32_t* header = reinterpret_cast<const uint32_t*>(bytes.data());
		const uint32_t magic = header[0];
		const uint32_t version = header[1];
		const uint32_t count = header[2];
		if (magic != kProbeSetMagic)
		{
			outError = "invalid probe magic";
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		if (version != kProbeSetVersion)
		{
			outError = "unsupported probe version";
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		const size_t payloadSize = sizeof(uint32_t) * 3u + static_cast<size_t>(count) * sizeof(ProbeRecord);
		if (bytes.size() != payloadSize)
		{
			outError = "unexpected probe payload size";
			LOG_ERROR(Core, "[ZoneProbes] Load probes FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		outProbeSet.probes.resize(count);
		if (count > 0)
		{
			std::memcpy(outProbeSet.probes.data(), bytes.data() + sizeof(uint32_t) * 3u, sizeof(ProbeRecord) * count);
		}

		LOG_INFO(Core, "[ZoneProbes] Load probes OK (path={}, count={})", fullPath.string(), outProbeSet.probes.size());
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
