#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/overhangs/OverhangCatalog.h"

#include <string>

namespace engine::core { class Config; }

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::overhangs
{
	/// Outil de placement d'overhangs (M100.41). Workflow :
	///   - charge le catalogue via `OverhangCatalog::LoadFromContent`,
	///   - l'utilisateur sélectionne un id de surplomb + ajuste yaw / tilt /
	///     scale et la slope minimale acceptable,
	///   - clic sur une falaise (raycast traité côté shell, on reçoit
	///     directement `worldX`/`worldY`/`worldZ` + `wallNormalAngleDeg`),
	///   - clic `Place` → pousse `PlaceOverhangCommand` sur `CommandStack`.
	///
	/// MVP éditeur-side : pas de détection automatique de cliff (le tool
	/// expose un slider `requiredSlopeDeg` informatif + un bool
	/// `slopeOk` que l'utilisateur valide manuellement). Le câblage au
	/// raycast viewport et à un `TerrainSlopeProbe` viendra avec le
	/// gizmo M100.17 et la généralisation de `ComputeCaveSplatWeights`
	/// au gradient terrain.
	class OverhangTool
	{
	public:
		bool Init(engine::editor::world::CommandStack& stack,
			MeshInsertDocument& meshDoc, const engine::core::Config& cfg);

		void Reset();

		void LoadCatalog(const std::string& contentRoot);
		const OverhangCatalog& Catalog() const { return m_catalog; }

		const std::string& SelectedId() const { return m_selectedId; }
		void SelectById(const std::string& id) { m_selectedId = id; }

		float& TargetWorldX() { return m_targetWorldX; }
		float& TargetWorldY() { return m_targetWorldY; }
		float& TargetWorldZ() { return m_targetWorldZ; }

		/// Angle horizontal (Y) auquel le mesh sera tourné pour aligner sa
		/// `wallNormalDirection` avec la falaise. 0 = +X (face est), 90 =
		/// +Z (face nord).
		float& WallNormalYawDeg() { return m_wallNormalYawDeg; }

		/// Tilt latéral (rotation autour de Z monde) — utile pour suivre
		/// l'inclinaison d'une falaise non parfaitement verticale.
		float& TiltDeg() { return m_tiltDeg; }

		float& UniformScale() { return m_uniformScale; }

		/// Slope minimale (deg) recommandée pour valider le placement.
		/// Purement informatif en MVP.
		float& RequiredSlopeDeg() { return m_requiredSlopeDeg; }

		/// Slope locale observée par l'utilisateur (input manuel en MVP,
		/// raycast normal automatique en follow-up M100.17).
		float& ObservedSlopeDeg() { return m_observedSlopeDeg; }

		bool   IsSlopeOk() const { return m_observedSlopeDeg >= m_requiredSlopeDeg; }

		bool&  CastsShadow()         { return m_castsShadow; }
		bool&  ReceivesAudioReverb() { return m_receivesAudioReverb; }
		float& LightProbeIntensity() { return m_lightProbeIntensity; }

		/// Pousse une `PlaceOverhangCommand`. Retourne false si aucun id
		/// sélectionné ou si la slope observée est insuffisante.
		bool Place();

		void Cancel();

	private:
		engine::editor::world::CommandStack* m_stack    = nullptr;
		MeshInsertDocument*                  m_meshDoc  = nullptr;
		const engine::core::Config*          m_cfg      = nullptr;

		OverhangCatalog m_catalog;
		std::string     m_selectedId;

		float m_targetWorldX = 0.0f;
		float m_targetWorldY = 0.0f;
		float m_targetWorldZ = 0.0f;

		float m_wallNormalYawDeg = 0.0f;
		float m_tiltDeg          = 0.0f;
		float m_uniformScale     = 1.0f;

		float m_requiredSlopeDeg = 35.0f;
		float m_observedSlopeDeg = 35.0f;

		bool  m_castsShadow         = true;
		bool  m_receivesAudioReverb = false;
		float m_lightProbeIntensity = 0.6f;
	};
}
