// AuthUiPresenter — threads de travail réseau (connexion, inscription, flux master, vérification d'e-mail, sondage d'état) et PollAsyncResult (Windows).
//
// Fonctions membres déplacées telles quelles depuis AuthUiPresenterCore.cpp
// (aucun changement de comportement). Documentation : AuthUi.h ; utilitaires
// communs : AuthUiPresenterInternal.h.

#include "src/client/auth/AuthUiPresenterInternal.h"

#if defined(_WIN32)
namespace engine::client
{
	using namespace engine::client::auth_detail;

	void AuthUiPresenter::PollAsyncResult(const engine::core::Config& cfg)
	{
		auto pendingKindTag = [](AsyncKind k) -> const char*
		{
			switch (k)
			{
			case AsyncKind::None:
				return "None";
			case AsyncKind::Register:
				return "Register";
			case AsyncKind::AuthOnly:
				return "AuthOnly";
			case AsyncKind::VerifyEmail:
				return "VerifyEmail";
			case AsyncKind::ResendVerification:
				return "ResendVerification";
			case AsyncKind::ForgotPassword:
				return "ForgotPassword";
			case AsyncKind::TermsStatus:
				return "TermsStatus";
			case AsyncKind::TermsAccept:
				return "TermsAccept";
			case AsyncKind::CharacterCreate:
				return "CharacterCreate";
			case AsyncKind::CharacterDelete:
				return "CharacterDelete";
			case AsyncKind::Login:
				return "Login";
			case AsyncKind::StatusProbe:
				return "StatusProbe";
			case AsyncKind::UsernameCheck:
				return "UsernameCheck";
			default:
				return "?";
			}
		};
		// Écran login initial : pas de worker, rien à consommer — évite de verrouiller sans nécessité.
		LOG_DEBUG(Core, "[DIAG-POLL] enter worker.joinable={} asyncResult.ready={}", m_worker.joinable() ? 1 : 0, m_asyncResult.ready ? 1 : 0);
		const bool workerJoinable = m_worker.joinable();
		if (!workerJoinable && !m_asyncResult.ready)
		{
			LOG_DEBUG(Core, "[DIAG-POLL] early return (no worker, no result)");
			return;
		}

		LOG_DEBUG(Core, "[DIAG-POLL] before mutex lock m_asyncMutex={}", (void*)m_asyncMutex.get());
		AsyncResult copy{};
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			LOG_DEBUG(Core, "[DIAG-POLL] mutex locked, asyncResult.ready={}", m_asyncResult.ready ? 1 : 0);
			if (!m_asyncResult.ready)
			{
				LOG_DEBUG(Core,
					"[DIAG-POLL] return (result not ready) pendingKind={}",
					pendingKindTag(m_pendingAsyncKind));
				return;
			}
			copy = m_asyncResult;
			m_asyncResult = {};
		}
		LOG_DEBUG(Core, "[DIAG-POLL] mutex released, joining worker");
		JoinWorker();

		const AsyncKind kind = m_pendingAsyncKind;
		m_pendingAsyncKind = AsyncKind::None;

		if (kind == AsyncKind::StatusProbe)
		{
			m_statusCache = copy.statusCache;
			// La sonde /status porte le compte de joueurs a jour : on le repercute sur
			// l'ecran de choix de serveur, sinon il reste fige sur l'instantanee de connexion.
			RefreshShardPickEntriesFromStatusCache();
			m_statusProbeCompletedOnce = true;
			m_lastStatusProbeHttpSuccess = copy.success;
			// Si on a fini la première vérification, on autorise les refresh périodiques.
			m_statusProbeInitialized = true;
			m_authAvailabilityChecking = false;
			m_authAvailabilityPollTimer = 0.f;
			m_authLogoRotationRad = 0.f;
			// Le master peut etre injoignable (transport HTTP en echec) ou repondre que le service
			// est indisponible (authOk/masterOk faux). Dans ces deux cas, exposer un message clair
			// dans m_infoBanner pour que l'utilisateur sache pourquoi la connexion ne progresse pas.
			// Quand la sonde redevient OK, on retire un eventuel ancien message server_unavailable
			// pour ne pas le laisser trainer.
			{
				const bool probeOk = copy.success && m_statusCache.authOk && m_statusCache.masterOk;
				const std::string unavailableMsg = Tr("auth.info.server_unavailable");
				if (!probeOk)
				{
					if (m_infoBanner.empty() || m_infoBanner == unavailableMsg)
					{
						m_infoBanner = m_statusCache.infoMessage.empty() ? unavailableMsg : m_statusCache.infoMessage;
					}
				}
				else if (m_infoBanner == unavailableMsg)
				{
					m_infoBanner.clear();
				}
			}
			LOG_INFO(Core,
				"[StatusProbe] thread principal: résultat consommé — httpLayerOk={} authOk={} masterOk={} shards={} totalJoueurs={} "
				"infoMessage='{}' workerMsg='{}' → UI: maintenance seulement si httpLayerOk et JSON indique indisponibilité",
				copy.success ? 1 : 0, m_statusCache.authOk ? 1 : 0, m_statusCache.masterOk ? 1 : 0, m_statusCache.servers.size(),
				m_statusCache.totalPlayers, m_statusCache.infoMessage, copy.message);
			return;
		}

