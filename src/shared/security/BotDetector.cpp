// M33.3 — Bot detection heuristics implementation.
#include "engine/server/BotDetector.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace engine::server
{
	// -----------------------------------------------------------------------
	// Configuration
	// -----------------------------------------------------------------------

	void BotDetector::SetConfig(const BotDetectorConfig& config)
	{
		m_config = config;
		LOG_INFO(Net,
			"[BotDetector] Config set: window={} min_interval_ms={} variance_threshold={} "
			"flag_threshold={} autoban_threshold={} idle_purge_sec={}",
			m_config.window_size,
			m_config.min_action_interval_ms,
			m_config.regularity_variance_threshold_ms2,
			m_config.flag_threshold,
			m_config.autoban_threshold,
			m_config.idle_purge_sec);
	}

	BotDetectorConfig BotDetector::LoadConfig(const engine::core::Config& config)
	{
		BotDetectorConfig c;
		c.window_size                       = static_cast<size_t>(config.GetInt("bot_detect.window_size", 50));
		c.min_action_interval_ms            = static_cast<double>(config.GetInt("bot_detect.min_action_interval_ms", 50));
		c.regularity_variance_threshold_ms2 = static_cast<double>(config.GetInt("bot_detect.regularity_variance_threshold_ms2", 25));
		c.flag_threshold                    = static_cast<uint32_t>(config.GetInt("bot_detect.flag_threshold", 3));
		c.autoban_threshold                 = static_cast<uint32_t>(config.GetInt("bot_detect.autoban_threshold", 10));
		c.idle_purge_sec                    = static_cast<uint32_t>(config.GetInt("bot_detect.idle_purge_sec", 600));
		return c;
	}

	// -----------------------------------------------------------------------
	// Action recording and heuristics
	// -----------------------------------------------------------------------

	BotSignal BotDetector::RecordAction(uint64_t userId, BotActionType type,
		std::chrono::steady_clock::time_point now)
	{
		UserActionState& st = m_byUser[userId];
		st.last_active = now;

		// Keep window bounded.
		if (st.timestamps.size() >= m_config.window_size)
			st.timestamps.pop_front();
		st.timestamps.push_back(now);

		// Need at least 2 samples to compute intervals.
		if (st.timestamps.size() < 2)
			return BotSignal::Clean;

		// Compute inter-action intervals in milliseconds.
		std::deque<double> intervals;
		for (size_t i = 1; i < st.timestamps.size(); ++i)
		{
			const double ms = std::chrono::duration<double, std::milli>(
				st.timestamps[i] - st.timestamps[i - 1]).count();
			intervals.push_back(ms);
		}

		bool anomaly = false;

		// Heuristic 1: impossible speed — at least one interval below human minimum.
		for (const double iv : intervals)
		{
			if (iv < m_config.min_action_interval_ms)
			{
				anomaly = true;
				LOG_DEBUG(Net,
					"[BotDetector] Impossible speed detected: userId={} interval_ms={:.1f} min_ms={:.1f}",
					userId, iv, m_config.min_action_interval_ms);
				break;
			}
		}

		// Heuristic 2: too-regular timing (low variance).
		if (!anomaly && intervals.size() >= 5)
		{
			const double mean     = Mean(intervals);
			const double variance = Variance(intervals, mean);
			if (variance < m_config.regularity_variance_threshold_ms2)
			{
				anomaly = true;
				LOG_DEBUG(Net,
					"[BotDetector] Regular timing detected: userId={} mean_ms={:.1f} variance={:.2f} threshold={}",
					userId, mean, variance, m_config.regularity_variance_threshold_ms2);
			}
		}

		if (!anomaly)
			return BotSignal::Clean;

		++st.suspicious_count;
		LOG_INFO(Net,
			"[BotDetector] Suspicious action: userId={} type={} count={} flag_threshold={} autoban_threshold={}",
			userId,
			static_cast<uint32_t>(type),
			st.suspicious_count,
			m_config.flag_threshold,
			m_config.autoban_threshold);

		// Auto-ban threshold.
		if (m_config.autoban_threshold > 0 && st.suspicious_count >= m_config.autoban_threshold)
		{
			m_autoban.insert(userId);
			m_flagged.insert(userId);
			LOG_WARN(Net,
				"[BotDetector] AUTO-BAN threshold reached: userId={} count={}",
				userId, st.suspicious_count);
			return BotSignal::AutoBan;
		}

		// Flag-for-review threshold.
		if (st.suspicious_count >= m_config.flag_threshold)
		{
			m_flagged.insert(userId);
			LOG_INFO(Net,
				"[BotDetector] Account flagged for review: userId={} count={}",
				userId, st.suspicious_count);
			return BotSignal::Suspicious;
		}

		return BotSignal::Suspicious;
	}

	// -----------------------------------------------------------------------
	// Query helpers
	// -----------------------------------------------------------------------

	bool BotDetector::IsSuspicious(uint64_t userId) const
	{
		return m_flagged.count(userId) > 0;
	}

	bool BotDetector::ShouldAutoBan(uint64_t userId) const
	{
		return m_autoban.count(userId) > 0;
	}

	void BotDetector::FlagForReview(uint64_t userId)
	{
		m_flagged.insert(userId);
		LOG_INFO(Net, "[BotDetector] User manually flagged for review: userId={}", userId);
	}

	void BotDetector::ClearFlag(uint64_t userId)
	{
		m_flagged.erase(userId);
		m_autoban.erase(userId);
		auto it = m_byUser.find(userId);
		if (it != m_byUser.end())
			it->second.suspicious_count = 0;
		LOG_INFO(Net, "[BotDetector] Flag cleared for userId={}", userId);
	}

	// -----------------------------------------------------------------------
	// Maintenance
	// -----------------------------------------------------------------------

	void BotDetector::PurgeExpired()
	{
		const auto now    = std::chrono::steady_clock::now();
		const auto cutoff = now - std::chrono::seconds(m_config.idle_purge_sec);

		for (auto it = m_byUser.begin(); it != m_byUser.end(); )
		{
			if (it->second.last_active < cutoff)
			{
				// Preserve flagged / autoban status even after purge of timing state.
				it = m_byUser.erase(it);
			}
			else
			{
				++it;
			}
		}
		LOG_DEBUG(Net, "[BotDetector] PurgeExpired: tracked_users={} flagged={} autoban={}",
			m_byUser.size(), m_flagged.size(), m_autoban.size());
	}

	// -----------------------------------------------------------------------
	// Math helpers
	// -----------------------------------------------------------------------

	double BotDetector::Mean(const std::deque<double>& v)
	{
		if (v.empty()) return 0.0;
		double sum = 0.0;
		for (double x : v) sum += x;
		return sum / static_cast<double>(v.size());
	}

	double BotDetector::Variance(const std::deque<double>& v, double mean)
	{
		if (v.size() < 2) return 0.0;
		double sq = 0.0;
		for (double x : v)
		{
			const double d = x - mean;
			sq += d * d;
		}
		return sq / static_cast<double>(v.size() - 1);
	}

} // namespace engine::server
