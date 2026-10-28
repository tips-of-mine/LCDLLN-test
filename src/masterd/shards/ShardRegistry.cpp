// M22.1 — Master shard registry: state machine (registering → online → degraded → offline), in-memory + optional persistence by caller.

#include "src/masterd/shards/ShardRegistry.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace engine::server
{
	std::optional<uint32_t> ShardRegistry::RegisterShard(std::string name, std::string endpoint, std::string udp_endpoint, uint32_t max_capacity,
		std::string region, std::string display_name,
		engine::network::ShardGameMode game_mode, engine::network::ShardRuleset ruleset)
	{
LOG_DEBUG(Server, "[SREG_REG] RegisterShard name='{}' endpoint='{}' cap={}", name.c_str(), endpoint.c_str(), max_capacity);
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_name_to_id.count(name) != 0)
		{
			LOG_WARN(Core, "[ShardRegistry] RegisterShard failed: duplicate name '{}'", name);
			LOG_DEBUG(Server, "[SREG_REG] RegisterShard result id=0");
			return std::nullopt;
		}
		uint32_t id = m_next_id++;
		Entry e;
		e.shard_id = id;
		e.name = std::move(name);
		e.endpoint = std::move(endpoint);
		e.udp_endpoint = std::move(udp_endpoint);
		e.region = std::move(region);
		e.max_capacity = max_capacity;
		e.current_load = 0;
		e.last_heartbeat = std::chrono::steady_clock::now();
		e.state = ShardState::Registering;
		// Repli : si aucun nom public fourni, on affiche l'identifiant technique.
		e.display_name = display_name.empty() ? e.name : std::move(display_name);
		e.game_mode = game_mode;
		e.ruleset = ruleset;
		m_shards[id] = std::move(e);
		m_name_to_id[m_shards[id].name] = id;
		LOG_INFO(Core, "[ShardRegistry] RegisterShard OK (id={}, name='{}', endpoint='{}', udp_endpoint='{}')", id, m_shards[id].name,
			m_shards[id].endpoint, m_shards[id].udp_endpoint.empty() ? "(non annonce)" : m_shards[id].udp_endpoint);
		if (m_shards[id].udp_endpoint.empty())
			LOG_WARN(Core, "[ShardRegistry] Shard '{}' sans endpoint UDP : les clients ne pourront pas entrer en jeu (renseigner SHARD_PUBLIC_HOST ou SHARD_REGISTER_UDP_ENDPOINT cote shard)",
				m_shards[id].name);
		return id;
	}

	bool ShardRegistry::UpdateShardAnnouncement(uint32_t shard_id, std::string endpoint, std::string udp_endpoint, uint32_t max_capacity,
		std::string region, std::string display_name,
		engine::network::ShardGameMode game_mode, engine::network::ShardRuleset ruleset)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_shards.find(shard_id);
		if (it == m_shards.end())
		{
			LOG_WARN(Core, "[ShardRegistry] UpdateShardAnnouncement: shard_id {} not found", shard_id);
			return false;
		}
		Entry& e = it->second;
		// On journalise uniquement ce qui change, pour qu'un réenregistrement banal
		// (coupure réseau, même configuration) reste silencieux.
		if (e.endpoint != endpoint)
		{
			LOG_INFO(Core, "[ShardRegistry] Shard {} : endpoint TCP '{}' -> '{}'", shard_id, e.endpoint, endpoint);
			e.endpoint = std::move(endpoint);
		}
		if (e.udp_endpoint != udp_endpoint)
		{
			LOG_INFO(Core, "[ShardRegistry] Shard {} : endpoint UDP '{}' -> '{}'", shard_id,
				e.udp_endpoint.empty() ? "(non annonce)" : e.udp_endpoint,
				udp_endpoint.empty() ? "(non annonce)" : udp_endpoint);
			e.udp_endpoint = std::move(udp_endpoint);
		}
		if (e.max_capacity != max_capacity)
		{
			LOG_INFO(Core, "[ShardRegistry] Shard {} : capacite {} -> {}", shard_id, e.max_capacity, max_capacity);
			e.max_capacity = max_capacity;
		}
		e.region = std::move(region);
		// Même repli que RegisterShard : nom public vide => nom technique.
		e.display_name = display_name.empty() ? e.name : std::move(display_name);
		e.game_mode = game_mode;
		e.ruleset = ruleset;
		if (e.udp_endpoint.empty())
			LOG_WARN(Core, "[ShardRegistry] Shard '{}' sans endpoint UDP : les clients ne pourront pas entrer en jeu", e.name);
		return true;
	}

	bool ShardRegistry::UpdateHeartbeat(uint32_t shard_id, uint32_t current_load, std::optional<bool> saturatedOpt)
	{
		auto now = std::chrono::steady_clock::now();
		bool log_degraded = false;
		bool log_recovered = false;
		uint32_t log_load = 0;
		uint32_t log_cap = 0;
		double log_ratio = 0.0;
		std::function<void(uint32_t)> degraded_cb;
		uint32_t degraded_shard_id = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_shards.find(shard_id);
			if (it == m_shards.end())
			{
				LOG_WARN(Core, "[ShardRegistry] UpdateHeartbeat: shard_id {} not found", shard_id);
				return false;
			}
			it->second.last_heartbeat = now;
			it->second.current_load = current_load;
			// ETAPE5.1 — un shard déclaré mort (Offline) qui redonne signe de vie (heartbeat ou
			// réenregistrement) redevient Online : ses verrous ont été libérés et ses joueurs
			// prévenus à sa mort, il peut de nouveau accueillir des joueurs. Sans ça, il restait
			// exclu de SelectShard / de la liste des serveurs jusqu'au redémarrage du master.
			const bool was_offline = (it->second.state == ShardState::Offline);
			bool became_online = (it->second.state == ShardState::Registering) || was_offline;
			if (became_online)
				it->second.state = ShardState::Online;
			if (became_online)
			{
				LOG_INFO(Core, "[ShardRegistry] Shard {} now online (load={}{})", shard_id, current_load,
					was_offline ? ", retour apres declaration de mort" : "");
			}

			// ETAPE5.4 — contre-pression : un shard saturé (délestage, file de sauvegarde
			// pleine) est traité comme plein.
			const bool was_saturated = it->second.saturated;
			const bool saturated = saturatedOpt.value_or(was_saturated);
			it->second.saturated = saturated;
			if (saturated != was_saturated)
				LOG_WARN(Core, "[ShardRegistry] ETAPE5.4 shard {} {} (contre-pression)", shard_id,
					saturated ? "sature : plus de nouveaux joueurs" : "n'est plus sature");

			// Transition Online → Degraded or Degraded → Online according to load ratio (STAB.10)
			if (it->second.state == ShardState::Online || it->second.state == ShardState::Degraded)
			{
				uint32_t cap = (it->second.max_capacity > 0u) ? it->second.max_capacity : 1u;
				double ratio = static_cast<double>(it->second.current_load) / static_cast<double>(cap);
				if (saturated)
					ratio = (std::max)(ratio, m_degraded_load_threshold);
				if (ratio >= m_degraded_load_threshold && it->second.state == ShardState::Online)
				{
					it->second.state = ShardState::Degraded;
					log_degraded = true;
					log_load = it->second.current_load;
					log_cap = it->second.max_capacity;
					log_ratio = ratio;
					degraded_cb = m_shard_degraded_callback;
					degraded_shard_id = shard_id;
				}
				else if (ratio < m_degraded_load_threshold && it->second.state == ShardState::Degraded)
				{
					it->second.state = ShardState::Online;
					log_recovered = true;
					log_load = it->second.current_load;
					log_cap = it->second.max_capacity;
					log_ratio = ratio;
				}
			}
		}
		if (log_degraded)
		{
			LOG_INFO(Core, "[ShardRegistry] Shard {} now degraded (load={}, cap={}, ratio={:.2f})",
				shard_id, log_load, log_cap, log_ratio);
			if (degraded_cb)
				degraded_cb(degraded_shard_id);
		}
		if (log_recovered)
			LOG_INFO(Core, "[ShardRegistry] Shard {} recovered to online (load={}, cap={}, ratio={:.2f})",
				shard_id, log_load, log_cap, log_ratio);
		return true;
	}

	bool ShardRegistry::MarkDown(uint32_t shard_id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_shards.find(shard_id);
		if (it == m_shards.end())
		{
			LOG_WARN(Core, "[ShardRegistry] MarkDown: shard_id {} not found", shard_id);
			return false;
		}
		it->second.state = ShardState::Offline;
		LOG_INFO(Core, "[ShardRegistry] MarkDown OK (id={})", shard_id);
		return true;
	}

	void ShardRegistry::SetShardDownCallback(std::function<void(uint32_t)> cb)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_shard_down_callback = std::move(cb);
	}

	void ShardRegistry::SetDegradedLoadThreshold(double threshold)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_degraded_load_threshold = threshold;
	}

	void ShardRegistry::SetShardDegradedCallback(std::function<void(uint32_t)> cb)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_shard_degraded_callback = std::move(cb);
	}

	void ShardRegistry::EvictStaleHeartbeats(int timeout_sec, std::optional<std::chrono::steady_clock::time_point> as_of)
	{
		auto now = as_of.value_or(std::chrono::steady_clock::now());
		auto threshold = now - std::chrono::seconds(timeout_sec);
		std::vector<uint32_t> marked;
		std::function<void(uint32_t)> cb;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			cb = m_shard_down_callback;
			for (auto& [id, e] : m_shards)
			{
				if (e.state != ShardState::Offline && e.last_heartbeat < threshold)
				{
					e.state = ShardState::Offline;
					marked.push_back(id);
				}
			}
		}
