#include "engine/world/OutputVersion.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <ostream>

namespace engine::world
{
	namespace
	{
		constexpr uint64_t kXxPrime1 = 11400714785074694791ull;
		constexpr uint64_t kXxPrime2 = 14029467366897019727ull;
		constexpr uint64_t kXxPrime3 = 1609587929392839161ull;
		constexpr uint64_t kXxPrime4 = 9650029242287828579ull;
		constexpr uint64_t kXxPrime5 = 2870177450012600261ull;

		/// Rotate a 64-bit integer left.
		uint64_t Rotl64(uint64_t value, int bits)
		{
			return (value << bits) | (value >> (64 - bits));
		}

		/// Core xxHash64 round.
		uint64_t Round64(uint64_t acc, uint64_t input)
		{
			acc += input * kXxPrime2;
			acc = Rotl64(acc, 31);
			acc *= kXxPrime1;
			return acc;
		}

		/// Merge one lane into the accumulated xxHash64 state.
		uint64_t MergeRound64(uint64_t acc, uint64_t value)
		{
			acc ^= Round64(0, value);
			acc = acc * kXxPrime1 + kXxPrime4;
			return acc;
		}

		/// Read a little-endian 32-bit word from an unaligned byte pointer.
		uint32_t Read32(const uint8_t* data)
		{
			uint32_t value = 0;
			std::memcpy(&value, data, sizeof(value));
			return value;
		}

		/// Read a little-endian 64-bit word from an unaligned byte pointer.
		uint64_t Read64(const uint8_t* data)
		{
			uint64_t value = 0;
			std::memcpy(&value, data, sizeof(value));
			return value;
		}

		/// Append raw bytes into a staging buffer used for content-hash fingerprinting.
		void AppendBytes(std::vector<uint8_t>& target, const void* data, size_t size)
		{
			const auto* bytes = static_cast<const uint8_t*>(data);
			target.insert(target.end(), bytes, bytes + size);
		}

		/// Append one string with an explicit length prefix.
		void AppendString(std::vector<uint8_t>& target, std::string_view value)
		{
			const uint64_t size = static_cast<uint64_t>(value.size());
			AppendBytes(target, &size, sizeof(size));
			AppendBytes(target, value.data(), value.size());
		}

		/// Append one file metadata fingerprint entry without hashing the full asset bytes.
		bool AppendFileFingerprint(std::vector<uint8_t>& fingerprint,
			const std::filesystem::path& fullPath,
			std::string_view relativePath,
			std::string& outError)
		{
			std::error_code ec;
			if (!std::filesystem::exists(fullPath, ec) || ec)
			{
				outError = "missing referenced asset: " + fullPath.string();
				return false;
			}

			const uint64_t fileSize = static_cast<uint64_t>(std::filesystem::file_size(fullPath, ec));
			if (ec)
			{
				outError = "failed to stat asset size: " + fullPath.string();
				return false;
			}

			const auto writeTime = std::filesystem::last_write_time(fullPath, ec);
			if (ec)
			{
				outError = "failed to stat asset timestamp: " + fullPath.string();
				return false;
			}

			const int64_t writeTicks = static_cast<int64_t>(writeTime.time_since_epoch().count());
			AppendString(fingerprint, relativePath);
			AppendBytes(fingerprint, &fileSize, sizeof(fileSize));
			AppendBytes(fingerprint, &writeTicks, sizeof(writeTicks));
			return true;
		}
	}

	uint64_t ComputeXxHash64(std::span<const uint8_t> bytes, uint64_t seed)
	{
		if (bytes.empty())
		{
			uint64_t hash = seed + kXxPrime5;
			hash ^= hash >> 33;
			hash *= kXxPrime2;
			hash ^= hash >> 29;
			hash *= kXxPrime3;
			hash ^= hash >> 32;
			return hash;
		}

		const uint8_t* p = bytes.data();
		const uint8_t* const end = p + bytes.size();
		uint64_t hash = 0;

		if (bytes.size() >= 32)
		{
			uint64_t acc1 = seed + kXxPrime1 + kXxPrime2;
			uint64_t acc2 = seed + kXxPrime2;
			uint64_t acc3 = seed + 0;
			uint64_t acc4 = seed - kXxPrime1;
			const uint8_t* const limit = end - 32;
			do
			{
				acc1 = Round64(acc1, Read64(p)); p += 8;
				acc2 = Round64(acc2, Read64(p)); p += 8;
				acc3 = Round64(acc3, Read64(p)); p += 8;
				acc4 = Round64(acc4, Read64(p)); p += 8;
			} while (p <= limit);

			hash = Rotl64(acc1, 1) + Rotl64(acc2, 7) + Rotl64(acc3, 12) + Rotl64(acc4, 18);
			hash = MergeRound64(hash, acc1);
			hash = MergeRound64(hash, acc2);
			hash = MergeRound64(hash, acc3);
			hash = MergeRound64(hash, acc4);
		}
		else
		{
			hash = seed + kXxPrime5;
		}

		hash += static_cast<uint64_t>(bytes.size());
		while (p + 8 <= end)
		{
			const uint64_t lane = Round64(0, Read64(p));
			hash ^= lane;
			hash = Rotl64(hash, 27) * kXxPrime1 + kXxPrime4;
			p += 8;
		}

		if (p + 4 <= end)
		{
			hash ^= static_cast<uint64_t>(Read32(p)) * kXxPrime1;
			hash = Rotl64(hash, 23) * kXxPrime2 + kXxPrime3;
			p += 4;
		}

		while (p < end)
		{
			hash ^= static_cast<uint64_t>(*p) * kXxPrime5;
			hash = Rotl64(hash, 11) * kXxPrime1;
			++p;
		}

		hash ^= hash >> 33;
		hash *= kXxPrime2;
		hash ^= hash >> 29;
		hash *= kXxPrime3;
		hash ^= hash >> 32;
		return hash;
	}

