// RetiredGameplayHandler — envoi de la réponse ERROR (voir RetiredGameplayHandler.h,
// ETAPE1.1 : masterd relais pur). La liste des opcodes est dans RetiredGameplayOpcodes.cpp.

#include "src/masterd/handlers/retired/RetiredGameplayHandler.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/NetErrorCode.h"
#include "src/shared/network/NetServer.h"

#include <span>
#include <string>

namespace engine::server
{
	void RetiredGameplayHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		const std::string_view feature = RetiredGameplayFeature(opcode);
		LOG_INFO(Net, "[RetiredGameplay] opcode {} ({}) refusé : fonction retirée du master, gérée par le shard (conn_id={})",
			opcode, feature, connId);
		if (!m_server)
			return;
		const std::string message = "fonction '" + std::string(feature) + "' retiree du master (geree par le shard)";
		const auto pkt = engine::network::BuildErrorPacket(engine::network::NetErrorCode::UNKNOWN_OPCODE,
			message, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, std::span<const uint8_t>(pkt.data(), pkt.size()));
	}
}
