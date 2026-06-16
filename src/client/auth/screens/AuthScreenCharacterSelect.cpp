// Phase 2 — Couche modèle pour l'écran de sélection de personnage.
// Affiche la liste reçue via CHARACTER_LIST_REQUEST (Phase 1) et propose 3 actions :
// Jouer (personnage sélectionné), Créer un nouveau personnage, Retour.
// Phase 3.9 — ajoute la suppression logique (CHARACTER_DELETE_REQUEST).

#include "src/client/auth/AuthUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/RequestResponseDispatcher.h"
#include "src/shared/platform/Input.h"
#include "src/shared/platform/Window.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
		// Correctif visibilité 1ère connexion — l'id à propager doit être NON NUL : un
		// character_id == 0 ferait partir le Hello UDP avec une clé erronée (le shard associerait
		// le client au mauvais personnage → invisibilité mutuelle). Si l'entrée sélectionnée a un
		// id 0 (liste pas encore réécrite après création, latence DB), on retombe sur le
		// character_id mémorisé à la création (m_lastCreatedCharacterId). Cf. CODEBASE_MAP §59.
		uint64_t resolvedCharacterId = chosen.character_id;
		if (resolvedCharacterId == 0u && m_lastCreatedCharacterId != 0u)
		{
			LOG_WARN(Core,
				"[AuthUiPresenter] character_id selectionne == 0 — fallback sur l'id du perso cree ({})",
				m_lastCreatedCharacterId);
			resolvedCharacterId = m_lastCreatedCharacterId;
		}
		if (resolvedCharacterId == 0u)
		{
			// Aucun id valide disponible : refuser l'entrée plutôt que de se connecter en
			// « personnage fantôme ». L'utilisateur voit une erreur et peut réessayer
			// (la liste sera rechargée correctement au prochain tour de flow).
			LOG_ERROR(Core, "[AuthUiPresenter] Entree en jeu refusee : character_id introuvable (index={})",
				m_selectedCharacterIndex);
			m_userErrorText = Tr("auth.error.character_session_inactive");
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] CharacterSelect -> activate (character_id={}, name={}, shard={}, endpoint={}, spawn=({},{},{}))",
			resolvedCharacterId, chosen.name, m_chosenShardId, m_chosenShardEndpoint,
			chosen.spawn_x, chosen.spawn_y, chosen.spawn_z);
		// Phase 3 — émission de la commande d'entrée dans le monde, consommée par
		// Engine::Update sur la première frame post-auth (cf. AuthGate else branch).
		// Phase 3.6 — la position spawn vient de la DB (characters.spawn_*) via la
		// payload CHARACTER_LIST. Si tout est à zéro (cas pré-migration ou perso fraichement
		// créé pas encore réécrit), hasSpawn reste false et l'engine appliquera son défaut.
		m_pendingEnterWorld = {};
		m_pendingEnterWorld.applyRequested = true;
		m_pendingEnterWorld.characterId = resolvedCharacterId;
		m_pendingEnterWorld.shardId = m_chosenShardId;
		m_pendingEnterWorld.shardEndpoint = m_chosenShardEndpoint;
		m_pendingEnterWorld.characterName = chosen.name;
		m_pendingEnterWorld.spawnX        = chosen.spawn_x;
		m_pendingEnterWorld.spawnY        = chosen.spawn_y;
		m_pendingEnterWorld.spawnZ        = chosen.spawn_z;
		m_pendingEnterWorld.spawnYawDeg   = chosen.spawn_yaw_deg;
		m_pendingEnterWorld.spawnPitchDeg = chosen.spawn_pitch_deg;
		// Sous-projet C MVP — propage race_str (DB, migration 0033) vers
		// EnterWorldCommand pour que l'engine resolve le mesh skinned de la
		// race via Engine::GetRaceMesh(). Vide pour les persos pre-migration
		// (Engine retombera sur le fallback humains).
		m_pendingEnterWorld.raceId        = chosen.race_str;
			// #1 serveur — genre du perso (DB, migration 0067) via CHARACTER_LIST -> Engine.
			m_pendingEnterWorld.gender        = chosen.gender;
			// Teinte de peau (DB, migration 0068) via CHARACTER_LIST -> Engine.
			m_pendingEnterWorld.skinColorIdx  = chosen.skin_color_idx;
			// Niveau du perso (CHARACTER_LIST) -> Engine -> arbre de competences.
			m_pendingEnterWorld.level         = chosen.level;
		const bool nonZero = (chosen.spawn_x != 0.0f) || (chosen.spawn_y != 0.0f)
			|| (chosen.spawn_z != 0.0f) || (chosen.spawn_yaw_deg != 0.0f)
			|| (chosen.spawn_pitch_deg != 0.0f);
		m_pendingEnterWorld.hasSpawn = nonZero;
		m_userErrorText.clear();
		m_infoBanner.clear();
		m_postRegistrationCharacterCreatePending = false;
		// Correctif visibilité 1ère connexion — id de création consommé : on le réinitialise
		// pour ne pas le réutiliser à tort lors d'une entrée ultérieure (autre perso/session).
		m_lastCreatedCharacterId = 0u;
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

	void AuthUiPresenter::ImGuiRequestDeleteCharacter(int index, const engine::core::Config& cfg)
	{
		if (m_phase != Phase::CharacterSelect)
			return;
		if (index < 0 || static_cast<size_t>(index) >= m_characterList.size())
			return;
		// Confirmation en deux temps : premier appel arme la confirmation
		// (le renderer affiche un bouton/dialogue distinct) ; second appel sur
		// le meme index lance la suppression effective.
		if (m_pendingDeleteCharacterIndex != index)
		{
			m_pendingDeleteCharacterIndex = index;
			LOG_INFO(Core, "[AuthUiPresenter] CharacterSelect : delete confirmation armed (index={}, character_id={})",
				index, m_characterList[static_cast<size_t>(index)].character_id);
			return;
		}
		// Second clic sur le meme index : on lance le worker reseau.
		m_pendingDeleteCharacterId = m_characterList[static_cast<size_t>(index)].character_id;
		LOG_INFO(Core, "[AuthUiPresenter] CharacterSelect : delete CONFIRMED (index={}, character_id={})",
			index, m_pendingDeleteCharacterId);
		StartCharacterDeleteWorker(cfg);
	}

	void AuthUiPresenter::ImGuiCancelDeleteCharacterConfirm()
	{
		if (m_pendingDeleteCharacterIndex < 0)
			return;
		LOG_INFO(Core, "[AuthUiPresenter] CharacterSelect : delete confirmation cancelled");
		m_pendingDeleteCharacterIndex = -1;
	}

	void AuthUiPresenter::StartCharacterDeleteWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0)
		{
			EnterAuthErrorPhase(Phase::CharacterSelect, Tr("auth.error.character_session_inactive"));
			return;
		}
		if (m_pendingDeleteCharacterId == 0u)
		{
			LOG_WARN(Core, "[AuthUiPresenter] StartCharacterDeleteWorker called with no pending character_id");
			return;
		}

		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const uint64_t characterId = m_pendingDeleteCharacterId;

		m_pendingAsyncKind = AsyncKind::CharacterDelete;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs, characterId]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			disp.SetSessionId(sessionId);
			bool done = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeCharacterDeleteRequest,
					engine::network::BuildCharacterDeleteRequestPayload(characterId),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "CHARACTER_DELETE timeout.";
							return;
						}
						auto resp = engine::network::ParseCharacterDeleteResponsePayload(pl.data(), pl.size());
						if (resp && resp->success != 0)
							return; // success — errMsg stays empty
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						errMsg = (er && !er->message.empty())
							? er->message
							: std::string("Character delete failed.");
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send CHARACTER_DELETE failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			local.ready = true;
			local.success = done && errMsg.empty();
			local.message = local.success
				? std::string("Character deleted.")
				: (errMsg.empty() ? std::string("CHARACTER_DELETE timeout.") : errMsg);
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

#else

	void AuthUiPresenter::ImGuiSelectCharacterEntry(int) {}
	void AuthUiPresenter::ImGuiActivateSelectedCharacter() {}
	void AuthUiPresenter::ImGuiCreateAnotherCharacterFromSelect() {}
	void AuthUiPresenter::ImGuiCancelCharacterSelectReturnToLogin() {}
	void AuthUiPresenter::ImGuiRequestDeleteCharacter(int, const engine::core::Config&) {}
	void AuthUiPresenter::ImGuiCancelDeleteCharacterConfirm() {}
	void AuthUiPresenter::StartCharacterDeleteWorker(const engine::core::Config&) {}
	void AuthUiPresenter::BuildModel_CharacterSelect(RenderModel&) const {}
	void AuthUiPresenter::Update_CharacterSelect(engine::platform::Input&, const engine::core::Config&,
		engine::platform::Window&, bool, bool) {}

#endif
} // namespace engine::client
