#pragma once
// CMANGOS.31 (Phase 5.31a) — GameEventManager : evenements saisonniers
// (Halloween, Christmas, etc.) avec activation periodique. Header-only.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::events
{
	using EventId = uint32_t;

	/// Sentinel : pas de restriction lunaire. Tous les bits a 1.
	inline constexpr uint16_t kLunarPhaseAny = 0xFFFFu;

	/// Mask des phases "Lune Noire" : 0 (NewMoon), 14 (EarthshineEarly),
	/// 15 (EarthshineLate). Illumination <= 1%, theme central LCDLLN.
	/// = (1 << 0) | (1 << 14) | (1 << 15) = 0xC001.
	inline constexpr uint16_t kLunarPhaseNoireMask =
		static_cast<uint16_t>((1u << 0) | (1u << 14) | (1u << 15));

	/// Mask des phases "Pleine Lune" : 6 (FullMoonRising), 7 (FullMoon),
	/// 8 (FullMoonSetting). Illumination >= ~95%.
	/// = (1 << 6) | (1 << 7) | (1 << 8) = 0x01C0.
	inline constexpr uint16_t kLunarPhaseFullMoonMask =
		static_cast<uint16_t>((1u << 6) | (1u << 7) | (1u << 8));

	struct GameEventDef
	{
		EventId     id          = 0;
		std::string name;
		uint64_t    startTsMs   = 0;       ///< absolute or recurring offset
		uint64_t    durationMs  = 0;       ///< event lasts this long
		uint64_t    recurMs     = 0;       ///< 0 = one-shot ; >0 = recurring period

		/// Bitmask des phases lunaires autorisees (bit i = phase i autorisee).
		/// kLunarPhaseAny (0xFFFF) = pas de restriction lunaire (default).
		/// Permet de gater des events sur la Lune Noire (kLunarPhaseNoireMask)
		/// ou la Pleine Lune (kLunarPhaseFullMoonMask), etc.
		uint16_t    requiresLunarPhaseMask = kLunarPhaseAny;
	};

	enum class EventState : uint8_t
	{
		Inactive = 0,
		Active   = 1,
	};

	class GameEventManager
	{
	public:
		void Register(GameEventDef def) { m_events[def.id] = std::move(def); }

		EventState GetState(EventId id, uint64_t nowMs) const
		{
			auto it = m_events.find(id);
			if (it == m_events.end()) return EventState::Inactive;
			const auto& d = it->second;
			if (d.recurMs == 0)
			{
				return (nowMs >= d.startTsMs && nowMs < d.startTsMs + d.durationMs)
					? EventState::Active : EventState::Inactive;
			}
			// Recurring : compute offset from startTsMs.
			if (nowMs < d.startTsMs) return EventState::Inactive;
			const uint64_t offset = (nowMs - d.startTsMs) % d.recurMs;
			return (offset < d.durationMs) ? EventState::Active : EventState::Inactive;
		}

		std::vector<EventId> ActiveEvents(uint64_t nowMs) const
		{
			std::vector<EventId> out;
			for (const auto& [id, def] : m_events)
				if (GetState(id, nowMs) == EventState::Active) out.push_back(id);
			return out;
		}

		/// Verifie si la phase \p phase (0..15) est autorisee par le masque
		/// \p mask. kLunarPhaseAny passe toujours. Phase >= 16 est rejetee.
		/// Sinon teste le bit correspondant dans le masque.
		///
		/// \param mask  bitmask des phases autorisees (16 bits = 16 phases).
		/// \param phase indice de phase (0..15).
		/// \return true si la phase est autorisee, false sinon.
		static constexpr bool IsLunarPhaseAllowed(uint16_t mask, uint8_t phase) noexcept
		{
			if (mask == kLunarPhaseAny) return true;
			if (phase >= 16) return false;
			return ((mask >> phase) & 1u) != 0u;
		}

		/// Retourne l'etat avec filtre lunaire applique. Si la phase lunaire
		/// courante n'est pas dans \c def.requiresLunarPhaseMask, l'etat passe
		/// a Inactive meme si time-wise l'event serait Active.
		///
		/// \param id                  identifiant de l'event a interroger.
		/// \param nowMs               instant courant en ms (system_clock epoch).
		/// \param currentLunarPhase   phase lunaire courante (0..15).
		/// \return Active uniquement si l'event est dans sa fenetre temporelle
		///         ET la phase lunaire courante est dans le masque autorise.
		EventState GetStateFiltered(EventId id, uint64_t nowMs, uint8_t currentLunarPhase) const
		{
			auto it = m_events.find(id);
			if (it == m_events.end()) return EventState::Inactive;
			EventState timeState = GetState(id, nowMs);
			if (timeState == EventState::Inactive) return EventState::Inactive;
			return IsLunarPhaseAllowed(it->second.requiresLunarPhaseMask, currentLunarPhase)
				? EventState::Active : EventState::Inactive;
		}

		/// Variante de \c ActiveEvents qui filtre par phase lunaire en plus
		/// du temps. Un event sans restriction lunaire (kLunarPhaseAny)
		/// reste filtre uniquement par le temps comme avant.
		///
		/// \param nowMs              instant courant en ms.
		/// \param currentLunarPhase  phase lunaire courante (0..15).
		/// \return liste des ids actifs apres double filtre temps + lune.
		std::vector<EventId> ActiveEventsFiltered(uint64_t nowMs, uint8_t currentLunarPhase) const
		{
			std::vector<EventId> out;
			for (const auto& [id, def] : m_events)
				if (GetStateFiltered(id, nowMs, currentLunarPhase) == EventState::Active)
					out.push_back(id);
			return out;
		}

		size_t Size() const noexcept { return m_events.size(); }

		/// CMANGOS.31 step 3+4 — Acces lecture seule a la map d'events pour
		/// les iterations cote handler (List response, snapshot subscribe).
		/// Modification additive : permet au GameEventHandler de construire
		/// les summaries sans dupliquer la liste des ids.
		const std::unordered_map<EventId, GameEventDef>& Events() const noexcept { return m_events; }

	private:
		std::unordered_map<EventId, GameEventDef> m_events;
	};
}
