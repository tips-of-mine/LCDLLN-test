#pragma once

#include "engine/client/LocalizationService.h"
#include "engine/core/Config.h"
#include "engine/network/CharacterPayloads.h"
#include "engine/network/NetClient.h"
#include "engine/network/ServerListPayloads.h"
#include "engine/platform/Input.h"
#include "engine/platform/StableMutex.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace engine::platform
{
	class Window;
}

namespace engine::client
{
	/// STAB.14 — voir StableMutex.h (CRITICAL_SECTION sur Windows à la place de std::mutex / SRWLOCK).
	using AuthMutex = engine::platform::StableMutex;

	/// Sous-écran des options (auth) : menu racine puis catégories.
	/// Déclaré au niveau du namespace pour éviter les soucis de parsing MSVC avec les enums imbriqués.
	enum class OptionsSubMenu : uint8_t
	{
		Root,
		Language,
		Video,
		Audio,
		Controls,
		Game
	};

	/// État de la vérification temps-réel du nom d'utilisateur.
	enum class UsernameCheckState : uint8_t
	{
		Idle       = 0, ///< Champ vide ou < 3 caractères ; aucun indicateur affiché.
		Pending    = 1, ///< Debounce en cours ou requête envoyée, réponse attendue.
		Available  = 2, ///< Serveur a confirmé disponibilité.
		Taken      = 3, ///< Serveur a indiqué login déjà pris.
	};

	/// STAB.13 — Login / register UI state machine; drives M20.5/M22.6 master flow without duplicating protocol.
	/// Assets reference: \c game/data/ui/login and \c game/data/ui/register (documented in panel text; no absolute paths).
	class AuthUiPresenter final
	{
	public:
		struct VideoSettingsCommand
		{
			bool applyRequested = false;
			bool fullscreen = false;
			bool vsync = true;
			int32_t resolutionWidth = 1920;
			int32_t resolutionHeight = 1080;
			/// 0 = basse … 3 = ultra (aligné \c SettingsMenuPresenter).
			int32_t qualityPreset = 2;
			float fovDegrees = 70.f;
		};

		/// Phase 3 — Commande one-shot émise au moment où l'utilisateur entre dans le monde
		/// (clic « Jouer » sur CharacterSelect, ou succès CharacterCreate). Consommée par
		/// l'engine sur la première frame post-auth pour câbler la connexion gameplay UDP au
		/// shard cible et amorcer la scène 3D. \c applyRequested vaut \c true tant que la
		/// commande n'a pas été consommée via \ref ConsumePendingEnterWorldCommand.
		struct EnterWorldCommand
		{
			bool applyRequested = false;
			uint64_t characterId = 0;
			uint32_t shardId = 0;
			std::string shardEndpoint; ///< host:port du shard pour la connexion gameplay UDP.
			std::string characterName;
			// Phase 3.6 — Position de spawn lue depuis characters.spawn_*. Un (0, 0, 0)
			// laisse l'engine appliquer son défaut config (client.world.default_spawn).
			float spawnX        = 0.0f;
			float spawnY        = 0.0f;
			float spawnZ        = 0.0f;
			float spawnYawDeg   = 0.0f;
			float spawnPitchDeg = 0.0f;
			bool  hasSpawn      = false; ///< true = spawn renseigné depuis la liste de personnages.
		};

		struct AudioSettingsCommand
		{
			bool applyRequested = false;
			float masterVolume = 1.0f;
			float musicVolume = 1.0f;
			float sfxVolume = 1.0f;
			float uiVolume = 1.0f;
		};

		struct ControlSettingsCommand
		{
			bool applyRequested = false;
			float mouseSensitivity = 0.002f;
			bool invertY = false;
			bool useZqsd = false;
		};

		struct GameSettingsCommand
		{
			bool applyRequested = false;
			bool gameplayUdpEnabled = false;
			bool allowInsecureDev = true;
			uint32_t authTimeoutMs = 5000;
		};

		struct VisualState
		{
			bool active = false;
			bool login = false;
			bool registerMode = false;
			bool verifyEmail = false;
			bool forgotPassword = false;
			bool terms = false;
			bool characterCreate = false;
			/// Phase 2 — écran de sélection de personnage (visible si \c m_phase == Phase::CharacterSelect).
			bool characterSelect = false;
			bool languageSelection = false;
			bool languageOptions = false;
			bool submitting = false;
			bool error = false;
			/// Pas de grand panneau bleu ni cadre opaque (fond photo visible).
			bool minimalChrome = false;
			/// Colonne décorative à gauche (login).
			bool loginArtColumn = false;
			/// Logo connexion : rotation tant que la disponibilité maître est en cours de vérification.
			bool authLogoSpin = false;
			/// Au moins une sonde de statut terminée (cache disponible ; le logo coin ne s’affiche que si \ref authLogoSpin).
			bool authStatusKnown = false;
			bool authStatusOk = false;
			/// Écran Options (auth) — aligné sur la phase \c LanguageOptions pour l’overlay ImGui.
			bool options = false;
			/// Choix du royaume / shard.
			bool shardPick = false;
			/// \c true uniquement si \c Phase::EmailConfirmationPending (écran « vérifiez vos e-mails »).
			bool emailConfirmationPending = false;
		};

		struct RenderField
		{
			std::string label;
			std::string value;
			bool active = false;
			bool hovered = false;
			bool secret = false;
			/// Molette / flèches : pas de saisie clavier chiffres (ex. jour/mois/année inscription).
			bool cyclePicker = false;
			/// Clé i18n pour infobulle (résolue en \c tooltipText).
			std::string tooltipKey;
			/// Texte d’aide affiché (Tr(tooltipKey) et/ou texte fourni à la construction du champ).
			std::string tooltipText;
			/// Texte indicatif ImGui (\c InputTextWithHint) lorsque le buffer est vide (connexion, etc.).
			std::string inputPlaceholder;
			/// Colonne dans la grille d’inscription (0 = gauche, 1 = milieu, 2 = droite).
			/// -1 = pas de grille (affichage en liste simple, comportement actuel).
			int32_t gridColumn = -1;
			/// Nombre de colonnes occupées (1, 2, ou 3). Ignoré si gridColumn == -1.
			int32_t gridSpan = 1;
			/// Message d’erreur par champ (validation partielle). Vide = pas d’erreur.
			std::string fieldError;
			/// Indicateur visuel de correspondance mdp (champ confirmPassword uniquement).
			/// 0 = neutre, 1 = correspond, -1 = ne correspond pas.
			int32_t passwordMatchState = 0;
			/// Indicateur de disponibilité username (champ login uniquement).
			/// Reflète UsernameCheckState : 0=Idle, 1=Pending, 2=Available, 3=Taken.
			int32_t usernameCheckState = 0;
		};

