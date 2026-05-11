// CMANGOS.42 (Phase 4.42 step 3+4) — Implementation Parse/Build des payloads Weather.
//
// Convention identique aux autres *Payloads.cpp du repo :
//   - Build*Payload retourne un std::vector<uint8_t> contenant uniquement le
//     payload (sans header protocol_v1). Utilise pour tests round-trip et
//     pour les requests cote client (envoyees via SendGenericRequestAsync
//     qui ajoute le header).
//   - Build*ResponsePacket / Build*NotificationPacket utilise PacketBuilder
//     pour assembler le paquet complet header + payload, pret a passer a
//     NetServer::Send.
//   - Parse* lit le payload nu (sans header).
//
// Sequencage des float32 (intensity 0..1) : on utilise WriteBytes /
// ReadBytes sur 4 octets bruts. Le float est suppose IEEE-754 32-bit en
// little-endian (cas Windows MSVC + Linux GCC sur x86_64 et arm64).
// Pattern identique a CharacterPayloads.cpp pour les coordonnees de spawn.

#include "src/shared/network/WeatherPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::network
{
	namespace
	{
		/// Ecrit un float32 little-endian sur 4 octets bruts. Renvoie false
		/// s'il n'y a pas la place dans le buffer.
		bool WriteFloatLE(ByteWriter& w, float value)
		{
			return w.WriteBytes(reinterpret_cast<const uint8_t*>(&value), sizeof(float));
		}

		/// Lit un float32 little-endian sur 4 octets bruts. Renvoie false
		/// s'il n'y a pas assez d'octets disponibles.
		bool ReadFloatLE(ByteReader& r, float& out)
		{
			return r.ReadBytes(reinterpret_cast<uint8_t*>(&out), sizeof(float));
		}

		/// Ecrit un WeatherZoneSummary (zoneId, name, kind, intensity).
		bool WriteZoneSummary(ByteWriter& w, const WeatherZoneSummary& zone)
		{
			if (!w.WriteU32(zone.zoneId))                 return false;
			if (!w.WriteString(zone.name))                return false;
			if (!w.WriteBytes(&zone.kind, 1u))            return false;
			if (!WriteFloatLE(w, zone.intensity))         return false;
			return true;
		}

		/// Lit un WeatherZoneSummary.
		bool ReadZoneSummary(ByteReader& r, WeatherZoneSummary& out)
		{
			if (!r.ReadU32(out.zoneId))                   return false;
			if (!r.ReadString(out.name))                  return false;
			uint8_t kindByte = 0;
			if (!r.ReadBytes(&kindByte, 1u))              return false;
			out.kind = kindByte;
			if (!ReadFloatLE(r, out.intensity))           return false;
			return true;
		}

		/// Serialise le body d'un WEATHER_UPDATE_NOTIFICATION (zoneId, kind,
		/// intensity).
		bool WriteUpdateNotificationBody(ByteWriter& w, uint32_t zoneId, uint8_t kind, float intensity)
		{
			if (!w.WriteU32(zoneId))                      return false;
			if (!w.WriteBytes(&kind, 1u))                 return false;
			if (!WriteFloatLE(w, intensity))              return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// WEATHER_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<WeatherListRequestPayload> ParseWeatherListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return WeatherListRequestPayload{};
	}

	std::vector<uint8_t> BuildWeatherListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// WEATHER_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<WeatherListResponsePayload> ParseWeatherListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		WeatherListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.zones.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			WeatherZoneSummary z;
			if (!ReadZoneSummary(r, z)) return std::nullopt;
			out.zones.push_back(std::move(z));
		}
		return out;
	}

	std::vector<uint8_t> BuildWeatherListResponsePayload(uint8_t error, const std::vector<WeatherZoneSummary>& zones)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(zones.size()))) return {};
			for (const auto& z : zones)
			{
				if (!WriteZoneSummary(w, z)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildWeatherListResponsePacket(uint8_t error, const std::vector<WeatherZoneSummary>& zones,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(zones.size()))) return {};
			for (const auto& z : zones)
			{
				if (!WriteZoneSummary(w, z)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeWeatherListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// WEATHER_SUBSCRIBE — Request
	// -------------------------------------------------------------------------

	std::optional<WeatherSubscribeRequestPayload> ParseWeatherSubscribeRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		WeatherSubscribeRequestPayload out;
		if (!r.ReadU32(out.zoneId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildWeatherSubscribeRequestPayload(uint32_t zoneId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(zoneId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// WEATHER_SUBSCRIBE — Response
	// -------------------------------------------------------------------------

	std::optional<WeatherSubscribeResponsePayload> ParseWeatherSubscribeResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		WeatherSubscribeResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		// Si Ok, body suit avec uint8 currentKind + float intensity.
		if (payloadSize < 1u + 1u + 4u) return std::nullopt;
		uint8_t kindByte = 0;
		if (!r.ReadBytes(&kindByte, 1u)) return std::nullopt;
		out.currentKind = kindByte;
		if (!ReadFloatLE(r, out.currentIntensity)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildWeatherSubscribeResponsePayload(uint8_t error, uint8_t currentKind, float currentIntensity)
	{
		// 1 octet error (+ 1 + 4 = 5 si Ok). Buffer max 6.
		std::vector<uint8_t> buf(6u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteBytes(&currentKind, 1u)) return {};
			if (!WriteFloatLE(w, currentIntensity)) return {};
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildWeatherSubscribeResponsePacket(uint8_t error, uint8_t currentKind, float currentIntensity,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteBytes(&currentKind, 1u)) return {};
			if (!WriteFloatLE(w, currentIntensity)) return {};
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeWeatherSubscribeResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// WEATHER_UNSUBSCRIBE — Request
	// -------------------------------------------------------------------------

	std::optional<WeatherUnsubscribeRequestPayload> ParseWeatherUnsubscribeRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 4u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		WeatherUnsubscribeRequestPayload out;
		if (!r.ReadU32(out.zoneId)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildWeatherUnsubscribeRequestPayload(uint32_t zoneId)
	{
		std::vector<uint8_t> buf(4u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU32(zoneId)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	// -------------------------------------------------------------------------
	// WEATHER_UNSUBSCRIBE — Response
	// -------------------------------------------------------------------------

	std::optional<WeatherUnsubscribeResponsePayload> ParseWeatherUnsubscribeResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		WeatherUnsubscribeResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildWeatherUnsubscribeResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildWeatherUnsubscribeResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeWeatherUnsubscribeResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// WEATHER_UPDATE_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<WeatherUpdateNotificationPayload> ParseWeatherUpdateNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 zoneId (4) + uint8 kind (1) + float intensity (4) = 9.
		if (!payload || payloadSize < 9u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		WeatherUpdateNotificationPayload out;
		if (!r.ReadU32(out.zoneId))      return std::nullopt;
		uint8_t kindByte = 0;
		if (!r.ReadBytes(&kindByte, 1u)) return std::nullopt;
		out.kind = kindByte;
		if (!ReadFloatLE(r, out.intensity)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildWeatherUpdateNotificationPayload(uint32_t zoneId, uint8_t kind, float intensity)
	{
		std::vector<uint8_t> buf(9u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteUpdateNotificationBody(w, zoneId, kind, intensity)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildWeatherUpdateNotificationPacket(uint32_t zoneId, uint8_t kind, float intensity,
		uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteUpdateNotificationBody(w, zoneId, kind, intensity)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeWeatherUpdateNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
