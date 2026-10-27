// ETAPE4.1 — sommeil et fréquence de traitement des mobs. Voir MobActivity.h.

#include "src/shardd/world/MobActivity.h"
#include "src/shardd/world/TickSlicing.h"

#include <algorithm>
#include <cmath>

namespace engine::server::mob_activity
{
	MobActivityConfig Sanitize(MobActivityConfig config, uint32_t tickHz)
	{
		const uint32_t hz = (std::max)(tickHz, 1u);
		config.combatHz = std::clamp(config.combatHz, 1u, hz);
		config.calmHz = std::clamp(config.calmHz, 1u, config.combatHz);
		config.ambientHz = std::clamp(config.ambientHz, 1u, config.combatHz);
		if (!std::isfinite(config.wakeRadiusMeters))
			config.wakeRadiusMeters = 0.0f;
		config.wakeRadiusMeters = std::clamp(config.wakeRadiusMeters, 0.0f, 2000.0f);
		if (!std::isfinite(config.sleepDelaySeconds))
			config.sleepDelaySeconds = 3.0f;
		config.sleepDelaySeconds = std::clamp(config.sleepDelaySeconds, 0.0f, 60.0f);
		if (!std::isfinite(config.maxCatchUpSeconds))
			config.maxCatchUpSeconds = 1.0f;
		config.maxCatchUpSeconds = std::clamp(config.maxCatchUpSeconds, 0.05f, 5.0f);
		return config;
	}

	MobSimClass Classify(bool ambient, bool busy)
	{
		if (busy)
			return MobSimClass::Combat;
		return ambient ? MobSimClass::Ambient : MobSimClass::Calm;
	}

	bool IsBusy(bool hasAggroTarget, bool hasThreat, bool inAggroOrReturnState)
	{
		return hasAggroTarget || hasThreat || inAggroOrReturnState;
	}

	uint32_t IntervalTicks(MobSimClass simClass, const MobActivityConfig& config, uint32_t tickHz)
	{
		const uint32_t hz = (std::max)(tickHz, 1u);
		uint32_t classHz = config.combatHz;
		switch (simClass)
		{
		case MobSimClass::Combat: classHz = config.combatHz; break;
		case MobSimClass::Calm: classHz = config.calmHz; break;
		case MobSimClass::Ambient: classHz = config.ambientHz; break;
		}
		classHz = std::clamp(classHz, 1u, hz);
		return (std::max)(1u, hz / classHz);
	}

	float CatchUpSeconds(uint32_t lastAiTick, uint32_t nowTick, uint32_t intervalTicks,
		uint32_t tickHz, const MobActivityConfig& config)
	{
		const float hz = static_cast<float>((std::max)(tickHz, 1u));
		const float nominal = static_cast<float>((std::max)(intervalTicks, 1u)) / hz;
		if (lastAiTick == 0u || nowTick <= lastAiTick)
			return (std::min)(nominal, config.maxCatchUpSeconds);
		const float elapsed = static_cast<float>(nowTick - lastAiTick) / hz;
		return std::clamp(elapsed, (std::min)(nominal, config.maxCatchUpSeconds), config.maxCatchUpSeconds);
	}

	SleepTransition UpdateSleep(SleepState& state, bool playerNear, bool busy, uint32_t nowTick,
		uint32_t spawnTick, uint32_t tickHz, const MobActivityConfig& config)
	{
		if (playerNear)
			state.lastPlayerNearTick = nowTick;
		if (busy || playerNear || config.wakeRadiusMeters <= 0.0f)
		{
			if (state.asleep)
			{
				state.asleep = false;
				return SleepTransition::WokeUp;
			}
			return SleepTransition::None;
		}
		if (state.asleep)
			return SleepTransition::None;
		const uint32_t since = state.lastPlayerNearTick != 0u ? state.lastPlayerNearTick : spawnTick;
		const uint32_t delayTicks = static_cast<uint32_t>(
			std::lround(config.sleepDelaySeconds * static_cast<float>((std::max)(tickHz, 1u))));
		if (nowTick >= since && nowTick - since >= delayTicks)
		{
			state.asleep = true;
			return SleepTransition::FellAsleep;
		}
		return SleepTransition::None;
	}

	bool WakeIfBusy(SleepState& state, bool busy)
	{
		if (!busy || !state.asleep)
			return false;
		state.asleep = false;
		return true;
	}

	bool IsWakeCheckTick(uint64_t entityId, uint32_t nowTick, uint32_t tickHz)
	{
		// ETAPE4.2 — même étalement que les autres travaux périodiques du shard.
		return tick_slicing::IsSliceTurn(entityId, nowTick, (std::max)(tickHz, 1u));
	}
}
