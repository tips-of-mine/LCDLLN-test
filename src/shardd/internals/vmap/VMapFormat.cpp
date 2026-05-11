#include "src/shardd/internals/vmap/VMapFormat.h"

#include <cstring>

namespace engine::server::shard::vmap
{
	namespace
	{
		/// Append little-endian raw bytes — on suppose un host
		/// little-endian (cas pratique : x86_64 + ARM64). Si on porte
		/// jamais sur un host big-endian, ce helper devient un swap
		/// explicite, mais ce n'est pas notre cas serveur (Linux x86_64).
		template<typename T>
		void Append(std::vector<uint8_t>& out, const T& v)
		{
			static_assert(std::is_trivially_copyable_v<T>,
				"Append needs a trivially-copyable type");
			const auto* p = reinterpret_cast<const uint8_t*>(&v);
			out.insert(out.end(), p, p + sizeof(T));
		}

		/// Lecture little-endian. Retourne false si pas assez de bytes.
		template<typename T>
		bool Read(std::span<const uint8_t> in, size_t& off, T& out)
		{
			static_assert(std::is_trivially_copyable_v<T>,
				"Read needs a trivially-copyable type");
			if (off + sizeof(T) > in.size())
				return false;
			std::memcpy(&out, in.data() + off, sizeof(T));
			off += sizeof(T);
			return true;
		}
	}

	std::vector<uint8_t> EncodeVMapTile(const VMapTile& tile)
	{
		std::vector<uint8_t> out;
		// Header : 6 floats + 4 uint32 = 24 + 16 = 40 bytes
		// Data : vertices*12 + triangles*12
		const size_t expected = 40
			+ tile.vertices.size() * sizeof(engine::math::Vec3)
			+ tile.triangles.size() * sizeof(VMapTri);
		out.reserve(expected);

		Append(out, kVMapMagic);
		Append(out, kVMapVersion);
		Append(out, tile.bbox.min.x);
		Append(out, tile.bbox.min.y);
		Append(out, tile.bbox.min.z);
		Append(out, tile.bbox.max.x);
		Append(out, tile.bbox.max.y);
		Append(out, tile.bbox.max.z);
		const auto nv = static_cast<uint32_t>(tile.vertices.size());
		const auto nt = static_cast<uint32_t>(tile.triangles.size());
		Append(out, nv);
		Append(out, nt);

		for (const auto& v : tile.vertices)
		{
			Append(out, v.x);
			Append(out, v.y);
			Append(out, v.z);
		}
		for (const auto& t : tile.triangles)
		{
			Append(out, t.a);
			Append(out, t.b);
			Append(out, t.c);
		}
		return out;
	}

	VMapDecodeResult DecodeVMapTile(std::span<const uint8_t> in, VMapTile& out,
		uint32_t* outErrVersion)
	{
		size_t off = 0;
		uint32_t magic = 0;
		if (!Read(in, off, magic))
			return VMapDecodeResult::BufferTooSmall;
		if (magic != kVMapMagic)
			return VMapDecodeResult::BadMagic;

		uint32_t version = 0;
		if (!Read(in, off, version))
			return VMapDecodeResult::BufferTooSmall;
		if (outErrVersion)
			*outErrVersion = version;
		if (version != kVMapVersion)
			return VMapDecodeResult::WrongVersion;

		float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
		if (!Read(in, off, minX) || !Read(in, off, minY) || !Read(in, off, minZ)
			|| !Read(in, off, maxX) || !Read(in, off, maxY) || !Read(in, off, maxZ))
			return VMapDecodeResult::BufferTooSmall;

		uint32_t nv = 0, nt = 0;
		if (!Read(in, off, nv) || !Read(in, off, nt))
			return VMapDecodeResult::BufferTooSmall;

		// Vérifier qu'on a assez de bytes pour vertices + triangles
		// AVANT de réserver — protection contre un fichier corrompu
		// qui annoncerait des nombres énormes.
		const size_t needVerts = static_cast<size_t>(nv) * sizeof(engine::math::Vec3);
		const size_t needTris  = static_cast<size_t>(nt) * sizeof(VMapTri);
		if (off + needVerts + needTris > in.size())
			return VMapDecodeResult::BufferTooSmall;

		out.bbox.min = engine::math::Vec3(minX, minY, minZ);
		out.bbox.max = engine::math::Vec3(maxX, maxY, maxZ);
		out.vertices.resize(nv);
		out.triangles.resize(nt);

		for (uint32_t i = 0; i < nv; ++i)
		{
			Read(in, off, out.vertices[i].x);
			Read(in, off, out.vertices[i].y);
			Read(in, off, out.vertices[i].z);
		}
		for (uint32_t i = 0; i < nt; ++i)
		{
			VMapTri t{};
			Read(in, off, t.a);
			Read(in, off, t.b);
			Read(in, off, t.c);
			// Validation : indices < vertexCount.
			if (t.a >= nv || t.b >= nv || t.c >= nv)
				return VMapDecodeResult::IndexOutOfRange;
			out.triangles[i] = t;
		}
		return VMapDecodeResult::OK;
	}
}
