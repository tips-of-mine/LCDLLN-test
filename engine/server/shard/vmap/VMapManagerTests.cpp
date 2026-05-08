// CMANGOS.05 (Phase 2.05c) — Tests VMapManager IsInLineOfSight + GetHeight.
// Pure : pas de DB, pas d'I/O disque (tile fabriqué en mémoire).

#include "engine/server/shard/vmap/VMapFormat.h"
#include "engine/server/shard/vmap/VMapManager.h"
#include "engine/core/Log.h"

#include <cmath>

namespace
{
	using engine::math::Vec3;
	using engine::server::shard::vmap::AABB;
	using engine::server::shard::vmap::VMapManager;
	using engine::server::shard::vmap::VMapTile;
	using engine::server::shard::vmap::VMapTri;

	bool ApproxEq(float a, float b, float eps = 1e-2f)
	{
		float d = a - b; if (d < 0) d = -d;
		return d <= eps;
	}

	/// Crée un tile contenant un mur vertical en plan x=5, s'étendant
	/// y ∈ [0, 10], z ∈ [-5, 5]. 2 triangles formant un quad.
	VMapTile MakeWallTile()
	{
		VMapTile t;
		t.bbox.min = Vec3(5, 0, -5);
		t.bbox.max = Vec3(5, 10, 5);
		t.vertices = {
			Vec3(5, 0, -5),  // 0 : bottom-left
			Vec3(5, 0,  5),  // 1 : bottom-right
			Vec3(5, 10, 5),  // 2 : top-right
			Vec3(5, 10,-5),  // 3 : top-left
		};
		t.triangles = { VMapTri{0, 1, 2}, VMapTri{0, 2, 3} };
		return t;
	}

	/// Crée un tile avec un sol horizontal à y=0, étendu sur [-10,10] × [-10,10].
	VMapTile MakeFloorTile()
	{
		VMapTile t;
		t.bbox.min = Vec3(-10, 0, -10);
		t.bbox.max = Vec3( 10, 0,  10);
		t.vertices = {
			Vec3(-10, 0, -10), // 0
			Vec3( 10, 0, -10), // 1
			Vec3( 10, 0,  10), // 2
			Vec3(-10, 0,  10), // 3
		};
		t.triangles = { VMapTri{0, 1, 2}, VMapTri{0, 2, 3} };
		return t;
	}

	bool TestEmptyManagerLOS()
	{
		VMapManager mgr;
		// Pas de tile chargé → pas d'obstruction connue → toujours LOS.
		if (!mgr.IsInLineOfSight(Vec3(0, 0, 0), Vec3(10, 0, 0))) return false;
		LOG_INFO(Core, "[VMapManagerTests] empty manager LOS=true OK");
		return true;
	}

	bool TestLoadAndCount()
	{
		VMapManager mgr;
		mgr.LoadTileDecoded(MakeWallTile());
		if (!mgr.IsLoaded()) return false;
		if (mgr.TriangleCount() != 2) return false;
		LOG_INFO(Core, "[VMapManagerTests] load 2 tris OK");
		return true;
	}

	bool TestLOSBlocked()
	{
		// Mur en x=5. Rayon (0,5,0) → (10,5,0) doit le traverser.
		VMapManager mgr;
		mgr.LoadTileDecoded(MakeWallTile());
		const bool los = mgr.IsInLineOfSight(Vec3(0, 5, 0), Vec3(10, 5, 0));
		if (los)
		{
			LOG_ERROR(Core, "[VMapManagerTests] expected LOS=false (wall in path)");
			return false;
		}
		LOG_INFO(Core, "[VMapManagerTests] LOS blocked by wall OK");
		return true;
	}

	bool TestLOSClear()
	{
		// Mur en x=5, hauteur [0..10]. Rayon en y=15 (au-dessus) doit
		// passer.
		VMapManager mgr;
		mgr.LoadTileDecoded(MakeWallTile());
		const bool los = mgr.IsInLineOfSight(Vec3(0, 15, 0), Vec3(10, 15, 0));
		if (!los)
		{
			LOG_ERROR(Core, "[VMapManagerTests] expected LOS=true (above wall)");
			return false;
		}
		LOG_INFO(Core, "[VMapManagerTests] LOS clear above wall OK");
		return true;
	}

