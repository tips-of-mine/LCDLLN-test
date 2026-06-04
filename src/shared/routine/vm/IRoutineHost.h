#pragma once

// M101.2 — Interface d'hôte de la VM de routines (frontière de pureté).
//
// La VM ne dépend QUE de cette interface : elle ne connaît rien du rendu, du
// réseau ni des systèmes de jeu. Le client de prod l'implémente (M101.8,
// ClientRoutineHost) ; les tests fournissent MockRoutineHost. C'est cette
// indirection qui rend la VM testable headless en CI.

#include <cstdint>
#include <string_view>

namespace engine::routine::vm
{
	/// Contexte d'une exécution. Le temps est LOGIQUE (fourni par l'appelant) :
	/// jamais d'horloge système dans la VM (déterminisme).
	struct RoutineRunContext
	{
		double   logicalTimeMs = 0.0;   ///< Temps logique fourni.
		uint64_t eventEntityId = 0;     ///< Entité ayant déclenché l'Event.
	};

	/// Pont abstrait entre la VM et le monde réel.
	class IRoutineHost
	{
	public:
		virtual ~IRoutineHost() = default;

		// --- capteurs (lecture) ---
		/// Heure du jour [0..24[, fournie par l'hôte (ex. SeasonClock côté client).
		virtual float GetTimeOfDayHours() const = 0;
		/// True si une entité joueur est à `meters` de l'entité `entityId`.
		virtual bool  IsPlayerInRange(uint64_t entityId, float meters) const = 0;

		// --- actions (effets) ---
		/// Ouvre (open=true) ou ferme un objet interactif.
		virtual void OpenInteractive(uint64_t interactiveId, bool open) = 0;
		/// Émet un broadcast de saison (réutilise l'opcode M100.25 côté client).
		virtual void BroadcastSeason(int seasonIndex) = 0;
		/// Émet un broadcast de météo (réutilise l'opcode M100.26 côté client).
		virtual void BroadcastWeather(int weatherIndex) = 0;

		// --- trace déterministe (diagnostic / tests) ---
		/// Journalise une étape (nœud exécuté). Optionnel ; no-op par défaut.
		virtual void Trace(std::string_view nodeLabel) { (void)nodeLabel; }
	};
}
