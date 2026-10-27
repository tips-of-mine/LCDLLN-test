// ETAPE3.4 — tests du canal fiable minimal (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shared/network/ReliableChannel.h"

#include <algorithm>
#include <cstdio>
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

	using engine::network::reliable::IsReliableKind;
	using engine::network::reliable::ReliableChannel;
	using engine::server::MessageKind;

	/// Un paquet du protocole de jeu (ChatRelay) dont le contenu porte \p tag.
	std::vector<std::byte> Message(uint8_t tag)
	{
		std::vector<std::byte> p = engine::server::BeginProtocolMessage(MessageKind::ChatRelay, 1);
		p.push_back(static_cast<std::byte>(tag));
		return p;
	}

	uint8_t Tag(const std::vector<std::byte>& p)
	{
		return static_cast<uint8_t>(p.back());
	}

	/// Livre à \p to tous les paquets (Reliable ou ReliableAck) de \p datagrams.
	void Deliver(ReliableChannel& to, const std::vector<std::vector<std::byte>>& datagrams, uint64_t nowMs,
		std::vector<std::vector<std::byte>>& delivered)
	{
		for (const auto& d : datagrams)
		{
			MessageKind kind{};
			if (!engine::server::PeekMessageKind(d, kind))
				continue;
			if (kind == MessageKind::Reliable)
				(void)to.OnReliable(d, nowMs, delivered);
			else if (kind == MessageKind::ReliableAck)
				(void)to.OnAck(d, nowMs);
		}
	}
}

