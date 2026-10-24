// Implémentation Parse/Build des payloads de la carte à découvrir (208–210).
// Convention identique aux autres *Payloads.cpp : Build*Payload = payload nu,
// Build*ResponsePacket = header + payload via PacketBuilder (handler master).

#include "src/shared/network/MapDiscoveryPayloads.h"

#include "src/shared/network/ByteReader.h"
#include "src/shared/network/ByteWriter.h"
#include "src/shared/network/PacketBuilder.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <span>

namespace engine::network
{
	namespace
	{
		namespace disc = engine::world::discovery;

		/// Écrit une entrée (zone, encodage, longueur, octets).
		bool WriteCell(ByteWriter& w, const MapDiscoveryCell& c)
		{
			disc::BitmapEncoding enc = disc::BitmapEncoding::Raw;
			const std::vector<uint8_t> data = disc::EncodeBitmap(c.bitmap, enc);
			const uint8_t encByte = static_cast<uint8_t>(enc);
			return w.WriteU32(c.cellZoneId)
				&& w.WriteBytes(&encByte, 1u)
				&& w.WriteU16(static_cast<uint16_t>(data.size()))
				&& w.WriteBytes(data.data(), data.size());
		}

		/// Lit et décode une entrée. false si tronquée ou masque invalide.
		bool ReadCell(ByteReader& r, MapDiscoveryCell& c)
		{
			uint8_t encByte = 0;
			uint16_t len = 0;
			if (!r.ReadU32(c.cellZoneId) || !r.ReadBytes(&encByte, 1u) || !r.ReadU16(len))
				return false;
			if (len > disc::kBitmapBytes || len > r.Remaining())
				return false;
			std::vector<uint8_t> data(len);
			if (len > 0u && !r.ReadBytes(data.data(), len))
				return false;
			return disc::DecodeBitmap(static_cast<disc::BitmapEncoding>(encByte),
				std::span<const uint8_t>(data.data(), data.size()), c.bitmap);
		}

		bool WriteCells(ByteWriter& w, const std::vector<MapDiscoveryCell>& cells)
		{
			if (cells.size() > kMapDiscoveryCellsPerPacket || !w.WriteArrayCount(static_cast<uint16_t>(cells.size())))
				return false;
			for (const MapDiscoveryCell& c : cells)
				if (!WriteCell(w, c))
					return false;
			return true;
		}

		bool ReadCells(ByteReader& r, std::vector<MapDiscoveryCell>& cells)
		{
			uint16_t count = 0;
			if (!r.ReadArrayCount(count) || count > kMapDiscoveryCellsPerPacket)
				return false;
			cells.resize(count);
			for (MapDiscoveryCell& c : cells)
				if (!ReadCell(r, c))
					return false;
			return true;
		}

		bool WriteResponseBody(ByteWriter& w, uint8_t error, uint16_t nextIndex, const std::vector<MapDiscoveryCell>& cells)
		{
			if (!w.WriteBytes(&error, 1u))
				return false;
			if (error != 0u)
				return true;
			return w.WriteU16(nextIndex) && WriteCells(w, cells);
		}
	}

	std::vector<uint8_t> BuildMapDiscoveryRequestPayload(uint16_t startIndex)
	{
		std::vector<uint8_t> buf(2u, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!w.WriteU16(startIndex))
			return {};
		return buf;
	}

	std::optional<MapDiscoveryRequestPayload> ParseMapDiscoveryRequestPayload(const uint8_t* payload, size_t payloadSize)
	{
		MapDiscoveryRequestPayload out;
		if (payload == nullptr || payloadSize < 2u)
			return out; // payload vide accepté : première page
		ByteReader r(payload, payloadSize);
		if (!r.ReadU16(out.startIndex))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMapDiscoveryResponsePayload(uint8_t error, uint16_t nextIndex,
		const std::vector<MapDiscoveryCell>& cells)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteResponseBody(w, error, nextIndex, cells))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<MapDiscoveryResponsePayload> ParseMapDiscoveryResponsePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 1u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MapDiscoveryResponsePayload out;
		if (!r.ReadBytes(&out.error, 1u))
			return std::nullopt;
		if (out.error != 0u)
			return out;
		if (!r.ReadU16(out.nextIndex) || !ReadCells(r, out.cells))
			return std::nullopt;
		return out;
	}

	std::vector<uint8_t> BuildMapDiscoveryResponsePacket(uint8_t error, uint16_t nextIndex,
		const std::vector<MapDiscoveryCell>& cells, uint32_t requestId, uint64_t sessionIdHeader)
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (!WriteResponseBody(w, error, nextIndex, cells))
			return {};
		const size_t payloadBytes = w.Offset();
		if (!builder.Finalize(kOpcodeMapDiscoveryResponse, 0u, requestId, sessionIdHeader, payloadBytes))
			return {};
		return builder.Data();
	}

	std::vector<uint8_t> BuildMapDiscoveryUpdatePayload(const std::vector<MapDiscoveryCell>& cells)
	{
		std::vector<uint8_t> buf(kProtocolV1MaxPacketSize, 0u);
		ByteWriter w(buf.data(), buf.size());
		if (!WriteCells(w, cells))
			return {};
		buf.resize(w.Offset());
		return buf;
	}

	std::optional<MapDiscoveryUpdatePayload> ParseMapDiscoveryUpdatePayload(const uint8_t* payload, size_t payloadSize)
	{
		if (payload == nullptr || payloadSize < 2u)
			return std::nullopt;
		ByteReader r(payload, payloadSize);
		MapDiscoveryUpdatePayload out;
		if (!ReadCells(r, out.cells))
			return std::nullopt;
		return out;
	}
}
