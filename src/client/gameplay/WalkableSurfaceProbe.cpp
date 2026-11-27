#include "src/client/gameplay/WalkableSurfaceProbe.h"

namespace engine::gameplay
{
	bool ProbeWalkableSurfaceBelow(const IWorldCollider& world, float x, float z, float fromY,
		float above, float below, float& outY)
	{
		// Petite capsule (sphère de 0,2 m) : son bas part de fromY + above et descend de
		// above + below ; la fraction du contact donne l'altitude du dessus touché.
		IWorldCollider::Capsule probe;
		probe.radius = 0.2f;
		probe.height = 0.4f;
		const float halfH = probe.height * 0.5f;
		const engine::math::Vec3 start{ x, fromY + above + halfH, z };
		const engine::math::Vec3 end{ x, fromY - below + halfH, z };
		IWorldCollider::SweepHit hit;
		if (!world.SweepCapsule(probe, start, end, hit) || !hit.hit || hit.normal.y <= 0.5f)
			return false;
		outY = start.y + (end.y - start.y) * hit.fraction - halfH;
		return true;
	}
}