		/// Bouton d’action : le fond est dessiné sans texte (AuthUiRenderer) ; le libellé vient des clés i18n,
		/// résolu dans \c ResolveActionButtonLabels avant affichage (AuthGlyphPass utilise \c label).
		struct RenderAction
		{
			/// Clé catalogue (ex. \c "common.submit"). Toujours renseignée pour les boutons standards.
			std::string labelKey;
			/// Si \c Tr(labelKey) est vide, essai de cette clé (ex. \c common.quit si \c common.quit_desktop manque).
			std::string labelKeyFallback;
			/// Texte résolu pour le rendu / accessibilité ; ne pas remplir à la main, utiliser les clés + résolution.
			std::string label;
			bool primary = false;
			bool active = false;
			bool hovered = false;
			/// Mise en avant (ex. Inscription / Options sur l’écran connexion).
			bool emphasized = false;
			/// Texte court affiché à droite dans le bouton (ex. raccourci, pictogramme) — écran connexion maquette.
			std::string actionBadge;
		};

		struct RenderBodyLine
		{
			std::string text;
			/// Si non vide : \p text = libellé (colonne gauche), \p valueText = valeur éditable affichée (colonne droite) — écran options.
			std::string valueText;
			bool active = false;
			bool hovered = false;
			/// Lien (ouvre une URL), pas un bouton d’action.
			bool link = false;
			bool checkbox = false;
			bool checkboxChecked = false;
		};

		struct DropdownOption
		{
			std::string label;   // Texte affiché (ex. "Janvier", "1", "2000")
			std::string value;   // Valeur interne (ex. "01", "2000")
		};

		struct RenderDropdown
		{
			std::string                  label;           // Label au-dessus (ex. "Jour")
			std::vector<DropdownOption>  options;
			int32_t                      selectedIndex = 0;
			bool                         isOpen        = false;
			int32_t                      x = 0, y = 0, w = 0, h = 0;  // bounding box (remplie par renderer)
		};

		/// Carte langue (écran premier lancement) — aligné sur la maquette Lune Noire.
		struct LanguageFirstRunCard
		{
			std::string localeTag;
			std::string nameAllCaps;
			std::string nativeLine;
			bool selected = false;
			bool hovered = false;
		};

		/// Variante d'erreur d'inscription (maquette auth_flow — pastilles + bandeau).
		struct AuthRegisterErrorVariantRow
		{
			std::string pillLabel;
			std::string bannerTitle;
			std::string bannerMessage;
			std::string fieldLabel;
			std::string fixHint;
			bool warningBanner = false;
		};

		struct RenderModel
		{
			bool visible = false;
			/// Index champ dont la zone info (inscription) est survolée, ou -1.
			int32_t hoveredFieldInfoIndex = -1;
			std::string titleLine1;
			std::string titleLine2;
			std::string sectionTitle;
			std::string infoBanner;
			std::string errorText;
			std::string footerHint;
			std::vector<RenderField> fields;
			std::vector<RenderAction> actions;
			std::vector<RenderBodyLine> bodyLines;
			int32_t visibleBodyLineStart = 0;
			int32_t visibleBodyLineCount = 0;
			/// Ajustements layout auth (lus depuis la config à l’init, recopiés ici pour BuildAuthUiLayoutMetrics).
			int32_t layoutAuthTitleLine1FromPanelTopPx = 4;
			int32_t layoutAuthGapTitleToSectionPx = 8;
			bool layoutAuthTitleCenterViewportWidth = true;
			int32_t layoutAuthFieldRowExtraPx = 0;
			/// Taille affichée du logo statut (px) ; pour placer le texte « vérification serveur » à sa droite.
			int32_t authLogoSizePx = 96;
		// Popup info (icône "i") — affiché par-dessus tout le reste quand visible.
		bool        infoPopupVisible = false;
		std::string infoPopupText;    // Texte localisé à afficher dans le popup.
		// Bounding box de l'icône "i" pour hit-testing souris.
		int32_t     infoIconX = 0, infoIconY = 0, infoIconW = 0, infoIconH = 0;
		bool        infoIconVisible = false;
		std::vector<RenderDropdown> dropdowns;
			/// Premier lancement : panneau central type « maquette » (cartes + pied).
			bool languageFirstRunLayout = false;
			std::string languagePanelSubtitle;
			std::string languageVersionLabel;
			std::vector<LanguageFirstRunCard> languageFirstRunCards;
			std::string languageFooterLeft;
			std::string languageFooterRight;
			/// Connexion (ImGui maquette) : pastille de version à droite du titre (ex. « v8.8.4 »).
			std::string authLoginVersionBadge;
			/// Sous-texte sous la bascule « se souvenir » (connexion).
			std::string authRememberDetailLine;
			/// Puces pied de panneau connexion : paire (touche / libellé court).
			std::vector<std::pair<std::string, std::string>> authLoginFooterChips;
			/// Inscription (ImGui) : pastille d’étape (ex. « 2 / 4 »), sous-titre panneau, aide courriel.
			std::string authRegisterPanelBadge;
			std::string authRegisterPanelSubtitle;
			std::string authRegisterEmailHint;
			/// Étapes du fil d’inscription (Langue, Compte, …) — texte déjà localisé.
			std::vector<std::string> authRegisterCrumbLabels;
			int authRegisterCrumbCurrent = 1;
			/// Liste pays (code ISO-2, libellé) pour combo ImGui.
			std::vector<std::pair<std::string, std::string>> authRegisterCountryPick;
			/// Puces pied panneau inscription.
			std::vector<std::pair<std::string, std::string>> authRegisterFooterChips;
			/// Libellé du lien « voir les erreurs » (inscription ImGui).
			std::string authRegisterShowErrorsLabel;
			/// Erreur : écran riche type « inscription impossible » (pastilles + détails).
			bool authErrorRichRegisterLayout = false;
			std::string authErrorPanelTitle;
			std::string authErrorPanelSubtitle;
			std::string authErrorVersionBadge;
			std::vector<AuthRegisterErrorVariantRow> authRegisterErrorVariants;
			int authRegisterErrorClassifiedIndex = 0;
			/// Si vrai, le corps du bandeau utilise \c errorText (message serveur ou validation) au lieu du texte de la variante.
			bool authErrorBannerBodyFromUserMessage = false;
			/// Masque l’encart « champ à corriger » (message global ex. champs obligatoires manquants).
			bool authErrorHideFieldBox = false;
			std::string authErrorFieldSectionLabel;
			std::string authErrorFixSectionLabel;
			std::string authErrorBackButtonLabel;
			std::string authErrorBackKeycap;
			std::string authErrorRetryButtonLabel;
			bool authErrorShowRetryButton = false;
			/// Vérification courriel (ImGui) — aligné maquette \c ConfirmEmailScreen (auth_flow).
			std::string authVerifyPanelTitle;
			std::string authVerifyPanelSubtitle;
			std::string authVerifyPanelBadge;
			std::string authVerifyInfoPopupText;
			std::string authVerifyDigitLabel;
			std::string authVerifyResendLabel;
			std::string authVerifyChangeEmailLabel;
			std::string authVerifySubmitLabel;
			std::string authVerifyBackLabel;
			std::string authVerifyBackKeycap;
			std::string authVerifySubmitKeycap;
			std::string authVerifyDevHint;
			/// \c Phase::EmailConfirmationPending — layout ImGui fusionné (bandeau OK + mêmes 6 cases).
			bool authEmailConfirmationPendingPanel = false;
			std::string authEmailConfirmationOkTitle;
			std::string authEmailConfirmationOkBody;
			/// \c true quand 15 min se sont écoulées : le bouton "Renvoyer le code" doit être affiché.
			bool authVerifyCanResend = false;
			/// Message affiché en bas du panneau lorsque le code a expiré.
			std::string authVerifyCodeExpiredMessage;
			/// AUTH-UI.6 — encart compte (overlay Options ImGui).
			std::string authOptionsAccountLogin;
			std::string authOptionsAccountTagId;
		};

