// AuthUiPresenter — cœur : init / arrêt, préférences mémorisées, titre de fenêtre,
// soumission de la phase courante. Threads réseau : AuthUiPresenterWorkers.cpp ;
// Update et modèle de rendu : AuthUiPresenterUpdate.cpp ; utilitaires communs :
// AuthUiPresenterInternal.h.

#include "src/client/auth/AuthUiPresenterInternal.h"

namespace engine::client
{
	using namespace engine::client::auth_detail;

	const char* AuthUiPresenter::PhaseLogName(Phase phase)
	{
		switch (phase)
		{
		case Phase::Login: return "Login";
		case Phase::Register: return "Register";
		case Phase::Submitting: return "Submitting";
		case Phase::ForgotPassword: return "ForgotPassword";
		case Phase::VerifyEmail: return "VerifyEmail";
		case Phase::EmailConfirmationPending: return "EmailConfirmationPending";
		case Phase::LanguageSelectionFirstRun: return "LanguageSelectionFirstRun";
		case Phase::LanguageOptions: return "LanguageOptions";
		case Phase::Terms: return "Terms";
		case Phase::CharacterCreate: return "CharacterCreate";
		case Phase::CharacterSelect: return "CharacterSelect";
		case Phase::ShardPick: return "ShardPick";
		case Phase::Error: return "Error";
		default: return "Unknown";
		}
	}

	AuthUiPresenter::~AuthUiPresenter()
	{
		Shutdown();
	}

#if !defined(_WIN32)

	bool AuthUiPresenter::Init(const engine::core::Config&)
	{
		m_initialized = true;
		m_flowComplete = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (auth UI skipped on non-Windows build)");
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_usingNativeAuthScreen = false;
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	/// Sous-projet C MVP (Task 12) — Stub Linux : aucun rendu ImGui, mais
	/// on garde l'API symetrique pour que le code partage (et eventuels
	/// tests build Linux) compile. Le presenter est quand meme cree par
	/// valeur dans la classe — son Init n'a juste pas ete appele.
	const engine::client::CharacterCreationPresenter* AuthUiPresenter::GetCharacterCreationPresenter() const
	{
		return &m_characterCreationPresenter;
	}

	/// Sous-projet C MVP (Task 12) — Stub Linux : pas de gestion races
	/// cote serveur Linux. Retourne nullptr (Engine n'est pas branche
	/// sur Linux car le client est Windows-only).
	engine::render::skinned::SkinnedMesh* AuthUiPresenter::GetRaceMeshForId(const std::string&)
	{
		return nullptr;
	}

	bool AuthUiPresenter::BlocksWorldInput() const
	{
		return false;
	}

	bool AuthUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_viewportW = width;
		m_viewportH = height;
		return width > 0 && height > 0;
	}

	void AuthUiPresenter::SaveRememberPreference()
	{
	}

	void AuthUiPresenter::ApplyLocaleSelection(bool)
	{
	}

	bool AuthUiPresenter::PatchPersistedLocaleKey(std::string_view)
	{
		return false;
	}

	void AuthUiPresenter::OpenLanguageOptions()
	{
	}

	std::string AuthUiPresenter::Tr(std::string_view key, const LocalizationService::Params&) const
	{
		return std::string(key);
	}

	std::string AuthUiPresenter::CurrentLocale() const
	{
		return "en";
	}

	std::string AuthUiPresenter::LocalizedLanguageName(std::string_view localeTag) const
	{
		return std::string(localeTag);
	}

	void AuthUiPresenter::SubmitCurrentPhase(const engine::core::Config&)
	{
	}

	void AuthUiPresenter::AppendPasswordStars(std::string&, size_t) const {}
	void AuthUiPresenter::EnsurePasswordSalt(const engine::core::Config&) {}
	std::string AuthUiPresenter::ComputeClientHash(const engine::core::Config&) const { return {}; }
	void AuthUiPresenter::StartRegisterWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartLoginWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartVerifyEmailWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartForgotPasswordWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartUsernameCheckWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartTermsStatusWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartTermsAcceptWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartCharacterCreateWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartStatusProbeWorker(const engine::core::Config&) {}
	void AuthUiPresenter::RefreshShardPickEntriesFromStatusCache() {}
	void AuthUiPresenter::ResetMasterSession() {}
	void AuthUiPresenter::StartMasterFlowWorker(const engine::core::Config&) {}
	void AuthUiPresenter::PollAsyncResult(const engine::core::Config&) {}
	void AuthUiPresenter::UpdateWindowTitle(engine::platform::Window&) const {}
	void AuthUiPresenter::JoinWorker() {}

