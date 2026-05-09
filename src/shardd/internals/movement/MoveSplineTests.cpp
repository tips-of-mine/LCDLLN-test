// CMANGOS.04 (Phase 2.04b) — Tests MoveSpline runtime + MoveSplineInit.
// Pas de DB ni de threading.

#include "src/shardd/internals/movement/MoveSpline.h"
#include "src/shardd/internals/movement/MoveSplineInit.h"
#include "src/shared/core/Log.h"

#include <chrono>

namespace
{
	using engine::server::shard::movement::HasFlag;
	using engine::server::shard::movement::MoveSpline;
	using engine::server::shard::movement::MoveSplineFlag;
	using engine::server::shard::movement::MoveSplineInit;
	using engine::server::shard::movement::Vec3;
	using namespace std::chrono_literals;

	using TP = MoveSpline::TimePoint;

	bool ApproxEq(float a, float b, float eps = 0.05f)
	{
		float d = a - b; if (d < 0) d = -d;
		return d <= eps;
	}

	bool TestFlagsBitwise()
	{
		auto f = MoveSplineFlag::Walking | MoveSplineFlag::Cyclic;
		if (!HasFlag(f, MoveSplineFlag::Walking)) return false;
		if (!HasFlag(f, MoveSplineFlag::Cyclic)) return false;
		if (HasFlag(f, MoveSplineFlag::Flying)) return false;
		LOG_INFO(Core, "[MoveSplineTests] flag bitwise OK");
		return true;
	}

	bool TestInitRejectsTooFewPoints()
	{
		MoveSplineInit init;
		MoveSpline ms;
		// 1 point seulement → invalide pour Linear (besoin ≥ 2).
		init.MoveTo(Vec3(0, 0, 0));
		init.SetVelocity(5.0f);
		const TP now{};
		if (init.Launch(ms, 1, now))
		{
			LOG_ERROR(Core, "[MoveSplineTests] Launch should refuse 1 point");
			return false;
		}
		LOG_INFO(Core, "[MoveSplineTests] reject too few points OK");
		return true;
	}

	bool TestInitLinearLaunch()
	{
		MoveSplineInit init;
		init.MoveTo(Vec3(0, 0, 0))
		    .MoveTo(Vec3(10, 0, 0))
		    .SetVelocity(5.0f)
		    .SetWalking();
		MoveSpline ms;
		const TP t0{};
		if (!init.Launch(ms, 42, t0))
		{
			LOG_ERROR(Core, "[MoveSplineTests] Launch should succeed (2 points, linear)");
			return false;
		}
		if (ms.SplineId() != 42 || ms.IsFinished())
		{
			LOG_ERROR(Core, "[MoveSplineTests] post-launch state wrong");
			return false;
		}
		// Position initiale = premier control point (0,0,0).
		const Vec3 p0 = ms.CurrentPosition();
		if (!ApproxEq(p0.x, 0) || !ApproxEq(p0.y, 0) || !ApproxEq(p0.z, 0)) return false;
		LOG_INFO(Core, "[MoveSplineTests] linear launch OK");
		return true;
	}

	bool TestUpdatePositionMidpoint()
	{
		MoveSplineInit init;
		init.MoveTo(Vec3(0, 0, 0))
		    .MoveTo(Vec3(10, 0, 0))
		    .SetVelocity(5.0f);  // 5 m/s sur 10m → trajet en 2s
		MoveSpline ms;
		const TP t0{};
		init.Launch(ms, 1, t0);

		// A t=1s : on devrait etre a (5, 0, 0).
		const TP t1 = t0 + std::chrono::seconds(1);
		const bool stillActive = ms.UpdatePosition(t1);
		if (!stillActive)
		{
			LOG_ERROR(Core, "[MoveSplineTests] should still be active at 1s");
			return false;
		}
		const Vec3 pos = ms.CurrentPosition();
		if (!ApproxEq(pos.x, 5.0f, 0.1f))
		{
			LOG_ERROR(Core, "[MoveSplineTests] mid expected x=5, got x={}", pos.x);
			return false;
		}
		LOG_INFO(Core, "[MoveSplineTests] mid-trajectory OK");
		return true;
	}

