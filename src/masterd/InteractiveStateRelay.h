#pragma once

// M100.32 — InteractiveStateRelay : état RAM des objets interactifs d'une zone
// côté master. HEADER-ONLY (logique pure, testable sans NetServer) : la classe
// ne fait QUE gérer la map id → state et les décisions de relai. L'envoi réseau
// (broadcast / sync) est assuré par InteractiveHandler qui consomme cette classe.
//
// AUCUNE validation gameplay : pas de portée, pas de droit d'ouverture, pas
// d'anti-triche (cf. ticket M100.32 §"Côté master"). Un StateChange pour un id
// inconnu est ignoré (ChangeResult::UnknownId) — le handler loggue un warning.
//
// Persistance : RAM uniquement. À la coupure du serveur, l'état est perdu et
// re-seedé depuis `initialState` au prochain démarrage (hors scope : persistance
// disque serveur).
//
// Thread-safety : la classe n'est PAS auto-synchronisée. Le handler la protège
// par son propre mutex (cf. InteractiveHandler), comme GameEventHandler le fait
// pour GameEventManager.

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::server::interactive
{
	/// Résultat d'une tentative d'application d'un changement d'état.
	enum class ChangeResult
	{
		Applied,   ///< id connu : état écrit.
		UnknownId, ///< id non seedé : ignoré (le handler loggue un warning).
	};

	/// État RAM des objets interactifs d'une zone (map id → state 0/1).
	class InteractiveStateRelay
	{
	public:
		/// Enregistre un objet avec son état initial. Appelé au chargement de
		/// la zone (depuis interactives.bin). Idempotent : un re-seed écrase
		/// l'état courant par `initialState`.
		void Seed(uint64_t id, uint8_t initialState)
		{
			m_states[id] = initialState != 0u ? 1u : 0u;
		}

		/// Applique un changement d'état reçu d'un client. Retourne UnknownId
		/// (sans rien modifier) si l'objet n'a pas été seedé.
		ChangeResult ApplyStateChange(uint64_t id, uint8_t newState)
		{
			auto it = m_states.find(id);
			if (it == m_states.end())
				return ChangeResult::UnknownId;
			it->second = newState != 0u ? 1u : 0u;
			return ChangeResult::Applied;
		}

		/// Lit l'état d'un objet. `*found` (si non-null) indique si l'id existe.
		/// Retourne 0 si inconnu.
		uint8_t GetState(uint64_t id, bool* found = nullptr) const
		{
			auto it = m_states.find(id);
			if (it == m_states.end())
			{
				if (found) *found = false;
				return 0u;
			}
			if (found) *found = true;
			return it->second;
		}

		/// Snapshot complet (id, state) pour la synchronisation initiale d'un
		/// client entrant. L'ordre n'est pas garanti (map non ordonnée).
		std::vector<std::pair<uint64_t, uint8_t>> Snapshot() const
		{
			std::vector<std::pair<uint64_t, uint8_t>> out;
			out.reserve(m_states.size());
			for (const auto& [id, st] : m_states)
				out.emplace_back(id, st);
			return out;
		}

		/// Nombre d'objets interactifs suivis.
		size_t Count() const { return m_states.size(); }

		/// Vide la map (rechargement de zone).
		void Clear() { m_states.clear(); }

	private:
		std::unordered_map<uint64_t, uint8_t> m_states;
	};
}
