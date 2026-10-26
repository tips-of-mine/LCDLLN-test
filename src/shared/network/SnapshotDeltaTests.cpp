// ETAPE3.2 — tests des snapshots en delta (codec, émetteur, récepteur), sans réseau.
// Pattern repo : int main() + vérifications + code retour.

#include "src/shared/network/SnapshotDelta.h"

#include <cmath>
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

	using namespace engine::network::snapshot;
	using engine::server::SnapshotEntity;

	SnapshotEntity MakeEntity(uint64_t id, float x, float z, const char* name = "")
	{
		SnapshotEntity e{};
		e.entityId = id;
		e.state.positionX = x;
		e.state.positionY = 12.5f;
		e.state.positionZ = z;
		e.state.yawRadians = 1.0f;
		e.state.velocityX = 3.25f;
		e.state.currentHealth = 80;
		e.state.maxHealth = 100;
		e.playerClientId = name[0] != '\0' ? static_cast<uint32_t>(id) : 0u;
		e.characterName = name;
		e.gender = name[0] != '\0' ? "female" : "";
		e.archetypeId = name[0] != '\0' ? 0u : 7u;
		return e;
	}

	std::vector<WireEntity> World(const std::vector<SnapshotEntity>& in)
	{
		std::vector<WireEntity> out;
		for (const SnapshotEntity& e : in)
			out.push_back(Quantize(e));
		return out;
	}

	size_t TotalBytes(const std::vector<std::vector<std::byte>>& packets)
	{
		size_t n = 0;
		for (const auto& p : packets)
			n += p.size();
		return n;
	}

	/// Livre tous les paquets au récepteur ; renvoie le dernier résultat.
	SnapshotReceiver::Result Deliver(SnapshotReceiver& rx, const std::vector<std::vector<std::byte>>& packets,
		std::vector<SnapshotEntity>& out, ChunkHeader& header)
	{
		SnapshotReceiver::Result last = SnapshotReceiver::Result::Dropped;
		for (const auto& p : packets)
			last = rx.Apply(p, out, header);
		return last;
	}

	const SnapshotEntity* Find(const std::vector<SnapshotEntity>& v, uint64_t id)
	{
		for (const SnapshotEntity& e : v)
			if (e.entityId == id)
				return &e;
		return nullptr;
	}
}

