#pragma once

namespace engine::editor::world
{
	/// Interface commune à tous les panneaux ancrables du shell éditeur monde
	/// (M100.1). Chaque panneau est rendu via ImGui::Begin/End par le shell ;
	/// la visibilité est portée par chaque implémentation, le shell appelle
	/// Render() seulement si IsVisible() == true.
	///
	/// Contraintes thread/timing : toute méthode est appelée depuis le main
	/// thread, dans la phase ImGui de Engine::DrawFrame (entre NewFrame et
	/// Render). Elles ne doivent pas être appelées depuis un thread worker.
	class IPanel
	{
	public:
		virtual ~IPanel() = default;

		/// Identifiant stable du panneau, utilisé comme nom de window ImGui.
		/// Doit être ASCII pur et invariant entre sessions (sert de clé d'ini).
		virtual const char* GetName() const = 0;

		/// Rend le contenu via ImGui::Begin/End. Appelé une fois par frame.
		/// Précondition : IsVisible() == true (le shell garde le contrat).
		/// Effet de bord : modifie l'état ImGui courant (windows, dock).
		virtual void Render() = 0;

		/// Retourne true si le panneau doit être rendu cette frame.
		virtual bool IsVisible() const = 0;

		/// Active ou désactive la visibilité du panneau.
		/// \param visible true pour rendre visible, false pour masquer.
		virtual void SetVisible(bool visible) = 0;
	};
}