LOG_DEBUG(Server, "[SREG_REG] EvictStaleHeartbeats timeout={}s marked={}", timeout_sec, marked.size());
		for (uint32_t id : marked)
		{
			LOG_INFO(Core, "[ShardRegistry] shard_down (shard_id={})", id);
			if (cb)
				cb(id);
		}
		if (!marked.empty())
			LOG_INFO(Core, "[ShardRegistry] EvictStaleHeartbeats: {} shard(s) marked offline (timeout={}s)", marked.size(), timeout_sec);
	}

	std::optional<ShardInfo> ShardRegistry::GetShard(uint32_t shard_id) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_shards.find(shard_id);
		if (it == m_shards.end())
			return std::nullopt;
		const Entry& e = it->second;
		ShardInfo info;
		info.shard_id = e.shard_id;
		info.name = e.name;
		info.endpoint = e.endpoint;
			info.udp_endpoint = e.udp_endpoint;
		info.region = e.region;
		info.max_capacity = e.max_capacity;
		info.current_load = e.current_load;
		info.last_heartbeat = e.last_heartbeat;
		info.state = e.state;
		info.saturated = e.saturated;
		info.display_name = e.display_name;
		info.game_mode = e.game_mode;
		info.ruleset = e.ruleset;
		return info;
	}

	std::vector<ShardInfo> ShardRegistry::ListShards() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<ShardInfo> out;
		out.reserve(m_shards.size());
		for (const auto& [id, e] : m_shards)
		{
			ShardInfo info;
			info.shard_id = e.shard_id;
			info.name = e.name;
			info.endpoint = e.endpoint;
			info.udp_endpoint = e.udp_endpoint;
			info.region = e.region;
			info.max_capacity = e.max_capacity;
			info.current_load = e.current_load;
			info.last_heartbeat = e.last_heartbeat;
			info.state = e.state;
			info.saturated = e.saturated;
			info.display_name = e.display_name;
			info.game_mode = e.game_mode;
			info.ruleset = e.ruleset;
			out.push_back(std::move(info));
		}
		return out;
	}

	std::optional<ShardInfo> ShardRegistry::SelectShard() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const Entry* best = nullptr;
		double best_ratio = 2.0; // > 1 so any Online shard is better
		for (const auto& [id, e] : m_shards)
		{
			if (e.state != ShardState::Online)
				continue;
			uint32_t cap = (e.max_capacity > 0u) ? e.max_capacity : 1u;
			double ratio = static_cast<double>(e.current_load) / static_cast<double>(cap);
			if (ratio < best_ratio)
			{
				best_ratio = ratio;
				best = &e;
			}
		}
		if (best == nullptr)
			return std::nullopt;
		ShardInfo info;
		info.shard_id = best->shard_id;
		info.name = best->name;
		info.endpoint = best->endpoint;
		info.udp_endpoint = best->udp_endpoint;
		info.region = best->region;
		info.max_capacity = best->max_capacity;
		info.current_load = best->current_load;
		info.last_heartbeat = best->last_heartbeat;
		info.state = best->state;
		info.display_name = best->display_name;
		info.game_mode = best->game_mode;
		info.ruleset = best->ruleset;
		return info;
	}

	void ShardRegistry::SetShardConnection(uint32_t shard_id, uint32_t connId)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_shards.find(shard_id) == m_shards.end())
			return; // shard inconnu (race avec un evict ?). On ignore plutôt qu'insérer un fantôme.
		m_shard_connections[shard_id] = connId;
	}

	std::optional<uint32_t> ShardRegistry::GetShardConnection(uint32_t shard_id) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_shard_connections.find(shard_id);
		if (it == m_shard_connections.end())
			return std::nullopt;
		return it->second;
	}
}
