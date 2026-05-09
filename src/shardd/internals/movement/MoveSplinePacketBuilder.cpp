#include "src/shardd/internals/movement/MoveSplinePacketBuilder.h"

#include <cstring>

namespace engine::server::shard::movement
{
	namespace
	{
		template<typename T>
		void Append(std::vector<uint8_t>& out, const T& v)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto* p = reinterpret_cast<const uint8_t*>(&v);
			out.insert(out.end(), p, p + sizeof(T));
		}

		template<typename T>
		bool Read(std::span<const uint8_t> in, size_t& off, T& out)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			if (off + sizeof(T) > in.size())
				return false;
			std::memcpy(&out, in.data() + off, sizeof(T));
			off += sizeof(T);
			return true;
		}
	}

	std::vector<uint8_t> EncodeMonsterMove(const MonsterMovePayload& msg)
	{
		std::vector<uint8_t> out;
		// Header fixe = 8 + 4 + 4 + 4 + 2 = 22 bytes.
		out.reserve(22 + msg.points.size() * sizeof(Vec3));

		Append(out, msg.entityGuid);
		Append(out, msg.splineId);
		const auto rawFlags = static_cast<uint32_t>(msg.flags);
		Append(out, rawFlags);
		Append(out, msg.velocity);
		const auto pc = static_cast<uint16_t>(
			msg.points.size() > kMaxMonsterMovePoints
				? kMaxMonsterMovePoints
				: msg.points.size());
		Append(out, pc);

		const size_t toWrite = pc;
		for (size_t i = 0; i < toWrite; ++i)
		{
			Append(out, msg.points[i].x);
			Append(out, msg.points[i].y);
			Append(out, msg.points[i].z);
		}
		return out;
	}

	MonsterMoveDecodeResult DecodeMonsterMove(std::span<const uint8_t> in,
		MonsterMovePayload& out)
	{
		size_t off = 0;
		uint64_t guid = 0;
		uint32_t splineId = 0;
		uint32_t flags = 0;
		float    velocity = 0.0f;
		uint16_t pointCount = 0;

		if (!Read(in, off, guid)
			|| !Read(in, off, splineId)
			|| !Read(in, off, flags)
			|| !Read(in, off, velocity)
			|| !Read(in, off, pointCount))
			return MonsterMoveDecodeResult::BufferTooSmall;

		if (pointCount > kMaxMonsterMovePoints)
			return MonsterMoveDecodeResult::TooManyPoints;

		const size_t needBytes = static_cast<size_t>(pointCount) * sizeof(Vec3);
		if (off + needBytes != in.size())
			return MonsterMoveDecodeResult::PointCountMismatch;

		out.entityGuid = guid;
		out.splineId   = splineId;
		out.flags      = static_cast<MoveSplineFlag>(flags);
		out.velocity   = velocity;
		out.points.clear();
		out.points.resize(pointCount);
		for (uint16_t i = 0; i < pointCount; ++i)
		{
			Read(in, off, out.points[i].x);
			Read(in, off, out.points[i].y);
			Read(in, off, out.points[i].z);
		}
		return MonsterMoveDecodeResult::OK;
	}

	MonsterMovePayload BuildPayloadFromSpline(uint64_t entityGuid,
		const MoveSpline& spline)
	{
		MonsterMovePayload p;
		p.entityGuid = entityGuid;
		p.splineId   = spline.SplineId();
		p.flags      = spline.Flags();
		p.velocity   = spline.Velocity();
		p.points     = spline.Path().Points();
		return p;
	}
}
