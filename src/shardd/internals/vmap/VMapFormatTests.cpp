// CMANGOS.05 (Phase 2.05b) — Tests VMapFormat encode/decode round-trip.
// Pure : pas de DB, pas d'I/O disque.

#include "src/shardd/internals/vmap/VMapFormat.h"
#include "src/shared/core/Log.h"

#include <cstring>

namespace
{
	using engine::math::Vec3;
	using engine::server::shard::vmap::AABB;
	using engine::server::shard::vmap::DecodeVMapTile;
	using engine::server::shard::vmap::EncodeVMapTile;
	using engine::server::shard::vmap::kVMapMagic;
	using engine::server::shard::vmap::kVMapVersion;
	using engine::server::shard::vmap::VMapDecodeResult;
	using engine::server::shard::vmap::VMapTile;
	using engine::server::shard::vmap::VMapTri;

	bool ApproxEq(float a, float b, float eps = 1e-6f)
	{
		float d = a - b; if (d < 0) d = -d;
		return d <= eps;
	}

	bool TestRoundTripEmpty()
	{
		VMapTile t;
		t.bbox.min = Vec3(0, 0, 0);
		t.bbox.max = Vec3(0, 0, 0);
		const auto blob = EncodeVMapTile(t);
		// header = 4+4+24+8 = 40 bytes
		if (blob.size() != 40)
		{
			LOG_ERROR(Core, "[VMapFormatTests] empty tile : expected 40 bytes, got {}", blob.size());
			return false;
		}

		VMapTile decoded;
		const auto r = DecodeVMapTile(blob, decoded);
		if (r != VMapDecodeResult::OK) return false;
		if (!decoded.vertices.empty() || !decoded.triangles.empty()) return false;
		LOG_INFO(Core, "[VMapFormatTests] empty roundtrip OK");
		return true;
	}

	bool TestRoundTripCube()
	{
		VMapTile t;
		t.bbox.min = Vec3(-1, -1, -1);
		t.bbox.max = Vec3(1, 1, 1);
		// 8 vertices d'un cube unitaire, 12 triangles (2 par face × 6).
		t.vertices = {
			Vec3(-1, -1, -1), Vec3( 1, -1, -1), Vec3( 1,  1, -1), Vec3(-1,  1, -1),
			Vec3(-1, -1,  1), Vec3( 1, -1,  1), Vec3( 1,  1,  1), Vec3(-1,  1,  1),
		};
		t.triangles = {
			{0,1,2},{0,2,3}, {4,6,5},{4,7,6},
			{0,4,5},{0,5,1}, {1,5,6},{1,6,2},
			{2,6,7},{2,7,3}, {3,7,4},{3,4,0},
		};

		const auto blob = EncodeVMapTile(t);
		VMapTile decoded;
		const auto r = DecodeVMapTile(blob, decoded);
		if (r != VMapDecodeResult::OK)
		{
			LOG_ERROR(Core, "[VMapFormatTests] cube decode result={}", static_cast<int>(r));
			return false;
		}
		if (decoded.vertices.size() != 8 || decoded.triangles.size() != 12)
		{
			LOG_ERROR(Core, "[VMapFormatTests] cube counts wrong (V={} T={})",
				decoded.vertices.size(), decoded.triangles.size());
			return false;
		}
		// Spot check : le vertex 0 doit être (-1,-1,-1).
		if (!ApproxEq(decoded.vertices[0].x, -1) || !ApproxEq(decoded.vertices[0].y, -1)
			|| !ApproxEq(decoded.vertices[0].z, -1))
		{
			LOG_ERROR(Core, "[VMapFormatTests] cube vertex[0] wrong");
			return false;
		}
		// Spot check : triangle[3] = {4,7,6}.
		if (decoded.triangles[3].a != 4 || decoded.triangles[3].b != 7
			|| decoded.triangles[3].c != 6)
		{
			LOG_ERROR(Core, "[VMapFormatTests] cube triangle[3] wrong");
			return false;
		}
		// bbox preserved.
		if (!ApproxEq(decoded.bbox.min.x, -1) || !ApproxEq(decoded.bbox.max.z, 1)) return false;

		LOG_INFO(Core, "[VMapFormatTests] cube roundtrip OK ({} bytes)", blob.size());
		return true;
	}

