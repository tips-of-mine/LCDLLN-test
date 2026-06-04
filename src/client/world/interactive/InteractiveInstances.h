#pragma once

// M100.32 — Sérialisation `instances/interactives.bin`.
//
// Sérialisation HEADER-ONLY (inline) : partagée engine_core (runtime/éditeur)
// et zone_builder_lib (writer offline) sans dupliquer de symboles. En-tête
// 24 octets compatible `engine::world::OutputVersionHeader`. Round-trip parfait
// (parité éditeur ↔ client). Fichier zone-level (pas par chunk).
//
// Layout (cf. ticket M100.32) :
//   [0..3]   uint32 magic ("INCT")
//   [4..7]   uint32 version
//   [8..11]  uint32 builderVersion
//   [12..15] uint32 engineVersion
//   [16..23] uint64 contentHash (0 ici, réservé)
//   [24..27] uint32 instanceCount
//   par instance : id(u64) type(u16) position(3xf32) rotationY(f32)
//                  meshAssetId(u32) pivotLocal(3xf32) axisLocal(3xf32)
//                  openAngleDeg(f32) animDurationSec(f32) initialState(u8)
//                  audioOpenEvent(u32 len + bytes) audioCloseEvent(u32 len + bytes)

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "src/client/world/interactive/InteractiveTypes.h"

namespace engine::world::interactive
{
	namespace detail
	{
		inline void PutU16(std::vector<uint8_t>& b, uint16_t v)
		{
			b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
		}
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
		inline void PutStr(std::vector<uint8_t>& b, const std::string& s)
		{
			PutU32(b, static_cast<uint32_t>(s.size()));
			for (char c : s) b.push_back(static_cast<uint8_t>(c));
		}
		inline bool GetU16(std::span<const uint8_t> b, size_t& p, uint16_t& out)
		{
			if (p + 2 > b.size()) return false;
			out = uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
			p += 2; return true;
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
		inline bool GetStr(std::span<const uint8_t> b, size_t& p, std::string& out)
		{
			uint32_t len = 0;
			if (!GetU32(b, p, len)) return false;
			if (p + len > b.size()) return false;
			out.assign(reinterpret_cast<const char*>(b.data() + p), len);
			p += len; return true;
		}
	}

	/// Sérialise une liste d'instances interactives dans le format
	/// `instances/interactives.bin`. Round-trip exact avec `LoadInteractivesBin`.
	inline std::vector<uint8_t> SaveInteractivesBin(const std::vector<InteractivePropInstance>& items)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kInteractivesMagic);
		detail::PutU32(b, kInteractivesVersion);
		detail::PutU32(b, kInteractivesBuilderVersion);
		detail::PutU32(b, kInteractivesEngineVersion);
		detail::PutU64(b, 0ull);
		detail::PutU32(b, static_cast<uint32_t>(items.size()));
		for (const InteractivePropInstance& it : items)
		{
			detail::PutU64(b, it.id);
			detail::PutU16(b, static_cast<uint16_t>(it.type));
			detail::PutF32(b, it.position.x); detail::PutF32(b, it.position.y); detail::PutF32(b, it.position.z);
			detail::PutF32(b, it.rotationY);
			detail::PutU32(b, it.meshAssetId);
			detail::PutF32(b, it.pivotLocal.x); detail::PutF32(b, it.pivotLocal.y); detail::PutF32(b, it.pivotLocal.z);
			detail::PutF32(b, it.axisLocal.x); detail::PutF32(b, it.axisLocal.y); detail::PutF32(b, it.axisLocal.z);
			detail::PutF32(b, it.openAngleDeg);
			detail::PutF32(b, it.animDurationSec);
			b.push_back(it.initialState);
			detail::PutStr(b, it.audioOpenEvent);
			detail::PutStr(b, it.audioCloseEvent);
		}
		return b;
	}

	/// Désérialise `instances/interactives.bin`. Retourne false + `err` rempli
	/// si magic invalide, version non supportée ou buffer tronqué.
	inline bool LoadInteractivesBin(std::span<const uint8_t> bytes,
		std::vector<InteractivePropInstance>& out, std::string& err)
	{
		size_t p = 0;
		uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kInteractivesMagic) { err = "interactives.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kInteractivesVersion) { err = "interactives.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder);
		detail::GetU32(bytes, p, engine);
		detail::GetU64(bytes, p, hash);
		uint32_t count = 0;
		if (!detail::GetU32(bytes, p, count)) { err = "interactives.bin: compteur manquant"; return false; }
		out.clear(); out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			InteractivePropInstance it;
			uint16_t typeRaw = 0;
			if (!detail::GetU64(bytes, p, it.id)) { err = "interactives.bin: tronque (id)"; return false; }
			if (!detail::GetU16(bytes, p, typeRaw)) { err = "interactives.bin: tronque (type)"; return false; }
			it.type = static_cast<InteractiveType>(typeRaw);
			detail::GetF32(bytes, p, it.position.x); detail::GetF32(bytes, p, it.position.y); detail::GetF32(bytes, p, it.position.z);
			detail::GetF32(bytes, p, it.rotationY);
			detail::GetU32(bytes, p, it.meshAssetId);
			detail::GetF32(bytes, p, it.pivotLocal.x); detail::GetF32(bytes, p, it.pivotLocal.y); detail::GetF32(bytes, p, it.pivotLocal.z);
			detail::GetF32(bytes, p, it.axisLocal.x); detail::GetF32(bytes, p, it.axisLocal.y); detail::GetF32(bytes, p, it.axisLocal.z);
			detail::GetF32(bytes, p, it.openAngleDeg);
			detail::GetF32(bytes, p, it.animDurationSec);
			if (p + 1 > bytes.size()) { err = "interactives.bin: tronque (initialState)"; return false; }
			it.initialState = bytes[p]; p += 1;
			if (!detail::GetStr(bytes, p, it.audioOpenEvent)) { err = "interactives.bin: tronque (audioOpen)"; return false; }
			if (!detail::GetStr(bytes, p, it.audioCloseEvent)) { err = "interactives.bin: tronque (audioClose)"; return false; }
			out.push_back(std::move(it));
		}
		return true;
	}
}
