#pragma once
// Wire payloads de la carte à découvrir (opcodes 208–210). Voir
// src/shared/world/MapDiscovery.h pour le découpage en cases.
//
//   - Request (208, client → master) : uint16 startIndex (page de lecture).
//   - Response (209, master → client) :
//       uint8  error (cf. MapDiscoveryErrorCode)
//       si error == 0 : uint16 nextIndex (0 = dernière page), uint16 count,
//                       <count> entrées
//   - Update (210, client → master, sans réponse) : uint16 count, <count> entrées
//
//   Entrée : uint32 cellZoneId (CellZoneId), uint8 encoding (BitmapEncoding),
//            uint16 length, <length> octets du masque encodé.
//
// Le personnage est celui de la connexion côté master (après EnterWorld).
// Format : ByteReader/ByteWriter little-endian.

#include "src/shared/world/MapDiscovery.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace engine::network
{
	/// Code d'erreur de la réponse 209.
	enum class MapDiscoveryErrorCode : uint8_t
	{
		Ok           = 0,
		Unauthorized = 6, ///< Pas de session / pas de personnage en jeu sur la connexion.
		Unavailable  = 7, ///< Store indisponible (mode no-DB) ou erreur DB.
	};

	/// Masque d'une cellule (décodé).
	struct MapDiscoveryCell
	{
		uint32_t cellZoneId = 0;
		engine::world::discovery::CellBitmap bitmap;
	};

	struct MapDiscoveryRequestPayload
	{
		uint16_t startIndex = 0;
	};

	struct MapDiscoveryResponsePayload
	{
		uint8_t error = 0;
		uint16_t nextIndex = 0; ///< 0 = plus rien à lire
		std::vector<MapDiscoveryCell> cells;
	};

	struct MapDiscoveryUpdatePayload
	{
		std::vector<MapDiscoveryCell> cells;
	};

	/// Nombre maximal de cellules par paquet garanti de tenir dans un paquet
	/// (pire cas : masques bruts de 3 200 octets → 4 × 3 207 + en-têtes < 16 Kio,
	/// kProtocolV1MaxPacketSize). FIX.16 : 16 → 4 avec les cases de 62,5 m.
	inline constexpr size_t kMapDiscoveryCellsPerPacket = 4u;
	static_assert(kMapDiscoveryCellsPerPacket * (4u + 1u + 2u + engine::world::discovery::kBitmapBytes) + 64u <= 16384u,
		"une page de masques bruts doit tenir dans un paquet");

	std::vector<uint8_t> BuildMapDiscoveryRequestPayload(uint16_t startIndex);
	std::optional<MapDiscoveryRequestPayload> ParseMapDiscoveryRequestPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildMapDiscoveryResponsePayload(uint8_t error, uint16_t nextIndex,
		const std::vector<MapDiscoveryCell>& cells);
	std::optional<MapDiscoveryResponsePayload> ParseMapDiscoveryResponsePayload(const uint8_t* payload, size_t payloadSize);
	/// Paquet complet (header + payload) pour le handler master.
	std::vector<uint8_t> BuildMapDiscoveryResponsePacket(uint8_t error, uint16_t nextIndex,
		const std::vector<MapDiscoveryCell>& cells, uint32_t requestId, uint64_t sessionIdHeader);

	/// \pre cells.size() ≤ kMapDiscoveryCellsPerPacket (sinon payload vide).
	std::vector<uint8_t> BuildMapDiscoveryUpdatePayload(const std::vector<MapDiscoveryCell>& cells);
	std::optional<MapDiscoveryUpdatePayload> ParseMapDiscoveryUpdatePayload(const uint8_t* payload, size_t payloadSize);
}
