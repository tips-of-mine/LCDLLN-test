// ETAPE3.3 (chantier 4.1) — règles de mouvement partagées client / shard.
// Voir MovementRules.h pour le principe.

#include "src/shared/movement/MovementRules.h"

#include <algorithm>
#include <cmath>

namespace engine::movement
{
	namespace
	{
		bool IsValidCoordinate(float v, float maxAbs)
		{
			return std::isfinite(v) && std::fabs(v) <= maxAbs;
		}

		/// Longueur du chevauchement de [a0, a1) et [b0, b1) (ms).
		uint64_t Overlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
		{
			const uint64_t lo = (std::max)(a0, b0);
			const uint64_t hi = (std::min)(a1, b1);
			return hi > lo ? hi - lo : 0u;
		}
	}

	void ResetMovement(MovementState& state, float x, float y, float z)
	{
		const uint32_t keptSequence = state.lastSequence;
		state = MovementState{};
		state.x = x;
		state.y = y;
		state.z = z;
		state.lastSequence = keptSequence; // un paquet ancien reste refusé après un téléport
	}

	MoveResult ApplyMoveCommand(MovementState& state, const MoveCommand& command, float maxSpeed,
		uint64_t nowWallMs, const MovementLimits& limits)
	{
		MoveResult result{};
		if (command.sequence <= state.lastSequence)
			return result; // Stale : en retard, en double ou rejouée
		if (state.started && command.clientTimeMs < state.lastClientTimeMs)
			return result; // Stale : horloge client qui recule

		// --- Budget de temps --------------------------------------------------
		if (!state.started)
		{
			const uint32_t first = (std::min)(limits.firstCommandDurationMs, command.clientTimeMs);
			state.budgetStartWallMs = nowWallMs >= first ? nowWallMs - first : 0u;
			state.budgetStartClientMs = static_cast<uint64_t>(command.clientTimeMs) - first;
			state.lastClientTimeMs = command.clientTimeMs - first;
		}
		const uint64_t elapsedWall = nowWallMs >= state.budgetStartWallMs ? nowWallMs - state.budgetStartWallMs : 0u;
		const uint64_t lastProgress = static_cast<uint64_t>(state.lastClientTimeMs) - state.budgetStartClientMs;
		// Temps non simulé gardé au plus maxBankedTimeMs : un client ne peut pas rester
		// immobile une minute pour courir ensuite à toute vitesse.
		if (elapsedWall > lastProgress + limits.maxBankedTimeMs)
		{
			const uint64_t forget = elapsedWall - lastProgress - limits.maxBankedTimeMs;
			state.budgetStartWallMs += forget;
		}
		const uint64_t elapsedAfter = nowWallMs >= state.budgetStartWallMs ? nowWallMs - state.budgetStartWallMs : 0u;
		const uint64_t allowedProgress = static_cast<uint64_t>(static_cast<double>(elapsedAfter) * limits.clockRateTolerance)
			+ limits.timeBudgetSlackMs;
		uint64_t effectiveClientMs = command.clientTimeMs;
		if (effectiveClientMs - state.budgetStartClientMs > allowedProgress)
			effectiveClientMs = state.budgetStartClientMs + allowedProgress; // horloge client trop rapide
		if (effectiveClientMs < state.lastClientTimeMs)
			effectiveClientMs = state.lastClientTimeMs;
		const uint64_t gap = (std::min)(effectiveClientMs - state.lastClientTimeMs,
			static_cast<uint64_t>(limits.maxCommandGapMs));
		const uint64_t periodStart = state.lastClientTimeMs;
		const uint64_t periodEnd = periodStart + gap;
		const float dt = static_cast<float>(gap) / 1000.0f;

		// --- Saut et esquive déclarés (instants, robustes à la perte d'une commande) --
		if (command.lastJumpMs != 0u && command.lastJumpMs > state.lastJumpClientMs
			&& command.lastJumpMs <= command.clientTimeMs
			&& (state.lastJumpClientMs == 0u || command.lastJumpMs - state.lastJumpClientMs >= limits.jumpCooldownMs))
		{
			state.lastJumpClientMs = command.lastJumpMs;
			state.jumpRiseLeft = limits.jumpApexMeters;
		}
		if (state.lastJumpClientMs != 0u && periodEnd > static_cast<uint64_t>(state.lastJumpClientMs) + 1000u)
			state.jumpRiseLeft = 0.0f; // un saut non utilisé ne sert plus une seconde après
		if (command.lastDodgeMs != 0u && command.lastDodgeMs > state.dodgeStartClientMs
			&& command.lastDodgeMs <= command.clientTimeMs
			&& (state.dodgeStartClientMs == 0u || command.lastDodgeMs - state.dodgeStartClientMs >= limits.dodgeCooldownMs))
		{
			state.dodgeStartClientMs = command.lastDodgeMs;
		}
		// Marges fixes (distance, marche, chute) au prorata du temps couvert : une rafale
		// de commandes sans temps écoulé (horloge figée) n'en gagne aucune.
		const float slackFactor = limits.slackPeriodMs == 0u ? 1.0f
			: (std::min)(static_cast<float>(gap) / static_cast<float>(limits.slackPeriodMs), 1.0f);
		const uint64_t boostMs = state.dodgeStartClientMs == 0u ? 0u
			: Overlap(periodStart, periodEnd, state.dodgeStartClientMs,
				static_cast<uint64_t>(state.dodgeStartClientMs) + limits.dodgeDurationMs);

		const float speed = (std::max)(maxSpeed, 0.0f);
		const float boostSpeed = (std::max)(speed, limits.dodgeSpeedMetersPerSecond);
		const float maxDistance = (speed * static_cast<float>(gap - boostMs) / 1000.0f
			+ boostSpeed * static_cast<float>(boostMs) / 1000.0f) * limits.speedTolerance + limits.distanceSlackMeters * slackFactor;
		result.maxDistanceMeters = maxDistance;

		const float oldX = state.x;
		const float oldY = state.y;
		const float oldZ = state.z;
		float newX = command.x;
		float newY = command.y;
		float newZ = command.z;
		bool corrected = false;

		if (!IsValidCoordinate(newX, limits.maxCoordinateMeters) || !IsValidCoordinate(newY, limits.maxCoordinateMeters)
			|| !IsValidCoordinate(newZ, limits.maxCoordinateMeters))
		{
			newX = oldX;
			newY = oldY;
			newZ = oldZ;
			corrected = true;
		}
		else
		{
			// Horizontal : ramené sur le cercle atteignable, dans la direction demandée.
			const float dx = newX - oldX;
			const float dz = newZ - oldZ;
			const float dist = std::sqrt(dx * dx + dz * dz);
			float acceptedHorizontal = dist;
			if (dist > maxDistance)
			{
				const float k = dist > 0.0f ? maxDistance / dist : 0.0f;
				newX = oldX + dx * k;
				newZ = oldZ + dz * k;
				acceptedHorizontal = maxDistance;
				corrected = true;
			}
			// Vertical : pente, marche, nage, puis le saut en réserve.
			const float dy = newY - oldY;
			const float freeRise = acceptedHorizontal * limits.maxSlopeRise + limits.stepHeightMeters * slackFactor
				+ limits.swimRiseMetersPerSecond * dt;
			if (dy > freeRise)
			{
				const float extra = dy - freeRise;
				const float fromJump = (std::min)(extra, state.jumpRiseLeft);
				state.jumpRiseLeft -= fromJump;
				if (extra > fromJump)
				{
					newY = oldY + freeRise + fromJump;
					result.verticalClamped = true; // freinée, sans recaler le client
				}
			}
			const float maxFall = limits.maxFallMetersPerSecond * dt + limits.fallSlackMeters * slackFactor;
			if (dy < -maxFall)
			{
				newY = oldY - maxFall;
				result.verticalClamped = true;
			}
		}

		state.x = newX;
		state.y = newY;
		state.z = newZ;
		state.lastSequence = command.sequence;
		state.lastClientTimeMs = static_cast<uint32_t>(periodEnd);
		state.started = true;

		result.verdict = corrected ? MoveVerdict::Corrected : MoveVerdict::Accepted;
		result.acceptedDurationMs = static_cast<uint32_t>(gap);
		if (dt > 0.0f)
		{
			result.velocityX = (newX - oldX) / dt;
			result.velocityY = (newY - oldY) / dt;
			result.velocityZ = (newZ - oldZ) / dt;
		}
		return result;
	}
}
