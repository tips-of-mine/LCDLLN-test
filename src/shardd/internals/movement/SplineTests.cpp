// CMANGOS.04 (Phase 2.04a) — Tests Spline<T> + MovementTypedefs.
// Pure math, pas de DB.

#include "engine/server/shard/movement/MovementTypedefs.h"
#include "engine/server/shard/movement/Spline.h"
#include "engine/core/Log.h"

#include <cmath>

namespace
{
	using engine::server::shard::movement::Quat;
	using engine::server::shard::movement::Spline;
	using engine::server::shard::movement::SplineMode;
	using engine::server::shard::movement::Vec3;
	using engine::server::shard::movement::Vec4;

	bool ApproxEq(float a, float b, float eps = 1e-4f)
	{
		float d = a - b; if (d < 0) d = -d;
		return d <= eps;
	}

	bool ApproxEqVec3(const Vec3& a, const Vec3& b, float eps = 1e-3f)
	{
		return ApproxEq(a.x, b.x, eps) && ApproxEq(a.y, b.y, eps) && ApproxEq(a.z, b.z, eps);
	}

	// --- Vec4 / Quat tests ---

	bool TestVec4Arithmetic()
	{
		Vec4 a(1, 2, 3, 4);
		Vec4 b(0, 1, 0, 1);
		Vec4 sum = a + b;
		if (!ApproxEq(sum.x, 1) || !ApproxEq(sum.y, 3) || !ApproxEq(sum.z, 3) || !ApproxEq(sum.w, 5))
			return false;
		Vec4 scaled = a * 2.0f;
		if (!ApproxEq(scaled.x, 2) || !ApproxEq(scaled.w, 8)) return false;
		// Dot product : a.b = 1*0 + 2*1 + 3*0 + 4*1 = 6
		if (!ApproxEq(a.Dot(b), 6.0f)) return false;
		LOG_INFO(Core, "[SplineTests] Vec4 arithmetic OK");
		return true;
	}

	bool TestQuatIdentity()
	{
		const Quat id = Quat::Identity();
		if (id.x != 0 || id.y != 0 || id.z != 0 || id.w != 1) return false;

		const Quat yaw0 = Quat::FromYaw(0.0f);
		// yaw=0 → sin(0)=0, cos(0)=1 → identity.
		if (!ApproxEq(yaw0.x, 0) || !ApproxEq(yaw0.y, 0)
			|| !ApproxEq(yaw0.z, 0) || !ApproxEq(yaw0.w, 1))
			return false;

		// yaw=π → sin(π/2)=1, cos(π/2)=0 → (0, 1, 0, 0)
		const Quat yawPi = Quat::FromYaw(static_cast<float>(M_PI));
		if (!ApproxEq(yawPi.y, 1.0f, 1e-3f) || !ApproxEq(yawPi.w, 0.0f, 1e-3f))
		{
			LOG_ERROR(Core, "[SplineTests] yawPi expected (0,1,0,0), got ({},{},{},{})",
				yawPi.x, yawPi.y, yawPi.z, yawPi.w);
			return false;
		}

		// Normalize d'un quaternion non-unitaire.
		Quat raw(1, 2, 3, 4);
		Quat n = raw.Normalized();
		const float len2 = n.x * n.x + n.y * n.y + n.z * n.z + n.w * n.w;
		if (!ApproxEq(len2, 1.0f, 1e-4f))
		{
			LOG_ERROR(Core, "[SplineTests] normalized len2={} expected 1", len2);
			return false;
		}

		LOG_INFO(Core, "[SplineTests] Quat identity + FromYaw + Normalize OK");
		return true;
	}

	// --- Spline<Vec3> Linear ---

	bool TestSplineLinearBasic()
	{
		Spline<Vec3> s;
		s.SetMode(SplineMode::Linear);
		s.SetPoints({Vec3(0, 0, 0), Vec3(10, 0, 0), Vec3(20, 0, 0)});

		if (s.SegmentCount() != 2) return false;
		if (!ApproxEqVec3(s.Evaluate(0.0f), Vec3(0, 0, 0))) return false;
		if (!ApproxEqVec3(s.Evaluate(1.0f), Vec3(10, 0, 0))) return false;
		if (!ApproxEqVec3(s.Evaluate(2.0f), Vec3(20, 0, 0))) return false;
		// Mid-segment.
		if (!ApproxEqVec3(s.Evaluate(0.5f), Vec3(5, 0, 0))) return false;
		if (!ApproxEqVec3(s.Evaluate(1.5f), Vec3(15, 0, 0))) return false;
		LOG_INFO(Core, "[SplineTests] linear basic OK");
		return true;
	}

