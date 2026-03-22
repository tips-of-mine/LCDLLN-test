#include "engine/gameplay/ClientPrediction.h"

#include "engine/core/Log.h"

#include <cmath>

namespace engine::gameplay
{

// ---------------------------------------------------------------------------
// Internal constants
// ---------------------------------------------------------------------------

static constexpr float kMinDt          = 1e-6f;
static constexpr float kMaxAccumulator = 0.2f; // clamp spiral-of-death (200 ms)

// ---------------------------------------------------------------------------
// Constructors / Destructor
// ---------------------------------------------------------------------------

ClientPredictionSystem::ClientPredictionSystem()
{
	LOG_INFO(Core, "[ClientPrediction] Default ctor (fixedDt={} networkHz={})",
	         m_cfg.fixedDt, m_cfg.networkTickHz);
}

ClientPredictionSystem::ClientPredictionSystem(const Config& cfg)
    : m_cfg(cfg)
{
	LOG_INFO(Core,
	         "[ClientPrediction] Ctor OK (fixedDt={} networkHz={} walkSpeed={} runSpeed={}"
	         " reconcileThreshold={} smoothDur={})",
	         m_cfg.fixedDt, m_cfg.networkTickHz,
	         m_cfg.walkSpeed, m_cfg.runSpeed,
	         m_cfg.reconciliationThreshold, m_cfg.smoothDurationSec);
}

ClientPredictionSystem::~ClientPredictionSystem()
{
	Shutdown();
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

bool ClientPredictionSystem::Init(const engine::math::Vec3& startPosition)
{
	if (m_initialized)
	{
		LOG_WARN(Core, "[ClientPrediction] Init called while already initialized — ignored");
		return false;
	}

	if (m_cfg.fixedDt <= kMinDt)
	{
		LOG_ERROR(Core, "[ClientPrediction] Init FAILED: invalid fixedDt ({})", m_cfg.fixedDt);
		return false;
	}

	if (m_cfg.networkTickHz <= 0.0f)
	{
		LOG_ERROR(Core, "[ClientPrediction] Init FAILED: invalid networkTickHz ({})",
		          m_cfg.networkTickHz);
		return false;
	}

	m_state          = PredictedState{};
	m_state.position = startPosition;
	m_currentTick    = 0;
	m_accumulator    = 0.0f;
	m_netAccumulator = 0.0f;
	m_tickHistory.clear();
	m_pendingSend.clear();

	// Smooth correction initialisation (M30.2).
	m_displayPosition = startPosition;
	m_smoothTarget    = startPosition;
	m_smoothTimer     = 0.0f;
	m_correcting      = false;

	m_initialized = true;

	LOG_INFO(Core,
	         "[ClientPrediction] Init OK (startPos={:.2f},{:.2f},{:.2f} fixedDt={} networkHz={})",
	         startPosition.x, startPosition.y, startPosition.z,
	         m_cfg.fixedDt, m_cfg.networkTickHz);
	return true;
}

void ClientPredictionSystem::Shutdown()
{
	if (!m_initialized)
		return;

	const size_t historyCount = m_tickHistory.size();
	m_tickHistory.clear();
	m_pendingSend.clear();
	m_correcting  = false;
	m_initialized = false;

	LOG_INFO(Core, "[ClientPrediction] Shutdown (lastTick={} historyPurged={})",
	         m_currentTick, historyCount);
}

// ---------------------------------------------------------------------------
// Update  (M30.1 — fixed-step simulation + network batch send)
// ---------------------------------------------------------------------------

void ClientPredictionSystem::Update(float dt,
                                    const engine::platform::Input& input,
                                    float yawRadians,
                                    const SendInputsBatchFn& sendFn)
{
	if (!m_initialized)
	{
		LOG_WARN(Core, "[ClientPrediction] Update called before Init — ignored");
		return;
	}

	if (dt <= kMinDt)
	{
		LOG_WARN(Core, "[ClientPrediction] Update: dt too small ({}) — skipped", dt);
		return;
	}

	// Clamp accumulator to avoid spiral-of-death on large frame spikes.
	m_accumulator += dt;
	if (m_accumulator > kMaxAccumulator)
	{
		LOG_WARN(Core, "[ClientPrediction] Accumulator clamped ({:.4f}s > max {:.2f}s)",
		         m_accumulator, kMaxAccumulator);
		m_accumulator = kMaxAccumulator;
	}

	// --- Fixed-step simulation ticks ------------------------------------
	while (m_accumulator >= m_cfg.fixedDt)
	{
		m_accumulator -= m_cfg.fixedDt;

		// 1. Build input command for this tick.
		InputCommand cmd;
		cmd.tick        = m_currentTick;
		cmd.keys        = SampleKeys(input);
		cmd.mouseDeltaX = static_cast<float>(input.MouseDeltaX());
		cmd.mouseDeltaY = static_cast<float>(input.MouseDeltaY());
		cmd.dt          = m_cfg.fixedDt;
		cmd.yawRadians  = yawRadians; // stored for deterministic replay (M30.2)

		// 2. Apply movement locally (optimistic update).
		ApplyCommand(cmd);

		// 3. Record command + resulting state in the unacknowledged history (M30.2).
		if (m_tickHistory.size() >= static_cast<size_t>(m_cfg.maxBufferSize))
		{
			LOG_WARN(Core,
			         "[ClientPrediction] History buffer full (cap={}) — dropping oldest (tick={})",
			         m_cfg.maxBufferSize, m_tickHistory.front().cmd.tick);
			m_tickHistory.pop_front();
		}
		m_tickHistory.push_back(TickRecord{ cmd, m_state });

		// 4. Accumulate for network batch.
		m_pendingSend.push_back(cmd);

		LOG_TRACE(Core,
		          "[ClientPrediction] Tick {} keys=0x{:02X} pos=({:.2f},{:.2f},{:.2f})",
		          m_currentTick,
		          static_cast<uint8_t>(cmd.keys),
		          m_state.position.x, m_state.position.y, m_state.position.z);

		++m_currentTick;
	}

	// --- Network send at configured Hz ----------------------------------
	const float netInterval  = 1.0f / m_cfg.networkTickHz;
	m_netAccumulator        += dt;

	if (m_netAccumulator >= netInterval && !m_pendingSend.empty())
	{
		m_netAccumulator -= netInterval;

		LOG_DEBUG(Core,
		          "[ClientPrediction] Sending batch (count={} firstTick={} lastTick={})",
		          m_pendingSend.size(),
		          m_pendingSend.front().tick,
		          m_pendingSend.back().tick);

		if (sendFn)
			sendFn(m_pendingSend);

		m_pendingSend.clear();
	}
}

// ---------------------------------------------------------------------------
// OnServerSnapshot  (M30.2 — reconciliation)
// ---------------------------------------------------------------------------

void ClientPredictionSystem::OnServerSnapshot(const ServerSnapshot& snap)
{
	if (!m_initialized)
	{
		LOG_WARN(Core, "[ClientReconcile] OnServerSnapshot called before Init — ignored");
		return;
	}

	// 1. Locate the predicted state at snap.serverTick in the history buffer.
	const TickRecord* matchedRecord = nullptr;
	for (const TickRecord& r : m_tickHistory)
	{
		if (r.cmd.tick == snap.serverTick)
		{
			matchedRecord = &r;
			break;
		}
	}

	if (!matchedRecord)
	{
		// Tick already purged (too old) or not yet reached (snapshot from the future).
		LOG_WARN(Core,
		         "[ClientReconcile] Snapshot tick={} not found in history (oldest={} newest={})"
		         " — skipped",
		         snap.serverTick,
		         m_tickHistory.empty() ? 0u : m_tickHistory.front().cmd.tick,
		         m_tickHistory.empty() ? 0u : m_tickHistory.back().cmd.tick);
		return;
	}

	// 2. Compute positional error between predicted and authoritative state.
	const engine::math::Vec3 predicted = matchedRecord->stateAfter.position;
	const float dx    = snap.position.x - predicted.x;
	const float dz    = snap.position.z - predicted.z;
	const float delta = std::sqrt(dx * dx + dz * dz);

	LOG_DEBUG(Core,
	          "[ClientReconcile] Snapshot tick={} delta={:.3f}m (predicted=({:.2f},{:.2f},{:.2f})"
	          " server=({:.2f},{:.2f},{:.2f}))",
	          snap.serverTick, delta,
	          predicted.x, predicted.y, predicted.z,
	          snap.position.x, snap.position.y, snap.position.z);

	// 3. Purge history for ticks <= snap.serverTick.
	while (!m_tickHistory.empty() && m_tickHistory.front().cmd.tick <= snap.serverTick)
		m_tickHistory.pop_front();

	// 4. If delta is below threshold: no reconciliation needed.
	if (delta <= m_cfg.reconciliationThreshold)
	{
		LOG_TRACE(Core,
		          "[ClientReconcile] delta={:.3f}m below threshold={:.2f}m — no correction",
		          delta, m_cfg.reconciliationThreshold);
		return;
	}

	// 5. Reconciliation: rewind to server-authoritative state and replay unacknowledged inputs.
	LOG_INFO(Core,
	         "[ClientReconcile] Reconciling: delta={:.3f}m > threshold={:.2f}m"
	         " (serverTick={} replayCount={})",
	         delta, m_cfg.reconciliationThreshold,
	         snap.serverTick, m_tickHistory.size());

	m_state.position = snap.position;
	m_state.velocity = snap.velocity;

	// Replay all remaining commands (tick > snap.serverTick) using stored yaw.
	for (TickRecord& r : m_tickHistory)
	{
		ApplyCommand(r.cmd);
		r.stateAfter = m_state; // update stored state after replay
	}

	// 6. Initiate smooth correction lerp from current display position to corrected position.
	m_smoothTarget = m_state.position;
	m_smoothTimer  = 0.0f;
	m_correcting   = true;

	LOG_INFO(Core,
	         "[ClientReconcile] Replay done. Corrected pos=({:.2f},{:.2f},{:.2f})"
	         " smoothDur={:.2f}s",
	         m_state.position.x, m_state.position.y, m_state.position.z,
	         m_cfg.smoothDurationSec);
}

// ---------------------------------------------------------------------------
// UpdateSmoothing  (M30.2 — display position lerp)
// ---------------------------------------------------------------------------

void ClientPredictionSystem::UpdateSmoothing(float dt)
{
	if (!m_initialized)
		return;

	if (!m_correcting)
	{
		// Follow predicted position directly when no correction is in progress.
		m_displayPosition = m_state.position;
		return;
	}

	// Advance lerp timer.
	m_smoothTimer += dt;

	if (m_smoothTimer >= m_cfg.smoothDurationSec)
	{
		// Lerp complete: snap to target and stop correcting.
		m_displayPosition = m_smoothTarget;
		m_correcting      = false;

		LOG_DEBUG(Core,
		          "[ClientReconcile] Smooth correction complete. displayPos=({:.2f},{:.2f},{:.2f})",
		          m_displayPosition.x, m_displayPosition.y, m_displayPosition.z);
	}
	else
	{
		// Linear interpolation from current display position toward the smooth target.
		const float t = m_smoothTimer / m_cfg.smoothDurationSec;

		m_displayPosition.x = m_displayPosition.x + t * (m_smoothTarget.x - m_displayPosition.x);
		m_displayPosition.y = m_displayPosition.y + t * (m_smoothTarget.y - m_displayPosition.y);
		m_displayPosition.z = m_displayPosition.z + t * (m_smoothTarget.z - m_displayPosition.z);
	}
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

MovementKeyFlags ClientPredictionSystem::SampleKeys(const engine::platform::Input& input)
{
	MovementKeyFlags flags = MovementKeyFlags::None;

	using Key = engine::platform::Key;

	if (input.IsDown(Key::W) || input.IsDown(Key::Up))
		flags = flags | MovementKeyFlags::Forward;

	if (input.IsDown(Key::S) || input.IsDown(Key::Down))
		flags = flags | MovementKeyFlags::Backward;

	if (input.IsDown(Key::A) || input.IsDown(Key::Left))
		flags = flags | MovementKeyFlags::Left;

	if (input.IsDown(Key::D) || input.IsDown(Key::Right))
		flags = flags | MovementKeyFlags::Right;

	if (input.IsDown(Key::Space))
		flags = flags | MovementKeyFlags::Jump;

	if (input.IsDown(Key::Shift))
		flags = flags | MovementKeyFlags::Run;

	return flags;
}

void ClientPredictionSystem::ApplyCommand(const InputCommand& cmd)
{
	// Build horizontal wish direction in XZ plane from key flags and camera yaw.
	// Forward in camera space maps to (-sin(yaw), 0, -cos(yaw)) in world space.
	const float sinYaw = std::sin(cmd.yawRadians);
	const float cosYaw = std::cos(cmd.yawRadians);

	float wishX = 0.0f;
	float wishZ = 0.0f;

	if (HasFlag(cmd.keys, MovementKeyFlags::Forward))
	{
		wishX += -sinYaw;
		wishZ += -cosYaw;
	}
	if (HasFlag(cmd.keys, MovementKeyFlags::Backward))
	{
		wishX += sinYaw;
		wishZ += cosYaw;
	}
	if (HasFlag(cmd.keys, MovementKeyFlags::Left))
	{
		wishX += -cosYaw;
		wishZ += sinYaw;
	}
	if (HasFlag(cmd.keys, MovementKeyFlags::Right))
	{
		wishX += cosYaw;
		wishZ += -sinYaw;
	}

	// Normalise horizontal wish direction.
	const float lenSq = wishX * wishX + wishZ * wishZ;
	if (lenSq > 1e-6f)
	{
		const float inv = 1.0f / std::sqrt(lenSq);
		wishX *= inv;
		wishZ *= inv;
	}

	// Choose target horizontal speed.
	const float speed = HasFlag(cmd.keys, MovementKeyFlags::Run)
	                    ? m_cfg.runSpeed
	                    : m_cfg.walkSpeed;

	// Instant velocity set (simple kinematic model; matches authoritative server).
	m_state.velocity.x = wishX * speed;
	m_state.velocity.z = wishZ * speed;

	// Integrate position.
	m_state.position.x += m_state.velocity.x * cmd.dt;
	m_state.position.z += m_state.velocity.z * cmd.dt;
	// Y-axis (vertical) is the responsibility of CharacterController; only XZ is predicted here.
}

} // namespace engine::gameplay
