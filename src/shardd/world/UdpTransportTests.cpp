// TA.1 — Tests du portage POSIX de UdpTransport (cf. docs/PLAN_replication_joueurs.md).
//
// Pattern aligne sur le reste du repo (WorldObjectTests.cpp, ObjectGuidTests.cpp) :
// int main() + assert + std::puts, sans framework. Cible CTest : udp_transport_tests
// (declaree dans la branche UNIX de src/CMakeLists.txt).
//
// Les tests valident le comportement *runtime* du transport sur la pile sockets de
// l'hote (BSD sockets sous Linux CI, Winsock sous Windows) via la boucle locale
// 127.0.0.1 : Init / Send / Receive / compteurs / non-blocking / Shutdown.
//
// IMPORTANT : le build CI Linux est Release (-DNDEBUG), ce qui transformerait
// chaque assert en no-op et ferait "passer" les tests sans rien verifier. On
// neutralise donc NDEBUG *avant* <cassert> pour que les assertions mordent
// reellement en CI.
#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "src/shardd/world/UdpTransport.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <thread>
#include <vector>

using engine::server::Datagram;
using engine::server::Endpoint;
using engine::server::UdpTransport;

namespace
{
	/// Adresse de boucle locale 127.0.0.1 en ordre hote (convention de Endpoint::address).
	constexpr uint32_t kLoopbackAddress = 0x7F000001u;

	/// Convertit un tableau d'octets bruts en span const pour Send().
	std::span<const std::byte> AsBytes(const std::vector<std::byte>& buffer)
	{
		return std::span<const std::byte>(buffer.data(), buffer.size());
	}

	/// Construit un petit payload deterministe de `count` octets (0, 1, 2, ...).
	std::vector<std::byte> MakePayload(size_t count)
	{
		std::vector<std::byte> payload(count);
		for (size_t i = 0; i < count; ++i)
		{
			payload[i] = static_cast<std::byte>(i & 0xFF);
		}
		return payload;
	}

