// ADM.0 — Tests du relais des commandes MJ master → shard (GmShardRelay) et des
// paquets 216/217 (ShardPayloads).

#include "src/masterd/admin/GmShardRelay.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/ShardPayloads.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	struct Sent
	{
		uint32_t shardId = 0;
		std::vector<uint8_t> packet;
	};

	/// Décode le payload d'un paquet complet construit par Build*Packet.
	bool PayloadOf(const std::vector<uint8_t>& packet, uint16_t expectedOpcode, const uint8_t*& payload, size_t& size)
	{
		engine::network::PacketView view;
		if (engine::network::PacketView::Parse(packet.data(), packet.size(), view) != engine::network::PacketParseResult::Ok)
			return false;
		if (view.Opcode() != expectedOpcode)
			return false;
		payload = view.Payload();
		size = view.PayloadSize();
		return true;
	}
}

int main()
{
	using namespace engine::network;
	using engine::server::GmShardRelay;

	// --- Paquet 216 : aller-retour ---
	{
		GmCommandRequestPayload req;
		req.requestId = 7;
		req.actorAccountId = 11;
		req.actorCharacterId = 22;
		req.actorRole = 2;
		req.actorName = "Hubert";
		req.command = "tp";
		req.args = "\"lac de Marn\"";
		const auto packet = BuildMasterToShardGmCommandPacket(req);
		const uint8_t* p = nullptr;
		size_t n = 0;
		Check(!packet.empty() && PayloadOf(packet, kOpcodeMasterToShardGmCommand, p, n), "216 : construit");
		const auto back = ParseGmCommandRequestPayload(p, n);
		Check(back && back->requestId == 7 && back->actorAccountId == 11 && back->actorCharacterId == 22
			&& back->actorRole == 2 && back->actorName == "Hubert" && back->command == "tp" && back->args == req.args,
			"216 : aller-retour");
		GmCommandRequestPayload bad = req;
		bad.command.clear();
		Check(BuildMasterToShardGmCommandPacket(bad).empty(), "216 : nom vide refusé");
		bad = req;
		bad.requestId = 0;
		Check(BuildMasterToShardGmCommandPacket(bad).empty(), "216 : requestId nul refusé");
		Check(!ParseGmCommandRequestPayload(p, 5), "216 : payload tronqué refusé");
	}

	// --- Paquet 217 : aller-retour + bornes ---
	{
		GmCommandResultPayload res;
		res.requestId = 9;
		res.actorAccountId = 11;
		res.status = static_cast<uint8_t>(GmCommandStatus::NotFound);
		res.lines = { "ligne 1", "Joueur introuvable : Zoé" };
		const auto packet = BuildShardToMasterGmCommandResultPacket(res);
		const uint8_t* p = nullptr;
		size_t n = 0;
		Check(!packet.empty() && PayloadOf(packet, kOpcodeShardToMasterGmCommandResult, p, n), "217 : construit");
		const auto back = ParseGmCommandResultPayload(p, n);
		Check(back && back->requestId == 9 && back->status == res.status && back->lines == res.lines, "217 : aller-retour");

		GmCommandResultPayload big = res;
		big.lines.assign(200, std::string(400, 'x'));
		const auto bigPacket = BuildShardToMasterGmCommandResultPacket(big);
		Check(!bigPacket.empty() && bigPacket.size() <= kProtocolV1MaxPacketSize, "217 : réponse énorme bornée au paquet max");
		Check(PayloadOf(bigPacket, kOpcodeShardToMasterGmCommandResult, p, n), "217 : paquet borné lisible");
		const auto bigBack = ParseGmCommandResultPayload(p, n);
		Check(bigBack && bigBack->lines.size() <= kGmCommandMaxLines && !bigBack->lines.empty()
			&& bigBack->lines.back().find("tronquée") != std::string::npos, "217 : troncature signalée");
	}

	// --- Relais : envoi, corrélation, usurpation, délai ---
	{
		GmShardRelay relay(5.0);
		std::vector<Sent> sent;
		bool shardUp = true;
		relay.SetSend([&](uint32_t shardId, const std::vector<uint8_t>& packet) {
			if (!shardUp)
				return false;
			sent.push_back(Sent{ shardId, packet });
			return true;
		});
		std::vector<std::pair<uint64_t, std::string>> replies;
		relay.SetReply([&](uint64_t accountId, uint32_t, const std::string& line) { replies.emplace_back(accountId, line); });

		GmCommandRequestPayload req;
		req.actorAccountId = 11;
		req.actorCharacterId = 22;
		req.command = "shardinfo";
		Check(relay.Forward(0, 1, req, 0.0) == GmShardRelay::ForwardResult::NoShard, "relais : shard 0 refusé");
		Check(relay.Forward(3, 1, req, 0.0) == GmShardRelay::ForwardResult::Sent && sent.size() == 1u && sent[0].shardId == 3,
			"relais : envoi au shard 3");
		const uint8_t* p = nullptr;
		size_t n = 0;
		Check(PayloadOf(sent[0].packet, kOpcodeMasterToShardGmCommand, p, n), "relais : paquet 216");
		const auto forwarded = ParseGmCommandRequestPayload(p, n);
		Check(forwarded && forwarded->requestId != 0u, "relais : requestId attribué");

		GmCommandResultPayload res;
		res.requestId = forwarded ? forwarded->requestId : 0u;
		res.actorAccountId = 11;
		res.lines = { "Shard : 1 joueur(s)" };
		Check(!relay.OnResult(4, res), "relais : réponse d'un autre shard ignorée");
		GmCommandResultPayload spoof = res;
		spoof.actorAccountId = 99;
		Check(!relay.OnResult(3, spoof), "relais : réponse pour un autre compte ignorée");
		Check(relay.OnResult(3, res) && replies.size() == 1u && replies[0].first == 11u, "relais : réponse affichée");
		Check(!relay.OnResult(3, res), "relais : réponse en double ignorée");
		Check(relay.PendingCount() == 0u, "relais : plus rien en attente");

		// Shard injoignable : rien ne reste en attente.
		shardUp = false;
		Check(relay.Forward(3, 1, req, 1.0) == GmShardRelay::ForwardResult::SendFailed && relay.PendingCount() == 0u,
			"relais : shard injoignable");
		shardUp = true;

		// Délai dépassé : l'appelant est prévenu.
		replies.clear();
		Check(relay.Forward(3, 1, req, 10.0) == GmShardRelay::ForwardResult::Sent, "relais : envoi avant délai");
		Check(relay.Tick(12.0) == 0u, "relais : pas encore expiré");
		Check(relay.Tick(15.5) == 1u && replies.size() == 1u && replies[0].second.find("pas répondu") != std::string::npos,
			"relais : délai dépassé signalé");

		// Anti-rafale par compte.
		for (size_t i = 0; i < GmShardRelay::kMaxPendingPerAccount; ++i)
			(void)relay.Forward(3, 1, req, 20.0);
		Check(relay.Forward(3, 1, req, 20.0) == GmShardRelay::ForwardResult::Busy, "relais : trop de demandes en vol");
		GmCommandRequestPayload other = req;
		other.actorAccountId = 12;
		Check(relay.Forward(3, 2, other, 20.0) == GmShardRelay::ForwardResult::Sent, "relais : un autre compte n'est pas bloqué");
	}

	if (s_failCount == 0)
		std::cout << "gm_shard_relay_tests : tout est vert" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