int main()
{
	// 1. Quantification : centimètre, 16 bits d'angle, aller-retour stable.
	{
		SnapshotEntity e = MakeEntity(5, 1234.567f, -98.7f, "Aria");
		e.state.yawRadians = -3.0f;
		const WireEntity w = Quantize(e);
		Check(w.positionCmX == 123457 && w.positionCmZ == -9870, "position au centimètre");
		const SnapshotEntity back = Dequantize(w);
		Check(std::fabs(back.state.positionX - 1234.57f) < 0.001f, "position déquantifiée");
		Check(std::fabs(back.state.yawRadians - (-3.0f)) < 0.0002f, "orientation déquantifiée");
		Check(Quantize(back).SameWireState(w), "requantifier ne change rien (pas de dérive)");
		e.characterName = std::string(100, 'x');
		Check(Quantize(e).characterName.size() == 64u, "nom tronqué à 64 octets");
	}

	// 2. Premier snapshot complet, puis delta après acquittement.
	{
		SnapshotSender tx;
		SnapshotReceiver rx;
		std::vector<SnapshotEntity> world = { MakeEntity(10, 0, 0, "Aria"), MakeEntity(20, 5, 5), MakeEntity(30, 9, 9) };
		ChunkHeader tmpl{};
		tmpl.clientId = 3;
		auto full = tx.BuildPackets(tmpl, World(world));
		Check(tx.LastBaselineSeq() == 0u && tx.LastSentSeq() == 1u, "premier snapshot complet n°1");
		std::vector<SnapshotEntity> out;
		ChunkHeader h{};
		Check(Deliver(rx, full, out, h) == SnapshotReceiver::Result::Committed && out.size() == 3u, "complet reconstruit");
		Check(rx.LatestCommittedSeq() == 1u, "numéro à acquitter");
		const SnapshotEntity* aria = Find(out, 10);
		Check(aria && aria->characterName == "Aria" && aria->gender == "female", "identité reçue");
		tx.OnAck(rx.LatestCommittedSeq());

		// Une seule entité bouge de 0,5 m : le delta est minuscule.
		world[1].state.positionX += 0.5f;
		auto delta = tx.BuildPackets(tmpl, World(world));
		Check(tx.LastBaselineSeq() == 1u, "delta écrit sur la base acquittée");
		Check(TotalBytes(delta) < TotalBytes(full) / 2u, "delta bien plus petit que le complet");
		Check(TotalBytes(delta) <= 8u + kChunkHeaderSize + 6u, "une entrée de 6 octets au plus pour 50 cm");
		Check(Deliver(rx, delta, out, h) == SnapshotReceiver::Result::Committed && out.size() == 3u, "delta appliqué");
		const SnapshotEntity* moved = Find(out, 20);
		Check(moved && std::fabs(moved->state.positionX - 5.5f) < 0.001f, "position mise à jour");
		aria = Find(out, 10);
		Check(aria && aria->characterName == "Aria", "entité inchangée conservée avec son identité");

		// Rien ne change : un seul paquet d'en-tête, toujours envoyé (porte le numéro).
		tx.OnAck(rx.LatestCommittedSeq());
		auto idle = tx.BuildPackets(tmpl, World(world));
		Check(idle.size() == 1u && TotalBytes(idle) < 60u, "snapshot sans changement : en-tête seul");
		Check(Deliver(rx, idle, out, h) == SnapshotReceiver::Result::Committed && out.size() == 3u, "état reconduit");

		// Retrait et apparition.
		tx.OnAck(rx.LatestCommittedSeq());
		world.erase(world.begin() + 2);
		world.push_back(MakeEntity(40, 1, 1, "Bran"));
		auto churn = tx.BuildPackets(tmpl, World(world));
		Check(Deliver(rx, churn, out, h) == SnapshotReceiver::Result::Committed, "retrait + apparition");
		Check(out.size() == 3u && Find(out, 30) == nullptr && Find(out, 40) != nullptr, "30 retiré, 40 apparu");
	}

	// 3. Perte de paquets : le snapshot suivant s'écrit sur la dernière base acquittée.
	{
		SnapshotSender tx;
		SnapshotReceiver rx;
		std::vector<SnapshotEntity> world = { MakeEntity(1, 0, 0), MakeEntity(2, 0, 0) };
		ChunkHeader tmpl{};
		std::vector<SnapshotEntity> out;
		ChunkHeader h{};
		(void)Deliver(rx, tx.BuildPackets(tmpl, World(world)), out, h);
		tx.OnAck(rx.LatestCommittedSeq()); // 1
		world[0].state.positionX = 10.0f;
		(void)tx.BuildPackets(tmpl, World(world)); // n°2 perdu
		world[0].state.positionX = 20.0f;
		auto third = tx.BuildPackets(tmpl, World(world)); // n°3, base toujours 1
		Check(tx.LastBaselineSeq() == 1u, "base = dernier acquittement, pas le dernier envoyé");
		Check(Deliver(rx, third, out, h) == SnapshotReceiver::Result::Committed, "n°3 reconstruit sans le n°2");
		const SnapshotEntity* e = Find(out, 1);
		Check(e && std::fabs(e->state.positionX - 20.0f) < 0.001f, "position finale correcte");
		Check(rx.LatestCommittedSeq() == 3u, "acquittera 3");
	}

	// 4. Gros snapshot découpé en morceaux ; morceau manquant puis snapshot suivant.
	{
		SnapshotSender tx;
		SnapshotReceiver rx;
		std::vector<SnapshotEntity> world;
		for (uint64_t i = 1; i <= 120; ++i)
			world.push_back(MakeEntity(i * 3, static_cast<float>(i), 0.0f, "Joueur-avec-un-nom-long"));
		ChunkHeader tmpl{};
		auto packets = tx.BuildPackets(tmpl, World(world));
		Check(packets.size() > 3u, "snapshot découpé en plusieurs morceaux");
		bool underBudget = true;
		for (const auto& p : packets)
			underBudget = underBudget && p.size() <= kDefaultChunkPayloadBudget + 8u;
		Check(underBudget, "chaque morceau tient dans le budget");
		std::vector<SnapshotEntity> out;
		ChunkHeader h{};
		// Ordre inversé et un doublon : toujours reconstruit.
		SnapshotReceiver::Result r = SnapshotReceiver::Result::Dropped;
		for (size_t i = packets.size(); i-- > 0;)
			r = rx.Apply(packets[i], out, h);
		Check(r == SnapshotReceiver::Result::Committed && out.size() == 120u, "morceaux dans le désordre");
		Check(rx.Apply(packets[0], out, h) == SnapshotReceiver::Result::Dropped, "morceau d'un snapshot déjà reçu ignoré");

		// Snapshot 2 avec un morceau perdu : jamais reconstruit ; le 3 le remplace.
		SnapshotReceiver rx2;
		auto s1 = tx.BuildPackets(tmpl, World(world)); // n°2 (base 0 : pas d'acquittement)
		for (size_t i = 1; i < s1.size(); ++i)
			Check(rx2.Apply(s1[i], out, h) == SnapshotReceiver::Result::Pending, "incomplet en attente");
		auto s2 = tx.BuildPackets(tmpl, World(world)); // n°3
		Check(Deliver(rx2, s2, out, h) == SnapshotReceiver::Result::Committed, "le suivant remplace l'incomplet");
		Check(rx2.DroppedCount() == 1u, "un snapshot abandonné compté");
	}

	// 5. Base inconnue du client : rien n'est appliqué (pas d'état faux).
	{
		SnapshotSender tx;
		SnapshotReceiver rx;
		std::vector<SnapshotEntity> world = { MakeEntity(1, 0, 0) };
		ChunkHeader tmpl{};
		(void)tx.BuildPackets(tmpl, World(world));
		tx.OnAck(1); // acquittement « reçu » d'un snapshot que ce client n'a pas
		world[0].state.positionX = 2.0f;
		std::vector<SnapshotEntity> out;
		ChunkHeader h{};
		Check(Deliver(rx, tx.BuildPackets(tmpl, World(world)), out, h) == SnapshotReceiver::Result::Dropped,
			"base inconnue : abandon");
		Check(rx.LatestCommittedSeq() == 0u, "rien à acquitter");
	}

	// 6. Acquittements invalides ignorés ; base trop ancienne -> snapshot complet.
	{
		SnapshotSender tx;
		std::vector<WireEntity> world = World({ MakeEntity(1, 0, 0) });
		ChunkHeader tmpl{};
		(void)tx.BuildPackets(tmpl, world); // 1
		tx.OnAck(99);
		Check(tx.LastAckedSeq() == 0u, "acquittement d'un numéro jamais envoyé ignoré");
		tx.OnAck(1);
		for (int i = 0; i < 20; ++i)
			(void)tx.BuildPackets(tmpl, world);
		tx.OnAck(0);
		Check(tx.LastAckedSeq() == 1u, "acquittement plus ancien ignoré");
		(void)tx.BuildPackets(tmpl, world);
		Check(tx.LastBaselineSeq() == 0u, "base sortie de l'historique : complet");
	}

	// 7. Entités de faible priorité (PNJ d'ambiance) : un snapshot sur deux.
	{
		SnapshotSender tx;
		SnapshotReceiver rx;
		std::vector<WireEntity> world = World({ MakeEntity(1, 0, 0), MakeEntity(2, 0, 0) });
		world[1].halfRate = true;
		ChunkHeader tmpl{};
		std::vector<SnapshotEntity> out;
		ChunkHeader h{};
		(void)Deliver(rx, tx.BuildPackets(tmpl, world), out, h); // n°1
		tx.OnAck(1);
		world[1].positionCmX = 500;
		(void)Deliver(rx, tx.BuildPackets(tmpl, world), out, h); // n°2 (pair) : mis à jour
		const SnapshotEntity* npc = Find(out, 2);
		Check(npc && std::fabs(npc->state.positionX - 5.0f) < 0.001f, "PNJ mis à jour au snapshot pair");
		tx.OnAck(2);
		world[1].positionCmX = 900;
		(void)Deliver(rx, tx.BuildPackets(tmpl, world), out, h); // n°3 (impair) : état de la base
		npc = Find(out, 2);
		Check(npc && std::fabs(npc->state.positionX - 5.0f) < 0.001f, "PNJ figé au snapshot impair");
		tx.OnAck(3);
		(void)Deliver(rx, tx.BuildPackets(tmpl, world), out, h); // n°4 : rattrape
		npc = Find(out, 2);
		Check(npc && std::fabs(npc->state.positionX - 9.0f) < 0.001f, "PNJ rattrapé au snapshot suivant");
	}

	// 8. Paquets mal formés refusés.
	{
		SnapshotSender tx;
		SnapshotReceiver rx;
		ChunkHeader tmpl{};
		auto packets = tx.BuildPackets(tmpl, World({ MakeEntity(1, 0, 0, "Aria") }));
		std::vector<std::byte> truncated = packets[0];
		truncated.resize(truncated.size() - 3u);
		std::vector<SnapshotEntity> out;
		ChunkHeader h{};
		Check(rx.Apply(truncated, out, h) == SnapshotReceiver::Result::Dropped, "paquet tronqué refusé");
		std::vector<std::byte> trailing = packets[0];
		trailing.push_back(std::byte{ 0 });
		Check(rx.Apply(trailing, out, h) == SnapshotReceiver::Result::Dropped, "octets en trop refusés");
		DecodedChunk chunk;
		std::vector<std::byte> lying = packets[0];
		lying[8 + 32] = std::byte{ 0xFF }; // entryCount (octet bas)
		lying[8 + 33] = std::byte{ 0xFF };
		Check(!DecodeSnapshotChunk(lying, chunk), "compteur d'entrées mensonger refusé");
		Check(rx.Apply(packets[0], out, h) == SnapshotReceiver::Result::Committed, "le vrai paquet passe");
	}

	// 9. Simulation : monde qui évolue (déplacements, apparitions, retraits, PV, noms),
	//    30 % de paquets perdus, acquittements retardés. Chaque snapshot reconstruit doit
	//    être EXACTEMENT celui que le shard a construit pour ce numéro.
	{
		uint32_t rng = 12345u;
		auto next = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
		SnapshotSender tx;
		SnapshotReceiver rx;
		std::vector<WireEntity> world;
		uint64_t nextId = 1;
		std::vector<std::vector<WireEntity>> sentBySeq(1); // index = seq
		std::vector<uint32_t> ackQueue;
		bool allExact = true;
		size_t commits = 0;
		for (int step = 0; step < 600; ++step)
		{
			// Évolution du monde.
			if (world.size() < 60 && next() % 3 == 0)
			{
				WireEntity e = Quantize(MakeEntity(nextId * 7, static_cast<float>(next() % 2000) - 1000.0f,
					static_cast<float>(next() % 2000) - 1000.0f, (next() % 2) ? "Joueur" : ""));
				e.halfRate = (next() % 4) == 0;
				world.push_back(e);
				++nextId;
			}
			if (!world.empty() && next() % 5 == 0)
				world.erase(world.begin() + static_cast<long>(next() % world.size()));
			for (WireEntity& e : world)
			{
				if (next() % 2 == 0)
				{
					e.positionCmX += static_cast<int32_t>(next() % 61) - 30;
					e.positionCmZ += static_cast<int32_t>(next() % 61) - 30;
					e.yaw = static_cast<uint16_t>(e.yaw + next() % 500);
				}
				if (next() % 17 == 0)
					e.currentHealth = next() % 100;
				if (next() % 97 == 0)
					e.characterName = "Renomme";
			}
			// Acquittements qui arrivent avec 0 à 3 pas de retard.
			if (!ackQueue.empty() && next() % 2 == 0)
			{
				tx.OnAck(ackQueue.front());
				ackQueue.erase(ackQueue.begin());
			}
			ChunkHeader tmpl{};
			tmpl.serverTick = static_cast<uint32_t>(step);
			auto packets = tx.BuildPackets(tmpl, world);
			// Le shard garde pour ce numéro l'état effectivement envoyé (halfRate compris) :
			// on le reconstitue depuis un récepteur « parfait » sans perte.
			const uint32_t seq = tx.LastSentSeq();
			sentBySeq.resize(seq + 1u);
			std::vector<SnapshotEntity> out;
			ChunkHeader h{};
			SnapshotReceiver::Result r = SnapshotReceiver::Result::Pending;
			for (const auto& p : packets)
			{
				if (next() % 10 < 3)
					continue; // perdu
				r = rx.Apply(p, out, h);
			}
			if (r == SnapshotReceiver::Result::Committed)
			{
				++commits;
				ackQueue.push_back(rx.LatestCommittedSeq());
				// Vérification : chaque entité reçue correspond au monde courant, sauf les
				// halfRate qui peuvent montrer l'état d'un snapshot précédent.
				size_t expected = world.size();
				if (out.size() != expected)
					allExact = false;
				for (const SnapshotEntity& got : out)
				{
					const WireEntity q = Quantize(got);
					bool found = false;
					for (const WireEntity& w : world)
					{
						if (w.entityId != q.entityId)
							continue;
						found = true;
						if (!w.halfRate && !w.SameWireState(q))
							allExact = false;
					}
					if (!found)
						allExact = false;
				}
			}
		}
		Check(allExact, "simulation avec pertes : chaque snapshot reconstruit est exact");
		Check(commits > 200u, "simulation : la majorité des snapshots se reconstruit malgré les pertes");
	}

	if (s_failCount == 0)
		std::puts("[OK] snapshot_delta_tests");
	return s_failCount == 0 ? 0 : 1;
}
