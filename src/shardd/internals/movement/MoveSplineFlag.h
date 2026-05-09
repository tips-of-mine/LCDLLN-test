#pragma once
// CMANGOS.04 (Phase 2.04b) — MoveSplineFlag : flags bitmask 32-bit
// pour decrire le mode de deplacement (Walk/Flying/Swimming/Falling/
// Cyclic, etc.). Aligne sur le pattern cmangos pour interoperabilite
// future, mais reduit aux flags utiles a LCDLLN.

#include <cstdint>

namespace engine::server::shard::movement
{
	/// Bitmask 32 bits. Plusieurs flags peuvent etre combines (ex.
	/// `Walking | Backward` pour reculer).
	enum class MoveSplineFlag : uint32_t
	{
		None       = 0u,

		// --- Locomotion mode (un seul a la fois normalement) ---
		Walking    = 1u << 0,   ///< Marche au sol (default).
		Flying     = 1u << 1,   ///< Vol — ignore la gravite.
		Swimming   = 1u << 2,   ///< Nage — gravite reduite.
		Falling    = 1u << 3,   ///< Chute libre — gravite + velocite initiale.

		// --- Modificateurs ---
		Backward   = 1u << 8,   ///< Recule (orientation inversee).
		Cyclic     = 1u << 9,   ///< Boucle a la fin du chemin.
		EnterCycle = 1u << 10,  ///< Premier passage avant boucle.
		Frozen     = 1u << 11,  ///< Pause — pas de progression mais conserve l'etat.

		// --- Hints client ---
		Catmullrom = 1u << 16,  ///< Interpolation lisse (sinon Linear).
		FacingTarget = 1u << 17, ///< Oriente vers une cible mobile.
	};

	constexpr MoveSplineFlag operator|(MoveSplineFlag a, MoveSplineFlag b) noexcept
	{
		return static_cast<MoveSplineFlag>(
			static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	constexpr MoveSplineFlag operator&(MoveSplineFlag a, MoveSplineFlag b) noexcept
	{
		return static_cast<MoveSplineFlag>(
			static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
	}
	constexpr MoveSplineFlag& operator|=(MoveSplineFlag& a, MoveSplineFlag b) noexcept
	{
		a = a | b; return a;
	}

	/// True si \p f contient au moins un des flags de \p mask.
	constexpr bool HasFlag(MoveSplineFlag f, MoveSplineFlag mask) noexcept
	{
		return (static_cast<uint32_t>(f) & static_cast<uint32_t>(mask)) != 0;
	}
}
