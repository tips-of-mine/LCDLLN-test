#pragma once

#include "engine/server/NetServer.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace engine::server
{
	/// Thread-safe simple histogram for DB query latency (M23.2). Buckets in milliseconds.
	class DbLatencyHistogram
	{
	public:
		DbLatencyHistogram();

		/// Records one observation (latency in milliseconds). Thread-safe.
		void Observe(int latencyMs);

		/// Returns bucket upper bounds (same order as GetBucketCounts). Includes +Inf.
		const std::vector<double>& GetBucketBounds() const { return m_bucketBounds; }

		/// Returns cumulative counts per bucket (thread-safe snapshot). Size = GetBucketBounds().size().
		std::vector<uint64_t> GetBucketCounts() const;

		/// Returns total sum of observed latencies (ms). Thread-safe.
		uint64_t GetSum() const { return m_sum.load(std::memory_order_relaxed); }

		/// Returns total number of observations. Thread-safe.
		uint64_t GetCount() const { return m_count.load(std::memory_order_relaxed); }

	private:
		std::vector<double> m_bucketBounds;
		mutable std::mutex m_mutex;
		std::vector<uint64_t> m_bucketCounts;
		std::atomic<uint64_t> m_sum{ 0 };
		std::atomic<uint64_t> m_count{ 0 };
	};

	/// Builds Prometheus exposition format (text) from the given snapshot. Thread-safe (reads snapshots).
	/// Optional values (sessionActive, shardsOnline, authSuccess, authFail) can be 0 for Shard or minimal endpoints.
	std::string BuildPrometheusText(
		const NetServerStats& netStats,
		uint64_t sessionsActive,
		uint64_t shardsOnline,
		uint64_t authSuccessTotal,
		uint64_t authFailTotal,
		const DbLatencyHistogram* dbLatencyHistogram);
}
