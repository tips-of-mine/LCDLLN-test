#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/arches/ArchCatalog.h"

#include <string>

namespace engine::core { class Config; }

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::arches
{
	/// Outil de placement d'arches naturelles (M100.42). Workflow :
	///   - charge `meshes/arches/catalog.json`,
	///   - sélection d'un id,
	///   - l'utilisateur fournit deux points monde (`pointA`, `pointB`)
	///     représentant les deux pieds de l'arche au sol,
	///   - le tool calcule automatiquement :
	///     * `worldPosition` = centre du segment AB,
	///     * `eulerRotationDeg.y` = atan2(Bz-Az, Bx-Ax) pour aligner
	///       l'axe natif AB avec le segment monde,
	///     * `uniformScale` = (real_span / native_span),
	///   - clic `Place` → pousse `PlaceArchCommand`.
	///
	/// MVP éditeur-side : pas de raycast viewport, les deux points sont
	/// saisis manuellement. Pas de validation que le sol descend sous
	/// les deux ancres (besoin de SurfaceQuery).
	class ArchTool
	{
	public:
		bool Init(engine::editor::world::CommandStack& stack,
			MeshInsertDocument& meshDoc, const engine::core::Config& cfg);

		void Reset();

		void LoadCatalog(const std::string& contentRoot);
		const ArchCatalog& Catalog() const { return m_catalog; }

		const std::string& SelectedId() const { return m_selectedId; }
		void SelectById(const std::string& id) { m_selectedId = id; }

		// Pieds A / B en coords monde (le tool dérive position + yaw + scale).
		float& PointAX() { return m_pointAX; }
		float& PointAY() { return m_pointAY; }
		float& PointAZ() { return m_pointAZ; }
		float& PointBX() { return m_pointBX; }
		float& PointBY() { return m_pointBY; }
		float& PointBZ() { return m_pointBZ; }

		/// Span monde calculé = distance XZ entre les deux pieds saisis.
		float SpanMeters() const;

		/// Scale uniforme dérivé = span_monde / span_natif (catalogue).
		/// Retourne 1.0f si entry absent ou span natif quasi nul.
		float DerivedScale() const;

		/// Yaw deg calculé pour aligner l'axe natif (archAnchorA→B) avec
		/// le segment monde (pointA→B).
		float DerivedYawDeg() const;

		bool&  CastsShadow()         { return m_castsShadow; }
		float& LightProbeIntensity() { return m_lightProbeIntensity; }

		/// Garde-fou : si span monde / span natif < ce ratio, le placement
		/// est refusé (l'arche serait écrasée).
		float& MinScaleRatio() { return m_minScaleRatio; }
		/// Idem pour le ratio max (l'arche serait étirée hors plausibilité).
		float& MaxScaleRatio() { return m_maxScaleRatio; }

		/// Pousse une `PlaceArchCommand`. Retourne false si aucun id, ou
		/// si le ratio de scale dérivé est hors bornes.
		bool Place();

		void Cancel();

	private:
		engine::editor::world::CommandStack* m_stack    = nullptr;
		MeshInsertDocument*                  m_meshDoc  = nullptr;
		const engine::core::Config*          m_cfg      = nullptr;

		ArchCatalog m_catalog;
		std::string m_selectedId;

		float m_pointAX = 0.0f, m_pointAY = 0.0f, m_pointAZ = 0.0f;
		float m_pointBX = 0.0f, m_pointBY = 0.0f, m_pointBZ = 0.0f;

		bool  m_castsShadow         = true;
		float m_lightProbeIntensity = 1.0f;

		float m_minScaleRatio = 0.25f;
		float m_maxScaleRatio = 4.0f;
	};
}
