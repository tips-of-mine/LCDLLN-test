#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/TerrainBrush.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/client/render/Camera.h"

#include <cstdint>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// Outil de sculpture terrain (M100.6). Vit dans le shell éditeur monde,
	/// branché sur le `CommandStack` partagé pour pousser un
	/// `TerrainSculptCommand` à chaque brushstroke (press → release).
	///
	/// Cycle de vie d'un stroke :
	///   - `OnMouseDown` : raycast, démarre `m_inFlight`, applique premier tick.
	///   - `OnMouseMove`(répétés tant que pressé) : raycast + ApplyBrushKernel.
	///   - `OnMouseUp` : pousse `TerrainSculptCommand(m_inFlight, m_strokeId)`
	///     sur le `CommandStack` (1 entrée historique par stroke).
	///
	/// Contraintes thread/timing : main thread (modifie le document terrain
	/// et la pile undo).
	class TerrainSculptTool
	{
	public:
		/// Initialise l'outil avec une référence au CommandStack et au
		/// TerrainDocument partagés. Doit être appelé une fois avant
		/// `OnMouseDown`. Retourne false si l'un des pointeurs est null.
		bool Init(CommandStack& stack, TerrainDocument& doc);

		/// Met à jour les paramètres live de la brosse. Le rayon, la force,
		/// le falloff sont lus à chaque tick — le changement prend effet
		/// immédiatement (utile pour l'UI qui modifie pendant le stroke).
		void SetParams(const TerrainBrushParams& p) { m_params = p; }

		/// Accès lecture seule aux paramètres (pour l'UI).
		const TerrainBrushParams& GetParams() const { return m_params; }

		/// Démarre un brushstroke. Raycast → applique un premier tick de la
		/// brosse. Génère un nouveau `m_strokeId` qui servira de mergeKey à
		/// la commande poussée à `OnMouseUp`.
		/// \param dtSeconds Delta temps depuis le tick précédent (sec).
		void OnMouseDown(const engine::render::Camera& cam,
			int sx, int sy, int vw, int vh,
			const engine::core::Config& cfg,
			float dtSeconds = 1.0f / 60.0f);

		/// Tick d'un brushstroke en cours. No-op si `OnMouseDown` n'a pas
		/// été appelé ou si `OnMouseUp` a déjà fermé le geste.
		void OnMouseMove(const engine::render::Camera& cam,
			int sx, int sy, int vw, int vh,
			const engine::core::Config& cfg,
			float dtSeconds = 1.0f / 60.0f);

		/// Termine le brushstroke : si `m_inFlight` est non vide, pousse une
		/// `TerrainSculptCommand` sur le CommandStack avec `m_strokeId` comme
		/// mergeKey, puis vide `m_inFlight`. No-op si rien n'a été touché.
		void OnMouseUp();

		/// True si un stroke est en cours (entre OnMouseDown et OnMouseUp).
		bool IsStroking() const { return m_pressing; }

		/// Nombre de chunks touchés par le stroke en cours.
		size_t InFlightChunkCount() const { return m_inFlight.size(); }

	private:
		/// Applique un tick de la brosse au point monde donné. Implémente la
		/// logique multi-chunk + couture inter-chunks (cellules sur les bords).
		void ApplyTickAtWorldPoint(float worldX, float worldZ,
			const engine::core::Config& cfg, float dtSeconds);

		/// Récupère ou crée la liste de cellules en vol pour un chunk donné
		/// dans `m_inFlight`. Retourne une référence stable jusqu'à la
		/// prochaine modification de `m_inFlight`.
		std::vector<TerrainSculptDeltaCell>& EnsureInFlightCells(
			engine::world::GlobalChunkCoord coord);

		CommandStack*           m_stack = nullptr;
		TerrainDocument*        m_doc = nullptr;
		TerrainBrushParams      m_params;
		std::vector<TerrainSculptDeltaChunk> m_inFlight;
		bool                    m_pressing = false;
		uint64_t                m_strokeId = 0;
	};
}
