// TE.2 — test d'intégration loopback : deux clients se voient mutuellement.
//
// Périmètre (volontairement étroit) :
//   - Valide le **contrat wire** Hello/Welcome/Input/Snapshot de bout en bout
//     via `UdpTransport` (cross-platform, TA.1) + helpers `ServerProtocol`
//     (engine_core, TC.1).
//   - Reproduit le scénario « 2 clients connectés au même shard, chacun doit
//     voir l'avatar de l'autre dans le snapshot reçu », qui est l'expérience
//     minimale attendue de la réplication des joueurs (Phase D).
//
// Hors-périmètre (couverts par d'autres tests / par le runtime réel) :
//   - Gate session `AdmittedCharacterRegistry` (admitted_character_registry_tests).
//   - Anti-triche `AntiCheatGameplay`.
//   - AoI / interest set / spawn de `ServerApp` (lourde init data-driven).
//   - Côté client `GameplayUdpClient` : Winsock-only donc non exécutable sur
//     le runner Linux ; on simule le wire client via une seconde instance
//     `UdpTransport` qui parle le même protocole.
//
// Pattern repo : int main() + assert + std::puts, sans framework (cf. TE.1).
// Le preset CI Linux est Release (-DNDEBUG) → on neutralise NDEBUG pour que les
// assert mordent réellement.
#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "src/shardd/world/UdpTransport.h"
#include "src/shared/network/ServerProtocol.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using engine::server::Datagram;
using engine::server::DecodeHello;
using engine::server::DecodeInput;
using engine::server::DecodeSnapshot;
using engine::server::DecodeWelcome;
using engine::server::EncodeHello;
using engine::server::EncodeInput;
using engine::server::EncodeSnapshot;
using engine::server::EncodeWelcome;
using engine::server::Endpoint;
using engine::server::HelloMessage;
using engine::server::InputMessage;
using engine::server::MessageKind;
using engine::server::PeekMessageKind;
using engine::server::SnapshotEntity;
using engine::server::SnapshotMessage;
using engine::server::UdpTransport;
using engine::server::WelcomeMessage;

namespace
{
	/// Petite poignée d'aide : laisse le scheduler OS livrer un datagramme localhost
	/// (chargé surtout sur Windows ; sous Linux l'aller-retour est généralement
	/// déjà visible au premier `Receive`).
	void YieldForLoopback()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	/// Spin de poll jusqu'à recevoir au moins \a expected datagrammes, ou abandon
	/// après \a maxIterations. Évite les tests flaky tout en restant rapide.
	size_t WaitForDatagrams(UdpTransport& transport, std::vector<Datagram>& out, size_t expected, int maxIterations = 100)
	{
		out.clear();
		std::vector<Datagram> tmp;
		for (int i = 0; i < maxIterations; ++i)
		{
			tmp.clear();
			(void)transport.Receive(tmp, 32);
			for (auto& dg : tmp)
				out.push_back(dg);
			if (out.size() >= expected)
				break;
			YieldForLoopback();
		}
		return out.size();
	}

	/// Mini-serveur loopback : attribue un clientId à chaque endpoint vu en Hello,
	/// répond Welcome, mémorise les positions reçues en Input, et diffuse à chaque
	/// client un Snapshot listant **toutes** les entités connues. Reproduit le
	/// contrat externe minimal de `ServerApp::HandleHello/HandleInput/SendSnapshot`
	/// sans dépendre de son init data-driven.
	struct MiniServer
	{
		UdpTransport transport;
		std::vector<std::pair<uint32_t, Endpoint>> clients; // clientId → endpoint
		std::vector<SnapshotEntity> entities;               // une entrée par client, indexée comme `clients`
		uint32_t nextClientId = 100;                        // 100, 101, …
		uint32_t currentTick = 0;

		bool Init() { return transport.Init(0); }

		void PumpOnce()
		{
			std::vector<Datagram> incoming;
			(void)transport.Receive(incoming, 32);
			for (const auto& dg : incoming)
			{
				std::span<const std::byte> payload(dg.bytes.data(), dg.size);
				MessageKind kind{};
				if (!PeekMessageKind(payload, kind))
					continue;
				if (kind == MessageKind::Hello)
					HandleHello(dg.endpoint, payload);
				else if (kind == MessageKind::Input)
					HandleInput(payload);
			}
		}

