// CMANGOS.04 (Phase 2.04c) — Tests MoveSplinePacketBuilder.
// Round-trip pure (encode → decode → equality).

#include "src/shardd/internals/movement/MoveSpline.h"
#include "src/shardd/internals/movement/MoveSplineInit.h"
#include "src/shardd/internals/movement/MoveSplinePacketBuilder.h"
#include "src/shared/core/Log.h"

#include <chrono>

namespace
{
	using engine::server::shard::movement::BuildPayloadFromSpline;
	using engine::server::shard::movement::DecodeMonsterMove;
	using engine::server::shard::movement::EncodeMonsterMove;
	using engine::server::shard::movement::HasFlag;
	using engine::server::shard::movement::MonsterMoveDecodeResult;
	using engine::server::shard::movement::MonsterMovePayload;
	using engine::server::shard::movement::MoveSpline;
	using engine::server::shard::movement::MoveSplineFlag;
	using engine::server::shard::movement::MoveSplineInit;
	using engine::server::shard::movement::Vec3;
	using engine::server::shard::movement::kMaxMonsterMovePoints;

	bool TestRoundTripBasic()
	{
		MonsterMovePayload p;
		p.entityGuid = 0xDEADBEEFCAFEBABEull;
		p.splineId   = 42;
		p.flags      = MoveSplineFlag::Walking | MoveSplineFlag::Cyclic;
		p.velocity   = 5.5f;
		p.points = { Vec3(0, 0, 0), Vec3(10, 1, 0), Vec3(20, 2, 0) };

		const auto blob = EncodeMonsterMove(p);
		MonsterMovePayload r;
		if (DecodeMonsterMove(blob, r) != MonsterMoveDecodeResult::OK)
		{
			LOG_ERROR(Core, "[MoveSplinePacketTests] decode failed");
			return false;
		}
		if (r.entityGuid != p.entityGuid) return false;
		if (r.splineId != p.splineId) return false;
		if (static_cast<uint32_t>(r.flags) != static_cast<uint32_t>(p.flags)) return false;
		if (r.velocity != p.velocity) return false;
		if (r.points.size() != 3) return false;
		if (r.points[1].x != 10.0f || r.points[1].y != 1.0f) return false;
		LOG_INFO(Core, "[MoveSplinePacketTests] roundtrip basic OK");
		return true;
	}

	bool TestRoundTripEmptyPoints()
	{
		MonsterMovePayload p;
		p.entityGuid = 1;
		p.splineId = 1;
		// Pas de points (cas degenere : payload minimal valide).
		const auto blob = EncodeMonsterMove(p);
		MonsterMovePayload r;
		if (DecodeMonsterMove(blob, r) != MonsterMoveDecodeResult::OK) return false;
		if (!r.points.empty()) return false;
		LOG_INFO(Core, "[MoveSplinePacketTests] roundtrip empty points OK");
		return true;
	}

	bool TestBufferTooSmall()
	{
		std::vector<uint8_t> tooShort(8, 0);
		MonsterMovePayload r;
		if (DecodeMonsterMove(tooShort, r) != MonsterMoveDecodeResult::BufferTooSmall)
			return false;
		LOG_INFO(Core, "[MoveSplinePacketTests] buffer too small OK");
		return true;
	}

	bool TestBuildFromSpline()
	{
		MoveSplineInit init;
		init.MoveTo(Vec3(0, 0, 0))
		    .MoveTo(Vec3(10, 0, 0))
		    .MoveTo(Vec3(20, 0, 0))
		    .SetVelocity(8.0f)
		    .SetWalking()
		    .SetCyclic();
		MoveSpline ms;
		init.Launch(ms, 999u, MoveSpline::TimePoint{});

		const auto p = BuildPayloadFromSpline(0xCAFEull, ms);
		if (p.entityGuid != 0xCAFEull) return false;
		if (p.splineId != 999u) return false;
		if (p.points.size() != 3) return false;
		if (!HasFlag(p.flags, MoveSplineFlag::Walking)) return false;
		if (!HasFlag(p.flags, MoveSplineFlag::Cyclic)) return false;
		LOG_INFO(Core, "[MoveSplinePacketTests] BuildFromSpline OK");
		return true;
	}

	bool TestPointCountMismatch()
	{
		// Encode 1 point, mais ajoute un byte parasite a la fin → mismatch.
		MonsterMovePayload p;
		p.entityGuid = 1;
		p.splineId = 1;
		p.points = { Vec3(0, 0, 0) };
		auto blob = EncodeMonsterMove(p);
		blob.push_back(0xFF);
		MonsterMovePayload r;
		if (DecodeMonsterMove(blob, r) != MonsterMoveDecodeResult::PointCountMismatch)
			return false;
		LOG_INFO(Core, "[MoveSplinePacketTests] PointCountMismatch detected OK");
		return true;
	}

	bool TestMaxPointsCap()
	{
		MonsterMovePayload p;
		p.entityGuid = 1;
		p.splineId = 1;
		// 300 points : encode cap a kMaxMonsterMovePoints (256).
		p.points.resize(300, Vec3(1, 2, 3));
		const auto blob = EncodeMonsterMove(p);
		MonsterMovePayload r;
		if (DecodeMonsterMove(blob, r) != MonsterMoveDecodeResult::OK) return false;
		if (r.points.size() != kMaxMonsterMovePoints)
		{
			LOG_ERROR(Core, "[MoveSplinePacketTests] expected cap to {} points, got {}",
				kMaxMonsterMovePoints, r.points.size());
			return false;
		}
		LOG_INFO(Core, "[MoveSplinePacketTests] cap to kMaxMonsterMovePoints OK");
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

	const bool ok = TestRoundTripBasic()
		&& TestRoundTripEmptyPoints()
		&& TestBufferTooSmall()
		&& TestBuildFromSpline()
		&& TestPointCountMismatch()
		&& TestMaxPointsCap();

	if (ok)
		LOG_INFO(Core, "[MoveSplinePacketTests] ALL OK");
	else
		LOG_ERROR(Core, "[MoveSplinePacketTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
