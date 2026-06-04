#pragma once

// M100.32 — InteractiveSimulator : animation locale open/close (logique PURE,
// déterministe, sans dépendance rendu/audio/réseau). STRICTEMENT CLIENT/ÉDITEUR
// (jamais compilé côté serveur : le master ne fait que relayer l'état).
//
// Le simulateur fait évoluer un facteur d'ouverture `openFactor` ∈ [0,1]
// (0 = fermé, 1 = ouvert) vers l'état cible, à la vitesse 1/animDurationSec.
// Le bouton "Trigger" de l'éditeur et le client en jeu appellent les mêmes
// fonctions (parité éditeur ↔ client) ; seul l'envoi réseau diffère.
//
// Compensation de latence : à la réception d'un évènement distant daté, on
// avance immédiatement `openFactor` de `latencySec / animDurationSec` vers la
// cible pour que la porte ne « saute » pas (transition visible, pas de pop).

#include <cstdint>

#include "src/client/world/interactive/InteractiveTypes.h"

namespace engine::world::interactive
{
	/// État runtime d'une instance interactive côté client/éditeur.
	struct InteractiveRuntimeState
	{
		uint8_t targetState = 0;   ///< Cible logique : 0 = fermé, 1 = ouvert.
		float   openFactor  = 0.0f; ///< Avancement courant ∈ [0,1] (0 fermé, 1 ouvert).
	};

	/// Construit l'état runtime initial à partir de la définition (initialState).
	/// `openFactor` est aligné sur l'état initial (0 ou 1) — pas d'animation.
	InteractiveRuntimeState MakeInitialRuntimeState(const InteractivePropInstance& def);

	/// Bascule la cible (fermé ↔ ouvert) sans téléportation : `openFactor`
	/// animera vers la nouvelle cible aux prochains `UpdateInteractive`.
	/// \return le nouvel état logique cible (0/1).
	uint8_t ToggleInteractive(InteractiveRuntimeState& rt);

	/// Force une cible explicite (utilisé pour appliquer un état réseau).
	void SetInteractiveTarget(InteractiveRuntimeState& rt, uint8_t newState);

	/// Avance l'animation de `dtSec` secondes vers la cible. `openFactor` est
	/// clampé dans [0,1]. `animDurationSec` ≤ 0 ⇒ application instantanée.
	void UpdateInteractive(InteractiveRuntimeState& rt, const InteractivePropInstance& def, float dtSec);

	/// Applique un état distant reçu du serveur avec compensation de latence :
	/// fixe la cible à `newState`, puis avance `openFactor` de
	/// `latencySec / animDurationSec` vers la cible (clampé). L'évènement étant
	/// survenu `latencySec` plus tôt côté émetteur, l'animation démarre déjà
	/// avancée — la porte ne saute pas.
	/// \param latencySec délai estimé depuis l'évènement (s) ; < 0 traité comme 0.
	void ApplyRemoteState(InteractiveRuntimeState& rt, const InteractivePropInstance& def,
		uint8_t newState, float latencySec);

	/// Angle d'ouverture courant en degrés (types rotatifs : DoorHinge /
	/// WindowHinge / Trapdoor / ChestSimple). Vaut `openAngleDeg * openFactor`.
	/// Pour DoorSliding, renvoie 0 (utiliser ComputeSlideOffsetMeters).
	float ComputeOpenAngleDeg(const InteractivePropInstance& def, const InteractiveRuntimeState& rt);

	/// Décalage de translation courant en mètres (DoorSliding uniquement).
	/// Vaut `openAngleDeg * openFactor` (le champ porte la translation max).
	/// Pour les types rotatifs, renvoie 0.
	float ComputeSlideOffsetMeters(const InteractivePropInstance& def, const InteractiveRuntimeState& rt);

	/// True si l'objet est en cours d'animation (openFactor strictement entre
	/// les deux extrêmes, ou pas encore aligné sur la cible).
	bool IsAnimating(const InteractiveRuntimeState& rt);
}