		if (kind == AsyncKind::Register)
		{
			LOG_INFO(Core,
				"[AUTH-REG] PollAsyncResult: consuming register outcome success={} accountId={} msg_len={}",
				copy.success ? 1 : 0,
				copy.accountId,
				copy.message.size());
			if (copy.success)
			{
				m_pendingVerifyAccountId = copy.accountId;
				m_verifyCodeSentAt = std::chrono::steady_clock::now();
				// Le compte vient d'être créé : il n'a aucun personnage. Une fois l'utilisateur ré-authentifié et les CGU acceptées,
				// l'écran ShardPick devra rediriger vers Phase::CharacterCreate et non vers la connexion directe au royaume.
				m_postRegistrationCharacterCreatePending = true;
				SetPhase(Phase::EmailConfirmationPending);
				// Conserver m_pendingVerifyAccountId = copy.accountId (doit rester en place)
				m_userErrorText.clear();
				m_infoBanner.clear();
				if (!copy.tagId.empty())
				{
					m_registeredTagId = copy.tagId;
				}
				LOG_INFO(Core, "[AuthUiPresenter] Register finished OK: accountId={} tagId={}", copy.accountId, m_registeredTagId);
			}
			else
			{
				EnterAuthErrorPhase(Phase::Register, copy.message);
				LOG_WARN(Core, "[AuthUiPresenter] Register FAILED: {}", copy.message);
			}
			(void)cfg;
			return;
		}

