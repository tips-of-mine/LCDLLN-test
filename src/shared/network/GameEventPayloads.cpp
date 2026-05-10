// CMANGOS.31 (Phase 5.31 step 3+4) — Implementation Parse/Build des payloads
// GameEvent.
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
// Sequencage des uint64 (timestamps ms epoch, durations) : ByteWriter::WriteU64
// / ByteReader::ReadU64 deja existants (8 octets little-endian).

#include "src/shared/network/GameEventPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <vector>

namespace engine::network
{
	namespace
	{
		/// Ecrit un GameEventSummary (eventId, name, state, startTsMs,
		/// durationMs, recurMs).
		bool WriteEventSummary(ByteWriter& w, const GameEventSummary& ev)
		{
			if (!w.WriteU32(ev.eventId))                  return false;
			if (!w.WriteString(ev.name))                  return false;
			if (!w.WriteBytes(&ev.state, 1u))             return false;
			if (!w.WriteU64(ev.startTsMs))                return false;
			if (!w.WriteU64(ev.durationMs))               return false;
			if (!w.WriteU64(ev.recurMs))                  return false;
			return true;
		}

		/// Lit un GameEventSummary.
		bool ReadEventSummary(ByteReader& r, GameEventSummary& out)
		{
			if (!r.ReadU32(out.eventId))                  return false;
			if (!r.ReadString(out.name))                  return false;
			uint8_t stateByte = 0;
			if (!r.ReadBytes(&stateByte, 1u))             return false;
			out.state = stateByte;
			if (!r.ReadU64(out.startTsMs))                return false;
			if (!r.ReadU64(out.durationMs))               return false;
			if (!r.ReadU64(out.recurMs))                  return false;
			return true;
		}

		/// Serialise le body d'un GAME_EVENT_STATE_CHANGE_NOTIFICATION
		/// (eventId, newState, untilTsMs).
		bool WriteStateChangeBody(ByteWriter& w, uint32_t eventId, uint8_t newState, uint64_t untilTsMs)
		{
			if (!w.WriteU32(eventId))                     return false;
			if (!w.WriteBytes(&newState, 1u))             return false;
			if (!w.WriteU64(untilTsMs))                   return false;
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// GAME_EVENT_LIST — Request
	// -------------------------------------------------------------------------

	std::optional<GameEventListRequestPayload> ParseGameEventListRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return GameEventListRequestPayload{};
	}

	std::vector<uint8_t> BuildGameEventListRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// GAME_EVENT_LIST — Response
	// -------------------------------------------------------------------------

	std::optional<GameEventListResponsePayload> ParseGameEventListResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GameEventListResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		if (out.error != 0u) return out;
		uint16_t count = 0;
		if (!r.ReadArrayCount(count)) return std::nullopt;
		out.events.reserve(static_cast<size_t>(count));
		for (uint16_t i = 0; i < count; ++i)
		{
			GameEventSummary ev;
			if (!ReadEventSummary(r, ev)) return std::nullopt;
			out.events.push_back(std::move(ev));
		}
		return out;
	}

	std::vector<uint8_t> BuildGameEventListResponsePayload(uint8_t error, const std::vector<GameEventSummary>& events)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(events.size()))) return {};
			for (const auto& ev : events)
			{
				if (!WriteEventSummary(w, ev)) return {};
			}
		}
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGameEventListResponsePacket(uint8_t error, const std::vector<GameEventSummary>& events,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		if (error == 0u)
		{
			if (!w.WriteArrayCount(static_cast<uint16_t>(events.size()))) return {};
			for (const auto& ev : events)
			{
				if (!WriteEventSummary(w, ev)) return {};
			}
		}
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGameEventListResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GAME_EVENT_SUBSCRIBE — Request
	// -------------------------------------------------------------------------

	std::optional<GameEventSubscribeRequestPayload> ParseGameEventSubscribeRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte (abonnement global).
		return GameEventSubscribeRequestPayload{};
	}

	std::vector<uint8_t> BuildGameEventSubscribeRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// GAME_EVENT_SUBSCRIBE — Response
	// -------------------------------------------------------------------------

	std::optional<GameEventSubscribeResponsePayload> ParseGameEventSubscribeResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GameEventSubscribeResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGameEventSubscribeResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildGameEventSubscribeResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGameEventSubscribeResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GAME_EVENT_UNSUBSCRIBE — Request
	// -------------------------------------------------------------------------

	std::optional<GameEventUnsubscribeRequestPayload> ParseGameEventUnsubscribeRequestPayload(const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		// Payload vide accepte.
		return GameEventUnsubscribeRequestPayload{};
	}

	std::vector<uint8_t> BuildGameEventUnsubscribeRequestPayload()
	{
		return std::vector<uint8_t>{};
	}

	// -------------------------------------------------------------------------
	// GAME_EVENT_UNSUBSCRIBE — Response
	// -------------------------------------------------------------------------

	std::optional<GameEventUnsubscribeResponsePayload> ParseGameEventUnsubscribeResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (!payload || payloadSize < 1u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GameEventUnsubscribeResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u)) return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGameEventUnsubscribeResponsePayload(uint8_t error)
	{
		std::vector<uint8_t> buf(1u, 0u);
		buf[0] = error;
		return buf;
	}

	std::vector<uint8_t> BuildGameEventUnsubscribeResponsePacket(uint8_t error,
		uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!w.WriteBytes(&error, 1u)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGameEventUnsubscribeResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	// -------------------------------------------------------------------------
	// GAME_EVENT_STATE_CHANGE_NOTIFICATION (push, requestId=0)
	// -------------------------------------------------------------------------

	std::optional<GameEventStateChangeNotificationPayload> ParseGameEventStateChangeNotificationPayload(const uint8_t* payload, size_t payloadSize)
	{
		// Min : uint32 eventId (4) + uint8 newState (1) + uint64 untilTsMs (8) = 13.
		if (!payload || payloadSize < 13u) return std::nullopt;
		ByteReader r(payload, payloadSize);
		GameEventStateChangeNotificationPayload out;
		if (!r.ReadU32(out.eventId))      return std::nullopt;
		uint8_t stateByte = 0;
		if (!r.ReadBytes(&stateByte, 1u)) return std::nullopt;
		out.newState = stateByte;
		if (!r.ReadU64(out.untilTsMs))    return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildGameEventStateChangeNotificationPayload(uint32_t eventId, uint8_t newState, uint64_t untilTsMs)
	{
		std::vector<uint8_t> buf(13u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteStateChangeBody(w, eventId, newState, untilTsMs)) return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::vector<uint8_t> BuildGameEventStateChangeNotificationPacket(uint32_t eventId, uint8_t newState, uint64_t untilTsMs,
		uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteStateChangeBody(w, eventId, newState, untilTsMs)) return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeGameEventStateChangeNotification, 0u, 0u, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}
}
