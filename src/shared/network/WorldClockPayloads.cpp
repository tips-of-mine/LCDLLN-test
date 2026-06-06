// Implementation Build/Parse des payloads WorldClock (opcodes 203-205).
// Format little-endian, sans PacketBuilder : ce module produit uniquement
// le payload nu (calque exact de LunarPayloads.cpp). Le WorldClockHandler
// futur appellera BuildPushPacket (pour le 205) ou PacketBuilder::Finalize
// (pour le 204) pour ajouter le header protocol_v1.

#include "src/shared/network/WorldClockPayloads.h"

#include <cstring>

namespace engine::network::worldclock
{
	namespace
	{
		/// Ecrit un uint8 a la fin de \p out.
		void WriteU8(std::vector<uint8_t>& out, uint8_t v)
		{
			out.push_back(v);
		}

		/// Ecrit un uint32 little-endian a la fin de \p out.
		void WriteU32LE(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(v >> 8));
			out.push_back(static_cast<uint8_t>(v >> 16));
			out.push_back(static_cast<uint8_t>(v >> 24));
		}

		/// Ecrit un uint64 little-endian a la fin de \p out.
		void WriteU64LE(std::vector<uint8_t>& out, uint64_t v)
		{
			for (int i = 0; i < 8; ++i)
				out.push_back(static_cast<uint8_t>(v >> (i * 8)));
		}

		/// Ecrit un float little-endian (bit-cast vers uint32) a la fin de \p out.
		void WriteFloatLE(std::vector<uint8_t>& out, float v)
		{
			uint32_t bits = 0;
			std::memcpy(&bits, &v, sizeof(bits));
			WriteU32LE(out, bits);
		}

		/// Ecrit un double little-endian (bit-cast vers uint64) a la fin de \p out.
		/// Calque sur le pattern float/u64 : memcpy 8 octets puis ecriture LE.
		void WriteDoubleLE(std::vector<uint8_t>& out, double v)
		{
			uint64_t bits = 0;
			std::memcpy(&bits, &v, sizeof(bits));
			WriteU64LE(out, bits);
		}

		/// Lit un uint8 et avance \p pos. Retourne false si insuffisant.
		bool ReadU8(const uint8_t* d, size_t sz, size_t& pos, uint8_t& out)
		{
			if (pos + 1 > sz) return false;
			out = d[pos];
			pos += 1;
			return true;
		}

		/// Lit un uint32 little-endian et avance \p pos. Retourne false si insuffisant.
		bool ReadU32LE(const uint8_t* d, size_t sz, size_t& pos, uint32_t& out)
		{
			if (pos + 4 > sz) return false;
			out = static_cast<uint32_t>(d[pos])
			    | (static_cast<uint32_t>(d[pos + 1]) << 8)
			    | (static_cast<uint32_t>(d[pos + 2]) << 16)
			    | (static_cast<uint32_t>(d[pos + 3]) << 24);
			pos += 4;
			return true;
		}

		/// Lit un uint64 little-endian et avance \p pos. Retourne false si insuffisant.
		bool ReadU64LE(const uint8_t* d, size_t sz, size_t& pos, uint64_t& out)
		{
			if (pos + 8 > sz) return false;
			out = 0;
			for (int i = 0; i < 8; ++i)
				out |= static_cast<uint64_t>(d[pos + i]) << (i * 8);
			pos += 8;
			return true;
		}

		/// Lit un float little-endian (bit-cast depuis uint32) et avance \p pos.
		bool ReadFloatLE(const uint8_t* d, size_t sz, size_t& pos, float& out)
		{
			uint32_t bits = 0;
			if (!ReadU32LE(d, sz, pos, bits)) return false;
			std::memcpy(&out, &bits, sizeof(out));
			return true;
		}

		/// Lit un double little-endian (bit-cast depuis uint64) et avance \p pos.
		bool ReadDoubleLE(const uint8_t* d, size_t sz, size_t& pos, double& out)
		{
			uint64_t bits = 0;
			if (!ReadU64LE(d, sz, pos, bits)) return false;
			std::memcpy(&out, &bits, sizeof(out));
			return true;
		}
	}

	void BuildWorldClockStateRequestPayload(std::vector<uint8_t>& out)
	{
		out.clear();
	}

	void BuildWorldClockStateResponsePayload(const WorldClockStateResponse& msg, std::vector<uint8_t>& out)
	{
		out.clear();
		WriteU8(out, static_cast<uint8_t>(msg.status));
		WriteU64LE(out, msg.serverTimeUnixMs);
		WriteU64LE(out, msg.epochRefUnixMs);
		WriteFloatLE(out, msg.timeScaleRealMinPerDay);
		WriteDoubleLE(out, msg.offsetGameSec);
		WriteU8(out, msg.paused);
		WriteDoubleLE(out, msg.pausedAtGameSec);
		WriteDoubleLE(out, msg.lunarPeriodGameSec);
		// Taille totale = 1+8+8+4+8+1+8+8 = 46 octets.
	}

	void BuildWorldClockChangeNotificationPayload(const WorldClockStateResponse& msg, std::vector<uint8_t>& out)
	{
		// Corps identique a StateResponse (memes champs).
		BuildWorldClockStateResponsePayload(msg, out);
	}

	bool ParseWorldClockStateRequestPayload(const uint8_t* /*data*/, size_t size, WorldClockStateRequest& out)
	{
		out = {};
		return size == 0;
	}

	bool ParseWorldClockStateResponsePayload(const uint8_t* d, size_t sz, WorldClockStateResponse& out)
	{
		// Reject-short ET reject-extra : la taille doit egaler exactement 46.
		if (sz != 46) return false;
		size_t pos = 0;
		uint8_t status = 0;
		if (!ReadU8(d, sz, pos, status)) return false;
		if (!ReadU64LE(d, sz, pos, out.serverTimeUnixMs)) return false;
		if (!ReadU64LE(d, sz, pos, out.epochRefUnixMs)) return false;
		if (!ReadFloatLE(d, sz, pos, out.timeScaleRealMinPerDay)) return false;
		if (!ReadDoubleLE(d, sz, pos, out.offsetGameSec)) return false;
		if (!ReadU8(d, sz, pos, out.paused)) return false;
		if (!ReadDoubleLE(d, sz, pos, out.pausedAtGameSec)) return false;
		if (!ReadDoubleLE(d, sz, pos, out.lunarPeriodGameSec)) return false;
		if (status > static_cast<uint8_t>(WorldClockStatus::Unauthorized)) return false;
		out.status = static_cast<WorldClockStatus>(status);
		return pos == sz;
	}

	bool ParseWorldClockChangeNotificationPayload(const uint8_t* d, size_t sz, WorldClockStateResponse& out)
	{
		// Corps identique a StateResponse (46 octets).
		return ParseWorldClockStateResponsePayload(d, sz, out);
	}
}