	bool TestSplineLinearClamp()
	{
		Spline<Vec3> s;
		s.SetMode(SplineMode::Linear);
		s.SetPoints({Vec3(0, 0, 0), Vec3(1, 0, 0)});
		// t hors [0, 1] est clampe.
		if (!ApproxEqVec3(s.Evaluate(-5.0f), Vec3(0, 0, 0))) return false;
		if (!ApproxEqVec3(s.Evaluate(99.0f), Vec3(1, 0, 0))) return false;
		LOG_INFO(Core, "[SplineTests] linear clamp OK");
		return true;
	}

	bool TestSplineLinearLength()
	{
		Spline<Vec3> s;
		s.SetMode(SplineMode::Linear);
		s.SetPoints({Vec3(0, 0, 0), Vec3(3, 4, 0)});  // distance = 5
		const float len = s.LengthApprox(8);
		if (!ApproxEq(len, 5.0f, 0.001f))
		{
			LOG_ERROR(Core, "[SplineTests] linear length expected ~5, got {}", len);
			return false;
		}
		LOG_INFO(Core, "[SplineTests] linear length OK");
		return true;
	}

	// --- Spline<Vec3> CatmullRom ---

	bool TestSplineCatmullRomNeedsFour()
	{
		Spline<Vec3> s;
		s.SetMode(SplineMode::CatmullRom);
		s.SetPoints({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(2, 0, 0)});
		if (s.SegmentCount() != 0)
		{
			LOG_ERROR(Core, "[SplineTests] CR with 3 points should have 0 segments");
			return false;
		}
		LOG_INFO(Core, "[SplineTests] CR needs ≥ 4 points OK");
		return true;
	}

	bool TestSplineCatmullRomPassesThroughInner()
	{
		// 4 points alignés sur l'axe X. Catmull-Rom doit passer
		// EXACTEMENT par P1 (à t=0) et P2 (à t=1).
		Spline<Vec3> s;
		s.SetMode(SplineMode::CatmullRom);
		s.SetPoints({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(2, 0, 0), Vec3(3, 0, 0)});

		if (s.SegmentCount() != 1) return false;
		if (!ApproxEqVec3(s.Evaluate(0.0f), Vec3(1, 0, 0)))
		{
			LOG_ERROR(Core, "[SplineTests] CR t=0 expected P1=(1,0,0), got ({},{},{})",
				s.Evaluate(0.0f).x, s.Evaluate(0.0f).y, s.Evaluate(0.0f).z);
			return false;
		}
		if (!ApproxEqVec3(s.Evaluate(1.0f), Vec3(2, 0, 0)))
		{
			LOG_ERROR(Core, "[SplineTests] CR t=1 expected P2=(2,0,0)");
			return false;
		}
		// Mid-point sur axe X linéaire : doit être (1.5, 0, 0).
		if (!ApproxEqVec3(s.Evaluate(0.5f), Vec3(1.5f, 0, 0), 1e-3f))
		{
			LOG_ERROR(Core, "[SplineTests] CR mid expected (1.5,0,0), got ({},{},{})",
				s.Evaluate(0.5f).x, s.Evaluate(0.5f).y, s.Evaluate(0.5f).z);
			return false;
		}
		LOG_INFO(Core, "[SplineTests] CR passes through P1/P2 OK");
		return true;
	}

	bool TestSplineCatmullRomCurve()
	{
		// 4 points : (0,0,0), (1,0,0), (1,1,0), (0,1,0) — forment un L.
		// La Catmull-Rom doit passer par P1=(1,0,0) et P2=(1,1,0).
		// À t=0.5, la courbe est entre les deux mais pas sur la droite
		// directe : on doit avoir z=0 et y entre 0 et 1, x proche mais
		// PAS exactement 1 (la courbure pousse).
		Spline<Vec3> s;
		s.SetMode(SplineMode::CatmullRom);
		s.SetPoints({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0), Vec3(0, 1, 0)});

		const Vec3 mid = s.Evaluate(0.5f);
		if (!ApproxEq(mid.z, 0.0f)) return false;          // reste 2D
		if (mid.y < 0.0f || mid.y > 1.0f) return false;    // y in [0,1]
		// La courbure rend mid.x > 1 (la courbe "déborde" vers l'extérieur
		// du virage en L pour rester smooth).
		if (mid.x <= 1.0f)
		{
			LOG_ERROR(Core, "[SplineTests] expected curve overshoot x>1 at L-corner, got x={}",
				mid.x);
			return false;
		}
		LOG_INFO(Core, "[SplineTests] CR L-curve overshoot OK (mid=({}, {}, {}))",
			mid.x, mid.y, mid.z);
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

	const bool ok = TestVec4Arithmetic()
		&& TestQuatIdentity()
		&& TestSplineLinearBasic()
		&& TestSplineLinearClamp()
		&& TestSplineLinearLength()
		&& TestSplineCatmullRomNeedsFour()
		&& TestSplineCatmullRomPassesThroughInner()
		&& TestSplineCatmullRomCurve();

	if (ok)
		LOG_INFO(Core, "[SplineTests] ALL OK");
	else
		LOG_ERROR(Core, "[SplineTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
