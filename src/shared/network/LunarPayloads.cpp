// Implementation Build/Parse des payloads Lunar (opcodes 192-194).
// Format little-endian, sans PacketBuilder : ce module produit uniquement
// le payload nu. Le LunarHandler appelle BuildPushPacket (pour le 194) ou
// PacketBuilder::Finalize (pour le 193) pour ajouter le header protocol_v1.

#include "src/shared/network/LunarPayloads.h"

#include <cstring>

namespace engine::network::lunar
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
	}

	void BuildLunarStateRequestPayload(std::vector<uint8_t>& out)
	{
		out.clear();
	}

	void BuildLunarStateResponsePayload(const LunarStateResponse& msg, std::vector<uint8_t>& out)
	{
		out.clear();
		WriteU8(out, static_cast<uint8_t>(msg.status));
		WriteU8(out, msg.phase);
		WriteFloatLE(out, msg.illumination);
		WriteU64LE(out, msg.cycleStartMs);
		WriteU64LE(out, msg.cycleDurationMs);
	}

	void BuildLunarPhaseChangeNotificationPayload(const LunarPhaseChangeNotification& msg, std::vector<uint8_t>& out)
	{
		out.clear();
		WriteU8(out, msg.newPhase);
		WriteFloatLE(out, msg.newIllumination);
		WriteU64LE(out, msg.nextChangeTsMs);
	}

	bool ParseLunarStateRequestPayload(const uint8_t* /*data*/, size_t size, LunarStateRequest& /*out*/)
	{
		return size == 0;
	}

	bool ParseLunarStateResponsePayload(const uint8_t* d, size_t sz, LunarStateResponse& out)
	{
		size_t pos = 0;
		uint8_t status = 0;
		if (!ReadU8(d, sz, pos, status)) return false;
		if (!ReadU8(d, sz, pos, out.phase)) return false;
		if (!ReadFloatLE(d, sz, pos, out.illumination)) return false;
		if (!ReadU64LE(d, sz, pos, out.cycleStartMs)) return false;
		if (!ReadU64LE(d, sz, pos, out.cycleDurationMs)) return false;
		out.status = static_cast<LunarStatus>(status);
		return pos == sz;
	}

	bool ParseLunarPhaseChangeNotificationPayload(const uint8_t* d, size_t sz, LunarPhaseChangeNotification& out)
	{
		size_t pos = 0;
		if (!ReadU8(d, sz, pos, out.newPhase)) return false;
		if (!ReadFloatLE(d, sz, pos, out.newIllumination)) return false;
		if (!ReadU64LE(d, sz, pos, out.nextChangeTsMs)) return false;
		return pos == sz;
	}
}