		void HandleHello(const Endpoint& endpoint, std::span<const std::byte> payload)
		{
			HelloMessage hello{};
			if (!DecodeHello(payload, hello))
				return;
			uint32_t assignedId = 0;
			for (const auto& [cid, ep] : clients)
			{
				if (ep == endpoint)
				{
					assignedId = cid;
					break;
				}
			}
			if (assignedId == 0)
			{
				assignedId = nextClientId++;
				clients.emplace_back(assignedId, endpoint);
				SnapshotEntity ent{};
				ent.entityId = static_cast<uint64_t>(assignedId);
				entities.push_back(ent);
			}
			WelcomeMessage welcome{};
			welcome.clientId = assignedId;
			welcome.tickHz = hello.requestedTickHz != 0 ? hello.requestedTickHz : 20u;
			welcome.snapshotHz = hello.requestedSnapshotHz != 0 ? hello.requestedSnapshotHz : 10u;
			const auto packet = EncodeWelcome(welcome);
			(void)transport.Send(endpoint, packet);
		}

		void HandleInput(std::span<const std::byte> payload)
		{
			InputMessage input{};
			if (!DecodeInput(payload, input))
				return;
			for (size_t i = 0; i < clients.size(); ++i)
			{
				if (clients[i].first == input.clientId)
				{
					auto& ent = entities[i].state;
					ent.positionX = input.positionMetersX;
					ent.positionY = input.positionMetersY;
					ent.positionZ = input.positionMetersZ;
					ent.yawRadians = input.yawRadians;
					break;
				}
			}
		}

		void BroadcastSnapshot()
		{
			++currentTick;
			for (const auto& [cid, ep] : clients)
			{
				SnapshotMessage msg{};
				msg.clientId = cid;
				msg.serverTick = currentTick;
				msg.connectedClients = static_cast<uint16_t>(clients.size());
				msg.entityCount = static_cast<uint16_t>(entities.size());
				msg.receivedPackets = static_cast<uint32_t>(transport.ReceivedPacketCount());
				msg.sentPackets = static_cast<uint32_t>(transport.SentPacketCount());
				const auto packet = EncodeSnapshot(msg, entities);
				(void)transport.Send(ep, packet);
			}
		}
	};

