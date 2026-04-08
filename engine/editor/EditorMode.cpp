#include "engine/editor/EditorMode.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include "engine/render/AssetRegistry.h"
#include "engine/world/WorldModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>

namespace engine::editor
{
	namespace
	{
		constexpr float kPi = 3.14159265f;

		struct Ray
		{
			engine::math::Vec3 origin;
			engine::math::Vec3 direction;
		};

		struct Aabb
		{
			engine::math::Vec3 min;
			engine::math::Vec3 max;
		};

		engine::math::Mat4 MakeTranslation(const engine::math::Vec3& value)
		{
			engine::math::Mat4 out;
			out.m[12] = value.x;
			out.m[13] = value.y;
			out.m[14] = value.z;
			return out;
		}

		engine::math::Mat4 MakeScale(const engine::math::Vec3& value)
		{
			engine::math::Mat4 out;
			out.m[0] = value.x;
			out.m[5] = value.y;
			out.m[10] = value.z;
			return out;
		}

		engine::math::Mat4 MakeRotationX(float radians)
		{
			engine::math::Mat4 out;
			const float c = std::cos(radians);
			const float s = std::sin(radians);
			out.m[5] = c;
			out.m[6] = s;
			out.m[9] = -s;
			out.m[10] = c;
			return out;
		}

		engine::math::Mat4 MakeRotationY(float radians)
		{
			engine::math::Mat4 out;
			const float c = std::cos(radians);
			const float s = std::sin(radians);
			out.m[0] = c;
			out.m[2] = -s;
			out.m[8] = s;
			out.m[10] = c;
			return out;
		}

		engine::math::Mat4 MakeRotationZ(float radians)
		{
			engine::math::Mat4 out;
			const float c = std::cos(radians);
			const float s = std::sin(radians);
			out.m[0] = c;
			out.m[1] = s;
			out.m[4] = -s;
			out.m[5] = c;
			return out;
		}