		/// Etat de disponibilité (status) des services côté serveur.
		/// Sert principalement aux écrans d'authentification, puis à des fonctionnalités ultérieures.
		struct GameServerStatus
		{
			std::string name;
			bool ok = false;
			uint32_t players = 0;
		};

		struct StatusCache
		{
			bool authOk = false;
			bool masterOk = false;
			uint32_t totalPlayers = 0;
			std::vector<GameServerStatus> servers;
			std::string infoMessage;
		};

		AuthUiPresenter() = default;
		~AuthUiPresenter();

		/// Load config flags; may mark flow complete immediately when auth UI is disabled or on non-Windows stubs.
		bool Init(const engine::core::Config& cfg);

		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		/// When false, the master/shard gate has passed and the world loop should run normally.
		bool IsFlowComplete() const { return m_flowComplete; }

		/// Utilisé par \c lcdlln_world_editor.exe : pas d’écran login, accès direct à la scène 3D (Vulkan).
		void BypassAuthGateForWorldEditor();

		/// Marque le flux d'authentification comme terminé après une transition EnterWorld
		/// validée (sélection royaume + perso + connexion shard OK). Sans cet appel,
		/// `IsFlowComplete()` reste à `false` même après EnterWorld, ce qui maintient
		/// `authGateActive` à true côté Engine.cpp et empêche le chat HUD de s'afficher.
		void MarkAuthFlowCompleteAfterEnterWorld();

		/// While the auth gate is active, gameplay camera and chat should not consume input.
		bool BlocksWorldInput() const;

		/// Per-frame: text input, Tab/Enter, register toggle, async worker completion, window title.
		void Update(engine::platform::Input& input, float deltaSeconds, engine::platform::Window& window,
			const engine::core::Config& cfg);

		/// Multi-line panel for HUD / logs (full form + asset paths + errors).
		std::string BuildPanelText() const;
		RenderModel BuildRenderModel() const;
		VisualState GetVisualState() const;
		bool IsUsingNativeAuthScreen() const { return m_usingNativeAuthScreen; }

		/// Angle du logo (rad/s accumulé) pour le rendu Vulkan ; 0 si pas de rotation.
		float GetAuthLogoRotationRadians() const { return m_authLogoRotationRad; }
		int32_t GetAuthLogoSizePx() const { return m_authLogoSizePx; }
		const StatusCache& GetStatusCache() const { return m_statusCache; }
		/// DIAG — retourne l'adresse du mutex alloué sur le tas (pour VirtualQuery Windows).
		const void* GetAsyncMutexAddr() const { return m_asyncMutex.get(); }
		VideoSettingsCommand ConsumePendingVideoSettings();
		/// Phase 3 — Consomme la commande d'entrée dans le monde. Retourne le contenu courant
		/// (avec \c applyRequested=true s'il y a quelque chose à appliquer) puis remet à zéro.
		EnterWorldCommand ConsumePendingEnterWorldCommand();

		/// Phase 3.6.6 — Sauvegarde fire-and-forget de la position courante via la connexion
		/// master encore vivante (m_masterClient + m_masterSessionId conservés post-EnterWorld
		/// grâce au fix Phase 2/3 qui a supprimé les ResetMasterSession() avant MasterFlow).
		/// Retourne true si le paquet a été émis (queue Send), false si pas de session active
		/// ou si l'envoi a échoué. La réponse est consommée par \ref PumpPostAuthEvents() côté
		/// engine — ici on n'attend rien (fire-and-forget).
		bool SavePositionAsync(uint64_t characterId, float x, float y, float z, float yawDeg, float pitchDeg);

		/// Phase 3.6.6 — Drain les événements de la connexion master encore active post-auth
		/// (réponses aux SAVE_POSITION, déconnexions inattendues, …). À appeler chaque frame
		/// par l'engine quand le gate auth est inactif. No-op si pas de session active.
		void PumpPostAuthEvents();

		/// Chat MVP — Envoi fire-and-forget d'un message chat sur la connexion master active.
		/// Sérialise via \ref engine::network::BuildChatSendRequestPayload (opcode 45,
		/// requestId=0). \p channel mappe \ref engine::net::ChatChannel raw value.
		/// \p targetToken vide sauf pour le canal Whisper. Retourne false si pas de session,
		/// payload vide, ou Send rejeté.
		bool SendChatAsync(uint8_t channel, std::string_view targetToken, std::string_view text);

