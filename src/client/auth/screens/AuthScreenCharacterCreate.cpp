// AUTH-UI.11 — Couche modèle pour l'écran de création de personnage.

// Couche modèle : BuildModel_CharacterCreate expose le champ nom, StartCharacterCreateWorker envoie la requête réseau.
#include "src/client/auth/AuthUi.h"
#include "src/client/app/Engine.h"  // SetAvatarGender (selecteur de genre) sur m_engineForRaceLookup

#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/ErrorPacket.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/RequestResponseDispatcher.h"
#include "src/shared/platform/Input.h"
#include "src/shared/platform/Window.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace engine::client
{
#if defined(_WIN32)

	namespace
	{
		/// Traduit un code d'erreur réseau en libellé lisible pour les messages d'erreur de création de personnage.
		const char* NetErrorLabel(engine::network::NetErrorCode c)
		{
			using engine::network::NetErrorCode;
			switch (c)
			{
			case NetErrorCode::OK: return "OK";
			case NetErrorCode::BAD_REQUEST: return "Bad request";
			case NetErrorCode::INVALID_CREDENTIALS: return "Invalid credentials";
			case NetErrorCode::ACCOUNT_NOT_FOUND: return "Account not found";
			case NetErrorCode::ACCOUNT_LOCKED: return "Account locked";
			case NetErrorCode::ALREADY_LOGGED_IN: return "Already logged in";
			case NetErrorCode::LOGIN_ALREADY_TAKEN: return "Login already taken";
			case NetErrorCode::INVALID_EMAIL: return "Invalid email";
			case NetErrorCode::WEAK_PASSWORD: return "Weak password";
			case NetErrorCode::INVALID_LOGIN: return "Invalid login";
			case NetErrorCode::EMAIL_VERIFICATION_REQUIRED: return "Email verification required";
			case NetErrorCode::EMAIL_ALREADY_VERIFIED: return "Email already verified";
			case NetErrorCode::VERIFICATION_CODE_INVALID: return "Verification code invalid";
			case NetErrorCode::REGISTRATION_DISABLED: return "Registration disabled";
			case NetErrorCode::REGISTRATION_INVALID: return "Registration invalid";
			case NetErrorCode::INTERNAL_ERROR: return "Server error";
			case NetErrorCode::TIMEOUT: return "Timeout";
			default: return "Network error";
			}
		}
	} // namespace

	/// Soumet le nom de personnage saisi depuis le renderer ImGui et lance la création.
	void AuthUiPresenter::ImGuiSubmitCharacterCreate(const engine::core::Config& cfg, const char* nameUtf8, const char* raceIdUtf8, const char* genderUtf8)
	{
		if (m_phase != Phase::CharacterCreate)
		{
			return;
		}
		m_characterName = nameUtf8 ? std::string(nameUtf8) : std::string();
		m_characterRaceId = raceIdUtf8 ? std::string(raceIdUtf8) : std::string();
		// #1 serveur — genre envoye au master dans la requete de creation (persiste en DB).
		m_characterGender = (genderUtf8 && std::string(genderUtf8) == "female") ? "female" : "male";
		// Applique aussi le genre au moteur AVANT la soumission : l'EnterWorld qui suit
		// resout le mesh via Engine::GetRaceMesh(raceId) qui lit m_avatarGender.
		// SetCharacterGender garde le fix client interim #1 (repli si la DB serveur n'a
		// pas encore le genre, ex. master pas redeploye).
		if (m_engineForRaceLookup != nullptr && genderUtf8 != nullptr)
			m_engineForRaceLookup->SetCharacterGender(m_characterName, genderUtf8);
		SubmitCurrentPhase(cfg);
	}

	/// Annule la création et retourne à la connexion (le compte reste valide mais sans personnage).
	void AuthUiPresenter::ImGuiCancelCharacterCreateReturnToLogin()
	{
		if (m_phase != Phase::CharacterCreate)
		{
			return;
		}
		m_characterName.clear();
		m_userErrorText.clear();
		m_activeField = 0;
		// On revient à l'écran de connexion : la prochaine boucle d'auth devra réafficher le ShardPick proprement.
		m_chosenShardId = 0;
		m_postRegistrationCharacterCreatePending = false;
		SetPhase(Phase::Login);
	}

	/// Peuple le modèle avec le champ nom de personnage et les règles de nommage.
	void AuthUiPresenter::BuildModel_CharacterCreate(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.panel.character_create");
		{
			RenderField f{};
			f.label = Tr("auth.label.character_name");
			f.value = m_characterName;
			f.active = true;
			f.hovered = m_hoveredFieldIndex == 0;
			f.secret = false;
			f.cyclePicker = false;
			model.fields.push_back(std::move(f));
		}
		{
			RenderBodyLine line{};
			line.text = Tr("auth.hint.character.rules");
			line.hovered = m_hoveredBodyLineIndex == 0;
			model.bodyLines.push_back(std::move(line));
		}
		{
			RenderAction a{};
			a.labelKey = "common.submit";
			a.primary = true;
			a.active = true;
			a.emphasized = false;
			a.hovered = m_hoveredActionIndex == 0;
			model.actions.push_back(std::move(a));
		}
	}

	void AuthUiPresenter::Update_CharacterCreate(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&,
		bool usingNativeAuth, bool /*authUiImguiMode*/)
	{
		if (usingNativeAuth || m_phase != Phase::CharacterCreate)
		{
			return;
		}
	}

	/// Lance le worker réseau qui envoie la requête de création de personnage au serveur maître.
	void AuthUiPresenter::StartCharacterCreateWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0)
		{
			EnterAuthErrorPhase(Phase::CharacterCreate, Tr("auth.error.character_session_inactive"));
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string characterName = m_characterName;
		const std::string characterRaceId = m_characterRaceId;
		const std::string characterGender = m_characterGender;  // #1 serveur

		m_pendingAsyncKind = AsyncKind::CharacterCreate;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs, characterName, characterRaceId, characterGender]() {
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
			if (!disp.SendRequest(engine::network::kOpcodeCharacterCreateRequest,
					engine::network::BuildCharacterCreateRequestPayload(characterName, characterRaceId, "", {}, characterGender),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "CHARACTER_CREATE timeout.";
							return;
						}
						auto resp = engine::network::ParseCharacterCreateResponsePayload(pl.data(), pl.size());
						if (resp && resp->success != 0)
						{
							local.accountId = resp->character_id;
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
						{
							errMsg = er->message.empty() ? std::string(NetErrorLabel(er->errorCode)) : er->message;
						}
						else
						{
							errMsg = "Character creation failed.";
						}
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send CHARACTER_CREATE failed.";
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
			local.message =
				local.success ? std::string("Character created successfully.") : (errMsg.empty() ? "Character creation timeout." : errMsg);
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

// Stubs Linux/Mac — aucune UI d'auth sur ces plateformes.
#else

	void AuthUiPresenter::ImGuiSubmitCharacterCreate(const engine::core::Config&, const char*, const char*, const char*) {}
	void AuthUiPresenter::ImGuiCancelCharacterCreateReturnToLogin() {}

	void AuthUiPresenter::BuildModel_CharacterCreate(RenderModel&) const {}

	void AuthUiPresenter::Update_CharacterCreate(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool)
	{
	}

#endif
} // namespace engine::client
