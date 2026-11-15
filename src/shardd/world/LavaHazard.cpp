// Seconde carte (Les Enfers) — lave mortelle (voir LavaHazard.h).
#include "src/shardd/world/LavaHazard.h"

#include <utility>

namespace engine::server
{
	void LavaHazard::AddMask(engine::world::LavaMask mask)
	{
		if (!mask.Empty())
			m_masks.push_back(std::move(mask));
	}

	bool LavaHazard::InLava(double x, double y, double z) const
	{
		for (const engine::world::LavaMask& m : m_masks)
		{
			float lava = 0.0f, ground = 0.0f;
			if (!m.Sample(x, z, lava, ground))
				continue;
			// Les grilles des cellules ne se recouvrent pas : le premier masque qui couvre
			// le point décide.
			return lava >= kLavaThreshold && y <= static_cast<double>(ground) + kMaxAboveGroundM;
		}
		return false;
	}

	LavaEvent LavaHazard::Update(LavaExposureState& state, bool onEnfers, bool alive, double x, double y, double z,
		float dtSeconds) const
	{
		const bool burning = onEnfers && alive && InLava(x, y, z);
		if (!burning)
		{
			const bool was = state.inLava;
			state = LavaExposureState{};
			return was && alive ? LavaEvent::Left : LavaEvent::None;
		}
		const bool entered = !state.inLava;
		state.inLava = true;
		state.seconds += dtSeconds > 0.0f ? dtSeconds : 0.0f;
		if (state.seconds >= kDeathSeconds)
		{
			state = LavaExposureState{};
			return LavaEvent::Killed;
		}
		return entered ? LavaEvent::Entered : LavaEvent::None;
	}
}
