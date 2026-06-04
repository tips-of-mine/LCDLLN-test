#pragma once

// M100.17 — Props instances : struct + sérialisation `instances/props.bin`.
//
// Sérialisation HEADER-ONLY (inline) : partagée engine_core (runtime/éditeur) et
// zone_builder_lib (writer offline) sans dupliquer de symboles. En-tête 24 octets
// compatible `engine::world::OutputVersionHeader`. Placement strictement éditeur
// (le client ne fait que lire/rendre).

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::instances
{
	enum class PlacementLayer : uint32_t
	{
		Default = 0, Rocks = 1, Trees = 2, Structures = 3, Props = 4
	};

	struct PropInstance
	{
		uint32_t assetId = 0;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		float rotationQuat[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // x,y,z,w
		engine::math::Vec3 scale{ 1.0f, 1.0f, 1.0f };
		uint32_t layerTag = 0;     // PlacementLayer
		uint32_t instanceId = 0;   // incrémental, unique zone
	};

	constexpr uint32_t kPropsMagic = 0x504F5250u; // "PROP"
	constexpr uint32_t kPropsVersion = 1u;
	constexpr uint32_t kPropsBuilderVersion = 1u;
	constexpr uint32_t kPropsEngineVersion = 1u;

	namespace detail
	{
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
			b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
		}
		inline void PutU64(std::vector<uint8_t>& b, uint64_t v)
		{
			for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i)));
		}
		inline void PutF32(std::vector<uint8_t>& b, float f)
		{
			uint32_t u; std::memcpy(&u, &f, 4); PutU32(b, u);
		}
		inline bool GetU32(std::span<const uint8_t> b, size_t& p, uint32_t& out)
		{
			if (p + 4 > b.size()) return false;
			out = uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) |
			      (uint32_t(b[p + 3]) << 24);
			p += 4; return true;
		}
		inline bool GetU64(std::span<const uint8_t> b, size_t& p, uint64_t& out)
		{
			if (p + 8 > b.size()) return false;
			out = 0; for (int i = 0; i < 8; ++i) out |= uint64_t(b[p + i]) << (8 * i);
			p += 8; return true;
		}
		inline bool GetF32(std::span<const uint8_t> b, size_t& p, float& out)
		{
			uint32_t u; if (!GetU32(b, p, u)) return false;
			std::memcpy(&out, &u, 4); return true;
		}
	}

	inline std::vector<uint8_t> SavePropsBin(const std::vector<PropInstance>& props)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kPropsMagic);
		detail::PutU32(b, kPropsVersion);
		detail::PutU32(b, kPropsBuilderVersion);
		detail::PutU32(b, kPropsEngineVersion);
		detail::PutU64(b, 0ull);
		detail::PutU32(b, static_cast<uint32_t>(props.size()));
		for (const PropInstance& p : props)
		{
			detail::PutU32(b, p.assetId);
			detail::PutF32(b, p.position.x); detail::PutF32(b, p.position.y); detail::PutF32(b, p.position.z);
			for (int i = 0; i < 4; ++i) detail::PutF32(b, p.rotationQuat[i]);
			detail::PutF32(b, p.scale.x); detail::PutF32(b, p.scale.y); detail::PutF32(b, p.scale.z);
			detail::PutU32(b, p.layerTag);
			detail::PutU32(b, p.instanceId);
		}
		return b;
	}

	inline bool LoadPropsBin(std::span<const uint8_t> bytes, std::vector<PropInstance>& out, std::string& err)
	{
		size_t p = 0;
		uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kPropsMagic) { err = "props.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kPropsVersion) { err = "props.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder);
		detail::GetU32(bytes, p, engine);
		detail::GetU64(bytes, p, hash);
		uint32_t count = 0;
		if (!detail::GetU32(bytes, p, count)) { err = "props.bin: compteur manquant"; return false; }
		out.clear(); out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			PropInstance pr;
			if (!detail::GetU32(bytes, p, pr.assetId)) { err = "props.bin: tronque"; return false; }
			detail::GetF32(bytes, p, pr.position.x); detail::GetF32(bytes, p, pr.position.y); detail::GetF32(bytes, p, pr.position.z);
			for (int k = 0; k < 4; ++k) detail::GetF32(bytes, p, pr.rotationQuat[k]);
			detail::GetF32(bytes, p, pr.scale.x); detail::GetF32(bytes, p, pr.scale.y); detail::GetF32(bytes, p, pr.scale.z);
			detail::GetU32(bytes, p, pr.layerTag);
			if (!detail::GetU32(bytes, p, pr.instanceId)) { err = "props.bin: tronque (instanceId)"; return false; }
			out.push_back(pr);
		}
		return true;
	}
}