		/// Phase 4 chat — Annonce au master le personnage actif après EnterWorld.
		/// Master valide ownership (account_id + character_id en DB) et enregistre le binding
		/// connId → (character_id, character_name) pour le sender display dans CHAT_RELAY +
		/// la résolution de cible /whisper. Fire-and-forget (la réponse 1=ok / 0=error est
		/// loggée en debug par PumpPostAuthEvents si l'engine l'a câblée).
		bool SendEnterWorldAsync(uint64_t characterId, std::string_view characterName);

		/// Phase 5 reconnect — L'engine appelle cette méthode juste après avoir consommé
		/// EnterWorldCommand (dans Engine.cpp), pour mémoriser le personnage actif. Cette
		/// info est nécessaire pour ré-envoyer un CHARACTER_ENTER_WORLD_REQUEST à la
		/// reconnexion master sans repasser par tout le flow Login/CharacterSelect.
		void RememberPostEnterWorldCharacter(uint64_t characterId, std::string characterName);

		/// Phase 5 reconnect — Tente une reconnexion master si requise et le délai dépassé.
		/// À appeler chaque frame post-auth depuis l'engine. No-op si pas en mode reconnect.
		/// Crée son propre worker thread (réutilise les credentials \c m_login/\c m_password
		/// stockés depuis le login initial) puis re-AUTH + re-EnterWorld.
		void TickReconnect(const engine::core::Config& cfg);

		/// Phase 5 reconnect — true tant qu'une tentative de reconnexion est en cours
		/// (utilisé par l'engine pour afficher une bannière à l'écran).
		bool IsReconnecting() const { return m_reconnectInProgress; }

		/// Phase 5 reconnect — texte localisé à afficher pendant la tentative
		/// (« Connexion perdue, reconnexion… » / « Reconnexion impossible… »).
		const std::string& ReconnectStatusText() const { return m_reconnectStatusText; }

		/// Chat MVP — Callback installée par l'engine pour recevoir les paquets push
		/// (request_id=0) sur la connexion master post-auth. Appelée depuis
		/// \ref PumpPostAuthEvents pour chaque \c PacketReceived. Le callback parse l'opcode
		/// et dispatche vers le presenter approprié (ex. CHAT_RELAY → ChatUiPresenter).
		using MasterPushHandler = std::function<void(uint16_t opcode, const uint8_t* payload, size_t payloadSize)>;
		void SetMasterPushHandler(MasterPushHandler handler) { m_masterPushHandler = std::move(handler); }

		AudioSettingsCommand ConsumePendingAudioSettings();
		ControlSettingsCommand ConsumePendingControlSettings();
		GameSettingsCommand ConsumePendingGameSettings();

		/// Escape: back from register to login, or clear error; returns true if consumed.
		bool OnEscape();

		bool SetViewportSize(uint32_t width, uint32_t height);

		/// Pont \c AuthImGuiRenderer (voir \c render.auth_ui.imgui.enabled) — évite de dupliquer la logique de phase.
		void ImGuiApplyFirstRunLanguageContinue(const engine::core::Config& cfg, std::string_view localeTag);
		/// Sélection d'une carte langue (ImGui) sans valider — met à jour \c m_languageSelectionIndex pour le sous-titre.
		void ImGuiSelectFirstRunLanguageCard(uint32_t cardIndex);
		void ImGuiSubmitLogin(const engine::core::Config& cfg, const char* loginUtf8, const char* passwordUtf8, bool rememberMe);
		void ImGuiNavigateToRegisterFromLogin();
		void ImGuiBackFromRegisterToLogin();
		void ImGuiOpenForgotPasswordPortal(const engine::core::Config& cfg, engine::platform::Window& window);
		void ImGuiOpenLanguageOptionsMenu();
		/// AUTH-UI.6 — actions menu Compte (overlay Options).
		enum class OptionsAccountMenuAction : uint8_t
		{
			ChangePassword = 0,
			ChangeEmail = 1,
			SignOut = 2,
		};
		void ImGuiOptionsAccountMenuAction(OptionsAccountMenuAction action);
		void ImGuiRequestClose(engine::platform::Window& window);

		/// Miroir des réglages \c *Pending pour l’overlay options ImGui (même sémantique que le menu classique).
		struct LanguageOptionsImGuiMirror
		{
			bool videoFullscreen{};
			bool videoVsync{};
			int32_t videoResWidth = 1920;
			int32_t videoResHeight = 1080;
			int32_t videoQualityPreset = 2;
			float videoFovDegrees = 70.f;
			float audioMaster01 = 1.f;
			float audioMusic01 = 1.f;
			float audioSfx01 = 1.f;
			float audioUi01 = 1.f;
			float mouseSensitivity = 0.002f;
			bool invertY{};
			bool useZqsd{};
			bool gameplayUdpEnabled{};
			bool allowInsecureDev = true;
			uint32_t authTimeoutMs = 5000u;
			uint32_t languageSelectionIndex{};
			/// AUTH-UI.6 — interface / réseau (session locale jusqu’à Appliquer).
			float uiScalePercent = 100.f;
			float panelOpacityPercent = 70.f;
			bool showTooltipUi = true;
			/// 0 = Europe (Morneplaine), 1 = Europe (Korvath), 2 = Automatique.
			uint32_t preferredServerIndex = 2u;
		};

		LanguageOptionsImGuiMirror BuildLanguageOptionsImGuiMirror() const;
		void ImGuiApplyLanguageOptionsMenu(const engine::core::Config& cfg, const LanguageOptionsImGuiMirror& mirror);
		void ImGuiCloseLanguageOptionsWithoutApply();

		struct RegisterImGuiSubmit
		{
			const char* login = nullptr;
			const char* email = nullptr;
			const char* password = nullptr;
			const char* passwordConfirm = nullptr;
			const char* firstName = nullptr;
			const char* lastName = nullptr;
			const char* birthDay = nullptr;
			const char* birthMonth = nullptr;
			const char* birthYear = nullptr;
			const char* countryIso2 = nullptr;
		};
		void ImGuiSubmitRegister(const engine::core::Config& cfg, const RegisterImGuiSubmit& form);
		/// AUTH-UI.3 — maquette : ouvre l’écran d’erreur riche (retour Phase::Register) pour prévisualiser les messages de validation.
		void ImGuiRegisterPreviewValidationErrors(const engine::core::Config& cfg);

		struct RegisterFieldsMirrorForImGui
		{
			std::string login;
			std::string email;
			std::string firstName;
			std::string lastName;
			std::string countryIso2;
			int32_t birthDayIndex{};
			int32_t birthMonthIndex{};
			int32_t birthYearIndex{};
		};
		RegisterFieldsMirrorForImGui BuildRegisterFieldsMirrorForImGui() const;

