#include "src/shardd/gameplay/ambient/AmbientRoutine.h"

#include <algorithm>
#include <cmath>

namespace engine::server::ambient
{
	bool ParseRoutineKind(std::string_view t, RoutineKind& out)
	{
		if (t == "tour") { out = RoutineKind::Tour; return true; }
		if (t == "patrol") { out = RoutineKind::Patrol; return true; }
		if (t == "post") { out = RoutineKind::Post; return true; }
		if (t == "wander") { out = RoutineKind::Wander; return true; }
		out = RoutineKind::None;
		return false;
	}

	bool ParseRoutineSchedule(std::string_view t, RoutineSchedule& out)
	{
		if (t == "always" || t.empty()) { out = RoutineSchedule::Always; return true; }
		if (t == "day") { out = RoutineSchedule::Day; return true; }
		if (t == "night") { out = RoutineSchedule::Night; return true; }
		out = RoutineSchedule::Always;
		return false;
	}

	bool IsScheduleOpen(RoutineSchedule schedule, float hours)
	{
		switch (schedule)
		{
		case RoutineSchedule::Day: return hours >= 6.0f && hours < 21.0f;
		case RoutineSchedule::Night: return hours >= 20.0f || hours < 6.0f;
		default: return true;
		}
	}

	RoutinePoint HomePoint(const RoutineDefinition& def, float homeX, float homeZ)
	{
		if (!def.path.empty()) return def.path.front();
		return { homeX, homeZ };
	}

	void ChooseNextTarget(const RoutineDefinition& def, RoutineState& state,
		float homeX, float homeZ, float (*roll01)(void*), void* ctx)
	{
		const size_t n = def.path.size();
		// v1.0.142 : le segment qui commence part de la cible atteinte (ou du logis).
		state.origin = state.hasTarget ? state.target : HomePoint(def, homeX, homeZ);
		switch (def.kind)
		{
		case RoutineKind::Tour:
		case RoutineKind::Patrol:
		{
			if (n == 0) { state.target = { homeX, homeZ }; break; }
			if (!state.hasTarget)
			{
				state.waypoint = 0;
				state.direction = 1;
			}
			else if (n > 1)
			{
				if (def.pingPong)
				{
					// Aller-retour : on rebondit sur les extrémités.
					int next = static_cast<int>(state.waypoint) + state.direction;
					if (next < 0 || next >= static_cast<int>(n))
					{
						state.direction = static_cast<int8_t>(-state.direction);
						next = static_cast<int>(state.waypoint) + state.direction;
					}
					state.waypoint = static_cast<uint32_t>(std::clamp(next, 0, static_cast<int>(n) - 1));
				}
				else
				{
					state.waypoint = static_cast<uint32_t>((state.waypoint + 1u) % n);
				}
			}
			state.target = def.path[std::min<size_t>(state.waypoint, n - 1)];
			break;
		}
		case RoutineKind::Post:
		case RoutineKind::Wander:
		{
			// Poste : petite errance autour du poste ; errance : dans le rayon.
			const RoutinePoint base = def.path.empty() ? RoutinePoint{ homeX, homeZ } : def.path.front();
			const float radius = std::max(0.0f, def.wanderRadiusMeters);
			if (radius <= 0.05f) { state.target = base; break; }
			const float a = roll01(ctx) * 6.2831853f;
			const float r = std::sqrt(roll01(ctx)) * radius;
			state.target = { base.x + std::cos(a) * r, base.z + std::sin(a) * r, base.y };
			break;
		}
		default:
			state.target = { homeX, homeZ };
			break;
		}
		state.hasTarget = true;
	}

	float RollPauseSeconds(const RoutineDefinition& def, float (*roll01)(void*), void* ctx)
	{
		// Poste et errance : toujours une pause (ils « vivent » sur place).
		const bool always = def.kind == RoutineKind::Post || def.kind == RoutineKind::Wander;
		if (!always && roll01(ctx) >= def.pauseChance) return 0.0f;
		const float lo = std::max(0.0f, def.pauseMinSec);
		const float hi = std::max(lo, def.pauseMaxSec);
		return lo + (hi - lo) * roll01(ctx);
	}

	float RoutineHeightAlong(const RoutinePoint& from, const RoutinePoint& to, float x, float z)
	{
		const float dx = to.x - from.x, dz = to.z - from.z;
		const float len2 = dx * dx + dz * dz;
		if (len2 < 1e-8f) return to.y;
		const float k = std::clamp(((x - from.x) * dx + (z - from.z) * dz) / len2, 0.0f, 1.0f);
		return from.y + (to.y - from.y) * k;
	}

	void BeginHomeRetrace(const RoutineDefinition& def, RoutineState& state, float homeX, float homeZ)
	{
		state.homeRetrace = true;
		if (def.path.empty())
		{
			state.origin = state.target;
			state.target = { homeX, homeZ, state.target.y };
			state.waypoint = 0;
			state.hasTarget = true;
			return;
		}
		const size_t n = def.path.size();
		const uint32_t w = static_cast<uint32_t>(std::min<size_t>(state.waypoint, n - 1));
		// Le PNJ est entre path[w − 1] et path[w] : il fait demi-tour vers path[w − 1].
		state.origin = state.hasTarget ? state.target : def.path[w];
		state.waypoint = w > 0 ? w - 1 : 0;
		state.target = def.path[state.waypoint];
		state.hasTarget = true;
	}

	bool AdvanceHomeRetrace(const RoutineDefinition& def, RoutineState& state)
	{
		if (def.path.empty() || state.waypoint == 0) return true;
		state.origin = state.target;
		--state.waypoint;
		state.target = def.path[state.waypoint];
		return false;
	}
}