	/// Cas central TE.2 : deux clients (A et B) envoient Hello puis Input ; après
	/// que le mini-serveur diffuse un Snapshot, **chacun** doit retrouver l'entité
	/// de l'**autre** dans le payload reçu.
	void TestTwoClientsSeeEachOther()
	{
		MiniServer server;
		assert(server.Init());
		const uint16_t serverPort = server.transport.BoundPort();
		assert(serverPort != 0);
		const Endpoint serverEndpoint{0x7F000001u, serverPort}; // 127.0.0.1

		// Deux « clients » : autre UdpTransport sur port éphémère (≡ rôle du
		// client Linux). En prod c'est `GameplayUdpClient` côté Windows.
		UdpTransport clientA;
		UdpTransport clientB;
		assert(clientA.Init(0));
		assert(clientB.Init(0));
		assert(clientA.BoundPort() != 0);
		assert(clientB.BoundPort() != 0);

		// Étape 1 — Hello des 2 clients.
		HelloMessage helloA{};
		helloA.requestedTickHz = 20;
		helloA.requestedSnapshotHz = 10;
		helloA.clientNonce = 0xAAAAAAAAAAAAAAAAull;
		assert(clientA.Send(serverEndpoint, EncodeHello(helloA)));

		HelloMessage helloB{};
		helloB.requestedTickHz = 20;
		helloB.requestedSnapshotHz = 10;
		helloB.clientNonce = 0xBBBBBBBBBBBBBBBBull;
		assert(clientB.Send(serverEndpoint, EncodeHello(helloB)));

		// Étape 2 — serveur consomme les 2 Hello et émet 2 Welcome.
		std::vector<Datagram> serverIn;
		assert(WaitForDatagrams(server.transport, serverIn, 2) == 2);
		for (const auto& dg : serverIn)
		{
			std::span<const std::byte> payload(dg.bytes.data(), dg.size);
			MessageKind kind{};
			assert(PeekMessageKind(payload, kind));
			assert(kind == MessageKind::Hello);
			server.HandleHello(dg.endpoint, payload);
		}
		assert(server.clients.size() == 2);

		// Étape 3 — chaque client reçoit son Welcome (et donc son clientId).
		std::vector<Datagram> clientAIn;
		std::vector<Datagram> clientBIn;
		assert(WaitForDatagrams(clientA, clientAIn, 1) >= 1);
		assert(WaitForDatagrams(clientB, clientBIn, 1) >= 1);

		WelcomeMessage welcomeA{};
		WelcomeMessage welcomeB{};
		assert(DecodeWelcome(std::span<const std::byte>(clientAIn.front().bytes.data(), clientAIn.front().size), welcomeA));
		assert(DecodeWelcome(std::span<const std::byte>(clientBIn.front().bytes.data(), clientBIn.front().size), welcomeB));
		assert(welcomeA.clientId != 0);
		assert(welcomeB.clientId != 0);
		assert(welcomeA.clientId != welcomeB.clientId);

		// Étape 4 — chaque client envoie un Input avec sa position propre (TC.1 : Y + yaw inclus).
		InputMessage inA{};
		inA.clientId = welcomeA.clientId;
		inA.inputSequence = 1u;
		inA.positionMetersX = 10.0f;
		inA.positionMetersY = 1.0f;
		inA.positionMetersZ = 5.0f;
		inA.yawRadians = 0.5f;
		assert(clientA.Send(serverEndpoint, EncodeInput(inA)));

		InputMessage inB{};
		inB.clientId = welcomeB.clientId;
		inB.inputSequence = 1u;
		inB.positionMetersX = 100.0f;
		inB.positionMetersY = 2.0f;
		inB.positionMetersZ = 50.0f;
		inB.yawRadians = -0.5f;
		assert(clientB.Send(serverEndpoint, EncodeInput(inB)));

		// Étape 5 — serveur ingère les 2 Inputs.
		assert(WaitForDatagrams(server.transport, serverIn, 2) == 2);
		for (const auto& dg : serverIn)
		{
			std::span<const std::byte> payload(dg.bytes.data(), dg.size);
			MessageKind kind{};
			assert(PeekMessageKind(payload, kind));
			assert(kind == MessageKind::Input);
			server.HandleInput(payload);
		}

		// Étape 6 — serveur diffuse 1 snapshot par client. Chaque snapshot doit
		// contenir LES DEUX entités (A et B).
		server.BroadcastSnapshot();
		assert(WaitForDatagrams(clientA, clientAIn, 2) >= 2); // welcome + snapshot
		assert(WaitForDatagrams(clientB, clientBIn, 2) >= 2);

		// On ne sait pas si le snapshot est le 2e datagramme reçu (Welcome reçu en 1er) :
		// on cherche dans tous les datagrammes accumulés ceux dont kind == Snapshot.
		auto FindSnapshot = [](const std::vector<Datagram>& in, SnapshotMessage& outMsg, std::vector<SnapshotEntity>& outEntities) -> bool
		{
			for (const auto& dg : in)
			{
				std::span<const std::byte> payload(dg.bytes.data(), dg.size);
				MessageKind kind{};
				if (!PeekMessageKind(payload, kind))
					continue;
				if (kind != MessageKind::Snapshot)
					continue;
				outEntities.clear();
				if (DecodeSnapshot(payload, outMsg, outEntities))
					return true;
			}
			return false;
		};

		SnapshotMessage snapA{};
		std::vector<SnapshotEntity> entitiesA;
		assert(FindSnapshot(clientAIn, snapA, entitiesA));
		SnapshotMessage snapB{};
		std::vector<SnapshotEntity> entitiesB;
		assert(FindSnapshot(clientBIn, snapB, entitiesB));

		// Le payload du snapshot doit annoncer 2 entités côté metadata ET 2 entrées dans la liste.
		assert(snapA.connectedClients == 2);
		assert(snapA.entityCount == 2);
		assert(entitiesA.size() == 2);
		assert(snapB.connectedClients == 2);
		assert(snapB.entityCount == 2);
		assert(entitiesB.size() == 2);

		// Client A doit voir l'entité B (et inversement). On vérifie aussi que
		// la position remontée par le snapshot correspond bien à ce que l'autre
		// client a envoyé en Input — ça valide le pipeline Input → state → Snapshot.
		auto ContainsEntity = [](const std::vector<SnapshotEntity>& list, uint64_t expectedId, float expectedX, float expectedZ) -> bool
		{
			for (const auto& e : list)
			{
				if (e.entityId == expectedId)
				{
					return e.state.positionX == expectedX && e.state.positionZ == expectedZ;
				}
			}
			return false;
		};

		assert(ContainsEntity(entitiesA, static_cast<uint64_t>(welcomeB.clientId), inB.positionMetersX, inB.positionMetersZ));
		assert(ContainsEntity(entitiesB, static_cast<uint64_t>(welcomeA.clientId), inA.positionMetersX, inA.positionMetersZ));

		std::puts("[OK] TestTwoClientsSeeEachOther");
	}
}

int main()
{
	TestTwoClientsSeeEachOther();
	std::puts("All Loopback integration tests passed");
	return 0;
}
