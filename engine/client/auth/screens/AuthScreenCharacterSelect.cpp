// Phase 2 — Couche modèle pour l'écran de sélection de personnage.
// Affiche la liste reçue via CHARACTER_LIST_REQUEST (Phase 1) et propose 3 actions :
// Jouer (personnage sélectionné), Créer un nouveau personnage, Retour.

#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

#include <string>

namespace engine::client
{
#if defined(_WIN32)

	void AuthUiPresenter::ImGuiSelectCharacterEntry(int index)
	{
		if (m_phase != Phase::CharacterSelect)
			return;
		if (index < 0 || static_cast<size_t>(index) >= m_characterList.size())
			return;
		m_selectedCharacterIndex = index;
	}

	void AuthUiPresenter::ImGuiActivateSelectedCharacter()
	{
		if (m_phase != Phase::CharacterSelect)
			return;
		if (m_selectedCharacterIndex < 0
			|| static_cast<size_t>(m_selectedCharacterIndex) >= m_characterList.size())
		{
			return;
		}
		const auto& chosen = m_characterList[static_cast<size_t>(m_selectedCharacterIndex)];
		LOG_INFO(Core, "[AuthUiPresenter] CharacterSelect -> activate (character_id={}, name={})",
			chosen.character_id, chosen.name);
		// Phase 2 : on termine le flow d'auth et on laisse l'engine gérer la suite.
		// La transition propre vers la scène jeu (EnterWorldCommand + spawn) sera ajoutée en Phase 3.
		m_userErrorText.clear();
		m_infoBanner.clear();
		m_postRegistrationCharacterCreatePending = false;
		m_flowComplete = true;
	}

	void AuthUiPresenter::ImGuiCreateAnotherCharacterFromSelect()
	{
		if (m_phase != Phase::CharacterSelect)
			return;
		LOG_INFO(Core, "[AuthUiPresenter] CharacterSelect -> CharacterCreate (utilisateur veut un nouveau personnage)");
		m_characterName.clear();
		m_userErrorText.clear();
		m_activeField = 0;
		// Le shard est déjà choisi via ShardPick et le ticket déjà accepté ; on garde m_chosenShardId.
		// Pas de m_postRegistrationCharacterCreatePending : on n'est pas en post-Register.
		SetPhase(Phase::CharacterCreate);
	}

	void AuthUiPresenter::ImGuiCancelCharacterSelectReturnToLogin()
	{
		if (m_phase != Phase::CharacterSelect)
			return;
		LOG_INFO(Core, "[AuthUiPresenter] ImGui: CharacterSelect -> Login");
		m_userErrorText.clear();
		m_infoBanner.clear();
		m_characterList.clear();
		m_selectedCharacterIndex = -1;
		m_shardPickChoiceShardId = 0;
		m_shardPickEntries.clear();
		m_chosenShardId = 0;
		m_postRegistrationCharacterCreatePending = false;
		SetPhase(Phase::Login);
	}

	void AuthUiPresenter::BuildModel_CharacterSelect(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.panel.character_select");
		{
			RenderBodyLine hint{};
			hint.text = Tr("auth.character_select.hint");
			model.bodyLines.push_back(std::move(hint));
		}
		for (size_t i = 0; i < m_characterList.size(); ++i)
		{
			const auto& c = m_characterList[i];
			RenderBodyLine line{};
			line.text = Tr("auth.character_select.line",
				{ { "slot", std::to_string(static_cast<unsigned>(c.slot) + 1u) },
				  { "name", c.name.empty() ? std::string("?") : c.name },
				  { "level", std::to_string(c.level) } });
			line.active = (m_selectedCharacterIndex == static_cast<int>(i));
			line.link = true;
			model.bodyLines.push_back(std::move(line));
		}
		{
			RenderAction play{};
			play.labelKey = "auth.character_select.play";
			play.primary = true;
			play.active = (m_selectedCharacterIndex >= 0
				&& static_cast<size_t>(m_selectedCharacterIndex) < m_characterList.size());
			play.emphasized = false;
			play.hovered = (m_hoveredActionIndex == 0);
			model.actions.push_back(std::move(play));
		}
		{
			RenderAction create{};
			create.labelKey = "auth.character_select.create_new";
			create.primary = false;
			create.active = (m_characterList.size() < 5u);
			create.emphasized = false;
			create.hovered = (m_hoveredActionIndex == 1);
			model.actions.push_back(std::move(create));
		}
		{
			RenderAction back{};
			back.labelKey = "common.back";
			back.primary = false;
			back.active = true;
			back.emphasized = false;
			back.hovered = (m_hoveredActionIndex == 2);
			model.actions.push_back(std::move(back));
		}
	}

	void AuthUiPresenter::Update_CharacterSelect(engine::platform::Input& input, const engine::core::Config& /*cfg*/,
		engine::platform::Window& /*window*/, bool usingNativeAuth, bool authUiImguiMode)
	{
		if (usingNativeAuth || m_phase != Phase::CharacterSelect)
			return;
		const int n = static_cast<int>(m_characterList.size());
		if (n == 0)
			return;
		if (input.WasPressed(engine::platform::Key::Up) || input.WasPressed(engine::platform::Key::Left))
		{
			if (m_selectedCharacterIndex < 0)
				m_selectedCharacterIndex = 0;
			else
				m_selectedCharacterIndex = (m_selectedCharacterIndex == 0) ? (n - 1) : (m_selectedCharacterIndex - 1);
		}
		if (input.WasPressed(engine::platform::Key::Down) || input.WasPressed(engine::platform::Key::Right))
		{
			if (m_selectedCharacterIndex < 0)
				m_selectedCharacterIndex = 0;
			else
				m_selectedCharacterIndex = (m_selectedCharacterIndex + 1) % n;
		}
		if (authUiImguiMode && m_selectedCharacterIndex >= 0
			&& m_selectedCharacterIndex < n
			&& input.WasPressed(engine::platform::Key::Enter))
		{
			ImGuiActivateSelectedCharacter();
		}
	}

#else

	void AuthUiPresenter::ImGuiSelectCharacterEntry(int) {}
	void AuthUiPresenter::ImGuiActivateSelectedCharacter() {}
	void AuthUiPresenter::ImGuiCreateAnotherCharacterFromSelect() {}
	void AuthUiPresenter::ImGuiCancelCharacterSelectReturnToLogin() {}
	void AuthUiPresenter::BuildModel_CharacterSelect(RenderModel&) const {}
	void AuthUiPresenter::Update_CharacterSelect(engine::platform::Input&, const engine::core::Config&,
		engine::platform::Window&, bool, bool) {}

#endif
} // namespace engine::client