	bool TestLOSStopsBeforeWall()
	{
		// Mur en x=5. Rayon (0,5,0) → (4,5,0) — s'arrête avant le mur.
		VMapManager mgr;
		mgr.LoadTileDecoded(MakeWallTile());
		const bool los = mgr.IsInLineOfSight(Vec3(0, 5, 0), Vec3(4, 5, 0));
		if (!los)
		{
			LOG_ERROR(Core, "[VMapManagerTests] expected LOS=true (segment ends before wall)");
			return false;
		}
		LOG_INFO(Core, "[VMapManagerTests] LOS stops before wall OK");
		return true;
	}

	bool TestGetHeightOnFloor()
	{
		VMapManager mgr;
		mgr.LoadTileDecoded(MakeFloorTile());
		// Sol y=0. Le raycast depuis (0, 1000, 0) vers le bas doit
		// retourner y=0.
		auto h = mgr.GetHeight(0.0f, 0.0f);
		if (!h)
		{
			LOG_ERROR(Core, "[VMapManagerTests] GetHeight expected value, got nullopt");
			return false;
		}
		if (!ApproxEq(*h, 0.0f))
		{
			LOG_ERROR(Core, "[VMapManagerTests] GetHeight expected 0, got {}", *h);
			return false;
		}
		LOG_INFO(Core, "[VMapManagerTests] GetHeight=0 on floor OK");
		return true;
	}

	bool TestGetHeightOutOfBounds()
	{
		VMapManager mgr;
		mgr.LoadTileDecoded(MakeFloorTile());
		// Tile = [-10,10] × [-10,10]. (50, 0, 0) hors plage → nullopt.
		auto h = mgr.GetHeight(50.0f, 0.0f);
		if (h)
		{
			LOG_ERROR(Core, "[VMapManagerTests] expected nullopt out of bounds, got {}", *h);
			return false;
		}
		LOG_INFO(Core, "[VMapManagerTests] GetHeight nullopt out-of-bounds OK");
		return true;
	}

	bool TestEncodeDecodeBlobRoundTrip()
	{
		// Encode → decode via VMapFormat → load via LoadTile (blob).
		const auto blob = engine::server::shard::vmap::EncodeVMapTile(MakeWallTile());
		VMapManager mgr;
		if (!mgr.LoadTile(blob))
		{
			LOG_ERROR(Core, "[VMapManagerTests] LoadTile from blob failed");
			return false;
		}
		// Doit bloquer comme avant.
		if (mgr.IsInLineOfSight(Vec3(0, 5, 0), Vec3(10, 5, 0)))
		{
			LOG_ERROR(Core, "[VMapManagerTests] LoadTile blob : LOS still expected blocked");
			return false;
		}
		LOG_INFO(Core, "[VMapManagerTests] LoadTile from blob roundtrip OK");
		return true;
	}

	bool TestClear()
	{
		VMapManager mgr;
		mgr.LoadTileDecoded(MakeWallTile());
		mgr.Clear();
		if (mgr.IsLoaded()) return false;
		if (mgr.TriangleCount() != 0) return false;
		// Après Clear : LOS toujours true (pas d'obstruction).
		if (!mgr.IsInLineOfSight(Vec3(0, 5, 0), Vec3(10, 5, 0))) return false;
		LOG_INFO(Core, "[VMapManagerTests] Clear OK");
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

	const bool ok = TestEmptyManagerLOS()
		&& TestLoadAndCount()
		&& TestLOSBlocked()
		&& TestLOSClear()
		&& TestLOSStopsBeforeWall()
		&& TestGetHeightOnFloor()
		&& TestGetHeightOutOfBounds()
		&& TestEncodeDecodeBlobRoundTrip()
		&& TestClear();

	if (ok)
		LOG_INFO(Core, "[VMapManagerTests] ALL OK");
	else
		LOG_ERROR(Core, "[VMapManagerTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
