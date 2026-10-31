// SND.5 — Sons de combat et de sorts (cf. CombatAudioCues.h).

#include "src/client/audio/CombatAudioCues.h"

#include <algorithm>

namespace engine::audio
{
	namespace
	{
		/// Priorité d'un son (plus grand = plus important).
		int Priority(std::string_view id)
		{
			namespace s = combat_sounds;
			if (id == s::kPlayerDeath) return 100;
			if (id == s::kPlayerHurt) return 80;
			if (id == s::kCrit) return 70;
			if (id == s::kDeath) return 60;
			if (id == s::kSpellRelease || id == s::kSpellFizzle || id == s::kSpellCast) return 50;
			if (id == s::kAuraBuff || id == s::kAuraDebuff) return 30;
			if (id == s::kWhoosh) return 20;
			return 40; // impacts
		}
	}

	std::string_view HitSoundForProfile(std::string_view profile)
	{
		namespace s = combat_sounds;
		if (profile == "melee" || profile == "tank" || profile == "voleur")
			return s::kHitBlade;
		if (profile == "distance" || profile == "pisteur")
			return s::kHitArrow;
		if (profile == "lanceur" || profile == "healer" || profile == "sacre")
			return s::kHitMagic;
		return s::kHitBlunt;
	}

	bool IsBeneficialAura(uint8_t effectType)
	{
		return effectType != 1 && effectType != 5 && effectType != 7;
	}

	bool LowHealthHeartbeat(uint32_t health, uint32_t maxHealth)
	{
		return maxHealth > 0 && health > 0 && static_cast<uint64_t>(health) * 4u < maxHealth;
	}

	std::vector<CombatAudioCue> DiffCombatAudio(const CombatAudioSnapshot& prev, const CombatAudioSnapshot& cur,
		uint64_t nowNs, size_t maxCues)
	{
		namespace s = combat_sounds;
		std::vector<CombatAudioCue> cues;
		// Première photo (entrée en jeu, reconnexion) : on n'annonce pas l'état initial.
		if (!prev.valid || !cur.valid || prev.playerId != cur.playerId)
			return cues;

		// --- Coups : entrées du journal plus récentes que la photo précédente.
		uint64_t prevSeq = 0;
		for (const CombatAudioHit& h : prev.hits)
			prevSeq = std::max(prevSeq, h.sequence);
		for (const CombatAudioHit& h : cur.hits)
		{
			if (h.sequence <= prevSeq)
				continue;
			if (!h.playerAttacker && !h.playerTarget)
			{
				// Combat entre tiers (autre joueur contre un mob) : plus discret.
				if (!h.miss)
				{
					const auto it = cur.entityIsCreature.find(h.attackerId);
					const bool creature = it != cur.entityIsCreature.end() && it->second;
					cues.push_back({ creature ? s::kHitClaw : s::kHitBlunt, h.targetId, 0.6f });
				}
				continue;
			}
			if (h.miss)
			{
				cues.push_back({ s::kWhoosh, h.playerAttacker ? 0 : h.attackerId, 0.8f });
				continue;
			}
			if (h.playerTarget)
			{
				// Le joueur encaisse : choc 2D + griffes si l'attaquant est une créature.
				cues.push_back({ s::kPlayerHurt, 0, 1.0f });
				const auto it = cur.entityIsCreature.find(h.attackerId);
				if (it != cur.entityIsCreature.end() && it->second)
					cues.push_back({ s::kHitClaw, 0, 0.7f });
			}
			else
			{
				cues.push_back({ HitSoundForProfile(cur.playerProfile), h.targetId, 1.0f });
			}
			if (h.crit)
				cues.push_back({ s::kCrit, h.playerTarget ? 0 : h.targetId, 0.9f });
		}

		// --- Morts.
		if (prev.playerHealth > 0 && cur.playerHealth == 0 && cur.playerMaxHealth > 0)
			cues.push_back({ s::kPlayerDeath, 0, 1.0f });
		for (const auto& [id, hp] : cur.entityHealth)
		{
			const auto it = prev.entityHealth.find(id);
			if (it != prev.entityHealth.end() && it->second > 0 && hp == 0)
				cues.push_back({ s::kDeath, id, 1.0f });
		}

		// --- Incantation.
		if (!prev.castActive && cur.castActive)
			cues.push_back({ s::kSpellCast, 0, 1.0f });
		else if (prev.castActive && !cur.castActive)
		{
			const uint64_t elapsedMs = nowNs > prev.castStartNs ? (nowNs - prev.castStartNs) / 1000000ull : 0;
			const bool completed = prev.castDurationMs == 0 || elapsedMs * 100u >= prev.castDurationMs * 85ull;
			cues.push_back({ completed ? s::kSpellRelease : s::kSpellFizzle, 0, 1.0f });
		}

		// --- Auras nouvelles sur le joueur (une seule par image suffit).
		for (const auto& [spell, type] : cur.playerAuras)
			if (prev.playerAuras.find(spell) == prev.playerAuras.end())
			{
				cues.push_back({ IsBeneficialAura(type) ? s::kAuraBuff : s::kAuraDebuff, 0, 0.9f });
				break;
			}

		// Les plus importants d'abord, puis tronqué (pas de mur de son en AoE).
		std::stable_sort(cues.begin(), cues.end(),
			[](const CombatAudioCue& a, const CombatAudioCue& b) { return Priority(a.soundId) > Priority(b.soundId); });
		if (cues.size() > maxCues)
			cues.resize(maxCues);
		return cues;
	}
}
