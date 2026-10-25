#pragma once
// ChatChannelRouting — quel processus relaie quel canal de chat (ETAPE5.1).
//
//   - Proximité (Dire, Crier) : le SHARD, qui connaît les positions
//     (ServerApp::HandleChatSend, rayons 25 m / 200 m, MessageKind::ChatSend).
//   - Chuchotement, groupe, guilde, zone, global, raid, amis : le MASTER
//     (ChatRelayHandler), qui connaît toutes les sessions et les comptes.
//   - Server : réservé aux annonces du serveur ; jamais accepté d'un client.
//
// Logique pure (valeurs de engine::net::ChatChannel), testée par
// chat_channel_routing_tests.

#include "src/shared/net/ChatSystem.h"

#include <cstdint>

namespace engine::server::chat
{
	/// Destination d'un canal envoyé par un client.
	enum class ChatChannelRoute : uint8_t
	{
		Master,    ///< Relayé par le master (ChatRelayHandler).
		Shard,     ///< Proximité : à envoyer au shard en UDP ; refusé par le master.
		Forbidden, ///< Interdit à un client (annonces serveur).
	};

	/// Route d'un canal client, à partir de sa valeur wire.
	constexpr ChatChannelRoute RouteClientChatChannel(uint8_t channelWire)
	{
		using engine::net::ChatChannel;
		if (channelWire == static_cast<uint8_t>(ChatChannel::Say) || channelWire == static_cast<uint8_t>(ChatChannel::Yell))
		{
			return ChatChannelRoute::Shard;
		}
		if (channelWire == static_cast<uint8_t>(ChatChannel::Server))
		{
			return ChatChannelRoute::Forbidden;
		}
		return ChatChannelRoute::Master;
	}
}
