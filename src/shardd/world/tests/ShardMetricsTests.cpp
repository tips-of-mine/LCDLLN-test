// ETAPE5.4 — tests des métriques de jeu du shard (formatage, saturation, tableau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/ShardMetrics.h"

#include <cmath>
#include <cstdio>
#include <string>

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

	bool Has(const std::string& text, const char* needle)
	{
		return text.find(needle) != std::string::npos;
	}

	using namespace engine::server;
}

int main()
{
	// 1. Formatage : chaque métrique a HELP, TYPE et sa valeur.
	{
		ShardGameplayMetrics m;
		m.tick = 12345;
		m.tickHz = 20;
		m.tickLastSeconds = 0.012;
		m.tickMaxSeconds = 0.25;
		m.tickLoad = 0.42;
		m.clients = 37;
		m.mobs = 900;
		m.mobsAsleep = 700;
		m.regionsWarm = 6;
		m.saveQueuePending = 3;
		m.saveQueueExecuted = 1000;
		const std::string t = FormatShardMetricsPrometheus(m);
		Check(Has(t, "# TYPE lcdlln_shard_tick_total counter\nlcdlln_shard_tick_total 12345\n"), "compteur de ticks");
		Check(Has(t, "lcdlln_shard_clients 37\n") && Has(t, "lcdlln_shard_mobs_asleep 700\n"), "joueurs et mobs endormis");
		Check(Has(t, "lcdlln_shard_tick_max_seconds 0.25\n") && Has(t, "lcdlln_shard_tick_load_ratio 0.42\n"), "durées du tick");
		Check(Has(t, "lcdlln_shard_regions_warm 6\n") && Has(t, "lcdlln_shard_save_queue_pending 3\n"), "cellules et file");
		Check(Has(t, "lcdlln_shard_saturated 0\n"), "non saturé");
		// Une ligne HELP par métrique, toutes préfixées.
		size_t helps = 0;
		for (size_t p = t.find("# HELP "); p != std::string::npos; p = t.find("# HELP ", p + 1))
		{
			++helps;
			Check(t.compare(p + 7, 13, "lcdlln_shard_") == 0, "préfixe lcdlln_shard_");
		}
		Check(helps == 22u, "22 métriques");
	}

	// 2. Saturation : délestage ou file de sauvegarde débordée.
	{
		ShardGameplayMetrics m;
		Check(!IsSaturated(m), "au repos : non saturé");
		m.sheddingActive = true;
		Check(IsSaturated(m) && Has(FormatShardMetricsPrometheus(m), "lcdlln_shard_saturated 1\n"), "délestage : saturé");
		m.sheddingActive = false;
		m.saveQueueOverloaded = true;
		Check(IsSaturated(m), "file débordée : saturé");
	}

	// 3. Charge en pourcentage (heartbeat) : arrondie, bornée, NaN → 0.
	{
		ShardGameplayMetrics m;
		m.tickLoad = 0.856;
		Check(TickLoadPercent(m) == 86u, "85,6 % → 86");
		m.tickLoad = 50.0;
		Check(TickLoadPercent(m) == 1000u, "borné à 1000 %");
		m.tickLoad = std::nan("");
		Check(TickLoadPercent(m) == 0u, "NaN → 0");
		Check(Has(FormatShardMetricsPrometheus(m), "lcdlln_shard_tick_load_ratio 0\n"), "NaN formaté 0");
	}

	// 4. Tableau partagé.
	{
		ShardMetricsBoard b;
		Check(!b.HasSnapshot() && b.Snapshot().tick == 0u, "vide avant publication");
		ShardGameplayMetrics m;
		m.tick = 7;
		b.Publish(m);
		Check(b.HasSnapshot() && b.Snapshot().tick == 7u, "photo publiée");
	}

	if (s_failCount == 0)
		std::puts("[OK] shard_metrics_tests");
	return s_failCount == 0 ? 0 : 1;
}
