#include "src/shared/network/DungeonPayloads.h"

#include <algorithm>
#include <cstring>

namespace engine::network
{
	namespace
	{
		void WriteU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
		void WriteU16(std::vector<uint8_t>& buf, uint16_t v)
		{
			buf.push_back(static_cast<uint8_t>(v & 0xFFu));
			buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
		}
		void WriteU64(std::vector<uint8_t>& buf, uint64_t v)
		{
			for (int i = 0; i < 8; ++i)
				buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
		}
		void WriteString(std::vector<uint8_t>& buf, std::string_view s)
		{
			const uint16_t len = static_cast<uint16_t>(
				s.size() > 0xFFFFu ? 0xFFFFu : s.size());
			WriteU16(buf, len);
			buf.insert(buf.end(), s.begin(), s.begin() + len);
		}

		bool ReadU8(const uint8_t* p, size_t size, size_t& cursor, uint8_t& out)
		{
			if (cursor + 1u > size) return false;
			out = p[cursor]; cursor += 1u;
			return true;
		}
		bool ReadU16(const uint8_t* p, size_t size, size_t& cursor, uint16_t& out)
		{
			if (cursor + 2u > size) return false;
			out = static_cast<uint16_t>(p[cursor])
			    | (static_cast<uint16_t>(p[cursor + 1]) << 8);
			cursor += 2u;
			return true;
		}
		bool ReadU64(const uint8_t* p, size_t size, size_t& cursor, uint64_t& out)
		{
			if (cursor + 8u > size) return false;
			out = 0u;
			for (int i = 0; i < 8; ++i)
				out |= static_cast<uint64_t>(p[cursor + i]) << (i * 8);
			cursor += 8u;
			return true;
		}
		bool ReadString(const uint8_t* p, size_t size, size_t& cursor,
			std::string& out, size_t maxBytes)
		{
			uint16_t len = 0u;
			if (!ReadU16(p, size, cursor, len)) return false;
			if (len > maxBytes) return false;
			if (cursor + len > size) return false;
			out.assign(reinterpret_cast<const char*>(p + cursor), len);
			cursor += len;
			return true;
		}
	}

	std::vector<uint8_t> BuildEnterDungeonRequestPayload(uint64_t characterId,
		std::string_view dungeonTemplateId, uint8_t difficulty)
	{
		std::vector<uint8_t> buf;
		buf.reserve(8u + 2u + dungeonTemplateId.size() + 1u);
		WriteU64(buf, characterId);
		WriteString(buf, dungeonTemplateId.substr(0,
			std::min<size_t>(dungeonTemplateId.size(), kMaxDungeonTemplateIdBytes)));
		WriteU8(buf, std::min<uint8_t>(difficulty, kMaxDungeonDifficulty));
		return buf;
	}

	std::optional<EnterDungeonRequestPayload> ParseEnterDungeonRequestPayload(
		const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr) return std::nullopt;
		size_t cursor = 0u;
		EnterDungeonRequestPayload p;
		if (!ReadU64(payload, payloadSize, cursor, p.characterId)) return std::nullopt;
		if (!ReadString(payload, payloadSize, cursor, p.dungeonTemplateId, kMaxDungeonTemplateIdBytes))
			return std::nullopt;
		if (!ReadU8(payload, payloadSize, cursor, p.difficulty)) return std::nullopt;
		if (p.difficulty == 0u || p.difficulty > kMaxDungeonDifficulty) return std::nullopt;
		return p;
	}

	std::vector<uint8_t> BuildEnterDungeonResponsePayload(bool success,
		uint64_t instanceId, std::string_view shardEndpoint, uint8_t errorCode)
	{
		std::vector<uint8_t> buf;
		buf.reserve(1u + 8u + 2u + shardEndpoint.size() + 1u);
		WriteU8(buf, success ? 1u : 0u);
		WriteU64(buf, instanceId);
		WriteString(buf, shardEndpoint);
		WriteU8(buf, errorCode);
		return buf;
	}

	std::optional<EnterDungeonResponsePayload> ParseEnterDungeonResponsePayload(
		const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr) return std::nullopt;
		size_t cursor = 0u;
		EnterDungeonResponsePayload p;
		uint8_t okFlag = 0u;
		if (!ReadU8(payload, payloadSize, cursor, okFlag)) return std::nullopt;
		p.success = (okFlag != 0u);
		if (!ReadU64(payload, payloadSize, cursor, p.instanceId)) return std::nullopt;
		if (!ReadString(payload, payloadSize, cursor, p.shardEndpoint, 255u))
			return std::nullopt;
		if (!ReadU8(payload, payloadSize, cursor, p.errorCode)) return std::nullopt;
		return p;
	}
}