		void ImGuiNavigateToForgotFromLogin();
		void ImGuiSubmitForgotPassword(const engine::core::Config& cfg, const char* emailUtf8);
		void ImGuiBackFromForgotToLogin();

		void ImGuiSubmitVerifyEmailCode(const engine::core::Config& cfg, const char* codeSixUtf8);
		/// Demande l'envoi d'un nouveau code de vérification (après expiration des 15 min).
		void ImGuiResendVerificationEmail(const engine::core::Config& cfg);
		void ImGuiBackFromVerifyToLogin();
		/// Efface les 6 chiffres saisis (lien « renvoyer » côté maquette ; pas d’appel serveur dédié pour l’instant).
		void ImGuiVerifyEmailClearDigits();
		/// Retour au formulaire d’inscription sur le champ courriel.
		void ImGuiVerifyEmailBackToEditRegisterEmail();
		/// Saisie ImGui : met à jour le code partiel (affichage / validation locale).
		void ImGuiSetVerifyEmailPartialCode(std::string_view codeDigitsInOrder);
		void ImGuiEmailConfirmationBackToLogin();

		void ImGuiAcknowledgeErrorScreen(const engine::core::Config& cfg);

		uint32_t ShardPickChoiceShardId() const { return m_shardPickChoiceShardId; }
		void ImGuiSetShardPickChoiceShardId(uint32_t shardId);
		const std::vector<engine::network::ServerListEntry>& ShardPickEntries() const { return m_shardPickEntries; }

		/// Phase 2 — Accessor pour le renderer ImGui de l'écran CharacterSelect.
		const std::vector<engine::network::CharacterListEntry>& CharacterListEntries() const { return m_characterList; }
		/// Phase 2 — Index du personnage sélectionné (-1 = aucun).
		int CharacterSelectIndex() const { return m_selectedCharacterIndex; }
		void ImGuiSubmitShardPick(const engine::core::Config& cfg);
		void ImGuiBackFromShardPickToLogin();

		void ImGuiNotifyTermsScrollReachedBottom(bool reached);
		void ImGuiSetTermsAcknowledgeChecked(bool on);
		void ImGuiTermsPrimaryClick(const engine::core::Config& cfg);
		void ImGuiTermsDecline(engine::platform::Window& window);

		void ImGuiSubmitCharacterCreate(const engine::core::Config& cfg, const char* nameUtf8);
		void ImGuiCancelCharacterCreateReturnToLogin();
		/// Phase 2 — sélectionne le i-ème personnage de \ref m_characterList (mise en surbrillance, pas d'entrée dans le monde).
		void ImGuiSelectCharacterEntry(int index);
		/// Phase 2 — l'utilisateur valide le personnage actuellement sélectionné et entre dans le monde.
		/// Aujourd'hui (Phase 2 isolée), positionne simplement \ref m_flowComplete ; la Phase 3 branchera la transition gameplay.
		void ImGuiActivateSelectedCharacter();
		/// Phase 2 — bouton "Créer un nouveau personnage" depuis l'écran CharacterSelect : route vers CharacterCreate.
		void ImGuiCreateAnotherCharacterFromSelect();
		/// Phase 2 — bouton "Retour" depuis CharacterSelect : revient à Login (réinitialise l'état du shard pick).
		void ImGuiCancelCharacterSelectReturnToLogin();

		/// Phase 3.9 — Demande la suppression du i-eme personnage. Passe par un etat de
		/// confirmation (\ref m_pendingDeleteCharacterIndex >= 0). Un second clic depuis
		/// la confirmation declenche StartCharacterDeleteWorker.
		void ImGuiRequestDeleteCharacter(int index, const engine::core::Config& cfg);
		/// Phase 3.9 — Annule la confirmation de suppression en cours.
		void ImGuiCancelDeleteCharacterConfirm();
		/// Phase 3.9 — Indique que l'utilisateur est en train de confirmer la suppression
		/// du personnage d'index donne (utilise par le renderer pour afficher un dialogue).
		int  PendingDeleteCharacterIndex() const { return m_pendingDeleteCharacterIndex; }

		const std::string& TermsFullTextForImGui() const { return m_termsContent; }
		const std::vector<std::string>& GetAvailableLocales() const { return m_localization.GetAvailableLocales(); }
		/// Index carte sélectionnée sur \c Phase::LanguageSelectionFirstRun (clavier / modèle).
		uint32_t FirstRunLanguageSelectionIndex() const { return m_languageSelectionIndex; }

		/// Libellés ImGui auth : même résolution que \ref Tr (clés \c options.imgui.*, etc.).
		std::string UiTranslate(std::string_view key, const LocalizationService::Params& params = {}) const
		{
			return Tr(key, params);
		}

	private:
		enum class Phase
		{
			Login,
			Register,
			VerifyEmail,
			EmailConfirmationPending,   // page intermédiaire post-inscription : "Vérifiez vos emails"
			ForgotPassword,
			Terms,
			CharacterCreate,
			CharacterSelect,            // Phase 2 — écran "Choisir un personnage" (>=1 perso sur le shard)
			ShardPick,
			LanguageSelectionFirstRun,
			LanguageOptions,
			Submitting,
			Error
		};

		static const char* PhaseLogName(Phase p);

