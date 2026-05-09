#pragma once

#include "engine/core/Config.h"
#include "engine/world/OutputVersion.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::world
{
	/// Binary file magic for `probes.bin` ("PROB" little-endian).
	constexpr uint32_t kProbeSetMagic = 0x424F5250u;
	/// Current payload version for `probes.bin`.
	constexpr uint32_t kProbeSetVersion = 1u;

	/// One global/local IBL probe description used by the MVP runtime fallback.
	struct ProbeRecord
	{
		float position[3]{ 0.0f, 0.0f, 0.0f };
		float radius = 0.0f;
		float extents[3]{ 0.0f, 0.0f, 0.0f };
		float reserved = 0.0f;
		float params[4]{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	/// Full probes payload loaded from `probes.bin`.
	struct ProbeSet
	{
		std::vector<ProbeRecord> probes;
	};

	/// Minimal per-zone atmosphere values used by the runtime lighting fallback.
	struct AtmosphereSettings
	{
		float sunDirection[3]{ 0.5774f, 0.5774f, 0.5774f };
		float sunColor[3]{ 1.0f, 0.95f, 0.85f };
		float ambientColor[3]{ 0.03f, 0.03f, 0.05f };
	};

	/// Load `probes.bin` from a content-relative path resolved via `paths.content`.
	/// When `validateContentHash` is true, the file header must match `expectedContentHash`.
	bool LoadProbeSet(const engine::core::Config& cfg,
		std::string_view relativePath,
		uint64_t expectedContentHash,
		bool validateContentHash,
		ProbeSet& outProbeSet,
		std::string& outError);

	/// Load `atmosphere.json` from a content-relative path resolved via `paths.content`.
	bool LoadAtmosphereSettings(const engine::core::Config& cfg, std::string_view relativePath, AtmosphereSettings& outSettings, std::string& outError);
}
