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

	void ShardRegistry::EvictStaleHeartbeats(int timeout_sec)
	{
		auto now = std::chrono::steady_clock::now();
		auto threshold = now - std::chrono::seconds(timeout_sec);
		std::lock_guard<std::mutex> lock(m_mutex);
		size_t n = 0;
		for (auto& [id, e] : m_shards)
		{
			if (e.state != ShardState::Offline && e.last_heartbeat < threshold)
			{
				e.state = ShardState::Offline;
				++n;
			}
		}
		if (n > 0)
			LOG_INFO(Core, "[ShardRegistry] EvictStaleHeartbeats: {} shard(s) marked offline (timeout={}s)", n, timeout_sec);
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
		for (const auto& [id, e] : m_shards)
		{
			if (e.state == ShardState::Online && e.current_load < e.max_capacity)
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
				return info;
			}
		}
		return std::nullopt;
	}
}