		engine::math::Vec3 TransformPoint(const engine::math::Mat4& matrix, const engine::math::Vec3& point)
		{
			return {
				matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12],
				matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13],
				matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14]
			};
		}

		float SnapValue(float value, float step)
		{
			if (step <= 0.0f)
			{
				return value;
			}
			return std::round(value / step) * step;
		}

		Aabb BuildWorldAabb(const engine::render::MeshAsset& mesh, const engine::math::Mat4& modelMatrix)
		{
			const std::array<engine::math::Vec3, 8> corners = {
				engine::math::Vec3{ mesh.localBoundsMin.x, mesh.localBoundsMin.y, mesh.localBoundsMin.z },
				engine::math::Vec3{ mesh.localBoundsMax.x, mesh.localBoundsMin.y, mesh.localBoundsMin.z },
				engine::math::Vec3{ mesh.localBoundsMin.x, mesh.localBoundsMax.y, mesh.localBoundsMin.z },
				engine::math::Vec3{ mesh.localBoundsMax.x, mesh.localBoundsMax.y, mesh.localBoundsMin.z },
				engine::math::Vec3{ mesh.localBoundsMin.x, mesh.localBoundsMin.y, mesh.localBoundsMax.z },
				engine::math::Vec3{ mesh.localBoundsMax.x, mesh.localBoundsMin.y, mesh.localBoundsMax.z },
				engine::math::Vec3{ mesh.localBoundsMin.x, mesh.localBoundsMax.y, mesh.localBoundsMax.z },
				engine::math::Vec3{ mesh.localBoundsMax.x, mesh.localBoundsMax.y, mesh.localBoundsMax.z }
			};

			Aabb out{};
			out.min = TransformPoint(modelMatrix, corners[0]);
			out.max = out.min;
			for (size_t i = 1; i < corners.size(); ++i)
			{
				const engine::math::Vec3 worldCorner = TransformPoint(modelMatrix, corners[i]);
				out.min.x = std::min(out.min.x, worldCorner.x);
				out.min.y = std::min(out.min.y, worldCorner.y);
				out.min.z = std::min(out.min.z, worldCorner.z);
				out.max.x = std::max(out.max.x, worldCorner.x);
				out.max.y = std::max(out.max.y, worldCorner.y);
				out.max.z = std::max(out.max.z, worldCorner.z);
			}
			return out;
		}

		bool IntersectRayAabb(const Ray& ray, const Aabb& bounds)
		{
			float tMin = 0.0f;
			float tMax = std::numeric_limits<float>::max();
			const float epsilon = 1e-6f;

			const float origins[3] = { ray.origin.x, ray.origin.y, ray.origin.z };
			const float directions[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
			const float mins[3] = { bounds.min.x, bounds.min.y, bounds.min.z };
			const float maxs[3] = { bounds.max.x, bounds.max.y, bounds.max.z };

			for (int axis = 0; axis < 3; ++axis)
			{
				const float dir = directions[axis];
				if (std::abs(dir) <= epsilon)
				{
					if (origins[axis] < mins[axis] || origins[axis] > maxs[axis])
					{
						return false;
					}
					continue;
				}

				const float invDir = 1.0f / dir;
				float t0 = (mins[axis] - origins[axis]) * invDir;
				float t1 = (maxs[axis] - origins[axis]) * invDir;
				if (t0 > t1)
				{
					std::swap(t0, t1);
				}
				tMin = std::max(tMin, t0);
				tMax = std::min(tMax, t1);
				if (tMax < tMin)
				{
					return false;
				}
			}

			return tMax >= 0.0f;
		}

		Ray BuildCameraRay(const engine::render::Camera& camera, int viewportWidth, int viewportHeight, int mouseX, int mouseY)
		{
			const float aspect = viewportHeight > 0
				? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
				: camera.aspect;
			const float halfTan = std::tan((camera.fovYDeg * kPi / 180.0f) * 0.5f);
			const float ndcX = ((static_cast<float>(mouseX) + 0.5f) / static_cast<float>(viewportWidth)) * 2.0f - 1.0f;
			const float ndcY = 1.0f - ((static_cast<float>(mouseY) + 0.5f) / static_cast<float>(viewportHeight)) * 2.0f;

			const float cy = std::cos(camera.yaw);
			const float sy = std::sin(camera.yaw);
			const float cp = std::cos(camera.pitch);
			const float sp = std::sin(camera.pitch);

			engine::math::Vec3 forward(-sy * cp, -sp, -cy * cp);
			engine::math::Vec3 right(forward.z, 0.0f, -forward.x);
			const float rightLen = right.Length();
			right = rightLen > 0.0f ? right * (1.0f / rightLen) : engine::math::Vec3(1.0f, 0.0f, 0.0f);

			engine::math::Vec3 up(
				forward.y * right.z - forward.z * right.y,
				forward.z * right.x - forward.x * right.z,
				forward.x * right.y - forward.y * right.x);
			const float upLen = up.Length();
			up = upLen > 0.0f ? up * (1.0f / upLen) : engine::math::Vec3(0.0f, 1.0f, 0.0f);

			engine::math::Vec3 direction = (forward + right * (ndcX * aspect * halfTan) + up * (ndcY * halfTan)).Normalized();
			return { camera.position, direction };
		}
	}

	bool EditorMode::Init(const engine::core::Config& config)
	{
		m_contentRoot = config.GetString("paths.content", "game/data");
		m_zoneJsonRel = config.GetString("editor.zone_json", "");
		if (!m_zoneJsonRel.empty())
		{
			const std::filesystem::path zonePath = engine::platform::FileSystem::ResolveContentPath(config, m_zoneJsonRel);
			const std::string json = engine::platform::FileSystem::ReadAllText(zonePath);
			if (json.empty())
			{
				LOG_WARN(Core, "[EditorMode] editor.zone_json set but file missing or empty ({})", zonePath.string());
			}
			else
			{
				engine::world::ZoneDescriptorV1 zoneDesc{};
				std::string zerr;
				if (!engine::world::ParseZoneDescriptorJson(json, zoneDesc, zerr))
				{
					LOG_WARN(Core, "[EditorMode] zone.json parse failed: {} ({})", zerr, zonePath.string());
				}
				else
				{
					const std::filesystem::path hm = engine::world::ResolveZoneHeightmapPath(zonePath, zoneDesc);
					std::string verr;
					if (!engine::world::ValidateZoneHeightmapAgainstFile(zonePath, zoneDesc, verr))
					{
						LOG_WARN(Core, "[EditorMode] Zone heightmap validation failed: {} ({})", verr, zonePath.string());
					}
					LOG_INFO(Core, "[EditorMode] Zone descriptor OK id={} heightmap={} seed={}",
						zoneDesc.zone_id,
						hm.string(),
						zoneDesc.has_seed ? std::to_string(zoneDesc.seed) : std::string("(none)"));
				}
			}
		}
		m_mode = GizmoMode::Translate;
		m_axis = GizmoAxis::X;
		m_activeLayer = PlacementLayer::Props;
		m_objectLayer = PlacementLayer::Props;
		m_translateSnapStep = 0.5f;
		m_rotationSnapStepDeg = 5.0f;
		m_position = { 0.0f, 0.0f, 0.0f };
		m_rotationDeg = { 0.0f, 0.0f, 0.0f };
		m_scale = { 1.0f, 1.0f, 1.0f };
		m_dirty = false;
		m_volumes.clear();
		m_nextVolumeId = 1;
		m_selectedVolumeIndex = -1;
		RebuildModelMatrix();
		m_initialized = true;
		m_lastWindowTitle.clear();
		LOG_INFO(Core, "[EditorMode] Init OK (content_root={}, layout={}, mesh={}, texture={})", m_contentRoot, m_volumeLayoutPath, m_meshPath, m_texturePath);
		LOG_INFO(Core, "[EditorMode] Controls: N/M/B=create trigger/spawn/transition, Q=select volume, O=shape, V=property, C=chunk overlay, 1/2/3=gizmo, X/Y/Z=axis, arrows=edit, Ctrl+S=export");
		return true;
	}

	void EditorMode::Shutdown(engine::platform::Window& window)
	{
		if (!m_initialized)
		{
			LOG_INFO(Core, "[EditorMode] Destroyed");
			return;
		}

		window.SetTitle("LCDLLN Engine");
		m_initialized = false;
		m_selected = false;
		m_lastWindowTitle.clear();
		LOG_INFO(Core, "[EditorMode] Destroyed");
	}

	engine::render::Camera EditorMode::BuildInitialCamera() const
	{
		engine::render::Camera camera;
		camera.position = { 0.0f, 1.5f, 5.0f };
		camera.pitch = 0.18f;
		return camera;
	}

	void EditorMode::Update(engine::platform::Input& input,
		engine::platform::Window& window,
		const engine::render::Camera& camera,
		const engine::render::MeshAsset* mesh,
		int viewportWidth,
		int viewportHeight,
		double dtSeconds)
	{
		if (!m_initialized)
		{
			return;
		}

		bool forceLog = UpdateWorkflowControls(input, mesh);

		if (input.WasPressed(engine::platform::Key::Digit1))
		{
			m_mode = GizmoMode::Translate;
			forceLog = true;
			LOG_INFO(Core, "[EditorMode] Gizmo mode=Translate");
		}
		else if (input.WasPressed(engine::platform::Key::Digit2))
		{
			m_mode = GizmoMode::Rotate;
			forceLog = true;
			LOG_INFO(Core, "[EditorMode] Gizmo mode=Rotate");
		}
		else if (input.WasPressed(engine::platform::Key::Digit3))
		{
			m_mode = GizmoMode::Scale;
			forceLog = true;
			LOG_INFO(Core, "[EditorMode] Gizmo mode=Scale");
		}

		if (input.WasPressed(engine::platform::Key::X))
		{
			m_axis = GizmoAxis::X;
			forceLog = true;
			LOG_INFO(Core, "[EditorMode] Gizmo axis=X");
		}
		else if (input.WasPressed(engine::platform::Key::Y))
		{
			m_axis = GizmoAxis::Y;
			forceLog = true;
			LOG_INFO(Core, "[EditorMode] Gizmo axis=Y");
		}
		else if (input.WasPressed(engine::platform::Key::Z))
		{
			m_axis = GizmoAxis::Z;
			forceLog = true;
			LOG_INFO(Core, "[EditorMode] Gizmo axis=Z");
		}

		if (input.WasMousePressed(engine::platform::MouseButton::Left))
		{
			forceLog |= UpdateSelection(input, camera, mesh, viewportWidth, viewportHeight);
		}

		if (UpdateGizmo(input, dtSeconds))
		{
			forceLog = true;
		}

		m_chunkOverlayText = m_chunkOverlayEnabled ? BuildChunkOverlayText(camera) : "off";
		RefreshShell(window, forceLog);
	}

	bool EditorMode::IsObjectVisible() const
	{
		return !GetObjectLayerState().hidden;
	}

	void EditorMode::RebuildModelMatrix()
	{
		const engine::math::Mat4 translation = MakeTranslation(m_position);
		const engine::math::Mat4 rotationX = MakeRotationX(m_rotationDeg.x * kPi / 180.0f);
		const engine::math::Mat4 rotationY = MakeRotationY(m_rotationDeg.y * kPi / 180.0f);
		const engine::math::Mat4 rotationZ = MakeRotationZ(m_rotationDeg.z * kPi / 180.0f);
		const engine::math::Mat4 scale = MakeScale(m_scale);
		m_modelMatrix = translation * rotationZ * rotationY * rotationX * scale;
	}

	void EditorMode::RefreshShell(engine::platform::Window& window, bool forceLog)
	{
		const std::string title = std::format(
			"LCDLLN Engine --editor{} | Scene: {} | Inspector: {} | Asset Browser: {}",
			m_dirty ? " *dirty*" : "",
			BuildScenePanelText(),
			BuildInspectorPanelText(),
			BuildAssetBrowserPanelText());
		if (title != m_lastWindowTitle)
		{
			window.SetTitle(title);
			m_lastWindowTitle = title;
			forceLog = true;
		}

		if (forceLog)
		{
			LOG_INFO(Core, "[EditorUI][Scene] {}", BuildScenePanelText());
			LOG_INFO(Core, "[EditorUI][Inspector] {}", BuildInspectorPanelText());
			LOG_INFO(Core, "[EditorUI][AssetBrowser] {}", BuildAssetBrowserPanelText());
		}
	}

	bool EditorMode::UpdateSelection(const engine::platform::Input& input, const engine::render::Camera& camera, const engine::render::MeshAsset* mesh, int viewportWidth, int viewportHeight)
	{
		bool volumeHit = false;
		int hitVolumeIndex = -1;
		if (viewportWidth > 0 && viewportHeight > 0)
		{
			const Ray ray = BuildCameraRay(camera, viewportWidth, viewportHeight, input.MouseX(), input.MouseY());
			for (size_t i = 0; i < m_volumes.size(); ++i)
			{
				const VolumeEntity& volume = m_volumes[i];
				if (GetLayerState(volume.layer).hidden || GetLayerState(volume.layer).locked)
				{
					continue;
				}

				Aabb bounds{};
				if (volume.shape == VolumeShape::Sphere)
				{
					bounds.min = { volume.position.x - volume.sphereRadius, volume.position.y - volume.sphereRadius, volume.position.z - volume.sphereRadius };
					bounds.max = { volume.position.x + volume.sphereRadius, volume.position.y + volume.sphereRadius, volume.position.z + volume.sphereRadius };
				}
				else
				{
					bounds.min = volume.position - volume.boxHalfExtents;
					bounds.max = volume.position + volume.boxHalfExtents;
				}

				if (IntersectRayAabb(ray, bounds))
				{
					volumeHit = true;
					hitVolumeIndex = static_cast<int>(i);
					break;
				}
			}
		}

		if (volumeHit)
		{
			m_selected = true;
			m_selectedVolumeIndex = hitVolumeIndex;
			LOG_INFO(Core, "[EditorMode] Selection hit volume id={} type={}", m_volumes[hitVolumeIndex].id, GetVolumeTypeName(m_volumes[hitVolumeIndex].type));
			return true;
		}

		if (GetObjectLayerState().hidden)
		{
			if (m_selected)
			{
				m_selected = false;
				LOG_INFO(Core, "[EditorMode] Selection cleared: object layer hidden");
				return true;
			}
			LOG_WARN(Core, "[EditorMode] Selection ignored: object layer hidden");
			return false;
		}
		if (GetObjectLayerState().locked)
		{
			LOG_WARN(Core, "[EditorMode] Selection ignored: object layer locked");
			return false;
		}
		if (!mesh || !mesh->hasLocalBounds || viewportWidth <= 0 || viewportHeight <= 0)
		{
			LOG_WARN(Core, "[EditorMode] Selection skipped: missing mesh bounds or invalid viewport");
			return false;
		}

		const Ray ray = BuildCameraRay(camera, viewportWidth, viewportHeight, input.MouseX(), input.MouseY());
		const Aabb worldBounds = BuildWorldAabb(*mesh, m_modelMatrix);
		const bool wasSelected = m_selected;
		m_selected = IntersectRayAabb(ray, worldBounds);
		if (m_selected)
		{
			LOG_INFO(Core, "[EditorMode] Selection hit TestMesh");
		}
		else if (wasSelected)
		{
			LOG_INFO(Core, "[EditorMode] Selection cleared");
		}
		else
		{
			LOG_INFO(Core, "[EditorMode] Selection miss");
		}
		return m_selected != wasSelected;
	}

	bool EditorMode::UpdateGizmo(engine::platform::Input& input, double)
	{
		if (!m_selected)
		{
			return false;
		}

		int direction = 0;
		if (input.WasPressed(engine::platform::Key::Right) || input.WasPressed(engine::platform::Key::Up))
		{
			++direction;
		}
		if (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Down))
		{
			--direction;
		}
		if (direction == 0)
		{
			return false;
		}

		float* target = nullptr;
		VolumeEntity* selectedVolume = GetSelectedVolume();
		if (selectedVolume)
		{
			if (GetLayerState(selectedVolume->layer).locked)
			{
				LOG_WARN(Core, "[EditorMode] Gizmo ignored: selected volume layer locked");
				return false;
			}

			switch (m_mode)
			{
			case GizmoMode::Translate:
				target = (m_axis == GizmoAxis::X) ? &selectedVolume->position.x : (m_axis == GizmoAxis::Y) ? &selectedVolume->position.y : &selectedVolume->position.z;
				*target = SnapValue(*target + m_translateSnapStep * static_cast<float>(direction), m_translateSnapStep);
				break;
			case GizmoMode::Rotate:
				target = (m_axis == GizmoAxis::X) ? &selectedVolume->rotationDeg.x : (m_axis == GizmoAxis::Y) ? &selectedVolume->rotationDeg.y : &selectedVolume->rotationDeg.z;
				*target = SnapValue(*target + m_rotationSnapStepDeg * static_cast<float>(direction), m_rotationSnapStepDeg);
				break;
			case GizmoMode::Scale:
				if (selectedVolume->shape == VolumeShape::Sphere)
				{
					selectedVolume->sphereRadius = std::max(m_translateSnapStep, SnapValue(selectedVolume->sphereRadius + m_translateSnapStep * static_cast<float>(direction), m_translateSnapStep));
				}
				else
				{
					target = (m_axis == GizmoAxis::X) ? &selectedVolume->boxHalfExtents.x : (m_axis == GizmoAxis::Y) ? &selectedVolume->boxHalfExtents.y : &selectedVolume->boxHalfExtents.z;
					*target = std::max(m_translateSnapStep, SnapValue(*target + m_translateSnapStep * static_cast<float>(direction), m_translateSnapStep));
				}
				break;
			}

			MarkDirty("volume-gizmo");
			LOG_INFO(Core, "[EditorMode] Volume gizmo apply (id={}, type={}, shape={}, mode={}, axis={})",
				selectedVolume->id, GetVolumeTypeName(selectedVolume->type), GetVolumeShapeName(selectedVolume->shape), GetModeName(), GetAxisName());
			return true;
		}

		if (GetObjectLayerState().locked)
		{
			if (input.IsDown(engine::platform::Key::Left) || input.IsDown(engine::platform::Key::Right)
				|| input.IsDown(engine::platform::Key::Up) || input.IsDown(engine::platform::Key::Down))
			{
				LOG_WARN(Core, "[EditorMode] Gizmo ignored: object layer locked");
			}
			return false;
		}

		switch (m_mode)
		{
		case GizmoMode::Translate:
			target = (m_axis == GizmoAxis::X) ? &m_position.x : (m_axis == GizmoAxis::Y) ? &m_position.y : &m_position.z;
			*target = SnapValue(*target + m_translateSnapStep * static_cast<float>(direction), m_translateSnapStep);
			break;
		case GizmoMode::Rotate:
			target = (m_axis == GizmoAxis::X) ? &m_rotationDeg.x : (m_axis == GizmoAxis::Y) ? &m_rotationDeg.y : &m_rotationDeg.z;
			*target = SnapValue(*target + m_rotationSnapStepDeg * static_cast<float>(direction), m_rotationSnapStepDeg);
			break;
		case GizmoMode::Scale:
			target = (m_axis == GizmoAxis::X) ? &m_scale.x : (m_axis == GizmoAxis::Y) ? &m_scale.y : &m_scale.z;
			*target = std::max(m_translateSnapStep, SnapValue(*target + m_translateSnapStep * static_cast<float>(direction), m_translateSnapStep));
			break;
		}

		RebuildModelMatrix();
		MarkDirty("gizmo");
		LOG_INFO(Core, "[EditorMode] Gizmo apply (mode={}, axis={}, translate_step={}, rotate_step_deg={})",
			GetModeName(), GetAxisName(), m_translateSnapStep, m_rotationSnapStepDeg);
		return true;
	}

	bool EditorMode::UpdateWorkflowControls(engine::platform::Input& input, const engine::render::MeshAsset* mesh)
	{
		bool changed = false;

		if (input.WasPressed(engine::platform::Key::C))
		{
			changed |= ToggleChunkOverlay();
		}

		if (input.WasPressed(engine::platform::Key::N))
		{
			changed |= CreateVolume(VolumeType::Trigger);
		}
		if (input.WasPressed(engine::platform::Key::M))
		{
			changed |= CreateVolume(VolumeType::SpawnArea);
		}
		if (input.WasPressed(engine::platform::Key::B))
		{
			changed |= CreateVolume(VolumeType::ZoneTransition);
		}
		if (input.WasPressed(engine::platform::Key::Q))
		{
			changed |= SelectNextVolume();
		}
		if (input.WasPressed(engine::platform::Key::O))
		{
			changed |= ToggleSelectedVolumeShape();
		}
		if (input.WasPressed(engine::platform::Key::V))
		{
			changed |= CycleSelectedVolumeProperty();
		}

		if (input.WasPressed(engine::platform::Key::Tab))
		{
			CycleActiveLayer();
			changed = true;
		}

		if (input.WasPressed(engine::platform::Key::G))
		{
			m_translateSnapStep = (m_translateSnapStep < 1.0f) ? 1.0f : 0.5f;
			LOG_INFO(Core, "[EditorMode] Grid snap={}m", m_translateSnapStep);
			changed = true;
		}

		if (input.WasPressed(engine::platform::Key::R))
		{
			m_rotationSnapStepDeg = (m_rotationSnapStepDeg < 15.0f) ? 15.0f : 5.0f;
			LOG_INFO(Core, "[EditorMode] Rotation snap={}deg", m_rotationSnapStepDeg);
			changed = true;
		}

		if (input.WasPressed(engine::platform::Key::H))
		{
			LayerState& activeLayer = m_layers[static_cast<size_t>(m_activeLayer)];
			activeLayer.hidden = !activeLayer.hidden;
			if (m_activeLayer == m_objectLayer && activeLayer.hidden)
			{
				m_selected = false;
			}
			MarkDirty("layer-hidden");
			LOG_INFO(Core, "[EditorMode] Layer visibility toggled (layer={}, hidden={})", activeLayer.name, activeLayer.hidden ? "true" : "false");
			changed = true;
		}

		if (input.WasPressed(engine::platform::Key::L))
		{
			LayerState& activeLayer = m_layers[static_cast<size_t>(m_activeLayer)];
			activeLayer.locked = !activeLayer.locked;
			if (m_activeLayer == m_objectLayer && activeLayer.locked)
			{
				m_selected = false;
			}
			MarkDirty("layer-lock");
			LOG_INFO(Core, "[EditorMode] Layer lock toggled (layer={}, locked={})", activeLayer.name, activeLayer.locked ? "true" : "false");
			changed = true;
		}

		if (input.WasPressed(engine::platform::Key::F))
		{
			changed |= AlignSelectionToGround(mesh);
		}

		if (input.IsDown(engine::platform::Key::Control) && input.WasPressed(engine::platform::Key::S))
		{
			SaveDirtyState();
			changed = true;
		}

		return changed;
	}

	bool EditorMode::AlignSelectionToGround(const engine::render::MeshAsset* mesh)
	{
		if (VolumeEntity* selectedVolume = GetSelectedVolume())
		{
			if (GetLayerState(selectedVolume->layer).locked)
			{
				LOG_WARN(Core, "[EditorMode] Align-to-ground ignored: selected volume layer locked");
				return false;
			}
			const float groundOffset = (selectedVolume->shape == VolumeShape::Sphere)
				? selectedVolume->sphereRadius
				: selectedVolume->boxHalfExtents.y;
			selectedVolume->position.y = groundOffset;
			MarkDirty("align-volume-ground");
			LOG_INFO(Core, "[EditorMode] Align-to-ground OK for volume (id={}, ground_y=0)", selectedVolume->id);
			return true;
		}

		if (!m_selected)
		{
			LOG_WARN(Core, "[EditorMode] Align-to-ground ignored: no selection");
			return false;
		}
		if (GetObjectLayerState().locked)
		{
			LOG_WARN(Core, "[EditorMode] Align-to-ground ignored: object layer locked");
			return false;
		}
		if (!mesh || !mesh->hasLocalBounds)
		{
			LOG_WARN(Core, "[EditorMode] Align-to-ground failed: missing mesh bounds");
			return false;
		}

		const Aabb worldBounds = BuildWorldAabb(*mesh, m_modelMatrix);
		const float offsetY = -worldBounds.min.y;
		m_position.y += offsetY;
		RebuildModelMatrix();
		MarkDirty("align-ground");
		LOG_INFO(Core, "[EditorMode] Align-to-ground OK (ground_y=0, offset_y={:.3f})", offsetY);
		return true;
	}

	void EditorMode::MarkDirty(std::string_view reason)
	{
		if (!m_dirty)
		{
			LOG_INFO(Core, "[EditorMode] Dirty state ON ({})", reason);
		}
		m_dirty = true;
	}

	void EditorMode::SaveDirtyState()
	{
		if (!m_dirty)
		{
			LOG_INFO(Core, "[EditorMode] Save skipped: already clean");
			return;
		}
		if (!ExportLayout())
		{
			LOG_ERROR(Core, "[EditorMode] Save dirty state FAILED: export");
			return;
		}
		m_dirty = false;
		LOG_INFO(Core, "[EditorMode] Save dirty state OK");
	}

	void EditorMode::CycleActiveLayer()
	{
		const uint32_t next = (static_cast<uint32_t>(m_activeLayer) + 1u) % static_cast<uint32_t>(m_layers.size());
		m_activeLayer = static_cast<PlacementLayer>(next);
		LOG_INFO(Core, "[EditorMode] Active layer={}", GetActiveLayerName());
	}

	bool EditorMode::IsActiveLayerLocked() const
	{
		return m_layers[static_cast<size_t>(m_activeLayer)].locked;
	}

	const LayerState& EditorMode::GetObjectLayerState() const
	{
		return m_layers[static_cast<size_t>(m_objectLayer)];
	}

	LayerState& EditorMode::GetObjectLayerState()
	{
		return m_layers[static_cast<size_t>(m_objectLayer)];
	}

	const LayerState& EditorMode::GetLayerState(PlacementLayer layer) const
	{
		return m_layers[static_cast<size_t>(layer)];
	}

	std::string EditorMode::BuildScenePanelText() const
	{
		return std::format("TestMesh layer={}{}{} | volumes={} {} | overlay={}",
			GetObjectLayerState().name,
			m_selected ? " [selected]" : " [idle]",
			IsObjectVisible() ? "" : " [hidden]",
			m_volumes.size(),
			BuildSelectedVolumeSummary(),
			m_chunkOverlayText.empty() ? "off" : m_chunkOverlayText);
	}

	std::string EditorMode::BuildInspectorPanelText() const
	{
		return std::format(
			"mode={} axis={} active_layer={} layer_locked={} dirty={} grid={}m rot={}deg object_pos=({:.2f},{:.2f},{:.2f}) object_rot=({:.1f},{:.1f},{:.1f}) object_scale=({:.2f},{:.2f},{:.2f}) selected_volume={}",
			GetModeName(),
			GetAxisName(),
			GetActiveLayerName(),
			IsActiveLayerLocked() ? "true" : "false",
			m_dirty ? "true" : "false",
			m_translateSnapStep,
			m_rotationSnapStepDeg,
			m_position.x, m_position.y, m_position.z,
			m_rotationDeg.x, m_rotationDeg.y, m_rotationDeg.z,
			m_scale.x, m_scale.y, m_scale.z,
			BuildSelectedVolumeSummary());
	}

	std::string EditorMode::BuildAssetBrowserPanelText() const
	{
		return std::format("{} | {} | {} | export={} | zone_json={} | layers=[{}:{}:{}, {}:{}:{}, {}:{}:{}]",
			m_contentRoot,
			m_meshPath,
			m_texturePath,
			m_volumeLayoutPath,
			m_zoneJsonRel.empty() ? "-" : m_zoneJsonRel,
			m_layers[0].name, m_layers[0].hidden ? "hidden" : "visible", m_layers[0].locked ? "locked" : "editable",
			m_layers[1].name, m_layers[1].hidden ? "hidden" : "visible", m_layers[1].locked ? "locked" : "editable",
			m_layers[2].name, m_layers[2].hidden ? "hidden" : "visible", m_layers[2].locked ? "locked" : "editable");
	}

	const char* EditorMode::GetModeName() const
	{
		switch (m_mode)
		{
		case GizmoMode::Translate: return "Translate";
		case GizmoMode::Rotate: return "Rotate";
		case GizmoMode::Scale: return "Scale";
		default: return "Unknown";
		}
	}

	const char* EditorMode::GetAxisName() const
	{
		switch (m_axis)
		{
		case GizmoAxis::X: return "X";
		case GizmoAxis::Y: return "Y";
		case GizmoAxis::Z: return "Z";
		default: return "Unknown";
		}
	}

	const char* EditorMode::GetActiveLayerName() const
	{
		return m_layers[static_cast<size_t>(m_activeLayer)].name;
	}

	bool EditorMode::CreateVolume(VolumeType type)
	{
		VolumeEntity volume{};
		volume.id = m_nextVolumeId++;
		volume.type = type;
		volume.shape = (type == VolumeType::SpawnArea) ? VolumeShape::Sphere : VolumeShape::Box;
		volume.layer = m_activeLayer;
		volume.position = m_position;
		volume.rotationDeg = { 0.0f, 0.0f, 0.0f };
		switch (type)
		{
		case VolumeType::Trigger:
			volume.stringId = "trigger.default";
			break;
		case VolumeType::SpawnArea:
			volume.stringId = "spawn.default";
			break;
		case VolumeType::ZoneTransition:
			volume.stringId = "transition.default";
			volume.targetZoneId = "zone.next";
			break;
		}

		m_volumes.push_back(volume);
		m_selectedVolumeIndex = static_cast<int>(m_volumes.size()) - 1;
		m_selected = true;
		MarkDirty("create-volume");
		LOG_INFO(Core, "[EditorMode] Volume created (id={}, type={}, shape={}, layer={})",
			volume.id, GetVolumeTypeName(volume.type), GetVolumeShapeName(volume.shape), GetLayerState(volume.layer).name);
		return true;
	}

	bool EditorMode::SelectNextVolume()
	{
		if (m_volumes.empty())
		{
			LOG_WARN(Core, "[EditorMode] Select volume ignored: no volumes");
			return false;
		}
		m_selectedVolumeIndex = (m_selectedVolumeIndex + 1) % static_cast<int>(m_volumes.size());
		m_selected = true;
		const VolumeEntity& volume = m_volumes[static_cast<size_t>(m_selectedVolumeIndex)];
		LOG_INFO(Core, "[EditorMode] Volume selected (id={}, type={}, shape={})", volume.id, GetVolumeTypeName(volume.type), GetVolumeShapeName(volume.shape));
		return true;
	}

	bool EditorMode::ToggleSelectedVolumeShape()
	{
		VolumeEntity* selectedVolume = GetSelectedVolume();
		if (!selectedVolume)
		{
			LOG_WARN(Core, "[EditorMode] Toggle shape ignored: no selected volume");
			return false;
		}
		selectedVolume->shape = (selectedVolume->shape == VolumeShape::Box) ? VolumeShape::Sphere : VolumeShape::Box;
		MarkDirty("volume-shape");
		LOG_INFO(Core, "[EditorMode] Volume shape toggled (id={}, shape={})", selectedVolume->id, GetVolumeShapeName(selectedVolume->shape));
		return true;
	}

	bool EditorMode::CycleSelectedVolumeProperty()
	{
		VolumeEntity* selectedVolume = GetSelectedVolume();
		if (!selectedVolume)
		{
			LOG_WARN(Core, "[EditorMode] Cycle property ignored: no selected volume");
			return false;
		}

		switch (selectedVolume->type)
		{
		case VolumeType::Trigger:
			selectedVolume->stringId = (selectedVolume->stringId == "trigger.default") ? "trigger.alt" : "trigger.default";
			break;
		case VolumeType::SpawnArea:
			selectedVolume->stringId = (selectedVolume->stringId == "spawn.default") ? "spawn.elite" : "spawn.default";
			break;
		case VolumeType::ZoneTransition:
			selectedVolume->targetZoneId = (selectedVolume->targetZoneId == "zone.next") ? "zone.return" : "zone.next";
			break;
		}

		MarkDirty("volume-property");
		LOG_INFO(Core, "[EditorMode] Volume property cycled (id={}, type={})", selectedVolume->id, GetVolumeTypeName(selectedVolume->type));
		return true;
	}

	bool EditorMode::ExportLayout()
	{
		if (!m_meshPath.ends_with(".gltf") && !m_meshPath.ends_with(".glb"))
		{
			LOG_WARN(Core, "[EditorMode] Export layout: instances array stays empty because editor mesh is not a glTF asset ({})", m_meshPath);
		}
		const std::string json = BuildLayoutJson();
		const std::filesystem::path layoutPath = engine::platform::FileSystem::Join(m_contentRoot, m_volumeLayoutPath);
		if (!engine::platform::FileSystem::WriteAllText(layoutPath, json))
		{
			LOG_ERROR(Core, "[EditorMode] Export layout FAILED (path={})", layoutPath.string());
			return false;
		}
		LOG_INFO(Core, "[EditorMode] Export layout OK (path={}, volumes={})", layoutPath.string(), m_volumes.size());
		return true;
	}

	bool EditorMode::ToggleChunkOverlay()
	{
		m_chunkOverlayEnabled = !m_chunkOverlayEnabled;
		LOG_INFO(Core, "[EditorMode] Chunk overlay {}", m_chunkOverlayEnabled ? "ON" : "OFF");
		return true;
	}

	VolumeEntity* EditorMode::GetSelectedVolume()
	{
		if (m_selectedVolumeIndex < 0 || static_cast<size_t>(m_selectedVolumeIndex) >= m_volumes.size())
		{
			return nullptr;
		}
		return &m_volumes[static_cast<size_t>(m_selectedVolumeIndex)];
	}

	const VolumeEntity* EditorMode::GetSelectedVolume() const
	{
		if (m_selectedVolumeIndex < 0 || static_cast<size_t>(m_selectedVolumeIndex) >= m_volumes.size())
		{
			return nullptr;
		}
		return &m_volumes[static_cast<size_t>(m_selectedVolumeIndex)];
	}

	bool EditorMode::IsSelectedVolumeVisible() const
	{
		const VolumeEntity* selectedVolume = GetSelectedVolume();
		return selectedVolume ? !GetLayerState(selectedVolume->layer).hidden : false;
	}

	bool EditorMode::IsSelectedVolumeLocked() const
	{
		const VolumeEntity* selectedVolume = GetSelectedVolume();
		return selectedVolume ? GetLayerState(selectedVolume->layer).locked : false;
	}

	std::string EditorMode::BuildSelectedVolumeSummary() const
	{
		const VolumeEntity* selectedVolume = GetSelectedVolume();
		if (!selectedVolume)
		{
			return "none";
		}

		if (selectedVolume->shape == VolumeShape::Sphere)
		{
			return std::format("id={} {} {} layer={} r={:.2f} pos=({:.1f},{:.1f},{:.1f}) prop={} target={} vis={} lock={} gizmo=lines",
				selectedVolume->id,
				GetVolumeTypeName(selectedVolume->type),
				GetVolumeShapeName(selectedVolume->shape),
				GetLayerState(selectedVolume->layer).name,
				selectedVolume->sphereRadius,
				selectedVolume->position.x, selectedVolume->position.y, selectedVolume->position.z,
				selectedVolume->stringId,
				selectedVolume->targetZoneId.empty() ? "-" : selectedVolume->targetZoneId,
				IsSelectedVolumeVisible() ? "on" : "off",
				IsSelectedVolumeLocked() ? "on" : "off");
		}

		return std::format("id={} {} {} layer={} ext=({:.2f},{:.2f},{:.2f}) pos=({:.1f},{:.1f},{:.1f}) prop={} target={} vis={} lock={} gizmo=lines",
			selectedVolume->id,
			GetVolumeTypeName(selectedVolume->type),
			GetVolumeShapeName(selectedVolume->shape),
			GetLayerState(selectedVolume->layer).name,
			selectedVolume->boxHalfExtents.x, selectedVolume->boxHalfExtents.y, selectedVolume->boxHalfExtents.z,
			selectedVolume->position.x, selectedVolume->position.y, selectedVolume->position.z,
			selectedVolume->stringId,
			selectedVolume->targetZoneId.empty() ? "-" : selectedVolume->targetZoneId,
			IsSelectedVolumeVisible() ? "on" : "off",
			IsSelectedVolumeLocked() ? "on" : "off");
	}

	std::string EditorMode::BuildLayoutJson() const
	{
		struct InstanceExport
		{
			std::string guid;
			std::string gltfPath;
			engine::math::Vec3 position;
		};

		std::vector<InstanceExport> instances;
		if (m_meshPath.ends_with(".gltf") || m_meshPath.ends_with(".glb"))
		{
			instances.push_back({ "editor.instance.1", m_meshPath, m_position });
		}

		std::vector<const VolumeEntity*> volumesSorted;
		std::vector<const VolumeEntity*> spawnsSorted;
		std::vector<const VolumeEntity*> transitionsSorted;
		volumesSorted.reserve(m_volumes.size());
		for (const VolumeEntity& volume : m_volumes)
		{
			volumesSorted.push_back(&volume);
			if (volume.type == VolumeType::SpawnArea)
			{
				spawnsSorted.push_back(&volume);
			}
			if (volume.type == VolumeType::ZoneTransition)
			{
				transitionsSorted.push_back(&volume);
			}
		}

		const auto sortByIdType = [](const VolumeEntity* a, const VolumeEntity* b)
		{
			if (a->id != b->id)
			{
				return a->id < b->id;
			}
			return static_cast<uint32_t>(a->type) < static_cast<uint32_t>(b->type);
		};
		std::sort(volumesSorted.begin(), volumesSorted.end(), sortByIdType);
		std::sort(spawnsSorted.begin(), spawnsSorted.end(), sortByIdType);
		std::sort(transitionsSorted.begin(), transitionsSorted.end(), sortByIdType);

		std::string json;
		json += "{\n";
		json += std::format("  \"version\": 1,\n");
		json += std::format("  \"instances\": [\n");
		for (size_t i = 0; i < instances.size(); ++i)
		{
			const InstanceExport& instance = instances[i];
			json += std::format(
				"    {{ \"guid\": \"{}\", \"gltf\": \"{}\", \"position\": [{:.3f}, {:.3f}, {:.3f}] }}{}\n",
				instance.guid,
				instance.gltfPath,
				instance.position.x, instance.position.y, instance.position.z,
				(i + 1u < instances.size()) ? "," : "");
		}
		json += "  ],\n";
		json += std::format("  \"volumes\": [\n");
		for (size_t i = 0; i < volumesSorted.size(); ++i)
		{
			const VolumeEntity& volume = *volumesSorted[i];
			json += std::format(
				"    {{ \"id\": {}, \"type\": \"{}\", \"shape\": \"{}\", \"layer\": \"{}\", \"position\": [{:.3f}, {:.3f}, {:.3f}], \"rotationDeg\": [{:.3f}, {:.3f}, {:.3f}], \"boxHalfExtents\": [{:.3f}, {:.3f}, {:.3f}], \"sphereRadius\": {:.3f}, \"stringId\": \"{}\", \"targetZoneId\": \"{}\" }}{}\n",
				volume.id,
				GetVolumeTypeName(volume.type),
				GetVolumeShapeName(volume.shape),
				GetLayerState(volume.layer).name,
				volume.position.x, volume.position.y, volume.position.z,
				volume.rotationDeg.x, volume.rotationDeg.y, volume.rotationDeg.z,
				volume.boxHalfExtents.x, volume.boxHalfExtents.y, volume.boxHalfExtents.z,
				volume.sphereRadius,
				volume.stringId,
				volume.targetZoneId,
				(i + 1u < volumesSorted.size()) ? "," : "");
		}
		json += "  ],\n";
		json += std::format("  \"spawns\": [\n");
		for (size_t i = 0; i < spawnsSorted.size(); ++i)
		{
			const VolumeEntity& volume = *spawnsSorted[i];
			json += std::format(
				"    {{ \"id\": {}, \"type\": \"{}\", \"shape\": \"{}\", \"position\": [{:.3f}, {:.3f}, {:.3f}], \"radius\": {:.3f}, \"stringId\": \"{}\" }}{}\n",
				volume.id,
				GetVolumeTypeName(volume.type),
				GetVolumeShapeName(volume.shape),
				volume.position.x, volume.position.y, volume.position.z,
				volume.sphereRadius,
				volume.stringId,
				(i + 1u < spawnsSorted.size()) ? "," : "");
		}
		json += "  ],\n";
		json += std::format("  \"transitions\": [\n");
		for (size_t i = 0; i < transitionsSorted.size(); ++i)
		{
			const VolumeEntity& volume = *transitionsSorted[i];
			json += std::format(
				"    {{ \"id\": {}, \"shape\": \"{}\", \"position\": [{:.3f}, {:.3f}, {:.3f}], \"targetZoneId\": \"{}\", \"stringId\": \"{}\" }}{}\n",
				volume.id,
				GetVolumeShapeName(volume.shape),
				volume.position.x, volume.position.y, volume.position.z,
				volume.targetZoneId,
				volume.stringId,
				(i + 1u < transitionsSorted.size()) ? "," : "");
		}
		json += "  ]\n";
		json += "}\n";
		return json;
	}

	std::string EditorMode::BuildChunkOverlayText(const engine::render::Camera& camera) const
	{
		const engine::world::GlobalChunkCoord chunk = engine::world::WorldToGlobalChunkCoord(camera.position.x, camera.position.z);
		engine::math::Vec3 highlightPosition = m_position;
		if (const VolumeEntity* selectedVolume = GetSelectedVolume())
		{
			highlightPosition = selectedVolume->position;
		}
		const engine::world::GlobalChunkCoord highlightChunk = engine::world::WorldToGlobalChunkCoord(highlightPosition.x, highlightPosition.z);
		constexpr int kCellSizeMeters = 64;
		const int cellX = static_cast<int>(std::floor(camera.position.x / static_cast<float>(kCellSizeMeters)));
		const int cellZ = static_cast<int>(std::floor(camera.position.z / static_cast<float>(kCellSizeMeters)));
		const auto bounds = engine::world::World::ChunkBounds(chunk);
		engine::world::World world;
		world.Update(camera.position);
		const engine::world::ChunkRing ring = world.GetRingForChunk(highlightChunk);
		return std::format("chunks256=on cells64=on chunk=({}, {}) cell=({}, {}) highlight=({}, {}) ring={} bounds=({:.0f},{:.0f})-({:.0f},{:.0f})",
			chunk.x,
			chunk.z,
			cellX,
			cellZ,
			highlightChunk.x,
			highlightChunk.z,
			GetChunkRingName(ring),
			bounds.minX,
			bounds.minZ,
			bounds.maxX,
			bounds.maxZ);
	}

	const char* EditorMode::GetVolumeTypeName(VolumeType type) const
	{
		switch (type)
		{
		case VolumeType::Trigger: return "trigger";
		case VolumeType::SpawnArea: return "spawnArea";
		case VolumeType::ZoneTransition: return "zoneTransition";
		default: return "unknown";
		}
	}

	const char* EditorMode::GetVolumeShapeName(VolumeShape shape) const
	{
		switch (shape)
		{
		case VolumeShape::Box: return "box";
		case VolumeShape::Sphere: return "sphere";
		default: return "unknown";
		}
	}

	const char* EditorMode::GetChunkRingName(engine::world::ChunkRing ring) const
	{
		switch (ring)
		{
		case engine::world::ChunkRing::Active: return "active";
		case engine::world::ChunkRing::Visible: return "visible";
		case engine::world::ChunkRing::Far: return "far";
		default: return "unknown";
		}
	}
}
