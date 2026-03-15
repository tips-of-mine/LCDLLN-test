#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Shard state machine: registering → online → degraded → offline (Master is source of truth).
	enum class ShardState : uint8_t
	{
		Registering = 0,
		Online = 1,
		Degraded = 2,
		Offline = 3
	};

	/// Immutable snapshot of a shard entry (capacity, load, last_heartbeat, endpoint/region).
	struct ShardInfo
	{
		uint32_t shard_id = 0;
		std::string name;
		std::string endpoint;
		std::string region;
		uint32_t max_capacity = 0;
		uint32_t current_load = 0;
		std::chrono::steady_clock::time_point last_heartbeat{};
		ShardState state = ShardState::Registering;
	};

	/// In-memory shard registry (Master source of truth). Optional persistence by caller (e.g. to shards table).
	/// Thread-safe. Timeout heartbeat => offline via EvictStaleHeartbeats.
	class ShardRegistry
	{
	public:
		ShardRegistry() = default;
		~ShardRegistry() = default;
		ShardRegistry(const ShardRegistry&) = delete;
		ShardRegistry& operator=(const ShardRegistry&) = delete;

		/// Registers a new shard (state Registering). Returns shard_id or nullopt if name duplicate.
		std::optional<uint32_t> RegisterShard(std::string name, std::string endpoint, uint32_t max_capacity, std::string region = {});

		/// Updates last_heartbeat (and optionally current_load). If state was Registering, transitions to Online.
		/// Returns true if shard exists and was updated.
		bool UpdateHeartbeat(uint32_t shard_id, uint32_t current_load = 0);

		/// Marks the shard as Offline.
		/// Returns true if shard existed.
		bool MarkDown(uint32_t shard_id);

		/// Marks any shard as Offline when last_heartbeat is older than \a timeout_sec.
		void EvictStaleHeartbeats(int timeout_sec);

		/// Returns a copy of the shard entry, or nullopt if not found.
		std::optional<ShardInfo> GetShard(uint32_t shard_id) const;

		/// Returns all shards (copy). Order unspecified.
		std::vector<ShardInfo> ListShards() const;

		/// Placeholder for M22.4: returns first Online shard with current_load < max_capacity, or nullopt.
		std::optional<ShardInfo> SelectShard() const;

	private:
		struct Entry
		{
			uint32_t shard_id = 0;
			std::string name;
			std::string endpoint;
			std::string region;
			uint32_t max_capacity = 0;
			uint32_t current_load = 0;
			std::chrono::steady_clock::time_point last_heartbeat{};
			ShardState state = ShardState::Registering;
		};
		mutable std::mutex m_mutex;
		std::unordered_map<uint32_t, Entry> m_shards;
		std::unordered_map<std::string, uint32_t> m_name_to_id;
		uint32_t m_next_id = 1;
	};
}
