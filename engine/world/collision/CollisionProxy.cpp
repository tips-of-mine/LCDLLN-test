// engine/world/collision/CollisionProxy.cpp
#include "engine/world/collision/CollisionProxy.h"

#include "engine/world/OutputVersion.h"

#include <cstring>
#include <fstream>

namespace engine::world::collision
{
	namespace
	{
		uint8_t* Write32(uint8_t* dst, uint32_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}
		uint8_t* Write64(uint8_t* dst, uint64_t v)
		{
			std::memcpy(dst, &v, sizeof(v));
			return dst + sizeof(v);
		}
		uint8_t* WriteVec3(uint8_t* dst, const engine::math::Vec3& v)
		{
			std::memcpy(dst,     &v.x, 4);
			std::memcpy(dst + 4, &v.y, 4);
			std::memcpy(dst + 8, &v.z, 4);
			return dst + 12;
		}

		size_t ComputePayloadSize(const CollisionProxy& proxy)
		{
			size_t s = 4u;  // proxyType
			switch (proxy.type)
			{
				case ProxyType::Capsule:
					s += 12u + 12u + 4u;
					break;
				case ProxyType::ConvexHull:
					s += 4u + proxy.vertices.size() * 12u;
					break;
				case ProxyType::TriMesh:
					s += 4u + 4u + proxy.vertices.size() * 12u + proxy.indices.size() * 4u;
					break;
			}
			return s;
		}
	}

