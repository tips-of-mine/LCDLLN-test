#pragma once

// M33.3 — Bot detection heuristics: track input timing, detect impossible actions.
// Records per-user action timestamps and computes timing regularity.
// Suspicious accounts are flagged for manual review or auto-banned.
// Thread-safe only if caller serialises access (single-worker v1).

#include "engine/core/Config.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::server
{
	/// Identifies the category of action being tracked for bot detection.
	enum class BotActionType : uint8_t
	{
		SkillUse    = 0, ///< Player used an ability/skill.
		ChatSend    = 1, ///< Player sent a chat message.
		MovementTick = 2, ///< Player reported a movement update.
		AttackHit   = 3, ///< Player landed a combat hit.
	};

	/// Configuration for BotDetector.
	struct BotDetectorConfig
	{
		/// Number of consecutive action timestamps to keep per user for timing analysis.
		size_t window_size = 50;
		/// Minimum plausible interval (ms) between two human actions (below = suspicious).
		double min_action_interval_ms = 50.0;
		/// Variance of inter-action intervals (ms²) below which the input is "too regular".
		/// Humans have higher variance; bots tend to fire at near-constant intervals.
		double regularity_variance_threshold_ms2 = 25.0;
		/// Count of suspicious events that triggers a "flag for review".
		uint32_t flag_threshold = 3;
		/// Count of suspicious events that triggers auto-ban (0 = disabled).
		uint32_t autoban_threshold = 10;
		/// Inactivity period (seconds) after which user state is purged.
		uint32_t idle_purge_sec = 600;
	};

	/// Bot detection result returned by RecordAction.
	enum class BotSignal : uint8_t
	{
		Clean      = 0, ///< No anomaly detected.
		Suspicious = 1, ///< Threshold crossed — flag for review.
		AutoBan    = 2, ///< Hard threshold — auto-ban immediately.
	};

	/// Bot heuristics detector.
	/// Wire RecordAction() into skill-use and chat handlers.
	/// Call IsSuspicious() / ShouldAutoBan() to gate further processing.
	class BotDetector
	{
	public:
		BotDetector() = default;

		/// Set configuration. Call before use.
		void SetConfig(const BotDetectorConfig& config);

		/// Build config from engine Config
		/// (keys: bot_detect.window_size, bot_detect.min_action_interval_ms,
		///        bot_detect.regularity_variance_threshold_ms2,
		///        bot_detect.flag_threshold, bot_detect.autoban_threshold,
		///        bot_detect.idle_purge_sec).
		static BotDetectorConfig LoadConfig(const engine::core::Config& config);

		/// Record one action from a user and evaluate heuristics.
		/// Returns BotSignal::Clean, ::Suspicious, or ::AutoBan.
		BotSignal RecordAction(uint64_t userId, BotActionType type,
			std::chrono::steady_clock::time_point now);

		/// Returns true if the user is currently flagged as suspicious.
		bool IsSuspicious(uint64_t userId) const;

		/// Returns true if the user crossed the auto-ban threshold.
		bool ShouldAutoBan(uint64_t userId) const;

		/// Manually flag a user for review (e.g., from a GM report).
		void FlagForReview(uint64_t userId);

		/// Clear the flag for a user (after successful manual review).
		void ClearFlag(uint64_t userId);

		/// Read-only view of currently flagged user IDs.
		const std::unordered_set<uint64_t>& GetFlaggedUsers() const { return m_flagged; }

		/// Read-only view of users that crossed the auto-ban threshold.
		const std::unordered_set<uint64_t>& GetAutoBanUsers() const { return m_autoban; }

		/// Purge stale user state. Call periodically (e.g., every few minutes).
		void PurgeExpired();

	private:
		/// Compute mean of a deque of double values.
		static double Mean(const std::deque<double>& v);

		/// Compute variance of a deque of double values.
		static double Variance(const std::deque<double>& v, double mean);

		struct UserActionState
		{
			/// Ring buffer of recent action timestamps.
			std::deque<std::chrono::steady_clock::time_point> timestamps;
			/// Cumulative suspicious-event count.
			uint32_t suspicious_count = 0;
			/// Last recorded action time (for idle purge).
			std::chrono::steady_clock::time_point last_active{};
		};

		BotDetectorConfig m_config;
		std::unordered_map<uint64_t, UserActionState> m_byUser;
		std::unordered_set<uint64_t> m_flagged;
		std::unordered_set<uint64_t> m_autoban;
	};
}