	bool TestBufferTooSmall()
	{
		std::vector<uint8_t> tooShort(8, 0);  // header needs 40 bytes
		VMapTile decoded;
		const auto r = DecodeVMapTile(tooShort, decoded);
		if (r != VMapDecodeResult::BufferTooSmall)
		{
			LOG_ERROR(Core, "[VMapFormatTests] expected BufferTooSmall, got {}", static_cast<int>(r));
			return false;
		}
		LOG_INFO(Core, "[VMapFormatTests] BufferTooSmall OK");
		return true;
	}

	bool TestBadMagic()
	{
		std::vector<uint8_t> blob(40, 0);
		uint32_t bad = 0xDEADBEEF;
		std::memcpy(blob.data(), &bad, 4);
		VMapTile decoded;
		const auto r = DecodeVMapTile(blob, decoded);
		if (r != VMapDecodeResult::BadMagic)
		{
			LOG_ERROR(Core, "[VMapFormatTests] expected BadMagic, got {}", static_cast<int>(r));
			return false;
		}
		LOG_INFO(Core, "[VMapFormatTests] BadMagic OK");
		return true;
	}

	bool TestWrongVersion()
	{
		// Encode normal tile, puis bumper la version dans le buffer.
		VMapTile t;
		t.bbox.min = Vec3(-1, -1, -1);
		t.bbox.max = Vec3(1, 1, 1);
		auto blob = EncodeVMapTile(t);

		// Version = bytes [4..7].
		uint32_t fakeVersion = 999;
		std::memcpy(blob.data() + 4, &fakeVersion, 4);

		VMapTile decoded;
		uint32_t errVer = 0;
		const auto r = DecodeVMapTile(blob, decoded, &errVer);
		if (r != VMapDecodeResult::WrongVersion)
		{
			LOG_ERROR(Core, "[VMapFormatTests] expected WrongVersion, got {}", static_cast<int>(r));
			return false;
		}
		if (errVer != 999)
		{
			LOG_ERROR(Core, "[VMapFormatTests] outErrVersion wrong (got {})", errVer);
			return false;
		}
		LOG_INFO(Core, "[VMapFormatTests] WrongVersion OK");
		return true;
	}

	bool TestIndexOutOfRange()
	{
		// Tile avec 2 vertices mais triangle référencant index 5 → invalide.
		VMapTile t;
		t.bbox.min = Vec3(0, 0, 0);
		t.bbox.max = Vec3(1, 1, 1);
		t.vertices = { Vec3(0, 0, 0), Vec3(1, 0, 0) };
		t.triangles = { VMapTri{0, 1, 5} };
		auto blob = EncodeVMapTile(t);

		VMapTile decoded;
		const auto r = DecodeVMapTile(blob, decoded);
		if (r != VMapDecodeResult::IndexOutOfRange)
		{
			LOG_ERROR(Core, "[VMapFormatTests] expected IndexOutOfRange, got {}", static_cast<int>(r));
			return false;
		}
		LOG_INFO(Core, "[VMapFormatTests] IndexOutOfRange OK");
		return true;
	}

	bool TestMagicValue()
	{
		// Le magic = 'L','V','M','1' little-endian = 0x314D564C.
		// Vérifie que les caractères ASCII tombent au bon endroit.
		const uint8_t expected[4] = {'L', 'V', 'M', '1'};
		uint8_t actual[4];
		std::memcpy(actual, &kVMapMagic, 4);
		for (int i = 0; i < 4; ++i)
		{
			if (actual[i] != expected[i])
			{
				LOG_ERROR(Core, "[VMapFormatTests] magic byte[{}] wrong (got 0x{:x})", i, actual[i]);
				return false;
			}
		}
		LOG_INFO(Core, "[VMapFormatTests] magic ASCII = LVM1 OK");
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

	const bool ok = TestMagicValue()
		&& TestRoundTripEmpty()
		&& TestRoundTripCube()
		&& TestBufferTooSmall()
		&& TestBadMagic()
		&& TestWrongVersion()
		&& TestIndexOutOfRange();

	if (ok)
		LOG_INFO(Core, "[VMapFormatTests] ALL OK");
	else
		LOG_ERROR(Core, "[VMapFormatTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
