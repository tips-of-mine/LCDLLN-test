// M22.1 — Master shard registry: state machine (registering → online → degraded → offline), in-memory + optional persistence by caller.

#include "engine/server/ShardRegistry.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <chrono>

namespace engine::server
{
	std::optional<uint32_t> ShardRegistry::RegisterShard(std::string name, std::string endpoint, uint32_t max_capacity, std::string region)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_name_to_id.count(name) != 0)
		{
			LOG_WARN(Core, "[ShardRegistry] RegisterShard failed: duplicate name '{}'", name);
			return std::nullopt;
		}
		uint32_t id = m_next_id++;
		Entry e;
		e.shard_id = id;
		e.name = std::move(name);
		e.endpoint = std::move(endpoint);
		e.region = std::move(region);
		e.max_capacity = max_capacity;
		e.current_load = 0;
		e.last_heartbeat = std::chrono::steady_clock::now();
		e.state = ShardState::Registering;
		m_shards[id] = std::move(e);
		m_name_to_id[m_shards[id].name] = id;
		LOG_INFO(Core, "[ShardRegistry] RegisterShard OK (id={}, name='{}', endpoint='{}')", id, m_shards[id].name, m_shards[id].endpoint);
		return id;
	}

	bool ShardRegistry::UpdateHeartbeat(uint32_t shard_id, uint32_t current_load)
	{
		auto now = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_shards.find(shard_id);
		if (it == m_shards.end())
		{
			LOG_WARN(Core, "[ShardRegistry] UpdateHeartbeat: shard_id {} not found", shard_id);
			return false;
		}
		it->second.last_heartbeat = now;
		it->second.current_load = current_load;
		bool became_online = (it->second.state == ShardState::Registering);
		if (became_online)
			it->second.state = ShardState::Online;
		if (became_online)
			LOG_INFO(Core, "[ShardRegistry] Shard {} now online (load={})", shard_id, current_load);
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
		info.region = e.region;
		info.max_capacity = e.max_capacity;
		info.current_load = e.current_load;
		info.last_heartbeat = e.last_heartbeat;
		info.state = e.state;
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
			info.region = e.region;
			info.max_capacity = e.max_capacity;
			info.current_load = e.current_load;
			info.last_heartbeat = e.last_heartbeat;
			info.state = e.state;
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
		info.region = best->region;
		info.max_capacity = best->max_capacity;
		info.current_load = best->current_load;
		info.last_heartbeat = best->last_heartbeat;
		info.state = best->state;
		return info;
	}
}