	uint32_t AuthUiPresenter::OptionsSubmenuLineCount(OptionsSubMenu sub)
	{
		(void)sub;
		return 0;
	}

	void AuthUiPresenter::EnterOptionsSubmenuFromRoot(uint32_t /*categoryIndex*/)
	{
	}

	void AuthUiPresenter::ImGuiSubmitLogin(const engine::core::Config&, const char*, const char*, bool) {}
	void AuthUiPresenter::ImGuiNavigateToRegisterFromLogin() {}
	void AuthUiPresenter::ImGuiBackFromRegisterToLogin() {}
	void AuthUiPresenter::ImGuiOpenForgotPasswordPortal(const engine::core::Config&, engine::platform::Window&) {}
	void AuthUiPresenter::ImGuiRequestClose(engine::platform::Window& window)
	{
		window.RequestClose();
	}

	AuthUiPresenter::LanguageOptionsImGuiMirror AuthUiPresenter::BuildLanguageOptionsImGuiMirror() const
	{
		return {};
	}

	void AuthUiPresenter::ImGuiApplyLanguageOptionsMenu(const engine::core::Config&, const LanguageOptionsImGuiMirror&) {}
	void AuthUiPresenter::ImGuiCloseLanguageOptionsWithoutApply() {}

	AuthUiPresenter::RegisterFieldsMirrorForImGui AuthUiPresenter::BuildRegisterFieldsMirrorForImGui() const
	{
		return {};
	}

	void AuthUiPresenter::ImGuiSubmitRegister(const engine::core::Config&, const RegisterImGuiSubmit&) {}
	void AuthUiPresenter::ImGuiNavigateToForgotFromLogin() {}
	void AuthUiPresenter::ImGuiSetShardPickChoiceShardId(uint32_t) {}
	void AuthUiPresenter::ImGuiSubmitShardPick(const engine::core::Config&) {}
	void AuthUiPresenter::ImGuiBackFromShardPickToLogin() {}

	void AuthUiPresenter::CommitLanguageOptionsMenuApply(const engine::core::Config&)
	{
	}

#else

