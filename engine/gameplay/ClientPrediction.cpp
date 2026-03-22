#include "engine/gameplay/ClientPrediction.h"

#include "engine/core/Log.h"

#include <cmath>

namespace engine::gameplay
{

// ---------------------------------------------------------------------------
// Helpers
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
	         "[ClientPrediction] Ctor OK (fixedDt={} networkHz={} walkSpeed={} runSpeed={})",
	         m_cfg.fixedDt, m_cfg.networkTickHz, m_cfg.walkSpeed, m_cfg.runSpeed);
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

	m_state        = PredictedState{};
	m_state.position = startPosition;
	m_currentTick    = 0;
	m_accumulator    = 0.0f;
	m_netAccumulator = 0.0f;
	m_inputBuffer.clear();
	m_pendingSend.clear();
	m_initialized = true;

	LOG_INFO(Core,
	         "[ClientPrediction] Init OK (startPos={},{},{} fixedDt={} networkHz={})",
	         startPosition.x, startPosition.y, startPosition.z,
	         m_cfg.fixedDt, m_cfg.networkTickHz);
	return true;
}

void ClientPredictionSystem::Shutdown()
{
	if (!m_initialized)
		return;

	m_inputBuffer.clear();
	m_pendingSend.clear();
	m_initialized = false;

	LOG_INFO(Core, "[ClientPrediction] Shutdown (tick={} bufferCleared)", m_currentTick);
}

// ---------------------------------------------------------------------------
// Update
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
		LOG_WARN(Core, "[ClientPrediction] Accumulator clamped ({:.4f}s > max {}s)",
		         m_accumulator, kMaxAccumulator);
		m_accumulator = kMaxAccumulator;
	}

	// --- Fixed-step simulation ticks ------------------------------------
	while (m_accumulator >= m_cfg.fixedDt)
	{
		m_accumulator -= m_cfg.fixedDt;

		// 1. Sample input state for this tick.
		InputCommand cmd;
		cmd.tick       = m_currentTick;
		cmd.keys       = SampleKeys(input);
		cmd.mouseDeltaX = static_cast<float>(input.MouseDeltaX());
		cmd.mouseDeltaY = static_cast<float>(input.MouseDeltaY());
		cmd.dt         = m_cfg.fixedDt;

		// 2. Apply movement locally (optimistic update).
		ApplyCommand(cmd, yawRadians);

		// 3. Store in unacknowledged buffer (for future reconciliation).
		if (m_inputBuffer.size() >= static_cast<size_t>(m_cfg.maxBufferSize))
		{
			LOG_WARN(Core,
			         "[ClientPrediction] Input buffer full (cap={}) — dropping oldest (tick={})",
			         m_cfg.maxBufferSize, m_inputBuffer.front().tick);
			m_inputBuffer.pop_front();
		}
		m_inputBuffer.push_back(cmd);

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
	const float netInterval = 1.0f / m_cfg.networkTickHz;
	m_netAccumulator += dt;

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

void ClientPredictionSystem::ApplyCommand(const InputCommand& cmd, float yawRadians)
{
	// Build horizontal wish direction in XZ plane from key flags and camera yaw.
	// Forward in camera space maps to (-sin(yaw), 0, -cos(yaw)) in world space.
	const float sinYaw = std::sin(yawRadians);
	const float cosYaw = std::cos(yawRadians);

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
	// Y-axis (vertical) is not handled here; that is the responsibility of the
	// CharacterController (gravity, jumping). Only XZ prediction is in scope for M30.1.
}

} // namespace engine::gameplay