	bool CollisionProxy::SaveToFile(const std::filesystem::path& path, std::string& outError) const
	{
		const size_t headerSize = 24u;
		const size_t payloadSize = ComputePayloadSize(*this);
		const size_t totalSize = headerSize + payloadSize;

		std::vector<uint8_t> bytes(totalSize, 0u);

		uint8_t* p = bytes.data() + headerSize;
		p = Write32(p, static_cast<uint32_t>(type));

		switch (type)
		{
			case ProxyType::Capsule:
				p = WriteVec3(p, capsuleA);
				p = WriteVec3(p, capsuleB);
				std::memcpy(p, &capsuleRadius, 4);
				p += 4;
				break;
			case ProxyType::ConvexHull:
			{
				const uint32_t vertCount = static_cast<uint32_t>(vertices.size());
				p = Write32(p, vertCount);
				for (const auto& v : vertices)
					p = WriteVec3(p, v);
				break;
			}
			case ProxyType::TriMesh:
			{
				const uint32_t vertCount = static_cast<uint32_t>(vertices.size());
				const uint32_t idxCount  = static_cast<uint32_t>(indices.size());
				p = Write32(p, vertCount);
				p = Write32(p, idxCount);
				for (const auto& v : vertices)
					p = WriteVec3(p, v);
				if (idxCount > 0)
				{
					std::memcpy(p, indices.data(), idxCount * 4u);
					p += idxCount * 4u;
				}
				break;
			}
		}

		std::span<const uint8_t> payload(bytes.data() + headerSize, totalSize - headerSize);
		const uint64_t contentHash = engine::world::ComputeXxHash64(payload);

		uint8_t* h = bytes.data();
		h = Write32(h, kCollisionMagic);
		h = Write32(h, kCollisionVersion);
		h = Write32(h, engine::world::kZoneBuilderVersion);
		h = Write32(h, engine::world::kZoneEngineVersion);
		h = Write64(h, contentHash);
		(void)h;

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f.good())
		{
			outError = "CollisionProxy: cannot open " + path.string() + " for write";
			return false;
		}
		f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!f.good())
		{
			outError = "CollisionProxy: write failed for " + path.string();
			return false;
		}
		return true;
	}

	bool CollisionProxy::LoadFromFile(const std::filesystem::path& path, std::string& outError)
	{
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f.good())
		{
			outError = "CollisionProxy: cannot open " + path.string();
			return false;
		}
		const std::streamsize size = f.tellg();
		f.seekg(0);
		if (size < 24 + 4)
		{
			outError = "CollisionProxy: file too small";
			return false;
		}
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		f.read(reinterpret_cast<char*>(bytes.data()), size);
		if (!f.good() && !f.eof())
		{
			outError = "CollisionProxy: read failed";
			return false;
		}

		engine::world::OutputVersionHeader hdr;
		if (!engine::world::ReadOutputVersionHeader(bytes, hdr, outError))
			return false;

		if (hdr.magic != kCollisionMagic)
		{
			outError = "CollisionProxy: bad magic (expected COLL)";
			return false;
		}
		if (hdr.formatVersion != kCollisionVersion)
		{
			outError = "CollisionProxy: bad version";
			return false;
		}

		std::span<const uint8_t> payload(bytes.data() + 24, bytes.size() - 24);
		if (engine::world::ComputeXxHash64(payload) != hdr.contentHash)
		{
			outError = "CollisionProxy: contentHash mismatch (file corrupted)";
			return false;
		}

		const uint8_t* p = bytes.data() + 24;
		const uint8_t* end = bytes.data() + bytes.size();
		uint32_t pTypeRaw = 0;
		std::memcpy(&pTypeRaw, p, 4); p += 4;
		if (pTypeRaw > 2u)
		{
			outError = "CollisionProxy: invalid proxyType " + std::to_string(pTypeRaw);
			return false;
		}
		type = static_cast<ProxyType>(pTypeRaw);

		vertices.clear();
		indices.clear();

		switch (type)
		{
			case ProxyType::Capsule:
				if (end - p < 28)
				{
					outError = "CollisionProxy: capsule payload truncated";
					return false;
				}
				std::memcpy(&capsuleA.x, p,      4);
				std::memcpy(&capsuleA.y, p + 4,  4);
				std::memcpy(&capsuleA.z, p + 8,  4);
				std::memcpy(&capsuleB.x, p + 12, 4);
				std::memcpy(&capsuleB.y, p + 16, 4);
				std::memcpy(&capsuleB.z, p + 20, 4);
				std::memcpy(&capsuleRadius, p + 24, 4);
				break;
			case ProxyType::ConvexHull:
			{
				if (end - p < 4)
				{
					outError = "CollisionProxy: hull header truncated";
					return false;
				}
				uint32_t vertCount = 0;
				std::memcpy(&vertCount, p, 4); p += 4;
				if (static_cast<size_t>(end - p) < vertCount * 12u)
				{
					outError = "CollisionProxy: hull vertices truncated";
					return false;
				}
				vertices.resize(vertCount);
				for (uint32_t i = 0; i < vertCount; ++i)
				{
					std::memcpy(&vertices[i].x, p,     4);
					std::memcpy(&vertices[i].y, p + 4, 4);
					std::memcpy(&vertices[i].z, p + 8, 4);
					p += 12;
				}
				break;
			}
			case ProxyType::TriMesh:
			{
				if (end - p < 8)
				{
					outError = "CollisionProxy: trimesh header truncated";
					return false;
				}
				uint32_t vertCount = 0, idxCount = 0;
				std::memcpy(&vertCount, p,     4);
				std::memcpy(&idxCount,  p + 4, 4);
				p += 8;
				if (static_cast<size_t>(end - p) < vertCount * 12u + idxCount * 4u)
				{
					outError = "CollisionProxy: trimesh payload truncated";
					return false;
				}
				vertices.resize(vertCount);
				for (uint32_t i = 0; i < vertCount; ++i)
				{
					std::memcpy(&vertices[i].x, p,     4);
					std::memcpy(&vertices[i].y, p + 4, 4);
					std::memcpy(&vertices[i].z, p + 8, 4);
					p += 12;
				}
				indices.resize(idxCount);
				if (idxCount > 0)
					std::memcpy(indices.data(), p, idxCount * 4u);
				break;
			}
		}
		return true;
	}
}
