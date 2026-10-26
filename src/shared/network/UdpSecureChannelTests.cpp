// ETAPE3.1 — tests de l'enveloppe chiffrée UDP (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shared/network/UdpSecureChannel.h"

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
}

int main()
{
	UdpSessionKey key{};
	Check(GenerateUdpSessionKey(key), "clé de session tirée");
	UdpSessionSalt salt{};
	Check(FillRandomBytes(std::span<uint8_t>(salt.data(), salt.size())), "sel tiré");

	UdpSecureSession client;
	UdpSecureSession server;
	Check(client.Init(key, salt, UdpSecureRole::Client), "init client");
	Check(server.Init(key, salt, UdpSecureRole::Server), "init shard");

	// 1. Poignée de main : en-tête lisible, contenu chiffré, ouverture côté shard.
	std::vector<std::byte> hello;
	Check(client.SealHandshake(42u, Bytes("HELLO-PAYLOAD"), hello), "scelle le Hello");
	Check(hello.size() == std::string("HELLO-PAYLOAD").size() + kUdpHandshakeOverhead, "taille poignée de main");
	Check(IsSecureDatagram(hello), "reconnu comme enveloppe");
	UdpHandshakeHeader header{};
	Check(PeekUdpHandshake(hello, header) && header.characterId == 42u && header.salt == salt, "en-tête en clair");
	Check(Text(hello).find("HELLO-PAYLOAD") == std::string::npos, "contenu non lisible en clair");
	std::vector<std::byte> plain;
	Check(server.OpenHandshake(hello, plain) && Text(plain) == "HELLO-PAYLOAD", "Hello ouvert par le shard");
	Check(!server.OpenHandshake(hello, plain), "poignée de main rejouée : refus");

	// 2. character_id falsifié dans l'en-tête : l'authentification échoue.
	{
		UdpSecureSession server2;
		(void)server2.Init(key, salt, UdpSecureRole::Server);
		std::vector<std::byte> forged = hello;
		forged[5] = static_cast<std::byte>(43);
		Check(!server2.OpenHandshake(forged, plain), "character_id modifié : refus");
	}

	// 3. Données dans les deux sens.
	std::vector<std::byte> c2s;
	Check(client.Seal(Bytes("INPUT-1"), c2s), "scelle une entrée");
	Check(c2s.size() == 7u + kUdpDataOverhead, "taille données");
	Check(server.Open(c2s, plain) && Text(plain) == "INPUT-1", "entrée ouverte");
	Check(!server.Open(c2s, plain), "entrée rejouée : refus");

	std::vector<std::byte> s2c;
	Check(server.Seal(Bytes("SNAPSHOT"), s2c), "scelle un snapshot");
	Check(client.Open(s2c, plain) && Text(plain) == "SNAPSHOT", "snapshot ouvert");

	// 4. Un datagramme ne s'ouvre pas dans le mauvais sens (réflexion).
	{
		std::vector<std::byte> mine;
		(void)client.Seal(Bytes("X"), mine);
		Check(!client.Open(mine, plain), "datagramme renvoyé à son émetteur : refus");
	}

	// 5. Octet modifié dans le chiffré ou l'étiquette : refus.
	{
		std::vector<std::byte> tampered;
		(void)client.Seal(Bytes("INPUT-2"), tampered);
		tampered[kUdpDataHeaderSize] ^= std::byte{ 0x01 };
		Check(!server.Open(tampered, plain), "chiffré modifié : refus");
		std::vector<std::byte> tamperedTag;
		(void)client.Seal(Bytes("INPUT-3"), tamperedTag);
		tamperedTag.back() ^= std::byte{ 0x80 };
		Check(!server.Open(tamperedTag, plain), "étiquette modifiée : refus");
		std::vector<std::byte> tamperedCounter;
		(void)client.Seal(Bytes("INPUT-4"), tamperedCounter);
		tamperedCounter[5] ^= std::byte{ 0x01 };
		Check(!server.Open(tamperedCounter, plain), "compteur modifié : refus");
		Check(!server.Open(std::vector<std::byte>{}, plain), "datagramme vide : refus");
		std::vector<std::byte> truncated;
		(void)client.Seal(Bytes("INPUT-5"), truncated);
		truncated.resize(truncated.size() - 1u);
		Check(!server.Open(truncated, plain), "datagramme tronqué : refus");
	}

	// 6. Désordre toléré dans la fenêtre, trop ancien refusé.
	{
		UdpSessionSalt salt2{};
		(void)FillRandomBytes(std::span<uint8_t>(salt2.data(), salt2.size()));
		UdpSecureSession a;
		UdpSecureSession b;
		(void)a.Init(key, salt2, UdpSecureRole::Client);
		(void)b.Init(key, salt2, UdpSecureRole::Server);
		std::vector<std::vector<std::byte>> packets(80);
		for (auto& p : packets)
			(void)a.Seal(Bytes("P"), p);
		Check(b.Open(packets[10], plain), "compteur 11 accepté");
		Check(b.Open(packets[5], plain), "compteur 6 (en retard, dans la fenêtre) accepté");
		Check(!b.Open(packets[5], plain), "compteur 6 rejoué : refus");
		Check(b.Open(packets[79], plain), "compteur 80 accepté");
		Check(!b.Open(packets[2], plain), "compteur 3 (77 de retard) : refus");
		Check(b.Open(packets[40], plain), "compteur 41 (39 de retard) accepté");
	}

	// 7. Autre sel = autre clé de travail : rien ne s'ouvre d'une session à l'autre.
	{
		UdpSessionSalt otherSalt = salt;
		otherSalt[0] ^= 0xFFu;
		UdpSecureSession other;
		(void)other.Init(key, otherSalt, UdpSecureRole::Server);
		std::vector<std::byte> p;
		(void)client.Seal(Bytes("Y"), p);
		Check(!other.Open(p, plain), "sel différent : refus");
		UdpSessionKey otherKey = key;
		otherKey[0] ^= 0x01u;
		UdpSecureSession wrongKey;
		(void)wrongKey.Init(otherKey, salt, UdpSecureRole::Server);
		Check(!wrongKey.Open(p, plain), "clé différente : refus");
	}

	// 8. Fenêtre anti-rejeu seule.
	{
		UdpReplayWindow w;
		Check(!w.CanAccept(0u), "compteur 0 jamais accepté (réservé)");
		Check(w.CanAccept(1u), "compteur 1 acceptable");
		w.Mark(1u);
		Check(!w.CanAccept(1u), "compteur 1 déjà vu");
		w.Mark(200u);
		Check(!w.CanAccept(100u), "compteur 100 hors fenêtre");
		Check(w.CanAccept(199u), "compteur 199 dans la fenêtre");
		Check(w.Highest() == 200u, "plus grand compteur");
	}

	// 9. Session non initialisée : tout est refusé.
	{
		UdpSecureSession none;
		std::vector<std::byte> p;
		Check(!none.Seal(Bytes("Z"), p), "Seal sans Init : refus");
		Check(!none.Open(c2s, plain), "Open sans Init : refus");
		Check(!server.SealHandshake(1u, Bytes("Z"), p), "poignée de main côté shard : refus");
	}

	// 10. Déplacement d'une session : la copie déplacée fonctionne.
	{
		UdpSecureSession moved = std::move(client);
		std::vector<std::byte> p;
		Check(moved.Seal(Bytes("MOVE"), p) && server.Open(p, plain) && Text(plain) == "MOVE", "session déplacée");
		Check(!client.IsReady(), "session d'origine vidée");
	}

	if (s_failCount == 0)
		std::puts("[OK] udp_secure_channel_tests");
	return s_failCount == 0 ? 0 : 1;
}
