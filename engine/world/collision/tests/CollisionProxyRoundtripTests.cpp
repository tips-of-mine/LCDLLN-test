// engine/world/collision/tests/CollisionProxyRoundtripTests.cpp
#include "engine/world/collision/CollisionProxy.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::collision::CollisionProxy;
	using engine::world::collision::ProxyType;
	using engine::math::Vec3;

	bool ApproxEq(float a, float b, float eps = 1e-5f)
	{
		return std::fabs(a - b) <= eps;
	}

	bool VecEq(const Vec3& a, const Vec3& b, float eps = 1e-5f)
	{
		return ApproxEq(a.x, b.x, eps) && ApproxEq(a.y, b.y, eps) && ApproxEq(a.z, b.z, eps);
	}

	void Test_Roundtrip_Capsule()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_capsule.bin";
		CollisionProxy src;
		src.type = ProxyType::Capsule;
		src.capsuleA = Vec3{ 1.0f, -2.0f, 3.0f };
		src.capsuleB = Vec3{ 4.0f,  5.0f, -6.0f };
		src.capsuleRadius = 0.75f;

		std::string err;
		REQUIRE(src.SaveToFile(path, err));
		REQUIRE(err.empty());

		CollisionProxy dst;
		REQUIRE(dst.LoadFromFile(path, err));
		REQUIRE(dst.type == ProxyType::Capsule);
		REQUIRE(VecEq(dst.capsuleA, src.capsuleA));
		REQUIRE(VecEq(dst.capsuleB, src.capsuleB));
		REQUIRE(ApproxEq(dst.capsuleRadius, src.capsuleRadius));

		std::filesystem::remove(path);
	}

	void Test_Roundtrip_ConvexHull()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_hull.bin";
		CollisionProxy src;
		src.type = ProxyType::ConvexHull;
		src.vertices = {
			{-1, -1, -1}, { 1, -1, -1}, {-1,  1, -1}, { 1,  1, -1},
			{-1, -1,  1}, { 1, -1,  1}, {-1,  1,  1}, { 1,  1,  1},
		};

		std::string err;
		REQUIRE(src.SaveToFile(path, err));

		CollisionProxy dst;
		REQUIRE(dst.LoadFromFile(path, err));
		REQUIRE(dst.type == ProxyType::ConvexHull);
		REQUIRE(dst.vertices.size() == 8);
		REQUIRE(std::memcmp(dst.vertices.data(), src.vertices.data(),
			src.vertices.size() * sizeof(Vec3)) == 0);

		std::filesystem::remove(path);
	}

	void Test_Roundtrip_TriMesh()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_trimesh.bin";
		CollisionProxy src;
		src.type = ProxyType::TriMesh;
		src.vertices = {
			{-1, -1, -1}, { 1, -1, -1}, {-1,  1, -1}, { 1,  1, -1},
		};
		src.indices = { 0, 1, 2,  1, 3, 2,  0, 2, 3,  0, 3, 1 };

		std::string err;
		REQUIRE(src.SaveToFile(path, err));

		CollisionProxy dst;
		REQUIRE(dst.LoadFromFile(path, err));
		REQUIRE(dst.type == ProxyType::TriMesh);
		REQUIRE(dst.vertices.size() == 4);
		REQUIRE(dst.indices.size() == 12);
		REQUIRE(std::memcmp(dst.vertices.data(), src.vertices.data(),
			src.vertices.size() * sizeof(Vec3)) == 0);
		REQUIRE(std::memcmp(dst.indices.data(), src.indices.data(),
			src.indices.size() * sizeof(uint32_t)) == 0);

		std::filesystem::remove(path);
	}

	void Test_Load_BadMagic_Fails()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_bad_magic.bin";
		std::ofstream f(path, std::ios::binary);
		const uint32_t badMagic = 0xDEADBEEFu;
		const uint32_t headerRest[5] = { 1, 1, 1, 0, 0 };
		f.write(reinterpret_cast<const char*>(&badMagic), sizeof(badMagic));
		f.write(reinterpret_cast<const char*>(headerRest), sizeof(headerRest));
		const uint32_t pType = 0;
		f.write(reinterpret_cast<const char*>(&pType), sizeof(pType));
		const float capsuleData[7] = { 0, 0, 0, 0, 1, 0, 0.5f };
		f.write(reinterpret_cast<const char*>(capsuleData), sizeof(capsuleData));
		f.close();

		CollisionProxy dst;
		std::string err;
		REQUIRE(!dst.LoadFromFile(path, err));
		REQUIRE(err.find("magic") != std::string::npos);

		std::filesystem::remove(path);
	}

	void Test_Load_BadContentHash_Fails()
	{
		auto path = std::filesystem::temp_directory_path() / "test_proxy_bad_hash.bin";
		CollisionProxy src;
		src.type = ProxyType::Capsule;
		src.capsuleA = Vec3{ 0, 0, 0 };
		src.capsuleB = Vec3{ 0, 1, 0 };
		src.capsuleRadius = 0.5f;
		std::string err;
		REQUIRE(src.SaveToFile(path, err));

		std::ifstream in(path, std::ios::binary | std::ios::ate);
		const std::streamsize size = in.tellg();
		in.seekg(0);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		in.read(reinterpret_cast<char*>(bytes.data()), size);
		in.close();
		bytes[32] ^= 0xFFu;  // flip 1 byte du payload (offset 24+4 proxyType +4 capsuleA.x = 32)

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), size);
		out.close();

		CollisionProxy dst;
		REQUIRE(!dst.LoadFromFile(path, err));
		REQUIRE(err.find("contentHash") != std::string::npos);

		std::filesystem::remove(path);
	}
}

int main()
{
	Test_Roundtrip_Capsule();
	Test_Roundtrip_ConvexHull();
	Test_Roundtrip_TriMesh();
	Test_Load_BadMagic_Fails();
	Test_Load_BadContentHash_Fails();
	return g_failed;
}
