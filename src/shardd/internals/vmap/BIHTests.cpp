// CMANGOS.05 (Phase 2.05a) — Tests AABB + BIH<T>.
// Pure math, pas de DB.

#include "src/shardd/internals/vmap/AABB.h"
#include "src/shardd/internals/vmap/BIH.h"
#include "src/shared/core/Log.h"

#include <cmath>
#include <vector>

namespace
{
	using engine::math::Vec3;
	using engine::server::shard::vmap::AABB;
	using engine::server::shard::vmap::BIH;
	using engine::server::shard::vmap::IntersectRayAABB;
	using engine::server::shard::vmap::Ray;

	bool ApproxEq(float a, float b, float eps = 1e-4f)
	{
		return std::fabs(a - b) <= eps;
	}

	bool TestAABBExpandContains()
	{
		AABB box = AABB::Empty();
		if (box.IsValid())
		{
			LOG_ERROR(Core, "[BIHTests] empty AABB should be invalid");
			return false;
		}
		box.Expand(Vec3(1, 2, 3));
		box.Expand(Vec3(-1, 0, 5));
		if (!box.IsValid()) return false;
		if (!box.Contains(Vec3(0, 1, 4))) return false;
		if (box.Contains(Vec3(2, 0, 0))) return false;
		LOG_INFO(Core, "[BIHTests] AABB expand+contains OK");
		return true;
	}

	bool TestRayHitsAABBOutside()
	{
		// Ray en (0,0,0) direction +x. Boîte [(5,-1,-1), (6,1,1)].
		// Hit attendu à t=5.
		const AABB box(Vec3(5, -1, -1), Vec3(6, 1, 1));
		const Ray  r(Vec3(0, 0, 0), Vec3(1, 0, 0));
		float t = -1.0f;
		if (!IntersectRayAABB(r, box, t))
		{
			LOG_ERROR(Core, "[BIHTests] expected hit, got miss");
			return false;
		}
		if (!ApproxEq(t, 5.0f))
		{
			LOG_ERROR(Core, "[BIHTests] expected t=5, got {}", t);
			return false;
		}
		LOG_INFO(Core, "[BIHTests] ray AABB outside OK");
		return true;
	}

	bool TestRayMissesAABB()
	{
		// Ray (0,0,0) +y, boîte sur axe x → miss.
		const AABB box(Vec3(5, -1, -1), Vec3(6, 1, 1));
		const Ray  r(Vec3(0, 0, 0), Vec3(0, 1, 0));
		float t = -1.0f;
		if (IntersectRayAABB(r, box, t))
		{
			LOG_ERROR(Core, "[BIHTests] expected miss, got hit at t={}", t);
			return false;
		}
		LOG_INFO(Core, "[BIHTests] ray AABB miss OK");
		return true;
	}

	bool TestRayInsideAABB()
	{
		// Ray origine dans la boîte → tHit = tMin (0).
		const AABB box(Vec3(-1, -1, -1), Vec3(1, 1, 1));
		const Ray  r(Vec3(0, 0, 0), Vec3(1, 0, 0));
		float t = -1.0f;
		if (!IntersectRayAABB(r, box, t)) return false;
		if (!ApproxEq(t, 0.0f))
		{
			LOG_ERROR(Core, "[BIHTests] inside : expected t=0, got {}", t);
			return false;
		}
		LOG_INFO(Core, "[BIHTests] ray inside AABB OK");
		return true;
	}

	bool TestRayParallelOutsideSlab()
	{
		// Ray parallèle à un axe ET hors de la slab → miss.
		const AABB box(Vec3(-1, -1, -1), Vec3(1, 1, 1));
		// Origine y=10, direction +x → la slab y est [−1, 1], on est en
		// dehors (y=10) et `dir.y == 0` → miss.
		const Ray  r(Vec3(0, 10, 0), Vec3(1, 0, 0));
		float t = -1.0f;
		if (IntersectRayAABB(r, box, t))
		{
			LOG_ERROR(Core, "[BIHTests] parallel-outside : expected miss");
			return false;
		}
		LOG_INFO(Core, "[BIHTests] parallel slab miss OK");
		return true;
	}

	// --- BIH<T> tests ---

	struct TestBox
	{
		AABB bounds;
		AABB Bounds() const { return bounds; }
	};

	bool TestBIHEmpty()
	{
		BIH<TestBox> b;
		std::vector<TestBox> items;
		b.Build(items);
		if (b.NodeCount() != 0 || b.ItemCount() != 0)
		{
			LOG_ERROR(Core, "[BIHTests] empty BIH should have 0 nodes/items");
			return false;
		}
		LOG_INFO(Core, "[BIHTests] empty BIH OK");
		return true;
	}

	bool TestBIHSingle()
	{
		BIH<TestBox> b;
		std::vector<TestBox> items{ TestBox{ AABB(Vec3(-1, -1, -1), Vec3(1, 1, 1)) } };
		b.Build(items);
		if (b.NodeCount() != 1 || b.ItemCount() != 1)
		{
			LOG_ERROR(Core, "[BIHTests] single item : expected 1 leaf node, got nodes={} items={}",
				b.NodeCount(), b.ItemCount());
			return false;
		}
		LOG_INFO(Core, "[BIHTests] single BIH OK");
		return true;
	}

