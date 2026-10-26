// ETAPE3.4 (chantier 4.4) — canal fiable minimal sur l'UDP de jeu.
// Voir ReliableChannel.h pour le principe.
//
// Formats (petit-boutiste, après l'en-tête du protocole de jeu) :
//   Reliable (102)    : seq u32 | paquet du protocole emballé (en-tête compris)
//   ReliableAck (103) : base u32 (tout jusqu'à base reçu) | masque u64
//                       (bit i = message base + 1 + i reçu, en avance d'un trou)

#include "src/shared/network/ReliableChannel.h"

#include <algorithm>
#include <array>

namespace engine::network::reliable
{
	using engine::server::MessageKind;

	namespace
	{
		void PutU32(std::vector<std::byte>& out, uint32_t v)
		{
			for (int i = 0; i < 4; ++i)
				out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFu));
		}

		void PutU64(std::vector<std::byte>& out, uint64_t v)
		{
			for (int i = 0; i < 8; ++i)
				out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFu));
		}

		uint32_t GetU32(std::span<const std::byte> in, size_t at)
		{
			uint32_t v = 0;
			for (int i = 0; i < 4; ++i)
				v |= static_cast<uint32_t>(static_cast<uint8_t>(in[at + static_cast<size_t>(i)])) << (8 * i);
			return v;
		}

		uint64_t GetU64(std::span<const std::byte> in, size_t at)
		{
			uint64_t v = 0;
			for (int i = 0; i < 8; ++i)
				v |= static_cast<uint64_t>(static_cast<uint8_t>(in[at + static_cast<size_t>(i)])) << (8 * i);
			return v;
		}
	}

	bool IsReliableKind(MessageKind kind)
	{
		switch (kind)
		{
		// Client -> shard : demandes du joueur qui changent l'état (une demande perdue
		// serait un clic sans effet, une demande en double est refusée par le shard).
		case MessageKind::PickupRequest:
		case MessageKind::TalkRequest:
		case MessageKind::ChatSend:
		case MessageKind::FriendRequest:
		case MessageKind::FriendAccept:
		case MessageKind::FriendDecline:
		case MessageKind::FriendRemove:
		case MessageKind::PartyInvite:
		case MessageKind::PartyAccept:
		case MessageKind::PartyDecline:
		case MessageKind::PartyKick:
		case MessageKind::PartyLootMode:
		case MessageKind::PartyLeave:
		case MessageKind::GuildCreate:
		case MessageKind::GuildInvite:
		case MessageKind::GuildInviteAccept:
		case MessageKind::GuildInviteDecline:
		case MessageKind::GuildKick:
		case MessageKind::GuildPromote:
		case MessageKind::GuildMotdUpdate:
		case MessageKind::ShopBuyRequest:
		case MessageKind::ShopSellRequest:
		case MessageKind::AuctionBrowseRequest:
		case MessageKind::AuctionListItemRequest:
		case MessageKind::AuctionBidRequest:
		case MessageKind::AuctionBuyoutRequest:
		case MessageKind::TradeRequest:
		case MessageKind::TradeAccept:
		case MessageKind::TradeDecline:
		case MessageKind::TradeAddItem:
		case MessageKind::TradeSetGold:
		case MessageKind::TradeLock:
		case MessageKind::TradeConfirm:
		case MessageKind::TradeCancel:
		case MessageKind::HarvestRequest:
		case MessageKind::HarvestCancelRequest:
		case MessageKind::LearnProfessionRequest:
		case MessageKind::CraftRecipeListRequest:
		case MessageKind::CraftRequest:
		case MessageKind::CraftCancelRequest:
		case MessageKind::RespawnRequest:
		case MessageKind::SetActionBarLayout:
		case MessageKind::ChooseClassSkillRequest:
		case MessageKind::QuestAcceptRequest:
		case MessageKind::QuestTurnInRequest:
		case MessageKind::EquipRequest:
		case MessageKind::UnequipRequest:
		case MessageKind::SetBeltLayout:
		// Shard -> client : résultats et états qui ne sont envoyés qu'au changement (une
		// perte laisserait le client faux jusqu'au changement suivant).
		case MessageKind::ZoneChange:
		case MessageKind::InventoryDelta:
		case MessageKind::QuestDelta:
		case MessageKind::EventState:
		case MessageKind::ChatRelay:
		case MessageKind::FriendRequestNotify:
		case MessageKind::FriendListSync:
		case MessageKind::FriendStatusUpdate:
		case MessageKind::PartyInviteNotify:
		case MessageKind::PartyUpdate:
		case MessageKind::GuildCreateResult:
		case MessageKind::GuildInviteNotify:
		case MessageKind::GuildRosterSync:
		case MessageKind::WalletUpdate:
		case MessageKind::ShopOpen:
		case MessageKind::AuctionBrowseResult:
		case MessageKind::TradeRequestNotify:
		case MessageKind::TradeWindowUpdate:
		case MessageKind::TradeComplete:
		case MessageKind::TradeCancelled:
		case MessageKind::HarvestStart:
		case MessageKind::HarvestComplete:
		case MessageKind::HarvestCancelled:
		case MessageKind::ProfessionUpdate:
		case MessageKind::CraftRecipeListResult:
		case MessageKind::CraftStart:
		case MessageKind::CraftComplete:
		case MessageKind::CraftCancelled:
		case MessageKind::PlayerStats:
		case MessageKind::LootNotify:
		case MessageKind::ActionBarLayoutUpdate:
		case MessageKind::ClassProgressionUpdate:
		case MessageKind::QuestGiverList:
		case MessageKind::PlayerXpUpdate:
		case MessageKind::EquipmentUpdate:
		case MessageKind::BeltLayoutUpdate:
			return true;
		default:
			// Non fiable : Hello/Welcome/Goodbye (gérés à part), Input, Snapshot,
			// ForcePosition et MoveCorrection (ETAPE3.3 : époque de mouvement + renvoi
			// toutes les 500 ms ; un ForcePosition fiable livré en retard ramènerait le
			// joueur en arrière sans pouvoir le détecter),
			// AttackRequest et CastRequest (une attaque en retard vaut pire qu'une
			// attaque perdue), CombatEvent, EmoteRelay, ResourceUpdate, CastBarUpdate,
			// AuraUpdate, ThreatUpdate (flux fréquents ou remplacés par le suivant), et
			// les enveloppes Reliable / ReliableAck elles-mêmes.
			return false;
		}
	}

	uint64_t ReliableChannel::CurrentRto() const
	{
		if (m_srttMs == 0u)
			return kInitialRtoMs;
		return std::clamp<uint64_t>(m_srttMs * 2u + 50u, kMinRtoMs, kMaxRtoMs);
	}

	bool ReliableChannel::Queue(std::span<const std::byte> packet, uint64_t nowMs, std::vector<std::byte>& outDatagram)
	{
		outDatagram.clear();
		if (m_failed || packet.empty() || packet.size() > kMaxMessageSize)
			return false;
		if (m_inFlight.size() >= kMaxInFlight || m_nextSeq == 0xFFFFFFFFu)
		{
			m_failed = true; // pair muet ou saturé : la session doit tomber
			return false;
		}
		const uint32_t seq = m_nextSeq++;
		outDatagram = engine::server::BeginProtocolMessage(MessageKind::Reliable, 4u + packet.size());
		PutU32(outDatagram, seq);
		outDatagram.insert(outDatagram.end(), packet.begin(), packet.end());
		InFlight entry;
		entry.seq = seq;
		entry.datagram = outDatagram;
		entry.firstSentMs = nowMs;
		entry.lastSentMs = nowMs;
		entry.rtoMs = CurrentRto();
		m_inFlight.push_back(std::move(entry));
		return true;
	}

	bool ReliableChannel::OnReliable(std::span<const std::byte> packet, uint64_t nowMs,
		std::vector<std::vector<std::byte>>& outDelivered)
	{
		(void)nowMs;
		std::span<const std::byte> payload;
		if (!engine::server::DecodeProtocolPayload(packet, MessageKind::Reliable, payload) || payload.size() < 4u + 8u)
			return false; // seq + au moins un en-tête de paquet emballé
		const uint32_t seq = GetU32(payload, 0);
		if (seq == 0u)
			return false;
		const std::span<const std::byte> inner = payload.subspan(4);
		engine::server::MessageKind innerKind{};
		if (!engine::server::PeekMessageKind(inner, innerKind) || innerKind == MessageKind::Reliable
			|| innerKind == MessageKind::ReliableAck)
			return false; // pas d'enveloppe dans une enveloppe
		m_ackPending = true; // même un doublon mérite un accusé (le premier s'est perdu)
		if (seq <= m_delivered)
			return true; // déjà livré
		if (seq - m_delivered > kMaxOutOfOrder)
			return true; // trop loin devant : l'émetteur le renverra
		m_outOfOrder.emplace(seq, std::vector<std::byte>(inner.begin(), inner.end()));
		for (auto it = m_outOfOrder.begin(); it != m_outOfOrder.end() && it->first == m_delivered + 1u;)
		{
			outDelivered.push_back(std::move(it->second));
			++m_delivered;
			it = m_outOfOrder.erase(it);
		}
		return true;
	}

	void ReliableChannel::Acknowledge(uint32_t seq, uint64_t nowMs)
	{
		const auto it = std::lower_bound(m_inFlight.begin(), m_inFlight.end(), seq,
			[](const InFlight& e, uint32_t v) { return e.seq < v; });
		if (it == m_inFlight.end() || it->seq != seq)
			return;
		// Temps d'aller-retour : seulement sur un message jamais renvoyé (sinon on ne sait
		// pas à quel envoi l'accusé répond). Lissage 7/8.
		if (!it->retransmitted && nowMs >= it->firstSentMs)
		{
			const uint64_t sample = nowMs - it->firstSentMs;
			m_srttMs = (m_srttMs == 0u) ? (std::max)(sample, uint64_t{ 1 }) : (m_srttMs * 7u + sample) / 8u;
		}
		m_inFlight.erase(it);
	}

	bool ReliableChannel::OnAck(std::span<const std::byte> packet, uint64_t nowMs)
	{
		std::span<const std::byte> payload;
		if (!engine::server::DecodeProtocolPayload(packet, MessageKind::ReliableAck, payload) || payload.size() != 12u)
			return false;
		const uint32_t base = GetU32(payload, 0);
		const uint64_t bitmap = GetU64(payload, 4);
		if (base >= m_nextSeq)
			return false; // accuse un message jamais envoyé
		while (!m_inFlight.empty() && m_inFlight.front().seq <= base)
			Acknowledge(m_inFlight.front().seq, nowMs);
		for (uint32_t i = 0; i < 64u && bitmap != 0u; ++i)
		{
			if ((bitmap & (uint64_t{ 1 } << i)) != 0u)
				Acknowledge(base + 1u + i, nowMs);
		}
		return true;
	}

	void ReliableChannel::Update(uint64_t nowMs, std::vector<std::vector<std::byte>>& outDatagrams)
	{
		if (!m_failed)
		{
			size_t resent = 0;
			for (InFlight& entry : m_inFlight)
			{
				if (nowMs - entry.firstSentMs >= kGiveUpMs)
				{
					m_failed = true;
					break;
				}
				// Au plus 32 renvois par appel : pas de rafale après une coupure.
				if (resent < 32u && nowMs - entry.lastSentMs >= entry.rtoMs)
				{
					outDatagrams.push_back(entry.datagram);
					entry.lastSentMs = nowMs;
					entry.rtoMs = (std::min)(entry.rtoMs * 2u, kMaxRtoMs);
					entry.retransmitted = true;
					++m_retransmits;
					++resent;
				}
			}
		}
		if (m_ackPending && nowMs - m_lastAckSentMs >= kAckIntervalMs)
		{
			uint64_t bitmap = 0;
			for (const auto& [seq, bytes] : m_outOfOrder)
			{
				(void)bytes;
				const uint32_t offset = seq - m_delivered - 1u;
				if (offset < 64u)
					bitmap |= uint64_t{ 1 } << offset;
			}
			std::vector<std::byte> ack = engine::server::BeginProtocolMessage(MessageKind::ReliableAck, 12u);
			PutU32(ack, m_delivered);
			PutU64(ack, bitmap);
			outDatagrams.push_back(std::move(ack));
			m_ackPending = false;
			m_lastAckSentMs = nowMs;
		}
	}

	void ReliableChannel::Reset()
	{
		m_nextSeq = 1;
		m_inFlight.clear();
		m_srttMs = 0;
		m_retransmits = 0;
		m_failed = false;
		m_delivered = 0;
		m_outOfOrder.clear();
		m_ackPending = false;
		m_lastAckSentMs = 0;
	}
}
