#pragma once

// M100.32 — Panneau secondaire « Interactive Props » de l'éditeur monde.
//
// Apparaît dans PlacementTool quand l'asset placé est marqué `interactive`
// dans la library. Permet de configurer type/pivot/axe/angle/durée/état
// initial/audios, et d'appeler le même InteractiveSimulator que le client en
// jeu via le bouton « Trigger » (parité éditeur ↔ client, sans envoi réseau).
//
// Rendu ImGui guardé Windows (_WIN32) ; l'état édité et la logique de trigger
// sont disponibles sur toutes plateformes (testables headless via le simulateur).

#include "src/client/world/interactive/InteractiveSimulator.h"
#include "src/client/world/interactive/InteractiveTypes.h"

namespace engine::editor::world
{
	/// Panneau d'édition d'une instance interactive en cours de pose.
	class InteractivePanel
	{
	public:
		/// Définition en cours d'édition (modifiée par les champs ImGui).
		engine::world::interactive::InteractivePropInstance& Def() { return m_def; }
		const engine::world::interactive::InteractivePropInstance& Def() const { return m_def; }

		/// État runtime d'aperçu (animé par Trigger + Update).
		const engine::world::interactive::InteractiveRuntimeState& Preview() const { return m_preview; }

		/// Réinitialise l'aperçu sur l'état initial de la définition courante.
		/// À appeler après modification de `initialState`.
		void ResetPreview();

		/// Bascule l'aperçu (ouvre/ferme) — exactement comme un trigger client,
		/// mais sans envoi réseau. Utilisé par le bouton « Trigger ».
		void Trigger();

		/// Avance l'animation d'aperçu de `dtSec` secondes (appelé chaque frame
		/// éditeur). Sans effet hors animation.
		void Update(float dtSec);

		/// Rendu du panneau ImGui. No-op hors Windows. Doit être appelé en main
		/// thread, dans une frame ImGui active.
		void Render();

	private:
		engine::world::interactive::InteractivePropInstance m_def;
		engine::world::interactive::InteractiveRuntimeState  m_preview;
	};
}
