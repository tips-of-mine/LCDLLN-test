#pragma once

// M100.29 — Splines (routes/chemins) : structs + sérialisation `instances/splines.bin`.
// Header-only (partagé engine_core/zone_builder).

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "src/shared/math/Math.h"

namespace engine::world::spline
{
	enum class SplineType : uint32_t { Road = 0, Path = 1, Wall = 2, RiverRef = 3 };
	enum class SplineCurve : uint32_t { CatmullRom = 0, Bezier = 1 };

	struct SplineNode
	{
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		float widthMeters = 6.0f;
		float tangentMagnitude = 0.0f; // Bezier
	};

	struct Spline
	{
		SplineType type = SplineType::Road;
		SplineCurve curve = SplineCurve::CatmullRom;
		bool closed = false;
		std::vector<SplineNode> nodes;
		uint32_t splatLayerIndex = 0;
		float splatStrength = 1.0f;
		float splatFeatherMeters = 0.5f;
	};

	constexpr uint32_t kSplinesMagic = 0x4C50534Eu; // "NSPL"
	constexpr uint32_t kSplinesVersion = 1u;

	namespace detail
	{
		inline void PutU32(std::vector<uint8_t>& b, uint32_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v>>8)); b.push_back(uint8_t(v>>16)); b.push_back(uint8_t(v>>24)); }
		inline void PutU64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8*i))); }
		inline void PutF32(std::vector<uint8_t>& b, float f) { uint32_t u; std::memcpy(&u, &f, 4); PutU32(b, u); }
		inline bool GetU32(std::span<const uint8_t> b, size_t& p, uint32_t& o) { if (p+4>b.size()) return false; o = uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24); p+=4; return true; }
		inline bool GetU64(std::span<const uint8_t> b, size_t& p, uint64_t& o) { if (p+8>b.size()) return false; o=0; for (int i=0;i<8;++i) o|=uint64_t(b[p+i])<<(8*i); p+=8; return true; }
		inline bool GetF32(std::span<const uint8_t> b, size_t& p, float& o) { uint32_t u; if (!GetU32(b,p,u)) return false; std::memcpy(&o,&u,4); return true; }
	}

	inline std::vector<uint8_t> SaveSplinesBin(const std::vector<Spline>& splines)
	{
		std::vector<uint8_t> b;
		detail::PutU32(b, kSplinesMagic); detail::PutU32(b, kSplinesVersion);
		detail::PutU32(b, 1u); detail::PutU32(b, 1u); detail::PutU64(b, 0ull);
		detail::PutU32(b, static_cast<uint32_t>(splines.size()));
		for (const Spline& s : splines)
		{
			detail::PutU32(b, static_cast<uint32_t>(s.type));
			detail::PutU32(b, static_cast<uint32_t>(s.curve));
			detail::PutU32(b, s.closed ? 1u : 0u);
			detail::PutU32(b, static_cast<uint32_t>(s.nodes.size()));
			for (const SplineNode& n : s.nodes)
			{
				detail::PutF32(b, n.position.x); detail::PutF32(b, n.position.y); detail::PutF32(b, n.position.z);
				detail::PutF32(b, n.widthMeters);
				detail::PutF32(b, n.tangentMagnitude);
			}
			detail::PutU32(b, s.splatLayerIndex);
			detail::PutF32(b, s.splatStrength);
			detail::PutF32(b, s.splatFeatherMeters);
		}
		return b;
	}

	inline bool LoadSplinesBin(std::span<const uint8_t> bytes, std::vector<Spline>& out, std::string& err)
	{
		size_t p = 0; uint32_t magic = 0, version = 0, builder = 0, engine = 0; uint64_t hash = 0;
		if (!detail::GetU32(bytes, p, magic) || magic != kSplinesMagic) { err = "splines.bin: magic invalide"; return false; }
		if (!detail::GetU32(bytes, p, version) || version > kSplinesVersion) { err = "splines.bin: version non supportee"; return false; }
		detail::GetU32(bytes, p, builder); detail::GetU32(bytes, p, engine); detail::GetU64(bytes, p, hash);
		uint32_t count = 0; if (!detail::GetU32(bytes, p, count)) { err = "splines.bin: compteur manquant"; return false; }
		out.clear(); out.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			Spline s; uint32_t t = 0, c = 0, closed = 0, nc = 0;
			if (!detail::GetU32(bytes, p, t)) { err = "splines.bin: tronque"; return false; }
			detail::GetU32(bytes, p, c); detail::GetU32(bytes, p, closed);
			s.type = static_cast<SplineType>(t); s.curve = static_cast<SplineCurve>(c); s.closed = (closed != 0);
			if (!detail::GetU32(bytes, p, nc)) { err = "splines.bin: node count"; return false; }
			s.nodes.reserve(nc);
			for (uint32_t k = 0; k < nc; ++k)
			{
				SplineNode n;
				detail::GetF32(bytes, p, n.position.x); detail::GetF32(bytes, p, n.position.y); detail::GetF32(bytes, p, n.position.z);
				detail::GetF32(bytes, p, n.widthMeters);
				if (!detail::GetF32(bytes, p, n.tangentMagnitude)) { err = "splines.bin: node tronque"; return false; }
				s.nodes.push_back(n);
			}
			detail::GetU32(bytes, p, s.splatLayerIndex);
			detail::GetF32(bytes, p, s.splatStrength);
			if (!detail::GetF32(bytes, p, s.splatFeatherMeters)) { err = "splines.bin: splat tronque"; return false; }
			out.push_back(std::move(s));
		}
		return true;
	}
}
