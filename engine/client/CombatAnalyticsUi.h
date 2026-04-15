#pragma once

#include "engine/core/Config.h"
#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::client
{
	/// Maximum lines stored in the combat log ring buffer (M39.4).
	inline constexpr uint32_t kCombatLogMaxLines = 500u;

	/// Maximum entries shown in the DPS and threat meters (M39.4).
	inline constexpr uint32_t kMeterMaxEntries = 5u;

	// ── DPS meter ─────────────────────────────────────────────────────────────

	/// Per-entity DPS tracker row (M39.4).
	struct DpsMeterEntry
	{
		engine::server::EntityId entityId    = 0;
		std::string              displayName;
		uint64_t                 totalDamage = 0;
		float                    dps         = 0.0f; ///< Damage per second over fight duration.
		float                    barFraction = 0.0f; ///< [0,1] relative to highest DPS player.
	};

	/// DPS meter panel state (M39.4).
	struct DpsMeterState
	{
		bool     inCombat       = false;
		float    elapsedSeconds = 0.0f;
		/// Top-5 entries sorted by DPS descending.
		std::vector<DpsMeterEntry> entries;
	};

	// ── Threat meter ──────────────────────────────────────────────────────────

	/// Threat colour tier (M39.4): red=pulling aggro, yellow=close, green=safe.
	enum class ThreatColor : uint8_t { Safe = 0, Warning = 1, Pulling = 2 };

	/// Per-entity threat row (M39.4).
	struct ThreatMeterEntry
	{
		engine::server::EntityId entityId     = 0;
		std::string              displayName;
		float                    threatValue  = 0.0f;
		float                    barFraction  = 0.0f; ///< [0,1] vs highest-threat entity.
		ThreatColor              color        = ThreatColor::Safe;
	};

	/// Threat meter panel state (M39.4).
	struct ThreatMeterState
	{
		engine::server::EntityId targetEntityId = 0;
		/// Entries sorted by threat descending (first = highest threat = 100%).
		std::vector<ThreatMeterEntry> entries;
	};

	// ── Combat log ────────────────────────────────────────────────────────────

	/// Category flags for combat log filtering (M39.4).
	enum class CombatLogCategory : uint8_t
	{
		Damage  = 0,
		Healing = 1,
		Buff    = 2,
		Death   = 3,
		Other   = 4
	};

	/// One combat log line (M39.4).
	struct CombatLogEntry
	{
		std::string       timestamp;  ///< "[HH:MM:SS]" formatted wall-clock time.
		std::string       text;       ///< Human-readable event description.
		CombatLogCategory category = CombatLogCategory::Other;
	};

	/// Active filter set for the combat log panel (M39.4).
	struct CombatLogFilter
	{
		bool showDamage  = true;
		bool showHealing = true;
		bool showBuff    = true;
		bool showDeath   = true;
		bool showOther   = true;
	};

	/// Combat log panel state (M39.4).
	struct CombatLogState
	{
		/// All lines in the ring buffer (up to kCombatLogMaxLines).
		std::vector<CombatLogEntry> lines;
		/// Lines that pass the current filter — rebuilt on each filter change.
		std::vector<const CombatLogEntry*> visibleLines;
		CombatLogFilter filter{};
		uint32_t        scrollOffset = 0; ///< First visible line index in visibleLines.
	};

	// ── Combined presenter state ──────────────────────────────────────────────

	/// Aggregated state of all three advanced UI panels (M39.4).
	struct CombatAnalyticsPanelState
	{
		DpsMeterState    dps{};
		ThreatMeterState threat{};
		CombatLogState   log{};
	};

	/// Advanced combat analytics UI presenter: DPS meter, threat meter,
	/// scrollable combat log with filters and CSV export (M39.4).
	///
	/// Feed data via:
	///   OnCombatEvent()   — damage events from the server
	///   OnThreatUpdate()  — threat values from the server
	///   AddLogEntry()     — raw log lines from any system
	///   Tick()            — advance timers (DPS recalculation)
	class CombatAnalyticsUiPresenter final
	{
	public:
		CombatAnalyticsUiPresenter()                                 = default;
		~CombatAnalyticsUiPresenter();

		CombatAnalyticsUiPresenter(const CombatAnalyticsUiPresenter&)            = delete;
		CombatAnalyticsUiPresenter& operator=(const CombatAnalyticsUiPresenter&) = delete;

		/// Initialise the presenter.
		bool Init();

		/// Shutdown and release all state.
		void Shutdown();

		// ── DPS meter ─────────────────────────────────────────────────────────

		/// Record a damage event (sourced from CombatEventMessage).
		/// Entering combat is implicit on the first OnCombatEvent call.
		void OnCombatEvent(engine::server::EntityId attackerEntityId,
		                   std::string_view         attackerDisplayName,
		                   uint32_t                 damage,
		                   uint64_t                 wallClockMs);

		/// Advance DPS timers and recompute bar fractions.
		void Tick(float deltaSeconds);

		/// Signal combat-end: freeze elapsed time, keep last values displayed.
		void OnCombatEnd();

		/// Reset DPS meter (clear all tracked damage).
		void ResetDpsMeter();

		// ── Threat meter ──────────────────────────────────────────────────────

		/// Update threat value for one entity against a specific target.
		void OnThreatUpdate(engine::server::EntityId targetEntityId,
		                    engine::server::EntityId entityId,
		                    std::string_view         displayName,
		                    float                    threatValue);

		/// Clear all threat data (e.g. target died).
		void ResetThreatMeter();

		// ── Combat log ────────────────────────────────────────────────────────

		/// Append a formatted entry to the combat log ring buffer.
		void AddLogEntry(std::string_view timestamp,
		                 std::string_view text,
		                 CombatLogCategory category);

		/// Convenience: builds the "[HH:MM:SS] ..." entry and appends it.
		/// wallClockMs is milliseconds since epoch (or since boot — just for formatting).
		void AddLogEntryNow(uint64_t wallClockMs, std::string_view text, CombatLogCategory category);

		/// Enable or disable one category in the log filter.
		void SetLogFilter(CombatLogCategory category, bool enabled);

		/// Scroll the visible log list by \p delta lines.
		void ScrollLog(int32_t delta);

		/// Export the full (unfiltered) combat log as CSV.
		/// Path is content-relative; directories are created as needed.
		/// Returns false on write failure.
		bool ExportLogCsv(const engine::core::Config& config,
		                  std::string_view relativePath = "logs/combat_log.csv") const;

		// ── State accessor ────────────────────────────────────────────────────

		const CombatAnalyticsPanelState& GetState() const { return m_state; }

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Rebuild the DPS meter entries sorted by DPS, capped to kMeterMaxEntries.
		void RebuildDpsMeter();

		/// Rebuild the threat meter entries sorted by threat, capped to kMeterMaxEntries.
		void RebuildThreatMeter();

		/// Rebuild m_state.log.visibleLines from current filter.
		void RebuildVisibleLog();

		/// Format a millisecond epoch as "[HH:MM:SS]".
		static std::string FormatTimestamp(uint64_t wallClockMs);

		// ── Internal damage tracking (not exposed to rendering) ───────────────
		struct DmgRecord
		{
			engine::server::EntityId entityId = 0;
			std::string              displayName;
			uint64_t                 totalDamage = 0;
		};

		// ── Internal threat tracking ──────────────────────────────────────────
		struct ThreatRecord
		{
			engine::server::EntityId entityId = 0;
			std::string              displayName;
			float                    threatValue = 0.0f;
		};

		CombatAnalyticsPanelState m_state{};

		std::vector<DmgRecord>    m_dmgRecords;
		std::vector<ThreatRecord> m_threatRecords;

		/// Ring buffer of all log entries (unfiltered).
		std::vector<CombatLogEntry> m_allLogEntries;

		/// DPS timing
		bool  m_inCombat       = false;
		float m_elapsedSeconds = 0.0f;
		bool  m_initialized    = false;
	};

} // namespace engine::client