	bool TestFinishesAtEnd()
	{
		MoveSplineInit init;
		init.MoveTo(Vec3(0, 0, 0))
		    .MoveTo(Vec3(10, 0, 0))
		    .SetVelocity(5.0f);  // 2s pour finir
		MoveSpline ms;
		const TP t0{};
		init.Launch(ms, 1, t0);

		// A t=3s : termine.
		const bool active = ms.UpdatePosition(t0 + 3s);
		if (active)
		{
			LOG_ERROR(Core, "[MoveSplineTests] should be finished at 3s");
			return false;
		}
		if (!ms.IsFinished())
		{
			LOG_ERROR(Core, "[MoveSplineTests] IsFinished should be true");
			return false;
		}
		// Position finale = derniere point.
		const Vec3 pos = ms.CurrentPosition();
		if (!ApproxEq(pos.x, 10.0f))
		{
			LOG_ERROR(Core, "[MoveSplineTests] end pos expected x=10, got x={}", pos.x);
			return false;
		}
		LOG_INFO(Core, "[MoveSplineTests] finishes at end OK");
		return true;
	}

	bool TestCyclicLoops()
	{
		MoveSplineInit init;
		init.MoveTo(Vec3(0, 0, 0))
		    .MoveTo(Vec3(10, 0, 0))
		    .SetVelocity(10.0f)  // 1s pour finir
		    .SetCyclic();
		MoveSpline ms;
		const TP t0{};
		init.Launch(ms, 1, t0);

		// A t=0.5s : milieu (~5m).
		ms.UpdatePosition(t0 + std::chrono::milliseconds(500));
		if (!ApproxEq(ms.CurrentPosition().x, 5.0f, 0.5f)) return false;

		// A t=1.5s : le mouvement aurait fini en non-cyclic, mais cyclic →
		// retour a 0.5s du cycle suivant → milieu.
		ms.UpdatePosition(t0 + std::chrono::milliseconds(1500));
		if (ms.IsFinished())
		{
			LOG_ERROR(Core, "[MoveSplineTests] cyclic should never finish");
			return false;
		}
		// Position attendue : 0.5s de cycle suivant → ~5m.
		if (!ApproxEq(ms.CurrentPosition().x, 5.0f, 0.5f))
		{
			LOG_ERROR(Core, "[MoveSplineTests] cyclic mid expected x~5, got x={}",
				ms.CurrentPosition().x);
			return false;
		}
		LOG_INFO(Core, "[MoveSplineTests] cyclic loops OK");
		return true;
	}

	bool TestVelocityZeroFreeze()
	{
		MoveSplineInit init;
		init.MoveTo(Vec3(0, 0, 0))
		    .MoveTo(Vec3(10, 0, 0))
		    .SetVelocity(0.0f);
		MoveSpline ms;
		const TP t0{};
		init.Launch(ms, 1, t0);

		ms.UpdatePosition(t0 + 100s);
		// Velocity 0 → pas de progression. Position reste au depart.
		if (ms.IsFinished())
		{
			LOG_ERROR(Core, "[MoveSplineTests] velocity 0 → should not finish");
			return false;
		}
		if (!ApproxEq(ms.CurrentPosition().x, 0.0f, 0.01f)) return false;
		LOG_INFO(Core, "[MoveSplineTests] velocity 0 freeze OK");
		return true;
	}

	bool TestSplineIdMonotoneCallerResponsibility()
	{
		// Le caller est responsable de l'unicite. On vérifie juste que
		// SplineId est bien transporté tel quel.
		MoveSplineInit init;
		init.MoveTo(Vec3(0, 0, 0)).MoveTo(Vec3(1, 0, 0)).SetVelocity(1.0f);
		MoveSpline ms;
		const TP t0{};
		init.Launch(ms, 999u, t0);
		if (ms.SplineId() != 999u) return false;
		LOG_INFO(Core, "[MoveSplineTests] SplineId carried OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestFlagsBitwise()
		&& TestInitRejectsTooFewPoints()
		&& TestInitLinearLaunch()
		&& TestUpdatePositionMidpoint()
		&& TestFinishesAtEnd()
		&& TestCyclicLoops()
		&& TestVelocityZeroFreeze()
		&& TestSplineIdMonotoneCallerResponsibility();

	if (ok)
		LOG_INFO(Core, "[MoveSplineTests] ALL OK");
	else
		LOG_ERROR(Core, "[MoveSplineTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
