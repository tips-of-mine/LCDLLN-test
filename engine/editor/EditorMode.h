#pragma once

#include "engine/math/Math.h"
#include "engine/render/Camera.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core { class Config; }
namespace engine::platform { class Input; class Window; }
namespace engine::render { struct MeshAsset; }
namespace engine::world { enum class ChunkRing : uint8_t; }

namespace engine::editor
{
	/// Fixed placement layers for the MVP editor workflow.
	enum class PlacementLayer : uint32_t
	{
		Default = 0,
		Props = 1,
		Terrain = 2
	};

	/// Active gizmo tool in editor mode.
	enum class GizmoMode
	{
		Translate,
		Rotate,
		Scale
	};

	/// Axis currently edited by the gizmo.
	enum class GizmoAxis
	{
		X,
		Y,
		Z
	};

	/// Layer state shown by the editor shell.
	struct LayerState
	{
		const char* name = "";
		bool hidden = false;
		bool locked = false;
	};

	/// Supported gameplay volume types for placement.
	enum class VolumeType : uint32_t
	{
		Trigger = 0,
		SpawnArea = 1,
		ZoneTransition = 2
	};

	/// Supported gameplay volume shapes.
	enum class VolumeShape : uint32_t
	{
		Box = 0,
		Sphere = 1
	};

	/// Serializable gameplay volume edited by the MVP editor shell.
	struct VolumeEntity
	{
		uint32_t id = 0;
		VolumeType type = VolumeType::Trigger;
		VolumeShape shape = VolumeShape::Box;
		PlacementLayer layer = PlacementLayer::Default;
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 rotationDeg{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 boxHalfExtents{ 1.0f, 1.0f, 1.0f };
		float sphereRadius = 1.0f;
		std::string stringId = "default";
		std::string targetZoneId;
	};

	/// Minimal editor mode shell: one editable scene object, CPU selection, and keyboard gizmos.
	class EditorMode final
	{
	public:
		EditorMode() = default;
		EditorMode(const EditorMode&) = delete;
		EditorMode& operator=(const EditorMode&) = delete;

		/// Initializes editor-only state from config and prepares the shell text.
		bool Init(const engine::core::Config& config);

		/// Releases editor-only state and restores the base window title.
		void Shutdown(engine::platform::Window& window);

		/// Returns the default editor camera framing the editable test mesh.
		engine::render::Camera BuildInitialCamera() const;

		/// Processes selection/gizmo input and refreshes the shell title.
		void Update(engine::platform::Input& input,
			engine::platform::Window& window,
			const engine::render::Camera& camera,
			const engine::render::MeshAsset* mesh,
			int viewportWidth,
			int viewportHeight,
			double dtSeconds);

		/// Returns the editable object's current model matrix (column-major).
		const float* GetObjectModelMatrix() const { return m_modelMatrix.m; }
		/// Returns true when the editable object layer is visible.
		bool IsObjectVisible() const;

