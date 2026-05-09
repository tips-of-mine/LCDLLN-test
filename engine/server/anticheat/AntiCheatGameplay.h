#pragma once
// CMANGOS.29 (Phase 5.29a) — AntiCheat gameplay : detection speed-hack
// + teleport hack via verification de la distance parcourue entre deux
// positions report et le temps ecoule. Header-only.

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace engine::server::anticheat
{
	using PlayerId = uint64_t;

	struct LastReportedPos
	{
		float    x = 0.0f, y = 0.0f, z = 0.0f;
		uint64_t tsMs = 0;
		bool     hasPrevious = false;
	};

	enum class CheatVerdict : uint8_t
	{
		OK            = 0,
		SpeedHack     = 1,    ///< distance / dt > maxSpeed * tolerance
		TeleportHack  = 2,    ///< distance > maxSingleStep
	};

	struct AntiCheatConfig
	{
		float    maxSpeedMps      = 7.5f;     ///< walk + run + mount typical
		float    speedTolerance   = 1.5f;     ///< 50%% lag headroom
		float    maxSingleStepM   = 50.0f;    ///< gros hop = teleport hack
	};

	class AntiCheatGameplay
	{
	public:
		explicit AntiCheatGameplay(AntiCheatConfig cfg = {}) : m_cfg(cfg) {}

		/// Verifie une nouvelle position reportee par le client. Met a jour
		/// l'etat interne. Retourne le verdict.
		CheatVerdict CheckMovement(PlayerId player, float x, float y, float z, uint64_t nowMs)
		{
			auto& last = m_state[player];
			if (!last.hasPrevious)
			{
				last = {x, y, z, nowMs, true};
				return CheatVerdict::OK;
			}

			const float dx = x - last.x;
			const float dy = y - last.y;
			const float dz = z - last.z;
			const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

			// Teleport hack : saut trop gros peu importe le temps.
			if (dist > m_cfg.maxSingleStepM)
			{
				last = {x, y, z, nowMs, true};
				return CheatVerdict::TeleportHack;
			}

			const uint64_t dtMs = (nowMs > last.tsMs) ? (nowMs - last.tsMs) : 1;
			const float speedMps = dist / (static_cast<float>(dtMs) / 1000.0f);
			const float maxAllowed = m_cfg.maxSpeedMps * m_cfg.speedTolerance;

			last = {x, y, z, nowMs, true};
			return (speedMps > maxAllowed) ? CheatVerdict::SpeedHack : CheatVerdict::OK;
		}

		void Reset(PlayerId player) { m_state.erase(player); }

	private:
		AntiCheatConfig m_cfg;
		std::unordered_map<PlayerId, LastReportedPos> m_state;
	};
}
