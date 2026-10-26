// ETAPE3.1 — tests de UdpSessionTable (sessions UDP chiffrées du shard), sans réseau.
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/UdpSessionTable.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	using engine::server::UdpSessionTable;
	using namespace engine::network;

	std::vector<std::byte> Bytes(const std::string& text)
	{
		std::vector<std::byte> out(text.size());
		std::memcpy(out.data(), text.data(), text.size());
		return out;
	}

	std::string Text(const std::vector<std::byte>& bytes)
	{
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	/// Client prêt : session initialisée avec un sel neuf + poignée de main scellée.
	struct TestClient
	{
		UdpSecureSession session;
		std::vector<std::byte> hello;

		TestClient(const UdpSessionKey& key, uint64_t characterId)
		{
			UdpSessionSalt salt{};
			(void)FillRandomBytes(std::span<uint8_t>(salt.data(), salt.size()));
			(void)session.Init(key, salt, UdpSecureRole::Client);
			(void)session.SealHandshake(characterId, Bytes("HELLO"), hello);
		}
	};
}

int main()
{
	UdpSessionKey key{};
	(void)GenerateUdpSessionKey(key);
	constexpr uint64_t kEndpointA = 0x0A000001'1F40ull;
	constexpr uint64_t kEndpointB = 0x0A000002'1F40ull;
	std::vector<std::byte> plain;

	// 1. Poignée de main ouverte, session installée, données dans les deux sens.
	UdpSessionTable table;
	TestClient client(key, 42u);
	Check(table.OpenHandshake(kEndpointA, key, client.hello, 1000u, plain) == UdpSessionTable::HandshakeResult::Opened,
		"poignée de main ouverte");
	Check(Text(plain) == "HELLO", "Hello déchiffré");
	Check(table.HasSession(kEndpointA) && table.SessionCharacterId(kEndpointA) == 42u, "session installée");

	std::vector<std::byte> c2s;
	(void)client.session.Seal(Bytes("INPUT"), c2s);
	Check(table.OpenData(kEndpointA, c2s, 1001u, plain) && Text(plain) == "INPUT", "données client ouvertes");
	Check(!table.OpenData(kEndpointA, c2s, 1002u, plain), "données rejouées : refus");
	Check(!table.OpenData(kEndpointB, c2s, 1002u, plain), "autre point de terminaison sans session : refus");

	std::vector<std::byte> s2c;
	Check(table.Seal(kEndpointA, Bytes("SNAP"), s2c) == UdpSessionTable::SealResult::Sealed, "scellé pour le client");
	Check(client.session.Open(s2c, plain) && Text(plain) == "SNAP", "le client ouvre le snapshot");
	Check(table.Seal(kEndpointB, Bytes("SNAP"), s2c) == UdpSessionTable::SealResult::NoSession, "pas de session : NoSession");

	// 2. Même Hello reçu deux fois du même point de terminaison : doublon, session gardée.
	Check(table.OpenHandshake(kEndpointA, key, client.hello, 1003u, plain) == UdpSessionTable::HandshakeResult::Duplicate,
		"doublon du même point de terminaison");
	std::vector<std::byte> c2sAfter;
	(void)client.session.Seal(Bytes("INPUT2"), c2sAfter);
	Check(table.OpenData(kEndpointA, c2sAfter, 1004u, plain), "la session d'origine continue après le doublon");

	// 3. Même Hello rejoué depuis un autre point de terminaison : refus.
	Check(table.OpenHandshake(kEndpointB, key, client.hello, 1005u, plain) == UdpSessionTable::HandshakeResult::Replayed,
		"rejeu depuis un autre point de terminaison");
	Check(!table.HasSession(kEndpointB), "pas de session pour l'attaquant");

	// 4. Faux Hello (clé inconnue) : refus, et le sel n'est PAS consommé.
	{
		UdpSessionKey otherKey{};
		(void)GenerateUdpSessionKey(otherKey);
		TestClient legit(key, 7u);
		// Le faux Hello reprend le sel du vrai (lisible en clair) mais pas la clé.
		UdpSessionTable t2;
		UdpSecureSession forgerSession;
		(void)forgerSession.Init(otherKey, legit.session.Salt(), UdpSecureRole::Client);
		std::vector<std::byte> forged;
		(void)forgerSession.SealHandshake(7u, Bytes("HELLO"), forged);
		Check(t2.OpenHandshake(kEndpointB, key, forged, 1u, plain) == UdpSessionTable::HandshakeResult::AuthFailed,
			"faux Hello : authentification refusée");
		Check(t2.OpenHandshake(kEndpointA, key, legit.hello, 2u, plain) == UdpSessionTable::HandshakeResult::Opened,
			"le vrai Hello passe après un faux");
	}

	// 5. Nouvelle clé (nouvel EnterWorld) : les sels de l'ancienne sont oubliés.
	{
		UdpSessionTable t3;
		TestClient first(key, 9u);
		Check(t3.OpenHandshake(kEndpointA, key, first.hello, 1u, plain) == UdpSessionTable::HandshakeResult::Opened,
			"ancienne clé ouverte");
		UdpSessionKey newKey{};
		(void)GenerateUdpSessionKey(newKey);
		TestClient second(newKey, 9u);
		Check(t3.OpenHandshake(kEndpointB, newKey, second.hello, 2u, plain) == UdpSessionTable::HandshakeResult::Opened,
			"nouvelle clé ouverte sur un autre point de terminaison");
		Check(t3.SessionCount() == 2u, "deux sessions (l'ancienne attend sa purge)");
	}

	// 6. Nouvelle socket du même client (nouveau sel) sur le même point de terminaison :
	//    la session est remplacée.
	{
		UdpSessionTable t4;
		TestClient sock1(key, 11u);
		TestClient sock2(key, 11u);
		(void)t4.OpenHandshake(kEndpointA, key, sock1.hello, 1u, plain);
		Check(t4.OpenHandshake(kEndpointA, key, sock2.hello, 2u, plain) == UdpSessionTable::HandshakeResult::Opened,
			"nouveau sel : nouvelle session");
		std::vector<std::byte> p;
		(void)sock2.session.Seal(Bytes("NEW"), p);
		Check(t4.OpenData(kEndpointA, p, 3u, plain) && Text(plain) == "NEW", "la nouvelle socket passe");
		Check(t4.SessionCount() == 1u, "une seule session pour ce point de terminaison");
	}

	// 7. Purge : session sans client et inactive supprimée ; session utilisée gardée.
	{
		UdpSessionTable t5;
		TestClient a(key, 21u);
		TestClient b(key, 22u);
		(void)t5.OpenHandshake(kEndpointA, key, a.hello, 0u, plain);
		(void)t5.OpenHandshake(kEndpointB, key, b.hello, 0u, plain);
		t5.Prune(10'000u, 30'000u, 600'000u, [](uint64_t) { return false; });
		Check(t5.SessionCount() == 2u, "pas encore inactives");
		t5.Prune(40'000u, 30'000u, 600'000u, [](uint64_t endpointKey) { return endpointKey == kEndpointA; });
		Check(t5.HasSession(kEndpointA) && !t5.HasSession(kEndpointB), "seule la session sans client est purgée");
		t5.Drop(kEndpointA);
		Check(t5.SessionCount() == 0u, "Drop");
		// Sels purgés après rétention : le même Hello redevient « neuf » (la clé, elle,
		// a expiré côté registre d'admission bien avant).
		t5.Prune(700'000u, 30'000u, 600'000u, {});
		Check(t5.OpenHandshake(kEndpointB, key, b.hello, 700'001u, plain) == UdpSessionTable::HandshakeResult::Opened,
			"sel oublié après la rétention");
	}

	if (s_failCount == 0)
		std::puts("[OK] udp_session_table_tests");
	return s_failCount == 0 ? 0 : 1;
}