	/// Receive est non-bloquant : sur la boucle locale le datagramme arrive quasi
	/// instantanement, mais on tolere un court delai de remise pour eviter le flaky.
	/// Reessaie jusqu'a obtenir >= 1 datagramme ou epuiser le budget (~1 s).
	size_t ReceiveWithRetry(UdpTransport& transport, std::vector<Datagram>& out, size_t maxDatagrams)
	{
		for (int attempt = 0; attempt < 200; ++attempt)
		{
			const size_t received = transport.Receive(out, maxDatagrams);
			if (received > 0)
			{
				return received;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return 0;
	}

	/// Init sur le port 0 (ephemere) doit reussir, marquer le transport valide,
	/// et exposer le port effectivement assigne par l'OS via BoundPort().
	void TestInitEphemeralPortExposesBoundPort()
	{
		UdpTransport transport;
		assert(transport.Init(0));
		assert(transport.IsValid());
		assert(transport.BoundPort() != 0);
		transport.Shutdown();
		assert(!transport.IsValid());
		std::puts("[OK] TestInitEphemeralPortExposesBoundPort");
	}

	/// Aller-retour boucle locale : un datagramme envoye est recu a l'identique,
	/// avec l'endpoint source correct et les compteurs rx/tx incrementes.
	void TestSendReceiveRoundTrip()
	{
		UdpTransport receiver;
		UdpTransport sender;
		assert(receiver.Init(0));
		assert(sender.Init(0));

		const std::vector<std::byte> payload = MakePayload(64);
		const Endpoint target{kLoopbackAddress, receiver.BoundPort()};
		assert(sender.Send(target, AsBytes(payload)));
		assert(sender.SentPacketCount() == 1);

		std::vector<Datagram> datagrams;
		const size_t received = ReceiveWithRetry(receiver, datagrams, 8);
		assert(received == 1);
		assert(datagrams.size() == 1);

		const Datagram& dg = datagrams.front();
		assert(dg.size == payload.size());
		for (size_t i = 0; i < payload.size(); ++i)
		{
			assert(dg.bytes[i] == payload[i]);
		}
		assert(dg.endpoint.address == kLoopbackAddress);
		assert(dg.endpoint.port == sender.BoundPort());
		assert(receiver.ReceivedPacketCount() == 1);

		receiver.Shutdown();
		sender.Shutdown();
		std::puts("[OK] TestSendReceiveRoundTrip");
	}

	/// Sans datagramme en attente, Receive doit retourner 0 immediatement (non
	/// bloquant) — c'est le chemin EWOULDBLOCK/EAGAIN (POSIX) / WSAEWOULDBLOCK (Win).
	void TestReceiveOnEmptyIsNonBlocking()
	{
		UdpTransport transport;
		assert(transport.Init(0));

		std::vector<Datagram> datagrams;
		const auto start = std::chrono::steady_clock::now();
		const size_t received = transport.Receive(datagrams, 8);
		const auto elapsed = std::chrono::steady_clock::now() - start;

		assert(received == 0);
		assert(datagrams.empty());
		// Non bloquant : ne doit pas attendre. Borne large pour absorber la charge CI.
		assert(elapsed < std::chrono::seconds(1));

		transport.Shutdown();
		std::puts("[OK] TestReceiveOnEmptyIsNonBlocking");
	}

	/// Receive ne remonte jamais plus de `maxDatagrams` paquets en un appel.
	void TestReceiveRespectsMaxDatagrams()
	{
		UdpTransport receiver;
		UdpTransport sender;
		assert(receiver.Init(0));
		assert(sender.Init(0));

		const std::vector<std::byte> payload = MakePayload(16);
		const Endpoint target{kLoopbackAddress, receiver.BoundPort()};
		for (int i = 0; i < 3; ++i)
		{
			assert(sender.Send(target, AsBytes(payload)));
		}

		// Laisse le temps aux 3 datagrammes d'arriver, puis lit avec un cap de 2.
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		std::vector<Datagram> datagrams;
		const size_t received = receiver.Receive(datagrams, 2);
		assert(received <= 2);
		assert(datagrams.size() == received);

		receiver.Shutdown();
		sender.Shutdown();
		std::puts("[OK] TestReceiveRespectsMaxDatagrams");
	}

	/// EndpointToString formate en notation decimale pointee + port, sans dependre
	/// de la plateforme (fonction pure).
	void TestEndpointToString()
	{
		assert(UdpTransport::EndpointToString(Endpoint{kLoopbackAddress, 8085}) == "127.0.0.1:8085");
		assert(UdpTransport::EndpointToString(Endpoint{0xC0A80101u, 1234}) == "192.168.1.1:1234");
		assert(UdpTransport::EndpointToString(Endpoint{0u, 0}) == "0.0.0.0:0");
		std::puts("[OK] TestEndpointToString");
	}

	/// Sur un transport non initialise : Send echoue proprement, Receive renvoie 0,
	/// aucun compteur ne bouge, et IsValid est faux.
	void TestUninitializedTransportIsInert()
	{
		UdpTransport transport;
		assert(!transport.IsValid());

		const std::vector<std::byte> payload = MakePayload(4);
		assert(!transport.Send(Endpoint{kLoopbackAddress, 9999}, AsBytes(payload)));

		std::vector<Datagram> datagrams;
		assert(transport.Receive(datagrams, 4) == 0);
		assert(transport.SentPacketCount() == 0);
		assert(transport.ReceivedPacketCount() == 0);
		std::puts("[OK] TestUninitializedTransportIsInert");
	}

	/// Shutdown est idempotent : un second appel (et celui du destructeur) ne crashe pas.
	void TestShutdownIsIdempotent()
	{
		UdpTransport transport;
		assert(transport.Init(0));
		assert(transport.IsValid());
		transport.Shutdown();
		assert(!transport.IsValid());
		transport.Shutdown();
		assert(!transport.IsValid());
		std::puts("[OK] TestShutdownIsIdempotent");
	}
}

int main()
{
	TestInitEphemeralPortExposesBoundPort();
	TestSendReceiveRoundTrip();
	TestReceiveOnEmptyIsNonBlocking();
	TestReceiveRespectsMaxDatagrams();
	TestEndpointToString();
	TestUninitializedTransportIsInert();
	TestShutdownIsIdempotent();
	std::puts("All UdpTransport tests passed");
	return 0;
}
