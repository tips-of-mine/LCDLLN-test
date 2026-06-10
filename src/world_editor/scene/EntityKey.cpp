#include "src/world_editor/scene/EntityKey.h"

namespace engine::editor::scene
{
	namespace
	{
		constexpr uint64_t kFnvOffset = 1469598103934665603ull;
		constexpr uint64_t kFnvPrime  = 1099511628211ull;

		uint64_t Fnv1a64(uint64_t seed, const unsigned char* data, size_t n)
		{
			uint64_t h = seed;
			for (size_t i = 0; i < n; ++i) { h ^= data[i]; h *= kFnvPrime; }
			return h;
		}
	}

	uint64_t MakeEntityKeyFromString(EntityKind kind, std::string_view guid)
	{
		uint64_t h = kFnvOffset;
		const unsigned char k = static_cast<unsigned char>(kind);
		h = Fnv1a64(h, &k, 1);
		h = Fnv1a64(h, reinterpret_cast<const unsigned char*>(guid.data()), guid.size());
		return h | 1ull; // jamais 0 (0 = non assigné côté LayersDocument)
	}

	uint64_t MakeEntityKeyFromGuid(EntityKind kind, uint64_t guid)
	{
		unsigned char buf[9];
		buf[0] = static_cast<unsigned char>(kind);
		for (int i = 0; i < 8; ++i) buf[1 + i] = static_cast<unsigned char>((guid >> (i * 8)) & 0xFF);
		return Fnv1a64(kFnvOffset, buf, sizeof(buf)) | 1ull;
	}
}
