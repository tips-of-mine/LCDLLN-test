// MapDiscoveryHandler — implémentation. Voir le header.

#include "src/masterd/handlers/mapdiscovery/MapDiscoveryHandler.h"

#include "src/masterd/mapdiscovery/MysqlMapDiscoveryStore.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionCharacterMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/network/MapDiscoveryPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/core/Log.h"
#include "src/shared/world/WorldCells.h"

#include <optional>
#include <vector>

namespace engine::server
{
	void MapDiscoveryHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		if ((opcode != kOpcodeMapDiscoveryRequest && opcode != kOpcodeMapDiscoveryUpdate)
			|| !m_server || !m_sessions || !m_connMap || !m_charMap)
			return;
		const bool isRequest = opcode == kOpcodeMapDiscoveryRequest;
		auto reply = [&](MapDiscoveryErrorCode err) {
			if (!isRequest) return;
			auto pkt = BuildMapDiscoveryResponsePacket(static_cast<uint8_t>(err), 0u, {}, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
		};

		// Session valide + personnage en jeu sur CETTE connexion.
		auto connSessionId = m_connMap->GetSessionId(connId);
		if (!connSessionId || *connSessionId == 0 || sessionIdHeader == 0 || *connSessionId != sessionIdHeader
			|| !m_sessions->GetAccountId(*connSessionId))
		{
			reply(MapDiscoveryErrorCode::Unauthorized);
			return;
		}
		const auto character = m_charMap->GetByConnId(connId);
		if (!character || character->characterId == 0)
		{
			reply(MapDiscoveryErrorCode::Unauthorized);
			return;
		}
		if (m_store == nullptr || !m_store->IsAvailable())
		{
			reply(MapDiscoveryErrorCode::Unavailable);
			return;
		}

		if (isRequest)
		{
			const auto req = ParseMapDiscoveryRequestPayload(payload, payloadSize);
			const uint32_t start = req ? req->startIndex : 0u;
			std::vector<MysqlMapDiscoveryStore::StoredCell> rows;
			uint32_t total = 0;
			if (!m_store->ListPage(character->characterId, start,
					static_cast<uint32_t>(kMapDiscoveryCellsPerPacket), rows, total))
			{
				reply(MapDiscoveryErrorCode::Unavailable);
				return;
			}
			std::vector<MapDiscoveryCell> cells;
			cells.reserve(rows.size());
			for (const auto& r : rows)
				cells.push_back(MapDiscoveryCell{ r.cellZoneId, r.bitmap });
			const uint32_t next = start + static_cast<uint32_t>(kMapDiscoveryCellsPerPacket);
			const uint16_t nextIndex = (next < total && next <= 0xFFFFu) ? static_cast<uint16_t>(next) : 0u;
			auto pkt = BuildMapDiscoveryResponsePacket(0u, nextIndex, cells, requestId, sessionIdHeader);
			if (!pkt.empty()) m_server->Send(connId, pkt);
			LOG_DEBUG(Net, "[MapDiscovery] page envoyee (character_id={}, debut={}, {} cellule(s) / {})",
				character->characterId, start, cells.size(), total);
			return;
		}

		// ETAPE2.5 — un seul écrivain par personnage : le shard propriétaire révèle la
		// carte autour de la position autoritaire et l'écrit dans sa transaction de
		// sauvegarde. Les mises à jour envoyées par le client (210) sont ignorées
		// (les clients actuels continuent d'en envoyer, sans effet).
		LOG_DEBUG(Net, "[MapDiscovery] mise a jour client ignoree : carte ecrite par le shard (character_id={}, {} octets)",
			character->characterId, payloadSize);
	}
}
