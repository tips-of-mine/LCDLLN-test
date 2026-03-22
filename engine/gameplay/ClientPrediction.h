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

	/// Bitwise AND test between two MovementKeyFlags values; returns true if any bit matches.
	inline bool HasFlag(MovementKeyFlags value, MovementKeyFlags flag)
	{
		return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
	}

	/// One buffered input command with tick number, key state and mouse delta (M30.1).
	/// Sent to the server in batches and used for client-side reconciliation.
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
	};

	/// Predicted character state produced by the client-side simulation (M30.1).
	struct PredictedState
	{
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 velocity{ 0.0f, 0.0f, 0.0f };
	};

	/// Callback invoked by ClientPredictionSystem when a batch of inputs must be sent to the server.
	using SendInputsBatchFn = std::function<void(const std::vector<InputCommand>&)>;

	/// Client-side prediction system for movement (M30.1).
	///
	/// Responsibilities:
	///  - Sample movement keys and mouse delta once per fixed tick.
	///  - Immediately apply the movement locally (optimistic update).
	///  - Buffer every InputCommand with its tick number.
	///  - Periodically (at ~20-30 Hz) invoke the provided callback with the
	///    accumulated batch so the caller can serialise and send it to the server.
	///
	/// Notes:
	///  - Uses a fixed timestep; the same dt must be used on the authoritative server.
	///  - Mouse delta is passed through unchanged; the caller is responsible for yaw
	///    integration and camera update.
	///  - The input buffer is capped at maxBufferSize entries; oldest entries are
	///    dropped when the cap is exceeded (should not happen under normal latency).
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
		};

		ClientPredictionSystem();
		explicit ClientPredictionSystem(const Config& cfg);
		~ClientPredictionSystem();

		/// Initialise the system with the character's starting world-space position.
		/// Must be called before Update().
		/// \return true on success; false if already initialised or position is invalid.
		bool Init(const engine::math::Vec3& startPosition);

		/// Release resources and reset state. Safe to call multiple times.
		void Shutdown();

		/// Advance the simulation by one application frame.
		///
		/// Internally:
		///  1. Accumulates \p dt and runs fixed-step ticks.
		///  2. Each tick: samples input, applies movement locally, stores InputCommand.
		///  3. Every 1/networkTickHz seconds: calls \p sendFn with buffered commands.
		///
		/// \param dt          Frame delta time (seconds); must be > 0.
		/// \param input       Read-only input state for the current frame.
		/// \param yawRadians  Camera yaw used to orient movement direction.
		/// \param sendFn      Callback to send the accumulated input batch.
		void Update(float dt,
		            const engine::platform::Input& input,
		            float yawRadians,
		            const SendInputsBatchFn& sendFn);

		/// World-space position predicted by the local simulation.
		engine::math::Vec3 GetPredictedPosition() const { return m_state.position; }

		/// Velocity predicted by the local simulation.
		engine::math::Vec3 GetPredictedVelocity() const { return m_state.velocity; }

		/// Current client tick counter (incremented once per fixed tick).
		uint32_t GetCurrentTick() const { return m_currentTick; }

		/// Number of unacknowledged InputCommands currently in the buffer.
		size_t GetBufferSize() const { return m_inputBuffer.size(); }

	private:
		/// Build a MovementKeyFlags snapshot from the current Input state.
		static MovementKeyFlags SampleKeys(const engine::platform::Input& input);

		/// Apply one InputCommand to the predicted state using simple kinematic integration.
		void ApplyCommand(const InputCommand& cmd, float yawRadians);

	private:
		Config         m_cfg{};
		PredictedState m_state{};
		uint32_t       m_currentTick    = 0;
		float          m_accumulator    = 0.0f;   ///< Fixed-step leftover accumulator.
		float          m_netAccumulator = 0.0f;   ///< Network send interval accumulator.
		bool           m_initialized    = false;

		/// Unacknowledged input commands (oldest first).
		/// Kept until the server acknowledges them (reconciliation, M30.2+).
		std::deque<InputCommand> m_inputBuffer;

		/// Commands accumulated since the last network send call.
		std::vector<InputCommand> m_pendingSend;
	};

} // namespace engine::gameplay
