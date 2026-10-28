// ETAPE5.4 — métriques de jeu du shard. Voir ShardMetrics.h.

#include "src/shardd/world/ShardMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine::server
{
	namespace
	{
		void Gauge(std::string& out, const char* name, const char* help, double value)
		{
			char line[160];
			out += "# HELP ";
			out += name;
			out += ' ';
			out += help;
			out += "\n# TYPE ";
			out += name;
			out += " gauge\n";
			std::snprintf(line, sizeof(line), "%s %.6g\n", name, std::isfinite(value) ? value : 0.0);
			out += line;
		}

		void Counter(std::string& out, const char* name, const char* help, uint64_t value)
		{
			char line[160];
			out += "# HELP ";
			out += name;
			out += ' ';
			out += help;
			out += "\n# TYPE ";
			out += name;
			out += " counter\n";
			std::snprintf(line, sizeof(line), "%s %llu\n", name, static_cast<unsigned long long>(value));
			out += line;
		}
	}

	uint16_t TickLoadPercent(const ShardGameplayMetrics& m)
	{
		if (!std::isfinite(m.tickLoad) || m.tickLoad <= 0.0)
			return 0u;
		return static_cast<uint16_t>(std::lround((std::min)(m.tickLoad * 100.0, 1000.0)));
	}

	bool IsSaturatedOrStale(const ShardGameplayMetrics& m, double ageSeconds)
	{
		return IsSaturated(m) || !(ageSeconds <= kShardMetricsStaleSeconds);
	}

	std::string FormatShardMetricsPrometheus(const ShardGameplayMetrics& m, double ageSeconds)
	{
		std::string out;
		out.reserve(3400);
		Counter(out, "lcdlln_shard_tick_total", "Ticks de simulation executes.", m.tick);
		Gauge(out, "lcdlln_shard_tick_hz", "Frequence nominale du tick.", m.tickHz);
		Gauge(out, "lcdlln_shard_tick_last_seconds", "Duree du dernier tick.", m.tickLastSeconds);
		Gauge(out, "lcdlln_shard_tick_max_seconds", "Pire tick de la derniere seconde.", m.tickMaxSeconds);
		Gauge(out, "lcdlln_shard_tick_load_ratio", "Charge lissee du tick (1 = budget entier).", m.tickLoad);
		Gauge(out, "lcdlln_shard_clients", "Joueurs connectes.", m.clients);
		Gauge(out, "lcdlln_shard_mobs", "Mobs vivants.", m.mobs);
		Gauge(out, "lcdlln_shard_mobs_asleep", "Mobs endormis (loin des joueurs).", m.mobsAsleep);
		Gauge(out, "lcdlln_shard_loot_bags", "Sacs de butin au sol.", m.lootBags);
		Gauge(out, "lcdlln_shard_regions_warm", "Cellules chaudes (actives ou chargees).", m.regionsWarm);
		Gauge(out, "lcdlln_shard_regions_tracked", "Cellules suivies (non dechargees).", m.regionsTracked);
		Gauge(out, "lcdlln_shard_pending_hellos", "Hello en attente d'admission.", m.pendingHellos);
		Gauge(out, "lcdlln_shard_pending_character_loads", "Lectures de personnage en cours.", m.pendingCharacterLoads);
		Gauge(out, "lcdlln_shard_save_queue_pending", "Sauvegardes en attente.", m.saveQueuePending);
		Gauge(out, "lcdlln_shard_save_queue_overloaded", "File de sauvegarde a 75 % ou plus (0/1).", m.saveQueueOverloaded ? 1.0 : 0.0);
		Counter(out, "lcdlln_shard_save_queue_executed_total", "Sauvegardes executees.", m.saveQueueExecuted);
		Counter(out, "lcdlln_shard_save_queue_rejected_total", "Sauvegardes refusees (file pleine).", m.saveQueueRejected);
		Counter(out, "lcdlln_shard_save_queue_coalesced_total", "Sauvegardes fusionnees.", m.saveQueueCoalesced);
		Gauge(out, "lcdlln_shard_shedding_active", "Delestage actif (0/1).", m.sheddingActive ? 1.0 : 0.0);
		Counter(out, "lcdlln_shard_shedding_enter_total", "Entrees en delestage.", m.sheddingEnterCount);
		Counter(out, "lcdlln_shard_stall_total", "Calages du tick (ecart >= seuil).", m.stallCount);
		Counter(out, "lcdlln_shard_save_blocks_written_total", "Blocs de personnage reecrits en base.", m.saveBlocksWritten);
		Counter(out, "lcdlln_shard_save_blocks_skipped_total", "Blocs inchanges non reecrits (sauvegarde differentielle).", m.saveBlocksSkipped);
		Counter(out, "lcdlln_shard_saves_without_write_total", "Sauvegardes sans aucune ecriture en base.", m.savesWithoutWrite);
		Gauge(out, "lcdlln_shard_saturated", "Contre-pression : nouveaux joueurs refuses (0/1).", IsSaturatedOrStale(m, ageSeconds) ? 1.0 : 0.0);
		Gauge(out, "lcdlln_shard_metrics_age_seconds", "Age de la photo des metriques (tick bloque si > 3 s).", ageSeconds);
		return out;
	}
}
