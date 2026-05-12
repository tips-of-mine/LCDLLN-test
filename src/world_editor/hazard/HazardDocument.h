// src/world_editor/hazard/HazardDocument.h
#pragma once

#include "src/client/world/hazard/HazardVolumes.h"

#include <cstddef>
#include <vector>

namespace engine::editor::hazard
{
	/// État monde éditeur pour les hazards. Liste mutable d'instances avec
	/// add/remove/get. Persistance via les helpers HazardVolumes.
	///
	/// Pas d'undo/redo intégré (utilisera le CommandStack éditeur dans un
	/// futur ticket — actuellement géré par M100.2 via un wrapper externe).
	class HazardDocument
	{
	public:
		/// Ajoute une nouvelle instance. Retourne son index.
		size_t Add(const engine::world::hazard::HazardInstance& hz);

		/// Retire l'instance à `index`. No-op si index invalide.
		void Remove(size_t index);

		/// Accès lecture seule à la scène complète.
		const engine::world::hazard::HazardScene& Scene() const noexcept { return m_scene; }

		/// Accès lecture/écriture (pour modifier les paramètres d'un hazard
		/// existant via Tool Properties).
		engine::world::hazard::HazardScene& MutableScene() noexcept { return m_scene; }

		/// Nombre d'instances.
		size_t Count() const noexcept { return m_scene.hazards.size(); }

		/// Reset complet.
		void Clear() noexcept { m_scene.hazards.clear(); }

	private:
		engine::world::hazard::HazardScene m_scene;
	};
}