	bool AuthUiPresenter::Init(const engine::core::Config& cfg)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AuthUiPresenter] Init ignored: already initialized");
			return true;
		}

		// Mutex sur le tas (STAB.11/13) : évite un std::mutex membre d’un gros objet Engine.
		// STAB.14 — CRITICAL_SECTION wrapper instead of std::mutex (SRWLOCK crashes).
		m_asyncMutex = std::make_unique<AuthMutex>();

		m_authEnabled = cfg.GetBool("client.auth_ui.enabled", true);
		if (!m_authEnabled)
		{
			m_flowComplete = true;
			m_initialized = true;
			LOG_INFO(Core, "[AuthUiPresenter] Init OK (disabled via client.auth_ui.enabled=false)");
			return true;
		}

		m_flowComplete = false;
		m_phase = Phase::Login;
		m_errorReturnPhase = Phase::Login;
		m_login.clear();
		m_password.clear();
		m_email.clear();
		m_firstName.clear();
		m_lastName.clear();
		m_birthDay.clear();
		m_birthMonth.clear();
		m_birthYear.clear();
		m_country.clear();
		m_verifyCode.clear();
		m_termsTitle.clear();
		m_termsVersionLabel.clear();
		m_termsLocale.clear();
		m_termsContent.clear();
		m_characterName.clear();
		m_activeField = 0;
		m_termsScrollOffset = 0;
		m_termsTotalLength = 0;
		m_userErrorText.clear();
		m_infoBanner.clear();
		m_registeredTagId.clear();
		m_pendingVerifyAccountId = 0;
		m_pendingTermsEditionId = 0;
		m_chosenShardId = 0;
		m_postRegistrationCharacterCreatePending = false;
		m_termsScrolledToBottom = false;
		m_termsAcknowledgeChecked = false;
		m_rememberLogin = false;
		m_savedRememberLogin = false;
		m_hasPersistedLocale = false;
		m_languageSelectionIndex = 0;
		m_optionsSubMenu = OptionsSubMenu::Root;
		m_optionsRootSelection = 0;
		m_optionsSubSelection = 0;
		m_phaseBeforeOptions = Phase::Login;
		m_selectedLocale.clear();
		m_persistedLocale.clear();
		m_videoFullscreen = cfg.GetBool("render.fullscreen", true);
		m_videoVsync = cfg.GetBool("render.vsync", true);
		m_videoResWidth = static_cast<int32_t>(std::max<int64_t>(640, cfg.GetInt("render.resolution_width", 1920)));
		m_videoResHeight = static_cast<int32_t>(std::max<int64_t>(480, cfg.GetInt("render.resolution_height", 1080)));
		m_videoQualityPreset = static_cast<int32_t>(std::clamp<int64_t>(cfg.GetInt("render.quality_preset", engine::client::kDefaultQualityPreset), 0, 3));
		m_videoFovDegrees = static_cast<float>(std::clamp(cfg.GetDouble("render.fov", 70.0), 60.0, 120.0));
		m_videoFullscreenPending = m_videoFullscreen;
		m_videoVsyncPending = m_videoVsync;
		m_videoResWidthPending = m_videoResWidth;
		m_videoResHeightPending = m_videoResHeight;
		m_videoQualityPresetPending = m_videoQualityPreset;
		m_videoFovDegreesPending = m_videoFovDegrees;
		m_pendingVideoSettings = {};
		m_audioMasterVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.master_volume", 1.0)));
		m_audioMusicVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.music_volume", 1.0)));
		m_audioSfxVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.sfx_volume", 1.0)));
		m_audioUiVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.ui_volume", 1.0)));
		m_audioMasterVolumePending = m_audioMasterVolume;
		m_audioMusicVolumePending = m_audioMusicVolume;
		m_audioSfxVolumePending = m_audioSfxVolume;
		m_audioUiVolumePending = m_audioUiVolume;
		m_pendingAudioSettings = {};
		m_mouseSensitivity = static_cast<float>(cfg.GetDouble("camera.mouse_sensitivity", 0.002));
		m_mouseSensitivityPending = m_mouseSensitivity;
		m_invertY = cfg.GetBool("controls.invert_y", false);
		m_invertYPending = m_invertY;
		m_useZqsd = cfg.GetString("controls.movement_layout", "wasd") == "zqsd";
		m_useZqsdPending = m_useZqsd;
		m_pendingControlSettings = {};
		m_gameplayUdpEnabled = cfg.GetBool("client.gameplay_udp.enabled", false);
		m_gameplayUdpEnabledPending = m_gameplayUdpEnabled;
		m_allowInsecureDev = cfg.GetBool("client.allow_insecure_dev", true);
		m_allowInsecureDevPending = m_allowInsecureDev;
		m_authTimeoutMs = static_cast<uint32_t>(std::clamp<int64_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000), 1000, 15000));
		m_authTimeoutMsPending = m_authTimeoutMs;
		m_authMinimalChrome = cfg.GetBool("render.auth_ui.minimal_chrome", true);
		m_authLoginArtColumn = cfg.GetBool("render.auth_ui.login_art_column", false);
		m_layoutAuthTitleLine1FromPanelTopPx = static_cast<int32_t>(
			std::clamp<int64_t>(cfg.GetInt("render.auth_ui.layout.title_line1_from_panel_top_px", 4), 0, 120));
		m_layoutAuthGapTitleToSectionPx = static_cast<int32_t>(
			std::clamp<int64_t>(cfg.GetInt("render.auth_ui.layout.gap_title_to_section_px", 8), 0, 80));
		m_layoutAuthTitleCenterViewportWidth = cfg.GetBool("render.auth_ui.layout.title_center_viewport_width", true);
		m_layoutAuthFieldRowExtraPx = static_cast<int32_t>(
			std::clamp<int64_t>(cfg.GetInt("render.auth_ui.layout.field_row_extra_px", 0), 0, 64));
		// URL de sonde "status" (client.status_api_url, via config/server.ini) au début de l'écran de connexion.
		m_masterAvailabilityUrl = ResolveStatusApiUrl(cfg);
		LOG_INFO(Core, "[StatusProbe] Init: URL statut maître = '{}'", m_masterAvailabilityUrl);
		m_statusProbeInitialized = false;
		m_statusProbeCompletedOnce = false;
		m_lastStatusProbeHttpSuccess = false;
		m_statusPollTimer = 0.f;
		m_authLogoSizePx = static_cast<int32_t>(std::clamp<int64_t>(cfg.GetInt("render.auth_ui.logo_size_px", 96), 32, 256));
		m_authAvailabilityChecking = false;
		m_authAvailabilityPollTimer = 0.f;
		m_pendingGameSettings = {};
		m_argonSalt.clear();
		m_asyncResult = {};
		m_pendingAsyncKind = AsyncKind::None;
		m_masterSessionId = 0;
		JoinWorker();
		m_masterClient.reset();
		LoadRememberPreference();
		const std::string configLocale = LocalizationService::NormalizeLocaleTag(cfg.GetString("client.locale", ""));
		const std::string requestedLocale = !m_persistedLocale.empty() ? m_persistedLocale : configLocale;
		if (!m_localization.Init(cfg, requestedLocale.empty() ? LocalizationService::DetectSystemLocaleTag() : requestedLocale))
		{
			LOG_ERROR(Core, "[AuthUiPresenter] Init FAILED: localization init failed");
			return false;
		}
		if (requestedLocale.empty())
		{
			// 1er lancement (aucune locale persistée) : on affiche l'écran illustré de
			// sélection de langue (Phase::LanguageSelectionFirstRun) avec la liste filtrée
			// {système, en} calculée IMMÉDIATEMENT par ComputeSuggestedLocales (fonction
			// PURE, aucun réseau). La langue déduite de l'IP enrichira la liste plus tard,
			// via PollGeoUpdate, quand la géoloc aura répondu.
			//
			// IMPORTANT — anti « device lost » : la géoloc (thread WinHTTP vers ip-api.com)
			// n'est PLUS démarrée ici. Lancée pendant le boot (#909) elle crashait
			// vkCreateInstance ; déplacée à la 1re frame de l'écran (#915) elle crashait
			// quand même le GPU (init réseau concurrente des toutes premières frames du
			// pilote → faute « device lost », diagnostic confirmé par bisection). Elle est
			// désormais démarrée DE MANIÈRE DIFFÉRÉE dans Update_LanguageSelect, après
			// chauffe du pipeline de rendu (cf. AuthScreenLanguageSelect.cpp). Le bypass
			// #915 (qui sautait cet écran) est donc retiré : l'écran refait ses propositions.
			m_selectedLocale = m_localization.GetCurrentLocale();
			m_firstRunLocales = engine::client::ComputeSuggestedLocales(
				m_selectedLocale, /*ipLocale (encore inconnue)*/ "", m_localization.GetAvailableLocales());
			auto it = std::find(m_firstRunLocales.begin(), m_firstRunLocales.end(), m_selectedLocale);
			m_languageSelectionIndex = (it != m_firstRunLocales.end())
				? static_cast<uint32_t>(std::distance(m_firstRunLocales.begin(), it)) : 0u;
			SetPhase(Phase::LanguageSelectionFirstRun);
			LOG_INFO(Core, "[AuthUiPresenter] 1er lancement : écran de langue (detected={}, {} proposition(s) immédiate(s) ; géoloc différée hors premières frames)",
				m_selectedLocale, m_firstRunLocales.size());
		}
		else
		{
			m_selectedLocale = m_localization.GetCurrentLocale();
			LOG_INFO(Core, "[AuthUiPresenter] Initial locale retained ({})", m_selectedLocale);
		}

		// Sous-projet C MVP (Task 12) — Charge en parallele la liste des
		// races pour pouvoir l'exposer au renderer ImGui de l'ecran de
		// creation de personnage. Init() parse races.json + classes.json
		// sous le contentRoot defini par config (paths.content). Si l'init
		// echoue (fichier absent / parse error), GetCharacterCreationPresenter()
		// retournera quand meme un pointer non-null mais GetRaces() sera
		// vide -> l'ecran ImGui retombera sur son fallback "(liste des
		// races indisponible)".
		if (!m_characterCreationPresenter.Init(cfg))
		{
			LOG_WARN(Core, "[AuthUiPresenter] CharacterCreationPresenter.Init FAIL -- ecran creation perso n'aura pas la liste des races");
		}

		m_initialized = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (master host from client.master_host ou client.master_tcp_host / client.master_port, locale={})", m_localization.GetCurrentLocale());
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		JoinWorker();
		ResetMasterSession();
		SaveRememberPreference();
		m_password.clear();
		m_localization.Shutdown();
		// Sous-projet C MVP (Task 12) — Libere les races/classes charges
		// dans Init. Idempotent ; suit la symetrie Init/Shutdown du presenter
		// pour pouvoir reinitialiser AuthUi sans fuite de RaceDefinition.
		m_characterCreationPresenter.Shutdown();
		m_engineForRaceLookup = nullptr;
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	/// Sous-projet C MVP (Task 12) — Retourne l'adresse du presenter
	/// charge dans \c Init. Toujours non-null tant que l'objet AuthUi
	/// est en vie (membre par valeur). Le caller doit verifier
	/// \c GetRaces().empty() pour gerer le cas "Init a rate / fichier
	/// races.json absent".
	const engine::client::CharacterCreationPresenter* AuthUiPresenter::GetCharacterCreationPresenter() const
	{
		return &m_characterCreationPresenter;
	}

	/// Sous-projet C MVP (Task 12) — Delegue le lookup race_str ->
	/// SkinnedMesh* a \c Engine::GetRaceMesh (qui implemente le fallback
	/// humains). Retourne nullptr si Engine n'est pas branche (cas test
	/// unitaire sans Engine ou boot rate avant SetEngineForRaceMeshLookup).
	engine::render::skinned::SkinnedMesh* AuthUiPresenter::GetRaceMeshForId(const std::string& raceId)
	{
		if (m_engineForRaceLookup == nullptr) return nullptr;
		return m_engineForRaceLookup->GetRaceMesh(raceId);
	}

	engine::render::skinned::SkinnedMesh* AuthUiPresenter::GetRaceMeshForId(const std::string& raceId,
	                                                                        const std::string& gender)
	{
		if (m_engineForRaceLookup == nullptr) return nullptr;
		return m_engineForRaceLookup->GetRaceMesh(raceId, gender);
	}

	void AuthUiPresenter::LoadRememberPreference()
	{
		engine::core::Config persisted;
		if (persisted.LoadFromFile(kUserSettingsPath))
		{
			m_rememberLogin = persisted.GetBool("client.auth_ui.remember_login", false);
			m_savedRememberLogin = m_rememberLogin;
			if (m_rememberLogin)
			{
				LoadRememberedLoginSidecar(m_login);
			}
			else
			{
				std::string sideLogin;
				LoadRememberedLoginSidecar(sideLogin);
				if (!sideLogin.empty())
				{
					m_rememberLogin = true;
					m_savedRememberLogin = true;
					m_login = std::move(sideLogin);
				}
			}
			m_persistedLocale = LocalizationService::NormalizeLocaleTag(persisted.GetString("client.locale", ""));
			m_hasPersistedLocale = !m_persistedLocale.empty();
			m_videoFullscreen = persisted.GetBool("render.fullscreen", m_videoFullscreen);
			m_videoVsync = persisted.GetBool("render.vsync", m_videoVsync);
			if (persisted.Has("render.resolution_width"))
				m_videoResWidth = static_cast<int32_t>(std::max<int64_t>(640, persisted.GetInt("render.resolution_width", m_videoResWidth)));
			if (persisted.Has("render.resolution_height"))
				m_videoResHeight = static_cast<int32_t>(std::max<int64_t>(480, persisted.GetInt("render.resolution_height", m_videoResHeight)));
			if (persisted.Has("render.quality_preset"))
				m_videoQualityPreset = static_cast<int32_t>(std::clamp<int64_t>(persisted.GetInt("render.quality_preset", m_videoQualityPreset), 0, 3));
			if (persisted.Has("render.fov"))
				m_videoFovDegrees = static_cast<float>(std::clamp(persisted.GetDouble("render.fov", m_videoFovDegrees), 60.0, 120.0));
			m_videoFullscreenPending = m_videoFullscreen;
			m_videoVsyncPending = m_videoVsync;
			m_videoResWidthPending = m_videoResWidth;
			m_videoResHeightPending = m_videoResHeight;
			m_videoQualityPresetPending = m_videoQualityPreset;
			m_videoFovDegreesPending = m_videoFovDegrees;
			m_audioMasterVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.master_volume", m_audioMasterVolume)));
			m_audioMusicVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.music_volume", m_audioMusicVolume)));
			m_audioSfxVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.sfx_volume", m_audioSfxVolume)));
			m_audioUiVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.ui_volume", m_audioUiVolume)));
			m_audioMasterVolumePending = m_audioMasterVolume;
			m_audioMusicVolumePending = m_audioMusicVolume;
			m_audioSfxVolumePending = m_audioSfxVolume;
			m_audioUiVolumePending = m_audioUiVolume;
			m_mouseSensitivity = static_cast<float>(persisted.GetDouble("camera.mouse_sensitivity", m_mouseSensitivity));
			m_mouseSensitivityPending = m_mouseSensitivity;
			m_invertY = persisted.GetBool("controls.invert_y", m_invertY);
			m_invertYPending = m_invertY;
			m_useZqsd = persisted.GetString("controls.movement_layout", m_useZqsd ? "zqsd" : "wasd") == "zqsd";
			m_useZqsdPending = m_useZqsd;
			m_gameplayUdpEnabled = persisted.GetBool("client.gameplay_udp.enabled", m_gameplayUdpEnabled);
			m_gameplayUdpEnabledPending = m_gameplayUdpEnabled;
			m_allowInsecureDev = persisted.GetBool("client.allow_insecure_dev", m_allowInsecureDev);
			m_allowInsecureDevPending = m_allowInsecureDev;
			m_authTimeoutMs = static_cast<uint32_t>(std::clamp<int64_t>(persisted.GetInt("client.auth_ui.timeout_ms", m_authTimeoutMs), 1000, 15000));
			m_authTimeoutMsPending = m_authTimeoutMs;
			LOG_INFO(Core, "[AuthUiPresenter] Preferences loaded (remember_login={}, locale={}, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
				m_rememberLogin, m_persistedLocale, m_videoFullscreen, m_videoVsync,
				m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
				m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
				m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
			return;
		}
		m_rememberLogin = false;
		m_savedRememberLogin = false;
		{
			std::string sideLogin;
			LoadRememberedLoginSidecar(sideLogin);
			if (!sideLogin.empty())
			{
				m_rememberLogin = true;
				m_savedRememberLogin = true;
				m_login = std::move(sideLogin);
			}
		}
		m_persistedLocale.clear();
		m_hasPersistedLocale = false;
		LOG_INFO(Core, "[AuthUiPresenter] Preferences defaulted (remember_login=false, locale=unset, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_videoFullscreen, m_videoVsync, m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
			m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
			m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
	}

	void AuthUiPresenter::SaveRememberPreference()
	{
		const std::filesystem::path settingsPath{ kUserSettingsPath };
		if (engine::platform::FileSystem::Exists(settingsPath))
		{
			if (PatchRememberLoginInSettingsFile(m_rememberLogin))
				m_savedRememberLogin = m_rememberLogin;
			else
				LOG_WARN(Core, "[AuthUiPresenter] remember_login non persisté dans user_settings.json (patch absent ou échoué)");
			return;
		}

		const std::string locale = CurrentLocale();
		std::string json = BuildUserSettingsJson(m_rememberLogin, locale, m_videoFullscreen, m_videoVsync);
		ReplaceAudioSettings(json, m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume);
		ReplaceControlSettings(json, m_mouseSensitivity, m_invertY, m_useZqsd);
		ReplaceGameSettings(json, m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
		if (!engine::platform::FileSystem::WriteAllText(settingsPath, json))
		{
			LOG_WARN(Core, "[AuthUiPresenter] Failed to persist preferences");
			return;
		}
		m_savedRememberLogin = m_rememberLogin;
		m_persistedLocale = locale;
		m_hasPersistedLocale = !locale.empty();
		LOG_INFO(Core, "[AuthUiPresenter] Preferences persisted (remember_login={}, locale={}, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_rememberLogin, locale, m_videoFullscreen, m_videoVsync,
			m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
			m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
			m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
	}

	bool AuthUiPresenter::BlocksWorldInput() const
	{
		return m_initialized && !m_flowComplete;
	}

	void AuthUiPresenter::JoinWorker()
	{
		if (m_worker.joinable())
		{
			m_worker.join();
			LOG_INFO(Core, "[AuthUiPresenter] Background worker joined");
		}
	}

	void AuthUiPresenter::ResetMasterSession()
	{
		JoinWorker();
		m_masterSessionId = 0;
		if (m_masterClient)
		{
			m_masterClient->Disconnect("auth_ui_reset");
			m_masterClient.reset();
		}
	}

	void AuthUiPresenter::AppendPasswordStars(std::string& out, size_t len) const
	{
		for (size_t i = 0; i < len; ++i)
			out.push_back('*');
	}

	void AuthUiPresenter::EnsurePasswordSalt(const engine::core::Config& cfg)
	{
		(void)cfg;
		m_argonSalt = engine::auth::DeriveClientPasswordSaltFromLogin(m_login);
		if (m_argonSalt.size() != engine::auth::kArgon2SaltLength)
		{
			m_argonSalt.clear();
			LOG_ERROR(Core, "[AuthUiPresenter] Client-side Argon2 salt derivation FAILED (login trim empty or crypto error)");
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] Client-side Argon2 salt derived from login (len={})", m_argonSalt.size());
	}

	std::string AuthUiPresenter::ComputeClientHash(const engine::core::Config& cfg) const
	{
		engine::auth::Argon2Params params;
		params.time_cost = static_cast<uint32_t>(cfg.GetInt("client.argon2.time_cost", static_cast<int64_t>(params.time_cost)));
		params.memory_kib = static_cast<uint32_t>(cfg.GetInt("client.argon2.memory_kib", static_cast<int64_t>(params.memory_kib)));
		params.parallelism = static_cast<uint32_t>(cfg.GetInt("client.argon2.parallelism", static_cast<int64_t>(params.parallelism)));
		params.hash_len = static_cast<uint32_t>(cfg.GetInt("client.argon2.hash_len", static_cast<int64_t>(params.hash_len)));
		return engine::auth::Hash(m_password, m_argonSalt, params);
	}

	void AuthUiPresenter::UpdateWindowTitle(engine::platform::Window& window) const
	{
		std::string t = Tr("app.title");
		t += " | ";
		switch (m_phase)
		{
		case Phase::Login:
			t += Tr("auth.phase.login");
			break;
		case Phase::Register:
			t += Tr("auth.phase.register");
			break;
		case Phase::VerifyEmail:
			t += Tr("auth.phase.verify_email");
			break;
		case Phase::EmailConfirmationPending:
			t += Tr("auth.phase.email_confirmation");
			break;
		case Phase::ForgotPassword:
			t += Tr("auth.phase.forgot_password");
			break;
		case Phase::Terms:
			t += Tr("auth.phase.terms");
			break;
		case Phase::CharacterCreate:
			t += Tr("auth.phase.character_create");
			break;
		case Phase::CharacterSelect:
			t += Tr("auth.phase.character_select");
			break;
		case Phase::ShardPick:
			t += Tr("auth.phase.shard_pick");
			break;
		case Phase::LanguageSelectionFirstRun:
			t += Tr("language.first_run.title");
			break;
		case Phase::LanguageOptions:
			t += Tr("language.options.title");
			break;
		case Phase::Submitting:
			t += Tr("auth.phase.submitting");
			break;
		case Phase::Error:
			t += Tr("auth.phase.error");
			break;
		}
		if (!m_userErrorText.empty() && m_phase == Phase::Error)
		{
			t += " — ";
			const size_t kMax = 80;
			if (m_userErrorText.size() > kMax)
				t.append(m_userErrorText.data(), kMax);
			else
				t += m_userErrorText;
		}
		window.SetTitle(t);
	}

	bool AuthUiPresenter::PatchPersistedLocaleKey(std::string_view locale)
	{
		return PatchLocaleInSettingsFile(locale);
	}














	std::string AuthUiPresenter::Tr(std::string_view key, const LocalizationService::Params& params) const
	{
		return m_localization.Translate(key, params);
	}

	std::string AuthUiPresenter::CurrentLocale() const
	{
		return m_localization.GetCurrentLocale();
	}

	std::string AuthUiPresenter::LocalizedLanguageName(std::string_view localeTag) const
	{
		return Tr(std::string("language.name.") + std::string(localeTag));
	}

	void AuthUiPresenter::RefreshShardPickEntriesFromStatusCache()
	{
		// m_shardPickEntries vient d'un unique SERVER_LIST pris pendant le flux de connexion,
		// puis la connexion master est fermee : son current_load reste donc fige (souvent 0)
		// meme si des joueurs entrent/sortent. La sonde /status, elle, est rafraichie toutes
		// les 2 min et porte le compte a jour (sessionCharMap cote master). On reinjecte donc
		// players -> current_load par correspondance d'endpoint.
		if (m_shardPickEntries.empty() || m_statusCache.servers.empty())
			return;
		size_t patched = 0;
		for (auto& entry : m_shardPickEntries)
		{
			if (entry.endpoint.empty())
				continue;
			for (const auto& srv : m_statusCache.servers)
			{
				if (srv.endpoint == entry.endpoint)
				{
					if (entry.current_load != srv.players)
					{
						entry.current_load = srv.players;
						++patched;
					}
					// Repli : si l'entree SERVER_LIST n'a pas de nom public (vieux master),
					// on complete depuis la sonde /status (presentation + mode/regle).
					if (entry.display_name.empty() && !srv.display_name.empty())
					{
						entry.display_name = srv.display_name;
						entry.game_mode = srv.game_mode;
						entry.ruleset = srv.ruleset;
					}
					break;
				}
			}
		}
		if (patched > 0)
			LOG_INFO(Core, "[AuthUiPresenter] Shard pick: {} entree(s) de charge rafraichie(s) depuis la sonde /status", patched);
	}

	bool AuthUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[AuthUiPresenter] SetViewportSize FAILED: {}x{}", width, height);
			return false;
		}
		m_viewportW = width;
		m_viewportH = height;
		LOG_INFO(Core, "[AuthUiPresenter] Viewport {}x{}", width, height);
		return true;
	}


void AuthUiPresenter::SubmitCurrentPhase(const engine::core::Config& cfg)
{
	if (m_phase == Phase::Error)
	{
		const Phase target = m_errorReturnPhase;
		SetPhase(target);
		m_activeField = 0;
		const char* const phaseLabel = AuthUiPresenter::PhaseLogName(target);
		LOG_INFO(Core, "[AuthUiPresenter] Error acknowledged -> {}", phaseLabel);
		return;
	}
	if (m_phase == Phase::ShardPick)
	{
		if (m_shardPickChoiceShardId == 0)
		{
			EnterAuthErrorPhase(Phase::ShardPick, Tr("auth.error.shard_pick_required"));
			LOG_WARN(Core, "[AuthUiPresenter] ShardPick submit rejected: no shard selected");
			return;
		}
		// Mémoriser le royaume choisi : il sert d'override pour le MasterFlow final
		// (après création de personnage pour un nouveau compte, ou directement pour un retour).
		m_chosenShardId = m_shardPickChoiceShardId;
		if (m_postRegistrationCharacterCreatePending)
		{
			LOG_INFO(Core, "[AuthUiPresenter] ShardPick -> CharacterCreate (compte fraîchement créé sur shard {})", m_chosenShardId);
			SetPhase(Phase::CharacterCreate);
			return;
		}
		SetPhase(Phase::Submitting);
		m_shardFlowOverrideId = m_chosenShardId;
		StartMasterFlowWorker(cfg);
		return;
	}
	if (m_phase == Phase::Login)
	{
		if (m_login.empty() || m_password.empty())
		{
			EnterAuthErrorPhase(Phase::Login, Tr("auth.error.enter_login_password"));
			LOG_WARN(Core, "[AuthUiPresenter] Submit rejected: empty fields");
			return;
		}
		SetPhase(Phase::Submitting);
		StartLoginWorker(cfg);
		return;
	}
	if (m_phase == Phase::Register)
	{
		if (m_login.empty() || m_password.empty() || m_passwordConfirm.empty() || m_email.empty()
			|| m_firstName.empty() || m_lastName.empty() || m_birthDay.empty()
			|| m_birthMonth.empty() || m_birthYear.empty() || m_country.empty())
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.enter_register_fields"));
			LOG_WARN(Core, "[AuthUiPresenter] Register submit rejected: empty fields");
			return;
		}
		if (m_password != m_passwordConfirm)
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.password_mismatch"));
			LOG_WARN(Core, "[AuthUiPresenter] Register submit rejected: password mismatch");
			return;
		}
		if (!IsValidBirthDateFields(m_birthDay, m_birthMonth, m_birthYear))
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.invalid_birth_date"));
			LOG_INFO(Core,
				"[AUTH-REG] submit rejected: invalid birth date (day_len={} month_len={} year_len={})",
				m_birthDay.size(),
				m_birthMonth.size(),
				m_birthYear.size());
			return;
		}
		if (m_country.size() != 2 || !std::isalpha(static_cast<unsigned char>(m_country[0])) || !std::isalpha(static_cast<unsigned char>(m_country[1])))
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.enter_country"));
			LOG_INFO(Core, "[AUTH-REG] submit rejected: invalid country ('{}')", m_country);
			return;
		}
		LOG_INFO(Core,
			"[AUTH-REG] submit accepted -> StartRegisterWorker (login_len={} email_len={} first_len={} last_len={} pwd_len={} "
			"birth_d_len={} birth_m_len={} birth_y_len={})",
			m_login.size(),
			m_email.size(),
			m_firstName.size(),
			m_lastName.size(),
			m_password.size(),
			m_birthDay.size(),
			m_birthMonth.size(),
			m_birthYear.size());
		SetPhase(Phase::Submitting);
		StartRegisterWorker(cfg);
		return;
	}
	if (m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
	{
		if (m_pendingVerifyAccountId == 0 || m_verifyCode.empty())
		{
			EnterAuthErrorPhase(Phase::VerifyEmail, Tr("auth.error.enter_verify_code"));
			return;
		}
		if (!IsValidVerificationCode(m_verifyCode))
		{
			EnterAuthErrorPhase(Phase::VerifyEmail, Tr("auth.error.invalid_verify_code"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartVerifyEmailWorker(cfg);
		return;
	}
	if (m_phase == Phase::ForgotPassword)
	{
		if (m_email.empty())
		{
			EnterAuthErrorPhase(Phase::ForgotPassword, Tr("auth.error.enter_recovery_email"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartForgotPasswordWorker(cfg);
		return;
	}
	if (m_phase == Phase::Terms)
	{
		if (!m_termsScrolledToBottom || !m_termsAcknowledgeChecked)
		{
			EnterAuthErrorPhase(Phase::Terms, Tr("auth.error.accept_terms"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartTermsAcceptWorker(cfg);
		return;
	}
	if (m_phase == Phase::CharacterCreate)
	{
		if (m_characterName.empty())
		{
			EnterAuthErrorPhase(Phase::CharacterCreate, Tr("auth.error.enter_character_name"));
			return;
		}
		if (!IsValidCharacterNameLocal(m_characterName))
		{
			EnterAuthErrorPhase(Phase::CharacterCreate, Tr("auth.error.invalid_character_name"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartCharacterCreateWorker(cfg);
	}
}
#endif

}