int main()
{
	// 0. Liste des types fiables.
	Check(IsReliableKind(MessageKind::InventoryDelta) && IsReliableKind(MessageKind::TradeConfirm)
		&& IsReliableKind(MessageKind::ChatSend) && IsReliableKind(MessageKind::LootNotify), "types fiables");
	Check(!IsReliableKind(MessageKind::Snapshot) && !IsReliableKind(MessageKind::Input)
		&& !IsReliableKind(MessageKind::Hello) && !IsReliableKind(MessageKind::Welcome)
		&& !IsReliableKind(MessageKind::Reliable) && !IsReliableKind(MessageKind::ReliableAck)
		&& !IsReliableKind(MessageKind::AttackRequest) && !IsReliableKind(MessageKind::ForcePosition)
		&& !IsReliableKind(MessageKind::MoveCorrection), "types non fiables");

	// 1. Sans perte : livraison dans l'ordre, accusé groupé, plus rien en attente.
	{
		ReliableChannel a;
		ReliableChannel b;
		std::vector<std::vector<std::byte>> wire;
		for (uint8_t i = 1; i <= 5; ++i)
		{
			std::vector<std::byte> d;
			Check(a.Queue(Message(i), 0, d), "mise en file");
			wire.push_back(d);
		}
		std::vector<std::vector<std::byte>> got;
		Deliver(b, wire, 10, got);
		Check(got.size() == 5u && Tag(got[0]) == 1u && Tag(got[4]) == 5u, "5 messages livrés dans l'ordre");
		std::vector<std::vector<std::byte>> acks;
		b.Update(50, acks);
		Check(acks.size() == 1u, "un seul accusé pour 5 messages");
		std::vector<std::vector<std::byte>> none;
		Deliver(a, acks, 60, none);
		Check(a.InFlightCount() == 0u, "tout accusé");
		Check(a.SmoothedRttMs() == 60u, "temps d'aller-retour mesuré");
	}

	// 2. Perte puis renvoi : le message perdu revient, l'ordre est respecté.
	{
		ReliableChannel a;
		ReliableChannel b;
		std::vector<std::byte> d1, d2, d3;
		(void)a.Queue(Message(1), 0, d1);
		(void)a.Queue(Message(2), 0, d2); // perdu
		(void)a.Queue(Message(3), 0, d3);
		std::vector<std::vector<std::byte>> got;
		Deliver(b, { d1, d3 }, 5, got);
		Check(got.size() == 1u && Tag(got[0]) == 1u, "3 retenu tant que 2 manque");
		std::vector<std::vector<std::byte>> acks;
		b.Update(50, acks);
		std::vector<std::vector<std::byte>> none;
		Deliver(a, acks, 60, none);
		Check(a.InFlightCount() == 1u, "1 et 3 accusés (masque), 2 en attente");
		std::vector<std::vector<std::byte>> resend;
		a.Update(100, resend);
		Check(resend.empty(), "pas de renvoi avant le délai");
		a.Update(300, resend);
		Check(resend.size() == 1u && a.RetransmitCount() == 1u, "2 renvoyé après 250 ms");
		Deliver(b, resend, 310, got);
		Check(got.size() == 3u && Tag(got[1]) == 2u && Tag(got[2]) == 3u, "2 puis 3 livrés");
		// Doublon tardif : rien de nouveau, mais un accusé repart.
		std::vector<std::vector<std::byte>> got2;
		Deliver(b, { d2 }, 400, got2);
		Check(got2.empty(), "doublon ignoré");
		std::vector<std::vector<std::byte>> acks2;
		b.Update(400, acks2);
		Check(acks2.size() == 1u, "accusé renvoyé pour un doublon");
		Deliver(a, acks2, 410, none);
		Check(a.InFlightCount() == 0u, "plus rien en attente");
	}

	// 3. Délai de renvoi doublé à chaque essai, puis abandon après 30 s.
	{
		ReliableChannel a;
		std::vector<std::byte> d;
		(void)a.Queue(Message(9), 0, d);
		std::vector<std::vector<std::byte>> out;
		a.Update(250, out);  // renvoi 1 (rto 250 -> 500)
		a.Update(500, out);  // trop tôt
		a.Update(750, out);  // renvoi 2 (rto 500 -> 1000)
		Check(out.size() == 2u, "renvois espacés (250 puis 500 ms)");
		Check(!a.Failed(), "pas encore en échec");
		a.Update(15000, out);
		Check(!a.Failed(), "pas d'échec à 15 s (chargement long toléré)");
		a.Update(ReliableChannel::kGiveUpMs, out);
		Check(a.Failed(), "échec après 30 s sans accusé");
		Check(!a.Queue(Message(10), ReliableChannel::kGiveUpMs + 1u, d), "plus rien n'entre dans un canal en échec");
	}

	// 4. Fenêtre pleine : échec (pair muet).
	{
		ReliableChannel a;
		std::vector<std::byte> d;
		bool allQueued = true;
		for (size_t i = 0; i < ReliableChannel::kMaxInFlight; ++i)
			allQueued = allQueued && a.Queue(Message(1), 0, d);
		Check(allQueued, "kMaxInFlight messages acceptés");
		Check(!a.Queue(Message(1), 0, d) && a.Failed(), "un de plus : échec");
	}

	// 5. Désordre au-delà de la fenêtre de réception : ignoré (sera renvoyé).
	{
		ReliableChannel a;
		ReliableChannel b;
		std::vector<std::vector<std::byte>> all;
		for (size_t i = 0; i < ReliableChannel::kMaxOutOfOrder + 2u; ++i)
		{
			std::vector<std::byte> d;
			(void)a.Queue(Message(static_cast<uint8_t>(i & 0xFFu)), 0, d);
			all.push_back(d);
		}
		std::vector<std::vector<std::byte>> got;
		Deliver(b, { all.back() }, 1, got); // n° kMaxOutOfOrder + 2, trou de kMaxOutOfOrder + 1
		Deliver(b, { all.front() }, 2, got);
		Check(got.size() == 1u, "seul le premier est livré, le trop lointain a été ignoré");
	}

	// 6. Paquets mal formés ou dangereux refusés.
	{
		ReliableChannel b;
		std::vector<std::vector<std::byte>> got;
		std::vector<std::byte> nested;
		ReliableChannel a;
		std::vector<std::byte> inner;
		(void)a.Queue(Message(1), 0, inner);
		std::vector<std::byte> outer;
		ReliableChannel a2;
		Check(a2.Queue(inner, 0, outer), "l'émetteur emballe ce qu'on lui donne");
		Check(!b.OnReliable(outer, 0, got), "enveloppe dans une enveloppe refusée");
		std::vector<std::byte> truncated = inner;
		truncated.resize(10);
		Check(!b.OnReliable(truncated, 0, got), "paquet tronqué refusé");
		std::vector<std::byte> badAck = engine::server::BeginProtocolMessage(MessageKind::ReliableAck, 12);
		for (int i = 0; i < 12; ++i)
			badAck.push_back(std::byte{ 0xFF });
		Check(!a.OnAck(badAck, 0), "accusé d'un numéro jamais envoyé refusé");
		Check(a.InFlightCount() == 1u, "rien n'a été effacé");
		std::vector<std::byte> huge = engine::server::BeginProtocolMessage(MessageKind::ChatRelay, 2000);
		huge.resize(ReliableChannel::kMaxMessageSize + 1u);
		Check(!a.Queue(huge, 0, inner) && !a.Failed(), "message trop gros refusé sans échec du canal");
	}

	// 7. Simulation : 2 000 messages, 30 % de pertes dans les deux sens, désordre.
	{
		ReliableChannel a;
		ReliableChannel b;
		uint32_t rng = 777u;
		auto next = [&rng]() { rng = rng * 1103515245u + 12345u; return (rng >> 8) % 100u; };
		std::vector<std::vector<std::byte>> got;
		std::vector<std::vector<std::byte>> toB;
		std::vector<std::vector<std::byte>> toA;
		uint8_t sent = 0;
		size_t queued = 0;
		uint64_t now = 0;
		for (int step = 0; step < 4000 && !a.Failed(); ++step, now += 20)
		{
			if (queued < 2000u && a.InFlightCount() < 64u)
			{
				std::vector<std::byte> d;
				if (a.Queue(Message(sent++), now, d))
				{
					toB.push_back(d);
					++queued;
				}
			}
			a.Update(now, toB);
			b.Update(now, toA);
			// Désordre : on livre la moitié dans l'ordre inverse.
			if (next() < 50u)
				std::reverse(toB.begin(), toB.end());
			std::vector<std::vector<std::byte>> keptB;
			for (auto& d : toB)
				if (next() >= 30u)
					keptB.push_back(std::move(d));
			std::vector<std::vector<std::byte>> keptA;
			for (auto& d : toA)
				if (next() >= 30u)
					keptA.push_back(std::move(d));
			toB.clear();
			toA.clear();
			Deliver(b, keptB, now, got);
			std::vector<std::vector<std::byte>> none;
			Deliver(a, keptA, now, none);
		}
		bool inOrder = got.size() == 2000u;
		for (size_t i = 0; i < got.size() && inOrder; ++i)
			inOrder = Tag(got[i]) == static_cast<uint8_t>(i & 0xFFu);
		Check(!a.Failed(), "simulation : pas d'échec malgré 30 % de pertes");
		Check(inOrder, "simulation : 2 000 messages livrés une fois, dans l'ordre");
	}

	if (s_failCount == 0)
		std::puts("[OK] reliable_channel_tests");
	return s_failCount == 0 ? 0 : 1;
}