	private:
		/// Creates one gameplay volume of the requested type at the current editor pivot.
		bool CreateVolume(VolumeType type);
		/// Selects the next volume, if any.
		bool SelectNextVolume();
		/// Toggles the selected volume shape between box and sphere.
		bool ToggleSelectedVolumeShape();
		/// Cycles the selected volume string IDs / transition target with simple MVP defaults.
		bool CycleSelectedVolumeProperty();
		/// Exports all gameplay volumes into a layout file under paths.content.
		bool ExportLayout();
		/// Toggles the chunk/cell debug overlay shown by the editor shell.
		bool ToggleChunkOverlay();
		/// Returns the currently selected volume, or nullptr.
		VolumeEntity* GetSelectedVolume();
		/// Returns the currently selected volume, or nullptr.
		const VolumeEntity* GetSelectedVolume() const;
		/// Returns true when the selected volume layer is visible.
		bool IsSelectedVolumeVisible() const;
		/// Returns true when the selected volume layer is locked.
		bool IsSelectedVolumeLocked() const;
		/// Rebuilds the selected volume visualization strings.
		std::string BuildSelectedVolumeSummary() const;
		/// Builds the export payload for the current gameplay volumes.
		std::string BuildLayoutJson() const;
		/// Builds the chunk/cell overlay payload for the current camera and editor selection.
		std::string BuildChunkOverlayText(const engine::render::Camera& camera) const;
		/// Returns the chunk ring label for the current overlay highlight.
		const char* GetChunkRingName(engine::world::ChunkRing ring) const;
		/// Rebuilds the model matrix from position/rotation/scale.
		void RebuildModelMatrix();
		/// Refreshes the shell text displayed in the native window title and optional logs.
		void RefreshShell(engine::platform::Window& window, bool forceLog);
		/// Performs a CPU raycast against the mesh proxy bounds to update the selection.
		bool UpdateSelection(const engine::platform::Input& input, const engine::render::Camera& camera, const engine::render::MeshAsset* mesh, int viewportWidth, int viewportHeight);
		/// Applies snapped gizmo operations when the editable layer is unlocked.
		bool UpdateGizmo(engine::platform::Input& input, double dtSeconds);
		/// Handles layer/snapping/save controls and returns true when the shell should refresh.
		bool UpdateWorkflowControls(engine::platform::Input& input, const engine::render::MeshAsset* mesh);
		/// Aligns the selected object to the ground plane using its current transformed bounds.
		bool AlignSelectionToGround(const engine::render::MeshAsset* mesh);
		/// Marks the placement state dirty and logs why it changed.
		void MarkDirty(std::string_view reason);
		/// Clears the dirty flag and logs the save action.
		void SaveDirtyState();
		/// Cycles the active layer used by the shell toggles.
		void CycleActiveLayer();
		/// Returns true when the selected layer is locked.
		bool IsActiveLayerLocked() const;
		/// Returns the placement layer currently assigned to the editable object.
		const LayerState& GetObjectLayerState() const;
		/// Returns the mutable placement layer currently assigned to the editable object.
		LayerState& GetObjectLayerState();
		/// Returns the immutable layer state associated with a placement layer.
		const LayerState& GetLayerState(PlacementLayer layer) const;
		std::string BuildScenePanelText() const;
		std::string BuildInspectorPanelText() const;
		std::string BuildAssetBrowserPanelText() const;
		const char* GetModeName() const;
		const char* GetAxisName() const;
		const char* GetActiveLayerName() const;
		const char* GetVolumeTypeName(VolumeType type) const;
		const char* GetVolumeShapeName(VolumeShape shape) const;

		bool m_initialized = false;
		bool m_selected = false;
		bool m_dirty = false;
		GizmoMode m_mode = GizmoMode::Translate;
		GizmoAxis m_axis = GizmoAxis::X;
		float m_translateSnapStep = 0.5f;
		float m_rotationSnapStepDeg = 5.0f;
		engine::math::Vec3 m_position{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 m_rotationDeg{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 m_scale{ 1.0f, 1.0f, 1.0f };
		engine::math::Mat4 m_modelMatrix{};
		std::array<LayerState, 3> m_layers{{
			{ "Default", false, false },
			{ "Props", false, false },
			{ "Terrain", false, false }
		}};
		std::vector<VolumeEntity> m_volumes;
		uint32_t m_nextVolumeId = 1;
		int m_selectedVolumeIndex = -1;
		PlacementLayer m_activeLayer = PlacementLayer::Props;
		PlacementLayer m_objectLayer = PlacementLayer::Props;
		bool m_chunkOverlayEnabled = false;
		std::string m_chunkOverlayText;
		std::string m_contentRoot;
		std::string m_volumeLayoutPath = "layout.json";
		std::string m_meshPath = "meshes/test.mesh";
		std::string m_texturePath = "textures/test.texr";
		std::string m_lastWindowTitle;
	};
}
