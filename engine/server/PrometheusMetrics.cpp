// M23.2 — Prometheus text format and DB latency histogram.

#include "engine/server/PrometheusMetrics.h"
#include "engine/core/Log.h"

#include <cmath>
#include <limits>
#include <sstream>

namespace engine::server
{
	namespace
	{
		/// Standard histogram buckets for latency (ms): 1, 5, 10, 25, 50, 100, 250, 500, 1000, +Inf.
		std::vector<double> DefaultLatencyBuckets()
		{
			return { 1.0, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0, 1000.0, std::numeric_limits<double>::infinity() };
		}

	}

	DbLatencyHistogram::DbLatencyHistogram() : m_bucketBounds(DefaultLatencyBuckets()), m_bucketCounts(m_bucketBounds.size(), 0u) {}

	void DbLatencyHistogram::Observe(int latencyMs)
	{
		double v = static_cast<double>(latencyMs);
		m_sum.fetch_add(static_cast<uint64_t>(latencyMs), std::memory_order_relaxed);
		m_count.fetch_add(1, std::memory_order_relaxed);
		std::lock_guard<std::mutex> lock(m_mutex);
		for (size_t i = 0; i < m_bucketBounds.size(); ++i)
		{
			if (v <= m_bucketBounds[i])
			{
				m_bucketCounts[i]++;
				break;
			}
		}
	}

	std::vector<uint64_t> DbLatencyHistogram::GetBucketCounts() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_bucketCounts;
	}

	std::string BuildPrometheusText(
		const NetServerStats& netStats,
		uint64_t sessionsActive,
		uint64_t shardsOnline,
		uint64_t authSuccessTotal,
		uint64_t authFailTotal,
		const DbLatencyHistogram* dbLatencyHistogram)
	{
		std::ostringstream out;
		out.precision(6);

		// Gauges
		out << "# HELP connections_active Number of active TCP connections.\n";
		out << "# TYPE connections_active gauge\n";
		out << "connections_active " << netStats.connectionsActive << "\n";

		out << "# HELP handshake_fail_total Total number of handshake failures.\n";
		out << "# TYPE handshake_fail_total counter\n";
		out << "handshake_fail_total " << netStats.handshakeFail << "\n";

		out << "# HELP bytes_in_total Total bytes received.\n";
		out << "# TYPE bytes_in_total counter\n";
		out << "bytes_in_total " << netStats.bytesIn << "\n";

		out << "# HELP bytes_out_total Total bytes sent.\n";
		out << "# TYPE bytes_out_total counter\n";
		out << "bytes_out_total " << netStats.bytesOut << "\n";

		out << "# HELP auth_success_total Total successful authentications.\n";
		out << "# TYPE auth_success_total counter\n";
		out << "auth_success_total " << authSuccessTotal << "\n";

		out << "# HELP auth_fail_total Total failed authentication attempts.\n";
		out << "# TYPE auth_fail_total counter\n";
		out << "auth_fail_total " << authFailTotal << "\n";

		out << "# HELP sessions_active Number of active sessions.\n";
		out << "# TYPE sessions_active gauge\n";
		out << "sessions_active " << sessionsActive << "\n";

		out << "# HELP shards_online Number of shards currently online.\n";
		out << "# TYPE shards_online gauge\n";
		out << "shards_online " << shardsOnline << "\n";

		if (dbLatencyHistogram)
		{
			out << "# HELP db_query_latency_ms Database query latency in milliseconds.\n";
			out << "# TYPE db_query_latency_ms histogram\n";
			std::vector<uint64_t> counts = dbLatencyHistogram->GetBucketCounts();
			const std::vector<double>& bounds = dbLatencyHistogram->GetBucketBounds();
			uint64_t cum = 0;
			for (size_t i = 0; i < bounds.size(); ++i)
			{
				if (i < counts.size())
					cum += counts[i];
				out << "db_query_latency_ms_bucket{le=\"";
				if (std::isinf(bounds[i]))
					out << "+Inf";
				else
					out << bounds[i];
				out << "\"} " << cum << "\n";
			}
			out << "db_query_latency_ms_sum " << dbLatencyHistogram->GetSum() << "\n";
			out << "db_query_latency_ms_count " << dbLatencyHistogram->GetCount() << "\n";
		}

		return out.str();
	}
}