	bool ComputeZoneContentHash(const engine::core::Config& cfg,
		std::string_view relativeLayoutPath,
		const std::vector<std::string>& referencedAssetPaths,
		uint64_t& outHash,
		std::string& outError)
	{
		LOG_INFO(Core, "[ZoneVersion] Computing content hash (layout={}, assets={})", relativeLayoutPath, referencedAssetPaths.size());

		const std::filesystem::path layoutPath = engine::platform::FileSystem::ResolveContentPath(cfg, relativeLayoutPath);
		const std::string layoutBytes = engine::platform::FileSystem::ReadAllTextContent(cfg, relativeLayoutPath);
		if (layoutBytes.empty())
		{
			outError = "failed to read layout for content hash";
			LOG_ERROR(Core, "[ZoneVersion] Content hash FAILED (layout={}, reason={})", layoutPath.string(), outError);
			return false;
		}

		std::vector<std::string> sortedAssets = referencedAssetPaths;
		std::sort(sortedAssets.begin(), sortedAssets.end());
		sortedAssets.erase(std::unique(sortedAssets.begin(), sortedAssets.end()), sortedAssets.end());

		std::vector<uint8_t> fingerprint;
		fingerprint.reserve(layoutBytes.size() + sortedAssets.size() * 96u);
		AppendString(fingerprint, relativeLayoutPath);
		AppendString(fingerprint, layoutBytes);
		for (const std::string& relativeAssetPath : sortedAssets)
		{
			const std::filesystem::path fullAssetPath = engine::platform::FileSystem::ResolveContentPath(cfg, relativeAssetPath);
			if (!AppendFileFingerprint(fingerprint, fullAssetPath, relativeAssetPath, outError))
			{
				LOG_ERROR(Core, "[ZoneVersion] Content hash FAILED (asset={}, reason={})", fullAssetPath.string(), outError);
				return false;
			}
		}

		outHash = ComputeXxHash64(fingerprint);
		LOG_INFO(Core, "[ZoneVersion] Content hash OK (layout={}, hash=0x{:016X})", layoutPath.string(), outHash);
		return true;
	}

	bool WriteOutputVersionHeader(std::ostream& stream, const OutputVersionHeader& header)
	{
		stream.write(reinterpret_cast<const char*>(&header.magic), sizeof(header.magic));
		stream.write(reinterpret_cast<const char*>(&header.formatVersion), sizeof(header.formatVersion));
		stream.write(reinterpret_cast<const char*>(&header.builderVersion), sizeof(header.builderVersion));
		stream.write(reinterpret_cast<const char*>(&header.engineVersion), sizeof(header.engineVersion));
		stream.write(reinterpret_cast<const char*>(&header.contentHash), sizeof(header.contentHash));
		return stream.good();
	}

	bool ReadOutputVersionHeader(std::span<const uint8_t> bytes, OutputVersionHeader& outHeader, std::string& outError)
	{
		if (bytes.size() < sizeof(OutputVersionHeader))
		{
			outError = "file too small for version header";
			return false;
		}

		const uint8_t* p = bytes.data();
		outHeader.magic = Read32(p); p += sizeof(uint32_t);
		outHeader.formatVersion = Read32(p); p += sizeof(uint32_t);
		outHeader.builderVersion = Read32(p); p += sizeof(uint32_t);
		outHeader.engineVersion = Read32(p); p += sizeof(uint32_t);
		outHeader.contentHash = Read64(p);
		return true;
	}

	bool ValidateOutputVersionHeader(const OutputVersionHeader& header,
		uint32_t expectedMagic,
		uint32_t expectedFormatVersion,
		uint64_t expectedContentHash,
		bool validateContentHash,
		std::string& outError)
	{
		if (header.magic != expectedMagic)
		{
			outError = "invalid file magic";
			return false;
		}

		if (header.formatVersion != expectedFormatVersion)
		{
			outError = "incompatible file format version";
			return false;
		}

		if (header.builderVersion != kZoneBuilderVersion)
		{
			outError = "incompatible builder version";
			return false;
		}

		if (header.engineVersion != kZoneEngineVersion)
		{
			outError = "incompatible engine version";
			return false;
		}

		if (validateContentHash && header.contentHash != expectedContentHash)
		{
			outError = "content hash mismatch";
			return false;
		}

		return true;
	}

	bool LoadVersionedFileHeader(const engine::core::Config& cfg,
		std::string_view relativePath,
		uint32_t expectedMagic,
		uint32_t expectedFormatVersion,
		OutputVersionHeader& outHeader,
		std::string& outError)
	{
		const std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(cfg, relativePath);
		LOG_INFO(Core, "[ZoneVersion] Loading header {}", fullPath.string());

		const std::vector<uint8_t> bytes = engine::platform::FileSystem::ReadAllBytesContent(cfg, relativePath);
		if (bytes.empty())
		{
			outError = "versioned file missing or empty";
			LOG_WARN(Core, "[ZoneVersion] Load header FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		if (!ReadOutputVersionHeader(bytes, outHeader, outError))
		{
			LOG_ERROR(Core, "[ZoneVersion] Load header FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		if (!ValidateOutputVersionHeader(outHeader, expectedMagic, expectedFormatVersion, 0, false, outError))
		{
			LOG_ERROR(Core, "[ZoneVersion] Load header FAILED (path={}, reason={})", fullPath.string(), outError);
			return false;
		}

		LOG_INFO(Core, "[ZoneVersion] Load header OK (path={}, hash=0x{:016X})", fullPath.string(), outHeader.contentHash);
		return true;
	}
}