	bool TestBIHRaycastFindsClosest()
	{
		// 3 cubes alignés sur +x : aux centres x=10, 20, 30, half-size 1.
		// Ray (0,0,0) → +x, doit toucher le premier (x=10), t≈9.
		std::vector<TestBox> items;
		for (int i = 1; i <= 3; ++i)
		{
			const float cx = static_cast<float>(i) * 10.0f;
			items.push_back(TestBox{ AABB(Vec3(cx - 1, -1, -1), Vec3(cx + 1, 1, 1)) });
		}
		BIH<TestBox> b;
		b.Build(items);
		const Ray r(Vec3(0, 0, 0), Vec3(1, 0, 0));

		// HitFn : test précis = test rayon-AABB de l'objet.
		auto hitTest = [&](uint32_t idx, const Ray& ray, float& tOut) {
			float t = std::numeric_limits<float>::infinity();
			if (IntersectRayAABB(ray, items[idx].Bounds(), t))
			{
				tOut = t;
				return true;
			}
			return false;
		};

		const float tHit = b.Raycast(r, items, hitTest);
		if (!ApproxEq(tHit, 9.0f, 1e-3f))
		{
			LOG_ERROR(Core, "[BIHTests] expected closest hit t=9, got {}", tHit);
			return false;
		}
		LOG_INFO(Core, "[BIHTests] raycast finds closest OK (t={})", tHit);
		return true;
	}

	bool TestBIHRaycastMisses()
	{
		// Cube en (5..6, 5..6, 5..6). Ray sur +x au sol → ne touche pas.
		std::vector<TestBox> items{ TestBox{ AABB(Vec3(5, 5, 5), Vec3(6, 6, 6)) } };
		BIH<TestBox> b;
		b.Build(items);
		const Ray r(Vec3(0, 0, 0), Vec3(1, 0, 0));

		auto hitTest = [&](uint32_t idx, const Ray& ray, float& tOut) {
			float t = std::numeric_limits<float>::infinity();
			if (IntersectRayAABB(ray, items[idx].Bounds(), t))
			{
				tOut = t;
				return true;
			}
			return false;
		};

		const float tHit = b.Raycast(r, items, hitTest);
		if (tHit != std::numeric_limits<float>::infinity())
		{
			LOG_ERROR(Core, "[BIHTests] expected miss (inf), got t={}", tHit);
			return false;
		}
		LOG_INFO(Core, "[BIHTests] raycast miss OK");
		return true;
	}

	bool TestBIHManyBoxes()
	{
		// 64 boîtes en grille : x/y/z ∈ {0, 4, 8, ..., 28}.
		// Ray à x=2, y=2, z=−10 → +z, doit toucher la boîte centrée à
		// (2, 2, 0..3) (la boîte 0 est en [-1, 3] × [-1, 3] × [-1, 3]).
		std::vector<TestBox> items;
		for (int xi = 0; xi < 4; ++xi)
		for (int yi = 0; yi < 4; ++yi)
		for (int zi = 0; zi < 4; ++zi)
		{
			const float cx = static_cast<float>(xi) * 4.0f;
			const float cy = static_cast<float>(yi) * 4.0f;
			const float cz = static_cast<float>(zi) * 4.0f;
			items.push_back(TestBox{ AABB(Vec3(cx - 1, cy - 1, cz - 1),
				Vec3(cx + 1, cy + 1, cz + 1)) });
		}
		if (items.size() != 64) return false;

		BIH<TestBox> b;
		b.Build(items);
		// Beaucoup de boîtes → BIH a forcément split au moins une fois.
		if (b.NodeCount() < 2)
		{
			LOG_ERROR(Core, "[BIHTests] expected >1 nodes for 64 boxes, got {}", b.NodeCount());
			return false;
		}

		const Ray r(Vec3(0, 0, -10), Vec3(0, 0, 1));
		auto hitTest = [&](uint32_t idx, const Ray& ray, float& tOut) {
			float t = std::numeric_limits<float>::infinity();
			if (IntersectRayAABB(ray, items[idx].Bounds(), t))
			{
				tOut = t;
				return true;
			}
			return false;
		};
		const float tHit = b.Raycast(r, items, hitTest);

		// La boîte la plus proche sur (0,0,*) est centre (0,0,0), bounds
		// (-1,-1,-1)..(1,1,1) → entrée à z=-1 → t = (-1 - (-10)) = 9.
		if (!ApproxEq(tHit, 9.0f, 1e-3f))
		{
			LOG_ERROR(Core, "[BIHTests] 64-grid : expected t=9, got {}", tHit);
			return false;
		}
		LOG_INFO(Core, "[BIHTests] 64-box raycast OK (nodes={} t={})",
			b.NodeCount(), tHit);
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

	const bool ok = TestAABBExpandContains()
		&& TestRayHitsAABBOutside()
		&& TestRayMissesAABB()
		&& TestRayInsideAABB()
		&& TestRayParallelOutsideSlab()
		&& TestBIHEmpty()
		&& TestBIHSingle()
		&& TestBIHRaycastFindsClosest()
		&& TestBIHRaycastMisses()
		&& TestBIHManyBoxes();

	if (ok)
		LOG_INFO(Core, "[BIHTests] ALL OK");
	else
		LOG_ERROR(Core, "[BIHTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
