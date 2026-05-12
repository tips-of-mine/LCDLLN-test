// src/world_editor/hazard/HazardTool.h
#pragma once

#include "src/client/world/hazard/HazardVolumes.h"

namespace engine::editor::hazard
{
	class HazardDocument;

	/// Outil éditeur "Hazard". Stocke les paramètres courants (type, shape,
	/// dimensions, escape) et offre une méthode `PlaceAt(pos)` qui crée
	/// une nouvelle instance dans le HazardDocument cible.
	///
	/// MVP : pas de gizmo Vulkan ni d'undo via CommandStack. Le panneau ImGui
	/// (Tool Properties) appellera directement ces accesseurs. L'intégration
	/// avec CommandStack viendra avec M100.34 (Save/Load Zone orchestrateur).
	class HazardTool
	{
	public:
		void SetDocument(HazardDocument* doc) noexcept { m_document = doc; }

		void SetType(engine::world::hazard::HazardType t) noexcept { m_template.type = t; }
		void SetShape(engine::world::hazard::HazardShape s) noexcept { m_template.shape = s; }
		void SetBoxHalfExtents(engine::math::Vec3 he) noexcept { m_template.boxHalfExtents = he; }
		void SetCylRadius(float r) noexcept { m_template.cylRadius = r; }
		void SetCylHeight(float h) noexcept { m_template.cylHeight = h; }
		void SetSinkRateMps(float r) noexcept { m_template.sinkRateMps = r; }
		void SetMaxDepthMeters(float d) noexcept { m_template.maxDepthMeters = d; }
		void SetSlowdownMul(float m) noexcept { m_template.slowdownMul = m; }
		void SetEscapeMode(engine::world::hazard::EscapeMode m) noexcept { m_template.escapeMode = m; }
		void SetRequiredItemId(uint32_t id) noexcept { m_template.requiredItemId = id; }

		const engine::world::hazard::HazardInstance& Template() const noexcept { return m_template; }

		/// Place une instance à la position monde demandée (typiquement issue
		/// d'un raycast click). Retourne l'index de la nouvelle instance, ou
		/// SIZE_MAX si pas de document.
		size_t PlaceAt(engine::math::Vec3 worldPos);

	private:
		HazardDocument* m_document = nullptr;
		engine::world::hazard::HazardInstance m_template{};
	};
}
