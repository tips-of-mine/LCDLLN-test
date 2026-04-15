#pragma once
// M39.4 — Advanced Combat UI: DPS meter, threat meter, combat log.
//
// Depends on M16.2 (CombatHud) and M32.2 (PartyHud).
// Self-contained presenter: no GPU resources, purely CPU-side state.

#include "engine/client/UIModel.h"
#include "engine/server/ServerProtocol.h"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	// =========================================================================
	// DPS meter
	// =========================================================================

	/// One row in the DPS meter (M39.4).
	struct DpsMeterEntry
	{
		engine::server::EntityId entityId      = 0;
		std::string              displayName;
		uint64_t                 totalDamage   = 0;
		/// DPS = totalDamage / fightElapsedSeconds.
		float                    dps           = 0.0f;
		/// [0.0, 1.0] — fraction of the top player's DPS (bar width).
		float                    barFraction   = 0.0f;
		/// Rank (1 = highest DPS).
		uint32_t                 rank          = 0;
	};

	// =========================================================================
	// Threat meter
	// =========================================================================

	/// Color coding for a threat entry (M39.4).
	enum class ThreatColor : uint8_t
	{
		Green  = 0, ///< Safe — threat < 80 % of highest.
		Yellow = 1, ///< Caution — threat >= 80 % of highest.
		Red    = 2, ///< Pulling aggro — highest threat.
	};

	/// One row in the threat meter (M39.4).
	struct ThreatMeterEntry
	{
		engine::server::EntityId entityId      = 0;
		std::string              displayName;
		uint64_t                 threatValue   = 0;
		/// [0.0, 100.0] — threat as percentage of the highest-threat player.
		float                    threatPercent = 0.0f;
		ThreatColor              color         = ThreatColor::Green;
		/// [0.0, 1.0] — normalised bar width (1.0 = max threat holder).
		float                    barFraction   = 0.0f;
	};

	// =========================================================================
	// Combat log
	// =========================================================================

	/// Type of a single combat log line (M39.4).
	enum class CombatLogLineType : uint8_t
	{
		Damage  = 0,
		Healing = 1,
		Buff    = 2,
		Debuff  = 3,
		Death   = 4,
		Info    = 5,
	};

	/// Filter bitmask controlling which line types appear in the visible window (M39.4).
	enum CombatLogFilter : uint32_t
	{
		CombatLogFilterNone    = 0u,
		CombatLogFilterDamage  = 1u << 0,
		CombatLogFilterHealing = 1u << 1,
		CombatLogFilterBuffs   = 1u << 2,
		CombatLogFilterDeaths  = 1u << 4,
		CombatLogFilterAll     = 0xFFFF'FFFFu,
	};

	/// One line in the combat log ring buffer (M39.4).
	struct CombatLogLine
	{
		/// Formatted text, e.g. "[00:01:23] Player hits Target for 150".
		std::string         text;
		CombatLogLineType   type          = CombatLogLineType::Info;
		/// Wall-clock time when the event was received (seconds since Init()).
		float               elapsedSec    = 0.0f;
	};

	// =========================================================================
	// Aggregated presenter state
	// =========================================================================

	/// Maximum number of entries shown in the DPS bar chart (M39.4).
	inline constexpr size_t kDpsMeterMaxRows = 5u;
	/// Ring buffer capacity for the combat log (M39.4).
	inline constexpr size_t kCombatLogCapacity = 500u;
	/// Maximum visible lines shown at once in the scrollable window (M39.4).
	inline constexpr size_t kCombatLogVisibleLines = 20u;

	/// Fully resolved advanced combat UI state ready for a UI renderer (M39.4).
	struct AdvancedCombatState
	{
		// ---- DPS meter ----
		bool                      dpsMeterVisible   = false;
		bool                      inCombat          = false;
		float                     fightElapsedSec   = 0.0f;
		std::vector<DpsMeterEntry> dpsMeter;       ///< Up to kDpsMeterMaxRows entries.

		// ---- Threat meter ----
		bool                         threatMeterVisible = false;
		engine::server::EntityId     threatTargetId     = 0;
		std::vector<ThreatMeterEntry> threatMeter;     ///< All tracked entities.

		// ---- Combat log ----
		bool                       combatLogVisible  = false;
		uint32_t                   activeFilter      = CombatLogFilterAll;
		/// Currently visible log lines after scroll offset and filter are applied.
		std::vector<CombatLogLine> visibleLogLines;
		/// Total lines stored in the ring buffer (≤ kCombatLogCapacity).
		size_t                     totalLogLines     = 0;
		/// First visible line index into the (filtered) list (scroll position).
		size_t                     scrollOffset      = 0;

		// ---- Debug ----
		std::string debugText;
	};

	// =========================================================================
	// AdvancedCombatPresenter
	// =========================================================================

	/// CPU-side presenter for DPS meter, threat meter and combat log (M39.4).
	///
	/// Consumes CombatEvent data from the UIModel and external threat updates,
	/// maintains a ring buffer for the combat log, and exposes aggregated
	/// AdvancedCombatState for any rendering layer.
	///
	/// Usage per frame:
	///   1. Call Tick(deltaSeconds) to advance the fight timer.
	///   2. Call ApplyModel(model, mask) when UIModelChangeCombat fires.
	///   3. Feed threat data with UpdateThreat() whenever the server emits it.
	///   4. Read GetState() for rendering.
	class AdvancedCombatPresenter final
	{
	public:
		AdvancedCombatPresenter()  = default;
		~AdvancedCombatPresenter();

		AdvancedCombatPresenter(const AdvancedCombatPresenter&)            = delete;
		AdvancedCombatPresenter& operator=(const AdvancedCombatPresenter&) = delete;

		// ---- Lifecycle ----

		/// Initialise the presenter.  Safe to call repeatedly (re-initialises).
		bool Init();

		/// Tear down and release all state.
		void Shutdown();

		// ---- Per-frame update ----

		/// Advance fight timer and recompute DPS values.
		/// Should be called once per frame while the presenter is active.
		void Tick(float deltaSeconds);

		/// Consume combat events from the UI model and update DPS + combat log.
		/// Triggers when \p changeMask includes UIModelChangeCombat.
		void ApplyModel(const UIModel& model, uint32_t changeMask);

		// ---- Threat feed ----

		/// Inject a threat update broadcast from the server (M39.4 step 2).
		/// \p targetId      Entity being targeted (the boss / mob).
		/// \p playerId      Player whose threat value changed.
		/// \p displayName   Human-readable name for the player (may be empty).
		/// \p threatValue   Raw threat value from the server.
		void UpdateThreat(engine::server::EntityId targetId,
		                  engine::server::EntityId playerId,
		                  std::string_view         displayName,
		                  uint64_t                 threatValue);

		/// Remove all threat entries for a given target.
		void ClearThreat(engine::server::EntityId targetId = 0);

		// ---- DPS controls ----

		/// Reset DPS tracking (call on combat end / new pull).
		void ResetDps();

		// ---- Combat log controls ----

		/// Set active filter bitmask (any combination of CombatLogFilter flags).
		void SetLogFilter(uint32_t filterMask);

		/// Scroll the combat log by \p deltaLines lines (positive = down).
		void ScrollLog(int32_t deltaLines);

		/// Scroll to the latest entry.
		void ScrollToBottom();

		/// Add an arbitrary informational line to the combat log
		/// (e.g. "Combat started", "Target died").
		void AddInfoLine(std::string_view message);

		// ---- Visibility toggles ----

		void SetDpsMeterVisible(bool visible);
		void SetThreatMeterVisible(bool visible);
		void SetCombatLogVisible(bool visible);

		// ---- Export (optional, M39.4 step 4) ----

		/// Export the full ring buffer to a CSV file at \p filePath.
		/// Returns true on success.
		bool ExportLogToCsv(const std::string& filePath) const;

		// ---- State accessor ----

		const AdvancedCombatState& GetState() const { return m_state; }
		bool IsInitialized() const { return m_initialized; }

	private:
		// ---- DPS helpers ----

		/// Rebuild the public DPS meter list from internal damage accumulators.
		void RebuildDpsMeter();

		/// Return a display name for an entity (falls back to "Entity_<id>").
		static std::string MakeDisplayName(engine::server::EntityId entityId,
		                                   std::string_view         knownName = {});

		// ---- Threat helpers ----

		/// Rebuild the public threat meter list and recalculate percentages.
		void RebuildThreatMeter();

		// ---- Combat log helpers ----

		/// Push one line into the ring buffer and refresh visible lines.
		void PushLogLine(CombatLogLine line);

		/// Refresh m_state.visibleLogLines from the ring buffer using the
		/// current filter and scroll offset.
		void RebuildVisibleLog();

		/// Format a CombatEvent as a log line.
		CombatLogLine FormatCombatEvent(const UICombatLogEntry& entry) const;

		/// Rebuild the human-readable debug dump.
		void RebuildDebugText();

		// ---- State ----

		AdvancedCombatState m_state{};

		// ---- DPS internals ----
		/// Damage totals per entity id.
		std::unordered_map<engine::server::EntityId, uint64_t> m_damageMap;
		/// Sequence number of the last processed combat entry.
		uint64_t m_lastCombatSeq = 0;

		// ---- Threat internals ----
		/// Per-target, per-player threat values.
		/// Outer key = targetId, inner key = playerId.
		std::unordered_map<engine::server::EntityId,
		    std::unordered_map<engine::server::EntityId, ThreatMeterEntry>> m_threatMap;

		// ---- Combat log internals ----
		/// Ring buffer holding up to kCombatLogCapacity lines (all types).
		std::deque<CombatLogLine> m_logBuffer;
		/// Wall-clock seconds since Init() (used for log timestamps).
		float m_wallClockSec = 0.0f;

		bool m_initialized = false;
	};

} // namespace engine::client
