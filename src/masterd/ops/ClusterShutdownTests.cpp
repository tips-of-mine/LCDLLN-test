// ETAPE5.3 — tests de l'arrêt ordonné du cluster (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/masterd/ops/ClusterShutdown.h"

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

	using namespace engine::server;
	using Kind = ClusterShutdownAction::Kind;

	/// Fait tourner la boucle (pas de 100 ms) jusqu'à \p until ; collecte les actions.
	std::vector<ClusterShutdownAction> Run(ClusterShutdown& s, double& now, double until)
	{
		std::vector<ClusterShutdownAction> all;
		while (now < until)
		{
			now += 0.1;
			for (const auto& a : s.Tick(now))
				all.push_back(a);
		}
		return all;
	}

	std::vector<uint32_t> Announces(const std::vector<ClusterShutdownAction>& v)
	{
		std::vector<uint32_t> out;
		for (const auto& a : v)
			if (a.kind == Kind::Announce)
				out.push_back(a.remainingSeconds);
		return out;
	}
}

int main()
{
	// 1. Arrêt dans 5 min : annonces aux paliers, vidage à zéro, sortie après le drain.
	{
		ClusterShutdown s(15.0);
		double now = 1000.0;
		Check(!s.AdmissionsBlocked() && s.Phase() == ClusterShutdownPhase::Idle, "au repos");
		Check(s.Start(now, 300u, "maintenance"), "Start accepté");
		Check(s.AdmissionsBlocked() && s.Phase() == ClusterShutdownPhase::Countdown, "compte à rebours : entrées refusées");
		Check(s.RemainingSeconds(now) == 300u && s.Reason() == "maintenance", "reste 300 s, motif");
		const auto actions = Run(s, now, 1000.0 + 300.0 + 0.05);
		const std::vector<uint32_t> expected = { 300u, 120u, 60u, 30u, 15u, 10u, 5u, 4u, 3u, 2u, 1u };
		Check(Announces(actions) == expected, "annonces : départ puis paliers, sans doublon");
		Check(!actions.empty() && actions.back().kind == Kind::StopShards, "à zéro : ordre d'arrêt aux shards");
		Check(s.Phase() == ClusterShutdownPhase::Draining && !s.Cancel(), "vidage : plus annulable");
		const auto drain = Run(s, now, now + 14.0);
		Check(drain.empty(), "pas de sortie avant la fin du vidage");
		const auto end = Run(s, now, now + 1.5);
		Check(end.size() == 1u && end[0].kind == Kind::Exit && s.Phase() == ClusterShutdownPhase::Done, "sortie après 15 s de vidage");
		Check(Run(s, now, now + 10.0).empty(), "plus rien après la sortie");
		Check(!s.Start(now, 60u, "x"), "Start refusé une fois terminé");
	}

	// 2. Délai qui n'est pas un palier : pas d'annonce de palier supérieur au délai.
	{
		ClusterShutdown s;
		double now = 0.0;
		(void)s.Start(now, 420u, "");
		const auto a = Announces(Run(s, now, 200.0));
		Check(a.size() == 2u && a[0] == 420u && a[1] == 300u, "7 min : départ puis 5 min");
	}

	// 3. Annulation puis reprogrammation.
	{
		ClusterShutdown s;
		double now = 0.0;
		(void)s.Start(now, 60u, "a");
		(void)Run(s, now, 10.0);
		Check(s.Cancel() && !s.AdmissionsBlocked() && s.Phase() == ClusterShutdownPhase::Idle, "annulé : entrées rouvertes");
		Check(!s.Cancel(), "seconde annulation refusée");
		Check(Run(s, now, 100.0).empty(), "plus aucune action après annulation");
		(void)s.Start(now, 120u, "b");
		(void)Run(s, now, 110.0); // reste 20 s
		Check(s.Start(now, 30u, "c") && s.Reason() == "c", "reprogrammation pendant le compte à rebours");
		const auto a = Announces(Run(s, now, now + 20.0));
		Check(!a.empty() && a[0] == 30u, "reprogrammation : nouvelle annonce de départ");
	}

	// 4. Arrêt immédiat et saut d'horloge.
	{
		ClusterShutdown s(1.0);
		double now = 0.0;
		(void)s.Start(now, 0u, "urgence");
		auto t = s.Tick(now + 0.1);
		Check(t.size() == 2u && t[0].kind == Kind::Announce && t[0].remainingSeconds == 0u && t[1].kind == Kind::StopShards,
			"délai 0 : annonce « maintenant » puis arrêt des shards");
		ClusterShutdown j;
		(void)j.Start(0.0, 600u, "");
		(void)j.Tick(0.1);                // annonce de départ
		auto jump = j.Tick(591.0);        // saut : reste 9 s
		Check(jump.size() == 1u && jump[0].kind == Kind::Announce && jump[0].remainingSeconds == 9u,
			"saut d'horloge : une seule annonce, durée réelle");
		Check(j.Start(1.0, ClusterShutdown::kMaxDelaySeconds + 100u, "") && j.RemainingSeconds(1.0) == ClusterShutdown::kMaxDelaySeconds,
			"délai borné à 24 h");
	}

	// 4 bis. Start entre la lecture de l'horloge et Tick : pas de « 5 min 1 s ».
	{
		ClusterShutdown s;
		(void)s.Start(10.5, 300u, "");
		auto t = s.Tick(10.0); // horloge lue avant le Start
		Check(t.size() == 1u && t[0].remainingSeconds == 300u, "annonce bornée au délai demandé");
		Check(s.Tick(10.6).empty(), "pas de seconde annonce « 5 min »");
	}

	// 4 ter. Motif tronqué sans couper un caractère.
	{
		std::string r(199, 'a');
		r += "\xC3\xA9"; // « é » à cheval sur la limite de 200 octets
		Check(TruncateUtf8(r, 200u).size() == 199u, "coupure avant un caractère multi-octet");
		Check(TruncateUtf8("court", 200u) == "court", "motif court inchangé");
	}

	// 5. Texte d'annonce.
	{
		Check(FormatShutdownAnnouncement(300u, "maj") == "Arrêt du serveur dans 5 min : maj. Votre personnage sera sauvegardé.", "texte minutes");
		Check(FormatShutdownAnnouncement(90u, "") == "Arrêt du serveur dans 1 min 30 s. Votre personnage sera sauvegardé.", "texte min + s");
		Check(FormatShutdownAnnouncement(0u, "") == "Arrêt du serveur maintenant. Votre personnage sera sauvegardé.", "texte maintenant");
	}

	if (s_failCount == 0)
		std::puts("[OK] cluster_shutdown_tests");
	return s_failCount == 0 ? 0 : 1;
}
