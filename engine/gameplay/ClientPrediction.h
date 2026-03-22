#pragma once

#include "engine/core/Log.h"
#include "engine/math/Math.h"
#include "engine/platform/Input.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace engine::gameplay
{
	/// Bitmask of movement keys captured per tick (M30.1).
	enum class MovementKeyFlags : uint8_t
	{
		None     = 0,
		Forward  = 1 << 0,
		Backward = 1 << 1,
		Left     = 1 << 2,
		Right    = 1 << 3,
		Jump     = 1 << 4,
		Run      = 1 << 5,
	};

	/// Bitwise OR between two MovementKeyFlags values.
	inline MovementKeyFlags operator|(MovementKeyFlags a, MovementKeyFlags b)
	{
		return static_cast<MovementKeyFlags>(
			static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	/// Bitwise AND test; returns true if any bit of flag is set in value.
	inline bool HasFlag(MovementKeyFlags value, MovementKeyFlags flag)
	{
		return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
	}

	/// One buffered input command with tick number, key state and mouse delta (M30.1).
	/// Sent to the server in batches and stored for client-side reconciliation (M30.2).
	struct InputCommand
	{
		/// Client tick at which this input was sampled.
		uint32_t tick = 0;

		/// Bitmask of held movement keys at sampling time.
		MovementKeyFlags keys = MovementKeyFlags::None;

		/// Mouse horizontal delta in pixels since last frame.
		float mouseDeltaX = 0.0f;

		/// Mouse vertical delta in pixels since last frame.
		float mouseDeltaY = 0.0f;

		/// Fixed timestep used when simulating this command (seconds).
		float dt = 0.0f;

		/// Camera yaw at simulation time (radians). Stored for deterministic replay (M30.2).
		float yawRadians = 0.0f;
	};

	/// Predicted character state produced by the client-side simulation (M30.1).
	struct PredictedState
	{
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 velocity{ 0.0f, 0.0f, 0.0f };
	};

	/// Server-authoritative snapshot used for reconciliation (M30.2).
	struct ServerSnapshot
	{
		/// Server tick at which this state is authoritative.
		uint32_t serverTick = 0;

		/// Authoritative world-space position.
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };

		/// Authoritative velocity.
		engine::math::Vec3 velocity{ 0.0f, 0.0f, 0.0f };
	};

	/// Callback invoked by ClientPredictionSystem when a batch of inputs must be sent to the server.
	using SendInputsBatchFn = std::function<void(const std::vector<InputCommand>&)>;

	/// Client-side prediction system for movement (M30.1) with server reconciliation (M30.2).
	///
	/// M30.1 responsibilities:
	///  - Sample movement keys and mouse delta once per fixed tick.
	///  - Immediately apply the movement locally (optimistic update).
	///  - Buffer every InputCommand with its tick number.
	///  - Periodically (~20-30 Hz) invoke the send callback with the accumulated batch.
	///
	/// M30.2 responsibilities:
	///  - On server snapshot: compare authoritative position with buffered predicted state.
	///  - If delta > 0.5 m: rewind to server state and replay all unacknowledged inputs.
	///  - Lerp the display position toward the corrected position over ~150 ms.
	///  - Purge input buffer for ticks <= server tick.
	///
	/// Notes:
	///  - Uses a fixed timestep; the same dt must be used on the authoritative server.
	///  - GetDisplayPosition() should be used for rendering; GetPredictedPosition() exposes
	///    the raw simulation state.
	class ClientPredictionSystem final
	{
	public:
		/// Configuration for the prediction system.
		struct Config
		{
			/// Fixed simulation timestep in seconds (must match server).
			float fixedDt = 1.0f / 60.0f;

			/// Rate at which accumulated inputs are sent to the server (Hz).
			float networkTickHz = 20.0f;

			/// Horizontal walk speed (m/s).
			float walkSpeed = 5.0f;

			/// Horizontal run speed (m/s).
			float runSpeed = 9.0f;

			/// Maximum number of unacknowledged commands kept in the buffer.
			/// Oldest entries are discarded if the cap is reached.
			uint32_t maxBufferSize = 128;

			/// Position error threshold above which reconciliation is triggered (metres).
			float reconciliationThreshold = 0.5f;

			/// Duration of the smooth correction lerp in seconds (M30.2).
			float smoothDurationSec = 0.15f;
		};

		ClientPredictionSystem();
		explicit ClientPredictionSystem(const Config& cfg);
		~ClientPredictionSystem();

		/// Initialise the system with the character's starting world-space position.
		/// Must be called before Update() and OnServerSnapshot().
		/// \return true on success; false if already initialised or configuration invalid.
		bool Init(const engine::math::Vec3& startPosition);

		/// Release resources and reset state. Safe to call multiple times.
		void Shutdown();

		/// Advance the simulation by one application frame (M30.1).
		///
		/// Internally:
		///  1. Accumulates dt and runs fixed-step ticks.
		///  2. Each tick: samples input, applies movement locally, stores TickRecord.
		///  3. Every 1/networkTickHz seconds: calls sendFn with buffered commands.
		///
		/// \param dt          Frame delta time (seconds); must be > 0.
		/// \param input       Read-only input state for the current frame.
		/// \param yawRadians  Camera yaw used to orient movement direction.
		/// \param sendFn      Callback to send the accumulated input batch.
		void Update(float dt,
		            const engine::platform::Input& input,
		            float yawRadians,
		            const SendInputsBatchFn& sendFn);

		/// Process one server-authoritative snapshot for reconciliation (M30.2).
		///
		/// Steps:
		///  1. Locate the predicted state at snap.serverTick in the history buffer.
		///  2. Compute delta between predicted and authoritative positions.
		///  3. Purge history for tick <= snap.serverTick.
		///  4. If delta > reconciliationThreshold: rewind to server state, replay remaining
		///     inputs, start smooth correction lerp toward the reconciled position.
		void OnServerSnapshot(const ServerSnapshot& snap);

		/// Advance the smooth correction lerp (M30.2). Call once per frame after Update().
		/// \param dt Frame delta time (seconds).
		void UpdateSmoothing(float dt);

		/// World-space position predicted by the local simulation (raw, no smoothing).
		engine::math::Vec3 GetPredictedPosition() const { return m_state.position; }

		/// Velocity predicted by the local simulation.
		engine::math::Vec3 GetPredictedVelocity() const { return m_state.velocity; }

		/// Smoothly interpolated display position for rendering (M30.2).
		/// Equals GetPredictedPosition() when no correction is in progress.
		engine::math::Vec3 GetDisplayPosition() const { return m_displayPosition; }

		/// Current client tick counter (incremented once per fixed tick).
		uint32_t GetCurrentTick() const { return m_currentTick; }

		/// Number of unacknowledged InputCommands currently in the buffer.
		size_t GetBufferSize() const { return m_tickHistory.size(); }

	private:
		/// Per-tick record stored in the unacknowledged history buffer.
		/// Combines the command that drove the tick with the resulting state (M30.2).
		struct TickRecord
		{
			InputCommand   cmd;        ///< Command applied during this tick (includes yaw).
			PredictedState stateAfter; ///< Predicted state after applying cmd.
		};

		/// Build a MovementKeyFlags snapshot from the current Input state.
		static MovementKeyFlags SampleKeys(const engine::platform::Input& input);

		/// Apply one InputCommand to m_state using simple kinematic integration.
		/// cmd.yawRadians is used to orient the movement direction.
		void ApplyCommand(const InputCommand& cmd);

	private:
		Config         m_cfg{};
		PredictedState m_state{};
		uint32_t       m_currentTick    = 0;
		float          m_accumulator    = 0.0f;   ///< Fixed-step leftover accumulator.
		float          m_netAccumulator = 0.0f;   ///< Network send interval accumulator.
		bool           m_initialized    = false;

		/// Unacknowledged tick records (oldest first), used for comparison and replay.
		std::deque<TickRecord> m_tickHistory;

		/// Commands accumulated since the last network send call.
		std::vector<InputCommand> m_pendingSend;

		// --- Smooth correction state (M30.2) ---
		engine::math::Vec3 m_displayPosition{};  ///< Interpolated position for rendering.
		engine::math::Vec3 m_smoothTarget{};      ///< Target of the current correction lerp.
		float              m_smoothTimer    = 0.0f;
		bool               m_correcting     = false;
	};

} // namespace engine::gameplay