		void AppendPasswordStars(std::string& out, size_t len) const;
		void EnsurePasswordSalt(const engine::core::Config& cfg);
		std::string ComputeClientHash(const engine::core::Config& cfg) const;
		void StartRegisterWorker(const engine::core::Config& cfg);
		void StartLoginWorker(const engine::core::Config& cfg);
		void StartVerifyEmailWorker(const engine::core::Config& cfg);
		/// Lance le worker de renvoi de code de vérification (opcode 37).
		void StartResendVerificationWorker(const engine::core::Config& cfg);
		void StartForgotPasswordWorker(const engine::core::Config& cfg);
		void StartUsernameCheckWorker(const engine::core::Config& cfg);
		void StartTermsStatusWorker(const engine::core::Config& cfg);
		void StartTermsAcceptWorker(const engine::core::Config& cfg);
		void StartCharacterCreateWorker(const engine::core::Config& cfg);
		/// Phase 3.9 — Lance le worker reseau pour supprimer le personnage selectionne (id stocke
		/// dans \ref m_pendingDeleteCharacterId par \ref ImGuiRequestDeleteCharacter).
		void StartCharacterDeleteWorker(const engine::core::Config& cfg);
		void StartStatusProbeWorker(const engine::core::Config& cfg);
		void ResetMasterSession();
		void StartMasterFlowWorker(const engine::core::Config& cfg);
		void PollAsyncResult(const engine::core::Config& cfg);
		void LoadRememberPreference();
		void SaveRememberPreference();
		void ApplyLocaleSelection(bool firstRun);
		/// Persistance ciblée de \c client.locale dans user_settings.json (utilisé par \c ApplyLocaleSelection au premier lancement).
		bool PatchPersistedLocaleKey(std::string_view locale);
		void OpenLanguageOptions();
		static uint32_t OptionsSubmenuLineCount(OptionsSubMenu sub);
		void EnterOptionsSubmenuFromRoot(uint32_t categoryIndex);
		std::string Tr(std::string_view key, const LocalizationService::Params& params = {}) const;
		std::string CurrentLocale() const;
		std::string LocalizedLanguageName(std::string_view localeTag) const;
		bool HandleNativeAuthScreen(engine::platform::Window& window, const engine::core::Config& cfg);
		void SubmitCurrentPhase(const engine::core::Config& cfg);
		void CommitLanguageOptionsMenuApply(const engine::core::Config& cfg);
		void UpdateWindowTitle(engine::platform::Window& window) const;
		void JoinWorker();
		/// Remplit \c RenderAction::label à partir de \c labelKey / \c labelKeyFallback (locale courante).
		void ResolveActionButtonLabels(RenderModel& model) const;
		/// AUTH-UI.2 — remplit \p model pour \c Phase::Login (appelé depuis \c BuildRenderModel).
		void BuildModel_Login(RenderModel& model) const;
		/// AUTH-UI.2 — raccourcis Ctrl+R / Ctrl+F / Ctrl+O sur l'écran connexion.
		void Update_LoginShortcuts(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.3 — remplit \p model pour \c Phase::Register (appelé depuis \c BuildRenderModel).
		void BuildModel_Register(RenderModel& model) const;
		/// AUTH-UI.3 — Tab / flèches sur la grille inscription (hors ImGui).
		void Update_Register(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.5 — remplit \p model pour \c Phase::VerifyEmail.
		void BuildModel_VerifyEmail(RenderModel& model) const;
		/// AUTH-UI.5 — remplit \p model pour \c Phase::EmailConfirmationPending.
		void BuildModel_EmailConfirmationPending(RenderModel& model) const;
		/// AUTH-UI.5 — Tab / saisie code hors ImGui (raccourcis communs avec confirmation courriel).
		void Update_VerifyEmail(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.6 — menu options classique (clavier / molette) et modèle rendu options.
		void Update_Options(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.4 — remplit \p model pour \c Phase::Error (appelé depuis \c BuildRenderModel).
		void BuildModel_Error(RenderModel& model) const;
		/// AUTH-UI.6 — \c Phase::LanguageOptions (menu classique + données compte pour ImGui).
		void BuildModel_Options(RenderModel& model) const;
		/// AUTH-UI.7 — \c Phase::LanguageSelectionFirstRun (premier lancement).
		void BuildModel_LanguageSelect(RenderModel& model) const;
		void Update_LanguageSelect(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.8 — \c Phase::ShardPick (liste classique + navigation clavier ImGui).
		void BuildModel_ShardPick(RenderModel& model) const;
		void Update_ShardPick(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.9 — \c Phase::ForgotPassword (split uniquement, pas de maquette).
		void BuildModel_ForgotPassword(RenderModel& model) const;
		void Update_ForgotPassword(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.10 — \c Phase::Terms (split uniquement).
		void BuildModel_Terms(RenderModel& model) const;
		void Update_Terms(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// AUTH-UI.11 — \c Phase::CharacterCreate (split uniquement).
		void BuildModel_CharacterCreate(RenderModel& model) const;
		void Update_CharacterCreate(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// Phase 2 — \c Phase::CharacterSelect : liste les personnages reçus du master après TICKET_ACCEPTED.
		void BuildModel_CharacterSelect(RenderModel& model) const;
		void Update_CharacterSelect(engine::platform::Input& input, const engine::core::Config& cfg, engine::platform::Window& window,
			bool usingNativeAuth, bool authUiImguiMode);
		/// Transition de phase avec reset des états hover et du texte d'erreur utilisateur.
		/// Ne touche PAS m_infoBanner sauf pour Phase::Error (évite la superposition infoBanner+errorText).
		void SetPhase(Phase p)
		{
			m_phase = p;
			m_hoveredFieldIndex = -1;
			m_hoveredFieldInfoIndex = -1;
			m_hoveredBodyLineIndex = -1;
			m_hoveredActionIndex = -1;
			m_hoveredLanguageCardIndex = -1;
			m_authPrevMouseLeftDown = false;
			m_openDropdownIndex = -1;
			m_userErrorText.clear();
			m_infoPopupVisible = false;
			if (p == Phase::Error)
			{
				m_infoBanner.clear();
			}
		}

		/// Passe en Phase::Error en mémorisant l'écran de retour (ImGui « retour au formulaire »).
		void EnterAuthErrorPhase(Phase returnTo, std::string userMessage)
		{
			m_errorReturnPhase = returnTo;
			SetPhase(Phase::Error);
			m_userErrorText = std::move(userMessage);
		}

		bool m_initialized = false;
		bool m_flowComplete = false;
		bool m_authEnabled = true;
		bool m_usingNativeAuthScreen = false;
		Phase m_phase = Phase::Login;
		/// Cible de \c SubmitCurrentPhase / ImGuiAcknowledgeErrorScreen lorsque \c m_phase == Phase::Error.
		Phase m_errorReturnPhase = Phase::Login;

		std::string m_login;
		std::string m_password;
		/// Saisie inscription uniquement : doit correspondre à \ref m_password avant envoi.
		std::string m_passwordConfirm;
		std::string m_email;
		std::string m_firstName;
		std::string m_lastName;
		std::string m_birthDay;
		std::string m_birthMonth;
		std::string m_birthYear;
		std::string m_country;        ///< Code pays ISO-2 (ex. "FR"). Champ inscription.
		int32_t m_birthDayIndex    = 0;   ///< Index dans options 1-31
		int32_t m_birthMonthIndex  = 0;   ///< Index dans 1-12
		int32_t m_birthYearIndex   = 80;  ///< Index dans 1900-2010, défaut=1980 (index 80)
		int32_t m_openDropdownIndex = -1; ///< -1=aucun, 0=jour, 1=mois, 2=année
		bool m_passwordsMatch = false; ///< Suivi temps-réel correspondance mdp / confirm.
		// --- Plan C: username availability debounce ---
		UsernameCheckState m_usernameCheckState = UsernameCheckState::Idle;
		uint32_t  m_usernameCheckSeq     = 0;    ///< Numéro de séquence ; réponses avec seq différent sont ignorées.
		double    m_usernameDebounceTimer = 0.0;  ///< Secondes restantes avant envoi. ≤0 = inactif.
		std::string m_usernameLastChecked;         ///< Login envoyé au serveur pour le seq courant.
		std::string m_verifyCode;
		std::string m_termsTitle;
		std::string m_termsVersionLabel;
		std::string m_termsLocale;
		std::string m_termsContent;
		std::string m_characterName;
		uint32_t m_activeField = 0;
		int32_t m_hoveredFieldIndex = -1;
		int32_t m_hoveredFieldInfoIndex = -1;
		int32_t m_hoveredBodyLineIndex = -1;
		int32_t m_hoveredActionIndex = -1;
		/// Phase \c LanguageSelectionFirstRun : carte sous le curseur, ou -1.
		int32_t m_hoveredLanguageCardIndex = -1;
		/// Front descendant du clic gauche (WasMousePressed ou première frame « bouton enfoncé »).
		bool m_authPrevMouseLeftDown = false;
		uint32_t m_termsScrollOffset = 0;
		uint32_t m_termsTotalLength = 0;
		std::string m_userErrorText;
		std::string m_infoBanner;
		uint64_t m_pendingVerifyAccountId = 0;
		/// Instant auquel le dernier code de vérification a été envoyé (steady_clock).
		/// Vaut time_point{} si aucun code n'a encore été envoyé dans cette session.
		std::chrono::steady_clock::time_point m_verifyCodeSentAt{};
		uint64_t m_pendingTermsEditionId = 0;
		bool m_termsScrolledToBottom = false;
		bool m_termsAcknowledgeChecked = false;
		bool m_rememberLogin = false;
		std::vector<engine::network::ServerListEntry> m_shardPickEntries;
		uint32_t m_shardPickChoiceShardId = 0;
		uint32_t m_shardFlowOverrideId = 0;
		/// Royaume choisi par l'utilisateur sur l'écran ShardPick. Persiste à travers Phase::CharacterCreate
		/// puis sert d'override pour StartMasterFlowWorker une fois le personnage créé.
		uint32_t m_chosenShardId = 0;
		/// Vrai entre l'inscription réussie et la création du personnage : ShardPick → CharacterCreate
		/// (un nouveau compte n'a pas encore de personnage). Faux pour un retour utilisateur classique :
		/// ShardPick → connexion directe au royaume.
		bool m_postRegistrationCharacterCreatePending = false;
		/// Phase 2 — Liste des personnages reçue après TICKET_ACCEPTED via CHARACTER_LIST_REQUEST.
		/// Vide = aucun personnage sur ce shard → routage vers \c Phase::CharacterCreate.
		/// Au moins un = routage vers \c Phase::CharacterSelect.
		std::vector<engine::network::CharacterListEntry> m_characterList;
		/// Phase 2 — Index du personnage actuellement sélectionné dans \ref m_characterList (-1 = rien).
		int m_selectedCharacterIndex = -1;
		/// Phase 3.9 — Index du perso pour lequel l'utilisateur est en train de
		/// confirmer la suppression. -1 = pas de confirmation en cours.
		int m_pendingDeleteCharacterIndex = -1;
		/// Phase 3.9 — character_id en cours de suppression (snapshoté au moment ou
		/// le worker est lance, indépendant de l'index qui peut bouger pendant l'attente).
		uint64_t m_pendingDeleteCharacterId = 0;
		bool        m_infoPopupVisible = false;
		std::string m_infoPopupText;
		bool m_savedRememberLogin = false;
		bool m_hasPersistedLocale = false;
		uint32_t m_languageSelectionIndex = 0;
		OptionsSubMenu m_optionsSubMenu = OptionsSubMenu::Root;
		uint32_t m_optionsRootSelection = 0;
		uint32_t m_optionsSubSelection = 0;
		Phase m_phaseBeforeOptions = Phase::Login;
		std::string m_selectedLocale;
		std::string m_persistedLocale;
		bool m_videoFullscreen = false;
		bool m_videoVsync = true;
		bool m_videoFullscreenPending = false;
		bool m_videoVsyncPending = true;
		int32_t m_videoResWidth = 1920;
		int32_t m_videoResHeight = 1080;
		int32_t m_videoResWidthPending = 1920;
		int32_t m_videoResHeightPending = 1080;
		int32_t m_videoQualityPreset = 2;
		int32_t m_videoQualityPresetPending = 2;
		float m_videoFovDegrees = 70.f;
		float m_videoFovDegreesPending = 70.f;
		VideoSettingsCommand m_pendingVideoSettings{};
		/// Phase 3 — Pending command for entering the world. Set by ImGuiActivateSelectedCharacter
		/// or by CharacterCreate success path; consumed by Engine::Update on first post-auth frame.
		EnterWorldCommand m_pendingEnterWorld{};
		/// Phase 3 — Endpoint host:port du shard accepté (persisté à travers CharacterSelect/Create
		/// pour pouvoir composer la EnterWorldCommand au moment du clic « Jouer »).
		std::string m_chosenShardEndpoint;
		float m_audioMasterVolume = 1.0f;
		float m_audioMusicVolume = 1.0f;
		float m_audioSfxVolume = 1.0f;
		float m_audioUiVolume = 1.0f;
		float m_audioMasterVolumePending = 1.0f;
		float m_audioMusicVolumePending = 1.0f;
		float m_audioSfxVolumePending = 1.0f;
		float m_audioUiVolumePending = 1.0f;
		AudioSettingsCommand m_pendingAudioSettings{};
		float m_mouseSensitivity = 0.002f;
		float m_mouseSensitivityPending = 0.002f;
		bool m_invertY = false;
		bool m_invertYPending = false;
		bool m_useZqsd = false;
		bool m_useZqsdPending = false;
		ControlSettingsCommand m_pendingControlSettings{};
		bool m_gameplayUdpEnabled = false;
		bool m_gameplayUdpEnabledPending = false;
		bool m_allowInsecureDev = true;
		bool m_allowInsecureDevPending = true;
		uint32_t m_authTimeoutMs = 5000u;
		uint32_t m_authTimeoutMsPending = 5000u;
		float m_uiScalePercent = 100.f;
		float m_uiScalePercentPending = 100.f;
		float m_panelOpacityPercent = 70.f;
		float m_panelOpacityPercentPending = 70.f;
		bool m_showTooltipUi = true;
		bool m_showTooltipUiPending = true;
		uint32_t m_preferredServerIndex = 2u;
		uint32_t m_preferredServerIndexPending = 2u;
		GameSettingsCommand m_pendingGameSettings{};
		LocalizationService m_localization;

		std::vector<uint8_t> m_argonSalt{};
		uint32_t m_viewportW = 0;
		uint32_t m_viewportH = 0;

		bool m_authMinimalChrome = true;
		bool m_authLoginArtColumn = false;
		int32_t m_layoutAuthTitleLine1FromPanelTopPx = 4;
		int32_t m_layoutAuthGapTitleToSectionPx = 8;
		bool m_layoutAuthTitleCenterViewportWidth = true;
		int32_t m_layoutAuthFieldRowExtraPx = 0;
		std::string m_masterAvailabilityUrl{};
		int32_t m_authLogoSizePx = 96;
		float m_authLogoRotationRad = 0.f;
		bool m_authAvailabilityChecking = false;
		float m_authAvailabilityPollTimer = 0.f;
		// Vérifie la disponibilité des services (et éventuellement le nombre de joueurs)
		// toutes les 2 minutes tant que l'utilisateur n'est pas authentifié.
		bool m_statusProbeInitialized = false;
		/// Au moins une sonde de statut a terminé (bannière maintenance / logo résultat).
		bool m_statusProbeCompletedOnce = false;
		/// Dernière sonde : réponse HTTP analysée avec succès (JSON). Si faux et \a authOk faux → échec réseau / HTTP, pas « maintenance » serveur.
		bool m_lastStatusProbeHttpSuccess = false;
		float m_statusPollTimer = 0.f;

		StatusCache m_statusCache{};

		/// Background worker for REGISTER_REQUEST or MasterShardClientFlow (TCP); join before destroy.
		struct AsyncResult
		{
			bool ready = false;
			bool success = false;
			uint64_t accountId = 0;
			uint64_t sessionId = 0;
			uint64_t termsEditionId = 0;
			uint32_t termsPendingCount = 0;
			uint32_t totalLength = 0;
			std::string termsTitle;
			std::string termsVersionLabel;
			std::string termsLocale;
			std::string termsContent;
			std::string message;
			std::string tagId; ///< TAG-ID reçu après inscription réussie (AsyncKind::Register). Vide si absent ou erreur.

			// Pour AsyncKind::StatusProbe.
			StatusCache statusCache{};

			// Pour AsyncKind::UsernameCheck (Plan C).
			uint8_t  usernameAvailable = 0; ///< 1 = disponible, 0 = pris.
			uint32_t usernameCheckSeq  = 0; ///< Seq renvoyé par le serveur.

			/// AsyncKind::Login : plusieurs shards en ligne, choix utilisateur requis.
			bool shardChoiceRequired = false;
			std::vector<engine::network::ServerListEntry> serverListForPick;
			/// Phase 2 — Personnages reçus via CHARACTER_LIST après TICKET_ACCEPTED. Vide si aucun
			/// (route vers CharacterCreate) ou si la requête optionnelle a échoué (même routage).
			std::vector<engine::network::CharacterListEntry> characterList;
			/// Phase 3 — Endpoint du shard accepté (host:port) pour câbler la gameplay UDP.
			std::string shardEndpoint;
			/// Phase 3 — shard_id du shard accepté (déjà connu côté presenter mais utile pour
			/// remonter explicitement avec l'endpoint).
			uint32_t shardId = 0;
		};
		AsyncResult m_asyncResult{};
		std::thread m_worker{};
		enum class AsyncKind
		{
			None,
			Register,
			AuthOnly,
			VerifyEmail,
			ResendVerification, ///< M33.2-bis : renvoi d'un nouveau code de vérification.
			ForgotPassword,
			TermsStatus,
			TermsAccept,
			CharacterCreate,
			CharacterDelete,    ///< Phase 3.9 : suppression logique d'un personnage.
			Login,
			StatusProbe,
			UsernameCheck  ///< Plan C : vérification disponibilité username (debounce).
		};
		AsyncKind m_pendingAsyncKind = AsyncKind::None;
		uint64_t m_masterSessionId = 0;
		std::unique_ptr<engine::network::NetClient> m_masterClient;
		// Heap-allocated in Init() — StableMutex avoids SRWLOCK crash (STAB.14).
		std::unique_ptr<AuthMutex> m_asyncMutex;
		std::string m_registeredTagId; ///< TAG-ID reçu après inscription réussie. Conservé pour affichage (bandeau) et usage futur (copie presse-papier, champ dédié).
		/// Chat MVP — callback installée par l'engine pour dispatcher les paquets push
		/// (request_id=0) reçus sur la connexion master post-auth (CHAT_RELAY notamment).
		MasterPushHandler m_masterPushHandler;

		// -----------------------------------------------------------------------------
		// Phase 5 reconnect — état de tentative de reconnexion automatique du master.
		// -----------------------------------------------------------------------------
		/// Personnage actif (mémorisé par l'engine via RememberPostEnterWorldCharacter)
		/// pour pouvoir ré-envoyer ENTER_WORLD à la reconnexion.
		uint64_t    m_postEnterWorldCharacterId = 0;
		std::string m_postEnterWorldCharacterName;
		/// True dès qu'on détecte une déconnexion master post-EnterWorld et qu'on n'a
		/// pas encore donné suite (succès / abandon).
		bool m_reconnectInProgress = false;
		/// Numéro d'essai courant (1-based). Plafond : \ref m_reconnectMaxAttempts.
		uint32_t m_reconnectAttempt = 0;
		uint32_t m_reconnectMaxAttempts = 1; ///< MVP : une seule tentative.
		std::chrono::steady_clock::time_point m_reconnectNextAt{};
		/// Texte affiché par l'engine pendant la phase de reconnexion (FR par défaut).
		std::string m_reconnectStatusText;
		/// True quand le worker reconnect est terminé et que le main thread doit consommer
		/// le résultat (succès ou abandon).
		std::atomic<bool> m_reconnectAsyncDone{ false };
		bool m_reconnectAsyncSuccess = false;
	};

}

