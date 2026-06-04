// M100.32 — Implémentation du panneau « Interactive Props » (ImGui guardé).

#include "src/world_editor/InteractivePanel.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	using engine::world::interactive::InteractiveType;

	/// Réinitialise l'aperçu sur l'état initial de la définition.
	void InteractivePanel::ResetPreview()
	{
		m_preview = engine::world::interactive::MakeInitialRuntimeState(m_def);
	}

	/// Bascule l'aperçu (parité avec un trigger client, sans réseau).
	void InteractivePanel::Trigger()
	{
		engine::world::interactive::ToggleInteractive(m_preview);
	}

	/// Avance l'animation d'aperçu.
	void InteractivePanel::Update(float dtSec)
	{
		engine::world::interactive::UpdateInteractive(m_preview, m_def, dtSec);
	}

	/// Rendu ImGui du panneau. No-op hors Windows.
	void InteractivePanel::Render()
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Interactive Props");
		ImGui::Separator();

		const char* kTypes[] = { "DoorHinge", "DoorSliding", "WindowHinge", "Trapdoor", "ChestSimple" };
		int typeIdx = static_cast<int>(m_def.type);
		if (ImGui::Combo("Type", &typeIdx, kTypes, IM_ARRAYSIZE(kTypes)))
		{
			m_def.type = static_cast<InteractiveType>(typeIdx);
		}

		float pivot[3] = { m_def.pivotLocal.x, m_def.pivotLocal.y, m_def.pivotLocal.z };
		if (ImGui::InputFloat3("Pivot local", pivot))
		{
			m_def.pivotLocal.x = pivot[0]; m_def.pivotLocal.y = pivot[1]; m_def.pivotLocal.z = pivot[2];
		}

		float axis[3] = { m_def.axisLocal.x, m_def.axisLocal.y, m_def.axisLocal.z };
		if (ImGui::InputFloat3("Axis local", axis))
		{
			m_def.axisLocal.x = axis[0]; m_def.axisLocal.y = axis[1]; m_def.axisLocal.z = axis[2];
		}

		// Pour DoorSliding, le champ porte la translation (m) ; sinon l'angle (°).
		if (m_def.type == InteractiveType::DoorSliding)
			ImGui::InputFloat("Open translation (m)", &m_def.openAngleDeg);
		else
			ImGui::InputFloat("Open angle (deg)", &m_def.openAngleDeg);

		ImGui::InputFloat("Anim duration (s)", &m_def.animDurationSec);

		int initIdx = m_def.initialState != 0u ? 1 : 0;
		ImGui::RadioButton("Closed", &initIdx, 0); ImGui::SameLine();
		ImGui::RadioButton("Opened (rare)", &initIdx, 1);
		const uint8_t newInit = static_cast<uint8_t>(initIdx);
		if (newInit != m_def.initialState)
		{
			m_def.initialState = newInit;
			ResetPreview();
		}

		// Champs audio : tampons simples (la library d'évènements audio viendra
		// en 2e passe client ; ici on édite la clé brute).
		char openBuf[64];
		char closeBuf[64];
#if defined(_MSC_VER)
		strncpy_s(openBuf, m_def.audioOpenEvent.c_str(), sizeof(openBuf) - 1);
		strncpy_s(closeBuf, m_def.audioCloseEvent.c_str(), sizeof(closeBuf) - 1);
#else
		std::snprintf(openBuf, sizeof(openBuf), "%s", m_def.audioOpenEvent.c_str());
		std::snprintf(closeBuf, sizeof(closeBuf), "%s", m_def.audioCloseEvent.c_str());
#endif
		if (ImGui::InputText("Audio open", openBuf, sizeof(openBuf)))
			m_def.audioOpenEvent = openBuf;
		if (ImGui::InputText("Audio close", closeBuf, sizeof(closeBuf)))
			m_def.audioCloseEvent = closeBuf;

		ImGui::Separator();
		if (ImGui::Button("Trigger"))
			Trigger();
		ImGui::SameLine();
		ImGui::Text("openFactor=%.2f target=%u", m_preview.openFactor,
			static_cast<unsigned>(m_preview.targetState));
#endif
	}
}
