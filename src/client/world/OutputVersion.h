#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::world
{
	/// Shared builder-side output header placed at the start of versioned `.bin` / `.pak` / `.meta` files.
	struct OutputVersionHeader
	{
		uint32_t magic = 0;
		uint32_t formatVersion = 0;
		uint32_t builderVersion = 1;
		uint32_t engineVersion = 1;
		uint64_t contentHash = 0;
	};

	static_assert(sizeof(OutputVersionHeader) == 24, "OutputVersionHeader must stay packed to 24 bytes");

	/// Current builder version written into all zone-builder outputs for compatibility checks.
	constexpr uint32_t kZoneBuilderVersion = 1u;
	/// Current runtime engine version expected by versioned zone outputs.
	constexpr uint32_t kZoneEngineVersion = 1u;
	/// Versioned file magic for `zone.meta` ("ZONE" little-endian).
	constexpr uint32_t kZoneMetaMagic = 0x454E4F5Au;
	/// Current payload version for `zone.meta`.
	constexpr uint32_t kZoneMetaVersion = 1u;
	/// Versioned file magic for `chunk.meta` ("CHNK" little-endian).
	constexpr uint32_t kChunkMetaMagic = 0x4B4E4843u;
	/// Current payload version for `chunk.meta`.
	constexpr uint32_t kChunkMetaVersion = 1u;
	/// Versioned file magic for `instances.bin` ("INST" little-endian).
	constexpr uint32_t kInstancesMagic = 0x54534E49u;
	/// Current payload version for `instances.bin`.
	constexpr uint32_t kInstancesVersion = 1u;

	/// Compute xxHash64 for an arbitrary byte span.
	uint64_t ComputeXxHash64(std::span<const uint8_t> bytes, uint64_t seed = 0);

	/// Compute the zone content hash from a layout file plus referenced asset metadata.
	bool ComputeZoneContentHash(const engine::core::Config& cfg,
		std::string_view relativeLayoutPath,
		const std::vector<std::string>& referencedAssetPaths,
		uint64_t& outHash,
		std::string& outError);

	/// Write a version header to a binary stream.
	bool WriteOutputVersionHeader(std::ostream& stream, const OutputVersionHeader& header);

	/// Read a version header from a complete in-memory file payload.
	bool ReadOutputVersionHeader(std::span<const uint8_t> bytes, OutputVersionHeader& outHeader, std::string& outError);

	/// Validate magic, format version, builder version, engine version, and optional content hash.
	bool ValidateOutputVersionHeader(const OutputVersionHeader& header,
		uint32_t expectedMagic,
		uint32_t expectedFormatVersion,
		uint64_t expectedContentHash,
		bool validateContentHash,
		std::string& outError);

	/// Load and validate only the version header of a content-relative file.
	bool LoadVersionedFileHeader(const engine::core::Config& cfg,
		std::string_view relativePath,
		uint32_t expectedMagic,
		uint32_t expectedFormatVersion,
		OutputVersionHeader& outHeader,
		std::string& outError);
}