		if (kind == AsyncKind::AuthOnly)
		{
			if (copy.success)
			{
				m_masterSessionId = copy.sessionId;
				if (copy.termsPendingCount > 0)
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					SetPhase(Phase::Terms);
					m_infoBanner = copy.message.empty() ? Tr("auth.info.terms_required") : copy.message;
				}
				else
				{
					// Phase 2/3 + hotfix réutilisation : on ne réinitialise pas la session ici.
					// StartMasterFlowWorker recyclera m_masterClient (Disconnect old + nouvelle
					// instance attribuée AVANT le worker), et la session post-Flow remplacera
					// m_masterSessionId via AsyncResult.sessionId dans le consumer Login success.
					SetPhase(Phase::Submitting);
					StartMasterFlowWorker(cfg);
				}
			}
			else
			{
				// Compte déjà connecté en jeu : message dédié (priorité sur le filtre
				// generique invalid_credentials/account_not_found, qui masque la
				// distinction identifiant/mot de passe).
				if (copy.flowAuthAlreadyLoggedIn)
				{
					EnterAuthErrorPhase(Phase::Login, Tr("auth.error.already_logged_in"));
				}
				// Masquer la distinction identifiant/mot de passe pour ne pas révéler
				// si c'est le login ou le mot de passe qui est incorrect.
				else if (copy.message == "Invalid credentials" || copy.message == "Account not found")
				{
					EnterAuthErrorPhase(Phase::Login, Tr("auth.error.invalid_credentials"));
				}
				else
				{
					EnterAuthErrorPhase(Phase::Login, copy.message);
				}
			}
			return;
		}

		if (kind == AsyncKind::VerifyEmail)
		{
			if (copy.success)
			{
				SetPhase(Phase::Login);
				m_userErrorText.clear();
				m_registeredTagId.clear();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.email_verified") : copy.message;
				LOG_INFO(Core, "[AuthUiPresenter] VerifyEmail OK: {}", m_infoBanner);
			}
			else
			{
				EnterAuthErrorPhase(Phase::VerifyEmail, copy.message);
				LOG_WARN(Core, "[AuthUiPresenter] VerifyEmail FAILED: {}", copy.message);
			}
			return;
		}

		if (kind == AsyncKind::ResendVerification)
		{
			if (copy.success)
			{
				// Réinitialiser le timer : le nouveau code expire dans 15 min.
				m_verifyCodeSentAt = std::chrono::steady_clock::now();
				m_userErrorText.clear();
				m_infoBanner = Tr("auth.info.email_resent");
				LOG_INFO(Core, "[AuthUiPresenter] ResendVerification OK");
			}
			else
			{
				const Phase returnTo = (m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
					? m_phase : Phase::EmailConfirmationPending;
				EnterAuthErrorPhase(returnTo, copy.message);
				LOG_WARN(Core, "[AuthUiPresenter] ResendVerification FAILED: {}", copy.message);
			}
			return;
		}

		if (kind == AsyncKind::ForgotPassword)
		{
			SetPhase(Phase::Login);
			if (copy.success)
			{
				m_userErrorText.clear();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.forgot_password") : copy.message;
			}
			else
			{
				m_infoBanner = copy.message;
			}
			return;
		}

		// Plan C : résultat de la vérification disponibilité username.
		if (kind == AsyncKind::UsernameCheck)
		{
			if (copy.usernameCheckSeq != m_usernameCheckSeq)
			{
				// Réponse obsolète (une nouvelle frappe a incrémenté le seq) : ignorer.
				LOG_DEBUG(Core, "[UsernameCheck] réponse obsolète seq={} attendu={}", copy.usernameCheckSeq, m_usernameCheckSeq);
				return;
			}
			m_usernameCheckState = (copy.usernameAvailable != 0)
				? UsernameCheckState::Available
				: UsernameCheckState::Taken;
			LOG_INFO(Core, "[UsernameCheck] résultat seq={} available={}", copy.usernameCheckSeq, (int)copy.usernameAvailable);
			return;
		}

		if (kind == AsyncKind::TermsStatus)
		{
			if (copy.success)
			{
				if (copy.termsPendingCount == 0)
				{
					m_infoBanner = Tr("auth.info.terms_done");
					// Avant de proposer la création/sélection d'un personnage, l'utilisateur doit choisir un royaume.
					// MasterFlow renvoie shard_choice_required → Phase::ShardPick.
					// Phase 2/3 fix : on garde m_masterClient (cf. ligne 1679) pour que CharacterCreate
					// post-MasterFlow trouve une session valide.
					SetPhase(Phase::Submitting);
					StartMasterFlowWorker(cfg);
					return;
				}
				else
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					SetPhase(Phase::Terms);
					m_infoBanner = copy.message;
				}
			}
			else
			{
				ResetMasterSession();
				EnterAuthErrorPhase(Phase::Terms, copy.message);
			}
			return;
		}

		if (kind == AsyncKind::TermsAccept)
		{
			if (copy.success)
			{
				if (copy.termsPendingCount == 0)
				{
					m_infoBanner = Tr("auth.info.terms_done");
					// Toutes les CGU acceptées : place au choix du royaume avant la création/sélection du personnage.
					// MasterFlow renvoie shard_choice_required → Phase::ShardPick.
					// Phase 2/3 fix : on garde m_masterClient (cf. ligne 1679).
					SetPhase(Phase::Submitting);
					StartMasterFlowWorker(cfg);
					return;
				}
				else
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					SetPhase(Phase::Terms);
					m_infoBanner = copy.message;
				}
			}
			else
			{
				ResetMasterSession();
				EnterAuthErrorPhase(Phase::Terms, copy.message);
			}
			return;
		}

		if (kind == AsyncKind::CharacterCreate)
		{
			if (copy.success)
			{
				ResetMasterSession();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.character_created") : copy.message;
				// Correctif visibilité 1ère connexion — mémorise le character_id renvoyé par
				// CHARACTER_CREATE (porté par accountId, cf. AuthScreenCharacterCreate). Permet de
				// re-sélectionner ce perso précis dans la CHARACTER_LIST rechargée et de fournir un
				// fallback d'id non nul à l'EnterWorld si la liste le manque encore (latence DB).
				m_lastCreatedCharacterId = copy.accountId;
				LOG_INFO(Core, "[AuthUiPresenter] CharacterCreate OK (character_id={}) — memorise pour l'entree en jeu",
					m_lastCreatedCharacterId);
				// Le personnage existe désormais sur m_chosenShardId : on saute l'écran ShardPick au tour suivant.
				m_postRegistrationCharacterCreatePending = false;
				m_shardFlowOverrideId = m_chosenShardId;
				SetPhase(Phase::Submitting);
				StartMasterFlowWorker(cfg);
			}
			else
			{
				EnterAuthErrorPhase(Phase::CharacterCreate, copy.message);
			}
			return;
		}

		if (kind == AsyncKind::CharacterDelete)
		{
			if (copy.success)
			{
				// Phase 3.9 — retire l'entree localement (au lieu de redemander la liste)
				// pour eviter un round-trip et garder l'ecran reactif. Si la liste devient
				// vide, l'ecran route automatiquement vers CharacterCreate via la logique
				// post-MasterFlow ; ici on reste sur CharacterSelect avec une entree de moins.
				if (m_pendingDeleteCharacterId != 0u)
				{
					auto it = std::find_if(m_characterList.begin(), m_characterList.end(),
						[this](const engine::network::CharacterListEntry& e) {
							return e.character_id == m_pendingDeleteCharacterId;
						});
					if (it != m_characterList.end())
					{
						m_characterList.erase(it);
					}
				}
				m_pendingDeleteCharacterIndex = -1;
				m_pendingDeleteCharacterId = 0;
				if (m_selectedCharacterIndex >= static_cast<int>(m_characterList.size()))
				{
					m_selectedCharacterIndex = m_characterList.empty() ? -1 : 0;
				}
				m_userErrorText.clear();
				m_infoBanner = Tr("auth.info.character_deleted");
				if (m_characterList.empty())
				{
					LOG_INFO(Core, "[AuthUiPresenter] CharacterDelete OK : liste vide -> CharacterCreate");
					SetPhase(Phase::CharacterCreate);
				}
				else
				{
					LOG_INFO(Core, "[AuthUiPresenter] CharacterDelete OK : {} entrees restantes", m_characterList.size());
				}
			}
			else
			{
				m_pendingDeleteCharacterIndex = -1;
				m_pendingDeleteCharacterId = 0;
				EnterAuthErrorPhase(Phase::CharacterSelect, copy.message);
			}
			return;
		}

		if (kind == AsyncKind::Login && copy.shardChoiceRequired)
		{
			m_shardPickEntries = std::move(copy.serverListForPick);
			// Applique tout de suite le dernier compte connu de la sonde /status (la
			// connexion master est fermee apres SERVER_LIST : pas d'autre rafraichissement
			// avant la prochaine sonde periodique).
			RefreshShardPickEntriesFromStatusCache();
			m_shardPickChoiceShardId = 0;
			for (const auto& e : m_shardPickEntries)
			{
				if (e.status == 1u && !e.endpoint.empty())
				{
					m_shardPickChoiceShardId = e.shard_id;
					break;
				}
			}
			SetPhase(Phase::ShardPick);
			m_userErrorText.clear();
			m_infoBanner.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Shard choice UI ({} liste entrées)", m_shardPickEntries.size());
			return;
		}

		if (copy.success)
		{
			m_userErrorText.clear();
			m_infoBanner = copy.message;
			LOG_INFO(Core, "[AuthUiPresenter] Master/shard flow OK: {}", copy.message);
			// Hotfix : MasterShardClientFlow re-AUTH crée une nouvelle session côté serveur
			// (l'ancienne session AuthOnly est kickée par duplicate-login). On synchronise
			// m_masterSessionId sur la nouvelle session pour que les RequestResponseDispatcher
			// post-flow (CharacterCreate, CharacterDelete, SAVE_POSITION) utilisent le bon
			// session_id dans leurs en-têtes de paquet.
			if (copy.sessionId != 0u)
			{
				m_masterSessionId = copy.sessionId;
			}
			if (kind == AsyncKind::Login)
			{
				SaveRememberPreference();
				if (m_rememberLogin)
				{
					PersistRememberedLoginSidecar(m_login);
				}
				else
				{
					ClearRememberedLoginSidecar();
				}
			}

			// Phase 2 — décision post-shard selon la liste des personnages reçue avec le ticket.
			// Si le compte vient d'être créé (post-Register), on garde le routage forcé vers CharacterCreate.
			// Sinon : 0 perso => CharacterCreate ; ≥1 perso => CharacterSelect.
			m_characterList = std::move(copy.characterList);
			// Horloge monde piggyback (opcode 40) — publiée vers les membres au même
			// moment que la liste perso ; consommée ensuite par l'Engine (ClearWorldClock).
			m_hasWorldClock = copy.hasWorldClock;
			m_worldClock = copy.worldClock;
			m_worldClockClientRecvMs = copy.worldClockClientRecvMs;
			m_selectedCharacterIndex = m_characterList.empty() ? -1 : 0;
			// Correctif visibilité 1ère connexion — si on vient de créer un personnage, on
			// sélectionne SON entrée (et pas l'index 0 par défaut) afin que le clic « Jouer »
			// parte avec le bon character_id. Si la liste rechargée ne le contient pas encore
			// (latence DB côté master), on le signale : le fallback à l'entrée en jeu
			// (ImGuiActivateSelectedCharacter) couvrira ce cas via m_lastCreatedCharacterId.
			if (m_lastCreatedCharacterId != 0u && !m_characterList.empty())
			{
				bool found = false;
				for (size_t i = 0; i < m_characterList.size(); ++i)
				{
					if (m_characterList[i].character_id == m_lastCreatedCharacterId)
					{
						m_selectedCharacterIndex = static_cast<int>(i);
						found = true;
						break;
					}
				}
				if (!found)
				{
					LOG_WARN(Core,
						"[AuthUiPresenter] Perso cree (character_id={}) absent de la CHARACTER_LIST rechargee "
						"({} entrees) — fallback d'id a l'entree en jeu actif",
						m_lastCreatedCharacterId, m_characterList.size());
				}
			}
			// Phase 3 — Persister l'endpoint du shard pour pouvoir le re-publier dans la
			// EnterWorldCommand quand l'utilisateur cliquera Jouer / validera CharacterCreate.
			if (!copy.shardEndpoint.empty())
			{
				m_chosenShardEndpoint = copy.shardEndpoint;
				if (copy.shardId != 0u)
				{
					m_chosenShardId = copy.shardId;
				}
			}
			if (m_postRegistrationCharacterCreatePending)
			{
				LOG_INFO(Core, "[AuthUiPresenter] post-Register: routage CharacterCreate (forcé)");
				SetPhase(Phase::CharacterCreate);
				return;
			}
			if (m_characterList.empty())
			{
				LOG_INFO(Core, "[AuthUiPresenter] aucun personnage sur ce shard -> CharacterCreate");
				SetPhase(Phase::CharacterCreate);
				return;
			}
			LOG_INFO(Core, "[AuthUiPresenter] {} personnage(s) sur ce shard -> CharacterSelect", m_characterList.size());
			SetPhase(Phase::CharacterSelect);
			return;
		}
		// Compte déjà connecté en jeu : le master a refusé cette 2ᵉ authentification
		// (ALREADY_LOGGED_IN). On affiche un message dédié — l'utilisateur doit savoir
		// que son compte est déjà en ligne, et contacter le support + changer son mot
		// de passe si ce n'est pas lui.
		const std::string flowErrorMsg = copy.flowAuthAlreadyLoggedIn
			? Tr("auth.error.already_logged_in")
			: copy.message;
		EnterAuthErrorPhase(Phase::Login, flowErrorMsg);
		LOG_WARN(Core, "[AuthUiPresenter] Master/shard flow FAILED: {} (alreadyLoggedIn={})",
			copy.message, copy.flowAuthAlreadyLoggedIn ? 1 : 0);
	}

	void AuthUiPresenter::StartLoginWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			EnterAuthErrorPhase(Phase::Login, Tr("auth.error.hash_password_failed"));
			return;
		}

		ResetMasterSession();
		// Correctif visibilité 1ère connexion — repart d'un login propre : pas d'id de
		// création résiduel d'une session précédente qui pourrait fausser le fallback d'entrée.
		m_lastCreatedCharacterId = 0u;
		m_masterClient = std::make_unique<engine::network::NetClient>();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const std::string login = m_login;
		const std::string locale = CurrentLocale();

		m_pendingAsyncKind = AsyncKind::AuthOnly;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		m_worker = std::thread([this, masterClient, host, port, timeoutMs, allowInsecure, serverFingerprint, login, hash, locale]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			ApplyMasterTlsConfig(*masterClient, serverFingerprint, allowInsecure);
			masterClient->Connect(host, port);
			if (!WaitConnected(masterClient, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			engine::network::RequestResponseDispatcher disp(masterClient);
			bool authDone = false;
			bool authOk = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeAuthRequest, engine::network::BuildAuthRequestPayload(login, hash),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						authDone = true;
						if (timeout)
						{
							errMsg = "AUTH timeout.";
							return;
						}
						auto auth = engine::network::ParseAuthResponsePayload(pl.data(), pl.size());
						if (auth && auth->success != 0)
						{
							local.sessionId = auth->session_id;
							authOk = true;
							return;
						}
						if (auth && auth->success == 0)
						{
							errMsg = std::string(NetErrorLabel(auth->error_code));
							// Compte déjà connecté en jeu : on lève le flag pour que le
							// consumer PollAsyncResult affiche le message localisé dédié
							// au lieu du label réseau brut « Already logged in ».
							if (auth->error_code == engine::network::NetErrorCode::ALREADY_LOGGED_IN)
								local.flowAuthAlreadyLoggedIn = true;
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
							errMsg = std::string(NetErrorLabel(er->errorCode)) + (er->message.empty() ? "" : (": " + er->message));
						else
							errMsg = "AUTH failed.";
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send AUTH failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!authDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!authOk)
			{
				masterClient->Disconnect("auth_failed");
				local.ready = true;
				local.message = errMsg.empty() ? "AUTH failed." : errMsg;
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			disp.SetSessionId(local.sessionId);
			bool statusDone = false;
			if (!disp.SendRequest(engine::network::kOpcodeTermsStatusRequest, engine::network::BuildTermsStatusRequestPayload(locale),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						statusDone = true;
						if (timeout)
						{
							errMsg = "TERMS status timeout.";
							return;
						}
						auto terms = engine::network::ParseTermsStatusResponsePayload(pl.data(), pl.size());
						if (!terms)
						{
							errMsg = "TERMS status parse failed.";
							return;
						}
						local.termsPendingCount = terms->pending_count;
						local.termsEditionId = terms->next_edition_id;
						local.termsTitle = terms->title;
						local.termsVersionLabel = terms->version_label;
						local.termsLocale = terms->resolved_locale;
					},
					timeoutMs))
			{
				masterClient->Disconnect("terms_status_send_failed");
				local.ready = true;
				local.message = "Send TERMS_STATUS failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!statusDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!statusDone)
			{
				masterClient->Disconnect("terms_status_timeout");
				local.ready = true;
				local.message = "TERMS status timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			if (local.termsPendingCount > 0 && local.termsEditionId != 0)
			{
				bool contentDone = false;
				if (!disp.SendRequest(engine::network::kOpcodeTermsContentRequest,
						engine::network::BuildTermsContentRequestPayload(local.termsEditionId, local.termsLocale, 0u, 8192u),
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							contentDone = true;
							if (timeout)
							{
								errMsg = "TERMS content timeout.";
								return;
							}
							auto content = engine::network::ParseTermsContentResponsePayload(pl.data(), pl.size());
							if (!content)
							{
								errMsg = "TERMS content parse failed.";
								return;
							}
							local.totalLength = content->total_length;
							local.termsContent = content->chunk;
						},
						timeoutMs))
				{
					masterClient->Disconnect("terms_content_send_failed");
					local.ready = true;
					local.message = "Send TERMS_CONTENT failed.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!contentDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				if (!contentDone)
				{
					masterClient->Disconnect("terms_content_timeout");
					local.ready = true;
					local.message = "TERMS content timeout.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				local.message = "Terms acceptance required before entering the game.";
			}
			else
			{
				local.message = "Authentication successful.";
			}

			local.ready = true;
			local.success = true;
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartMasterFlowWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			EnterAuthErrorPhase(Phase::Login, Tr("auth.error.hash_password_failed"));
			LOG_ERROR(Core, "[AuthUiPresenter] Master flow aborted: empty client_hash");
			return;
		}

		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const std::string login = m_login;
		const bool shardPickWhenMultiple = cfg.GetBool("client.auth_ui.shard_pick_when_multiple", true);
		const uint32_t shardOverride = m_shardFlowOverrideId;
		m_shardFlowOverrideId = 0;

		m_pendingAsyncKind = AsyncKind::Login;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		// Hotfix : MasterShardClientFlow ouvre une nouvelle connexion + re-AUTH. L'ancienne
		// session AuthOnly va être kickée (duplicate login), il faut donc fermer proprement
		// l'ancienne connexion m_masterClient AVANT le worker pour ne pas avoir deux
		// connexions concurrentes du même compte.
		// Surtout : on alloue le NEW NetClient en main thread et on l'attribue à m_masterClient
		// AVANT de lancer le worker, qui ne capte qu'un raw pointer. Ainsi la connexion
		// SURVIT à la fin du worker et reste utilisable par CharacterCreate / CharacterDelete /
		// SAVE_POSITION post-flow. Le presenter reste seul propriétaire (unique_ptr).
		if (m_masterClient)
		{
			m_masterClient->Disconnect("master_flow_restart");
			m_masterClient.reset();
		}
		m_masterClient = std::make_unique<engine::network::NetClient>();
		ApplyMasterTlsConfig(*m_masterClient, serverFingerprint, allowInsecure);
		engine::network::NetClient* const masterClient = m_masterClient.get();

		m_worker = std::thread([this, masterClient, host, port, timeoutMs, login, hash, shardPickWhenMultiple, shardOverride]() {
			AsyncResult local{};
			engine::network::MasterShardClientFlow flow;
			flow.SetMasterAddress(host, port);
			flow.SetCredentials(login, hash);
			flow.SetTimeoutMs(timeoutMs);
			flow.SetShardPickWhenMultiple(shardPickWhenMultiple);
			flow.SetShardIdOverride(shardOverride);
			LOG_INFO(Net, "[AuthUiPresenter] MasterShardClientFlow starting (login='{}')", login);
			engine::network::MasterShardFlowResult r = flow.Run(masterClient);
			local.ready = true;
			if (r.shard_choice_required)
			{
				local.shardChoiceRequired = true;
				local.serverListForPick = std::move(r.server_list_for_pick);
				local.success = false;
				local.message.clear();
			}
			else
			{
				local.success = r.success;
				local.message = r.success ? (std::string("Shard ready (shard_id=") + std::to_string(r.shard_id) + ").") : r.errorMessage;
				local.flowAuthAlreadyLoggedIn =
					!r.success && r.authErrorCode == engine::network::NetErrorCode::ALREADY_LOGGED_IN;
				if (r.success)
				{
					// Phase 2 — la liste optionnelle est récupérée par le flow sur la connexion master ;
					// vide si la requête a échoué (le client retombera sur CharacterCreate).
					local.characterList = std::move(r.character_list);
					// Horloge monde piggyback (opcode 40) — propagée à l'état pour
					// application par l'Engine au démarrage du monde.
					local.hasWorldClock = r.has_world_clock;
					local.worldClock = r.world_clock;
					local.worldClockClientRecvMs = r.world_clock_client_recv_ms;
					// Phase 3 — l'endpoint du shard accepté est nécessaire pour câbler la gameplay UDP.
					local.shardId = r.shard_id;
					local.shardEndpoint = std::move(r.shard_endpoint);
					// Hotfix : nouvelle session post-Flow (l'ancienne AuthOnly a été kickée).
					local.sessionId = r.session_id;
				}
			}
			{
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
			}
			LOG_INFO(Net, "[AuthUiPresenter] MasterShardClientFlow finished (success={} shard_pick={})", (int)r.success, (int)r.shard_choice_required);
		});
	}

	void AuthUiPresenter::StartVerifyEmailWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const uint64_t accountId = m_pendingVerifyAccountId;
		const std::string code = m_verifyCode;

		m_pendingAsyncKind = AsyncKind::VerifyEmail;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, serverFingerprint, accountId, code]() {
			AsyncResult local{};
			engine::network::NetClient client;
			ApplyMasterTlsConfig(client, serverFingerprint, allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			std::vector<uint8_t> payload = engine::network::BuildVerifyEmailRequestPayload(accountId, code);
			bool done = false;
			bool ok = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeVerifyEmailRequest, payload,
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "VERIFY_EMAIL timeout.";
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
						{
							errMsg = std::string(NetErrorLabel(er->errorCode));
							return;
						}
						ok = true;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send VERIFY_EMAIL failed.";
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
			local.success = ok;
			local.message = ok ? std::string("Email verified successfully.") : (errMsg.empty() ? "VERIFY_EMAIL failed." : errMsg);
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartResendVerificationWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const uint64_t accountId = m_pendingVerifyAccountId;

		m_pendingAsyncKind = AsyncKind::ResendVerification;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, serverFingerprint, accountId]() {
			AsyncResult local{};
			engine::network::NetClient client;
			ApplyMasterTlsConfig(client, serverFingerprint, allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			std::vector<uint8_t> payload = engine::network::BuildResendVerificationRequestPayload(accountId);
			bool done = false;
			bool ok = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeResendVerificationRequest, payload,
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "RESEND_VERIFICATION timeout.";
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
						{
							errMsg = std::string(NetErrorLabel(er->errorCode));
							return;
						}
						ok = true;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send RESEND_VERIFICATION failed.";
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
			local.success = ok;
			local.message = ok ? std::string("Verification email resent.") : (errMsg.empty() ? "RESEND_VERIFICATION failed." : errMsg);
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartStatusProbeWorker(const engine::core::Config& cfg)
	{
		LOG_INFO(Core, "[StatusProbe] démarrage worker — url='{}'", m_masterAvailabilityUrl);
		JoinWorker();

		const std::string url = m_masterAvailabilityUrl;
		m_pendingAsyncKind = AsyncKind::StatusProbe;
		m_asyncResult = {};
		LOG_INFO(Core, "[StatusProbe] état async réinitialisé, kind=StatusProbe");

		auto fillSimulatedStatus = [&](AsyncResult& out, std::string_view reason)
		{
			out.ready = true;
			out.success = true;
			out.statusCache = {};
			out.statusCache.authOk = true;
			out.statusCache.masterOk = true;
			out.statusCache.totalPlayers = 0;
			out.statusCache.servers.clear();
			out.statusCache.servers.push_back({ "EU-1", true, 128u });
			out.statusCache.servers.push_back({ "US-1", true, 74u });
			out.statusCache.servers.push_back({ "AS-1", true, 19u });
			for (const auto& s : out.statusCache.servers)
				out.statusCache.totalPlayers += s.players;
			out.message = std::string("Status probe simulated: ") + std::string(reason);
		};

		if (url.empty())
		{
			LOG_WARN(Core, "[StatusProbe] URL vide — utilisation du jeu de données simulé (dev)");
			AsyncResult local{};
			fillSimulatedStatus(local, "empty status url");
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
			return;
		}

		const uint32_t timeoutMs =
			static_cast<uint32_t>(std::clamp<int64_t>(cfg.GetInt("client.auth_ui.status_probe_timeout_ms", 1500), 300, 5000));
		LOG_INFO(Core, "[StatusProbe] timeout HTTP configuré: {} ms", timeoutMs);

		m_worker = std::thread([this, url, timeoutMs]() {
			LOG_INFO(Core, "[StatusProbe] thread réseau: GET en cours vers {}", url);
			AsyncResult local{};
			// Mesure de l'aller-retour HTTP (latence master affichee dans la liste des
			// serveurs). On encadre uniquement l'appel reseau.
			const auto httpT0 = std::chrono::steady_clock::now();
#if defined(_WIN32)
			const StatusHttpResponse resp = HttpGetStatusUrlWin32(url, timeoutMs);
#else
			const StatusHttpResponse resp = HttpGetStatusUrlCurl(url, timeoutMs);
#endif
			const auto httpT1 = std::chrono::steady_clock::now();
			const int httpLatencyMs = static_cast<int>(
				std::chrono::duration_cast<std::chrono::milliseconds>(httpT1 - httpT0).count());

			auto publishFailure = [&](std::string_view userMessage, std::string_view transportDetail) {
				local.ready = true;
				local.success = false;
				local.statusCache = {};
				local.statusCache.authOk = false;
				local.statusCache.masterOk = false;
				const std::string detail = std::string(transportDetail);
				local.statusCache.infoMessage = detail.empty() ? std::string(userMessage) : detail;
				local.message = std::string(userMessage);
				LOG_WARN(Core, "[StatusProbe] échec — msg='{}' transport='{}'", userMessage, detail);
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
			};

			if (!resp.transportError.empty())
			{
				publishFailure("erreur transport HTTP", resp.transportError);
				return;
			}

			LOG_INFO(Core, "[StatusProbe] réponse brute: code HTTP {} — {} octet(s)", resp.statusCode, resp.body.size());

			if (resp.statusCode < 200u || resp.statusCode >= 300u)
			{
				publishFailure("code HTTP hors plage 2xx", std::to_string(resp.statusCode));
				return;
			}

			if (resp.body.empty())
			{
				publishFailure("corps HTTP vide", {});
				return;
			}

			std::string parseDetail;
			if (!ParseStatusProbeJsonBody(resp.body, local.statusCache, parseDetail))
			{
				publishFailure(parseDetail, {});
				return;
			}

			// Latence master mesuree (ms) : renseignee seulement quand la sonde aboutit.
			local.statusCache.latencyMs = httpLatencyMs;
			local.ready = true;
			local.success = true;
			local.message = parseDetail;
			LOG_INFO(Core, "[StatusProbe] latence master mesuree: {} ms", httpLatencyMs);
			LOG_INFO(Core,
				"[StatusProbe] succès — authOk={} masterOk={} shards={} totalJoueurs={} message='{}'",
				local.statusCache.authOk ? "oui" : "non", local.statusCache.masterOk ? "oui" : "non", local.statusCache.servers.size(),
				local.statusCache.totalPlayers, local.message);

			{
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				LOG_INFO(Core, "[StatusProbe] résultat publié (mutex), prêt pour PollAsyncResult");
			}
		});
		LOG_INFO(Core, "[StatusProbe] worker std::thread créé, joinable={}", m_worker.joinable() ? 1 : 0);
	}
}
#endif
