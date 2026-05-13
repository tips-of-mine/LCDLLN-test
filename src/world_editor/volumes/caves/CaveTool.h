#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/caves/CaveCatalog.h"

#include <cstdint>
#include <optional>
#include <string>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;
}

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
}

namespace engine::editor::world::volumes::caves
{
	/// Outil de placement de grotte (M100.40 MVP éditeur-side). Workflow :
	///   - charge le catalogue via `CaveCatalog::LoadFromContent`,
	///   - l'utilisateur sélectionne un id de grotte + ajuste rotation /
	///     scale / camouflage,
	///   - clic sur le terrain (raycast traité côté shell, on reçoit
	///     directement `worldX`/`worldZ`) → preview position courante,
	///   - clic `Place` → pousse `PlaceCaveCommand` sur `CommandStack`.
	///
	/// Note MVP : pas de gizmo translate/rotate visuel (M100.17 absent) ;
	/// pas de rendu glTF (tinygltf absent) ; pas d'auto-props rochers
	/// (`InstanceDocument` M100.17 absent). Le tool est néanmoins
	/// fonctionnel end-to-end : place une `MeshInsertInstance` dans
	/// `MeshInsertDocument` + patch splat de camouflage. La PR
	/// d'intégration runtime câblera le rendu.
	class CaveTool
	{
	public:
		bool Init(engine::editor::world::CommandStack& stack,
			MeshInsertDocument& meshDoc,
			engine::editor::world::TerrainDocument& terrain,
			const engine::core::Config& cfg);

		void Reset();

		/// Recharge le catalogue depuis le content root.
		void LoadCatalog(const std::string& contentRoot);

		const CaveCatalog& Catalog() const { return m_catalog; }

		const std::string& SelectedId() const { return m_selectedId; }
		void SelectById(const std::string& id) { m_selectedId = id; }

		// Position cible (typiquement issue d'un raycast viewport).
		float& TargetWorldX() { return m_targetWorldX; }
		float& TargetWorldZ() { return m_targetWorldZ; }
		float  TargetWorldY() const { return m_targetWorldY; }
		void   SetTargetWorldY(float y) { m_targetWorldY = y; }

		// Paramètres de placement.
		float& RotationYDeg() { return m_rotationYDeg; }
		float& UniformScale() { return m_uniformScale; }
		bool&  SnapToGround() { return m_snapToGround; }
		bool&  CamouflageEnabled() { return m_camouflageEnabled; }
		float& CamouflageRadius() { return m_camouflageRadius; }
		float& CamouflageStrength() { return m_camouflageStrength; }
		bool&  HasInteriorVolume() { return m_hasInteriorVolume; }
		bool&  ReceivesAudioReverb() { return m_receivesAudioReverb; }
		bool&  AllowsWaterIngress() { return m_allowsWaterIngress; }
		float& LightProbeIntensity() { return m_lightProbeIntensity; }

		/// Pousse une `PlaceCaveCommand` sur la pile undo. Retourne false si
		/// aucun id de grotte sélectionné ou si l'id n'est pas dans le
		/// catalogue.
		bool Place();

		/// Abandonne la sélection courante (purement cosmétique : aucune
		/// commande n'est jamais "in-flight" en l'état actuel).
		void Cancel();

	private:
		engine::editor::world::CommandStack*    m_stack    = nullptr;
		MeshInsertDocument*                     m_meshDoc  = nullptr;
		engine::editor::world::TerrainDocument* m_terrain  = nullptr;
		const engine::core::Config*             m_cfg      = nullptr;

		CaveCatalog m_catalog;
		std::string m_selectedId;

		float m_targetWorldX = 0.0f;
		float m_targetWorldY = 0.0f;
		float m_targetWorldZ = 0.0f;

		float m_rotationYDeg        = 0.0f;
		float m_uniformScale        = 1.0f;
		bool  m_snapToGround        = true;
		bool  m_camouflageEnabled   = true;
		float m_camouflageRadius    = 8.0f;
		float m_camouflageStrength  = 0.6f;
		bool  m_hasInteriorVolume   = true;
		bool  m_receivesAudioReverb = true;
		bool  m_allowsWaterIngress  = false;
		float m_lightProbeIntensity = 0.4f;
	};
}
