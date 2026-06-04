#pragma once

// M100.30 — Lookup des kits modulaires (pont/mur). Hash de nom stable (FNV-1a).
// Header-only.

#include <cstdint>
#include <string>

namespace engine::world::structures
{
	inline uint32_t HashKitName(const std::string& name)
	{
		uint32_t h = 2166136261u;
		for (unsigned char c : name) { h ^= c; h *= 16777619u; }
		return h;
	}
}
