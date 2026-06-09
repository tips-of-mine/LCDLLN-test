#pragma once

#include <cfloat>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/client/auth/AuthUi.h"
#include "src/client/character_creation/CharacterCustomization.h"

namespace engine::core
{
	class Config;
}
namespace engine::platform
{
	class Window;
}
namespace engine::render::race
{
	/// Sous-projet C MVP (Task 12) — declare forward pour eviter d'inclure
	/// le header complet (qui tire vulkan_core.h) ici.
	class RacePreviewViewport;
}

namespace engine::render
{
	/// Rendu auth via Dear ImGui (Windows uniquement : \c imgui_lcdlln lié sous WIN32).
	/// Les données métier restent portées par \c AuthUiPresenter ; cette classe gère l’affichage
	/// et un état transient (buffers de saisie). La synchronisation complète presenter ↔ ImGui
	/// pourra être branchée progressivement.
	class AuthImGuiRenderer final
	{
	public:
		void Render(const engine::client::AuthUiPresenter::VisualState& vs,
			const engine::client::AuthUiPresenter::RenderModel& rm,
			float viewportW,
			float viewportH);

		void Reset();

		/// Pointeurs non possédés : définis une fois depuis \c Engine après init ImGui.
		void BindAuthUiBridge(engine::client::AuthUiPresenter* presenter, const engine::core::Config* cfg,
			engine::platform::Window* window);

		/// Sous-projet C MVP (Task 12) — Branche le viewport offscreen
		/// detenu par \c Engine pour l'apercu race dans l'ecran de
		/// creation de personnage (\c AuthImGuiCharacterCreate). Pointer
		/// non possede ; lifetime = Engine. Doit etre appele apres
		/// \c RacePreviewViewport::Init et avant la 1ere frame qui rend
		/// l'ecran de creation de perso.
		void SetRacePreview(engine::render::race::RacePreviewViewport* preview) { m_racePreview = preview; }

	private:
		using VisualState = engine::client::AuthUiPresenter::VisualState;
		using RenderModel = engine::client::AuthUiPresenter::RenderModel;

		int m_selectedLang = 0;
		char m_loginId[128]{};
		char m_loginPw[128]{};
		bool m_rememberMe = true;
		char m_regId[128]{};
		char m_regEmail[128]{};
		char m_regFirstName[128]{};
		char m_regLastName[128]{};
		char m_regCountry[8]{};
		char m_regPw[128]{};
		char m_regPw2[128]{};
		char m_verifyCode[7]{};
		int m_regBirthDayIdx = 0;
		int m_regBirthMonthIdx = 0;
		int m_regBirthYearIdx = 20;
		int m_regCountryComboIdx = 0;
		int m_optionsTab = 0;
		uint32_t m_lastSyncedPhaseToken = 0xffffffffu;

		char m_forgotEmail[256]{};
		char m_charName[96]{};
		/// Index dans la liste des races jouables (cf. RenderCharCreateScreen).
		/// 0 = humains, 1 = elfes, 2 = orcs, 3 = nains, 4 = demons, 5 = chevaliers_dragons.
		int m_charRaceIdx = 0;
		/// Systeme de personnages PR2 — index dans la liste des factions
		/// *selectionnables* du presenter (combo "FACTION"). La race est deduite
		/// de la faction (GetRaceIdForFaction) ; m_charRaceIdx est resynchronise
		/// sur la race correspondante pour conserver l'apercu 3D.
		int m_charFactionIdx = 0;
		/// Systeme de personnages PR2 — index de la classe choisie dans les classes
		/// de la faction selectionnee (GetFactionClasses). Remis a 0 a chaque
		/// changement de faction.
		int m_charClassIdx = 0;

		/// Genre choisi dans l'ecran de creation : 0 = Homme (male), 1 = Femme
		/// (female). Pilote l'apercu 3D (mesh genre) et est transmis a la creation.
		int m_charGender = 0;
		/// Teinte de peau choisie : 0 = Claire, 1 = Foncée. Pilote l'apercu 3D
		/// (materiau peau clair/fonce) ; transmis a la creation (skinColorIdx).
		int m_charSkinTone = 0;
		/// Memorise (raceIdx, gender, teinte) du dernier etat pousse a l'apercu pour
		/// ne le re-pousser que sur changement. -1 = jamais pousse (force le 1er envoi).
		int m_racePreviewSentRaceIdx = -1;
		int m_racePreviewSentGender  = -1;
		int m_racePreviewSentSkinTone = -1;

		/// Métriques de proportions éditées par le panneau « Apparence physique »
		/// (CHAR-MODEL.25). Bornées aux limites de la race sélectionnée. Réinitialisées
		/// aux valeurs par défaut de la race quand \c m_charMetricsRaceIdx diffère de
		/// \c m_charRaceIdx.
		engine::client::CharacterBodyMetrics m_charBodyMetrics{};
		int m_charMetricsRaceIdx = -1; ///< Race pour laquelle m_charBodyMetrics a été initialisé.

		bool m_optDirty = false;
		bool m_optFullscreen = true;
		bool m_optVsync = true;
		float m_optAudioMaster = 1.f;
		float m_optAudioMusic = 1.f;
		float m_optAudioSfx = 1.f;
		float m_optAudioUi = 1.f;
		float m_optMouseSens = 0.002f;
		bool m_optInvertY = false;
		bool m_optUseZqsd = false;
		bool m_optGameplayUdp = false;
		bool m_optAllowInsecureDev = true;
		uint32_t m_optAuthTimeoutMs = 5000u;
		int m_optLangIndex = 0;
		int m_optResIdx = 2;
		int m_optQualityPreset = 2;
		float m_optFovDegrees = 70.f;
		float m_optUiScalePct = 100.f;
		float m_optPanelOpacityPct = 70.f;
		bool m_optShowTooltipsUi = true;
		int m_optPreferredServer = 2;

		/// Réglages visuels locaux (maquette « Tweaks ») sur l’écran premier lancement.
		/// `m_langTweakAnimBg` pilote (à terme) l’animation décorative du fond auth — voir
		/// CODEBASE_MAP.md §13 « Tweaks d’auth ». Tant que l’animation n’existe pas, ce flag n’a
		/// qu’un effet visuel sur l’état des boutons ACTIVE / DESACTIVE.
		int m_langTweakRace = 0;
		bool m_langTweakAnimBg = true;
		bool m_authTweakPanelMinimized = false;

		/// Badge éphémère « Langue : Français » affiché au-dessus du panneau de connexion, juste
		/// après une transition LanguageSelectionFirstRun → Login. Le texte est figé au moment de
		/// la transition (lecture de `rm.infoBanner` ce frame-là) puis fade out automatiquement
		/// au bout d’environ `kLoginLangBadgeDurationSec` secondes.
		std::string m_loginLangBadgeText;
		double m_loginLangBadgeStartTime = -1.0;
		uint32_t m_prevPhaseToken = 0u;
		/// Durée totale d'affichage du badge éphémère « Langue : … » au-dessus du panneau login.
		static constexpr double kLoginLangBadgeDurationSec = 4.0;
		/// Durée du fondu sortant inclus dans `kLoginLangBadgeDurationSec`.
		static constexpr double kLoginLangBadgeFadeOutSec = 1.0;
		/// Écran erreur inscription : pastille active (maquette) ; -1 = suivre le modèle classifié.
		int m_authErrorPillPreview = -1;

		engine::client::AuthUiPresenter* m_authPresenter = nullptr;
		const engine::core::Config* m_authCfg = nullptr;
		engine::platform::Window* m_authWindow = nullptr;
		/// Sous-projet C MVP (Task 12) — Viewport offscreen pour
		/// l'apercu race dans l'ecran de creation de perso. nullptr si
		/// Engine n'a pas (ou n'a pas reussi) a initialiser le viewport.
		/// Pointer non possede ; lifetime = Engine.
		engine::render::race::RacePreviewViewport* m_racePreview = nullptr;
		/// Sous-projet C MVP (Task 12) — Flag : "le mesh courant
		/// du m_racePreview correspond a la race m_charRaceIdx". Mis a
		/// false par defaut (Init du viewport laisse m_currentMesh
		/// nullptr) : la 1ere frame de l'ecran de creation force un
		/// SetMesh, puis flippe a true. Reset a false par AuthUi quand
		/// l'utilisateur quitte / re-entre l'ecran (TODO si le repop
		/// devient un cas reel ; pour MVP la session reste lineaire).
		bool m_racePreviewInitialMeshSent = false;

		void RenderLangScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderLoginScreen(const VisualState& vs, const RenderModel& rm, float vpW, float vpH);
		void RenderRegisterScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderErrorScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderVerifyScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderEmailConfirmationScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderOptionsScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderShardScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderForgotScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderTermsScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderCharCreateScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderCharacterSelectScreen(const RenderModel& rm, float vpW, float vpH);

		void BeginFullscreenOverlay(float vpW, float vpH, float windowBgAlpha = 1.f);
		/// Helper unifié pour le rendu du grand titre « LES CHRONIQUES » + sous-titre
		/// « DE LA LUNE NOIRE » sur tous les écrans auth — pattern de référence calé
		/// sur l'écran Login (h1 scale 5.0x, Dummy 28 px pour passer sous la jambe du R,
		/// h2 scale 2.5x). Ouvre un BeginChild "##ln_<screenId>_stage" 96 % vpW que le
		/// caller doit refermer via ImGui::EndChild() après EndPanel.
		void DrawAuthBigTitle(const RenderModel& rm, float vpW, float vpH, const char* screenId);
		bool BeginPanel(float width, float vpW, float vpH, std::string_view title, std::string_view subtitle,
			std::string_view versionLabel = {}, bool versionLeadingInfoGlyph = false, bool subtitleWelcomeAccent = false,
			float fixedHeight = 0.f);
		void EndPanel();
		int DrawLanguageFirstRunCards(const RenderModel& rm, int selected);
		void DrawAuthTweaksPanel(float vpW, float vpH);
		/// Affiche au-dessus du cadre login le badge « Langue : … » capturé lors de la transition
		/// depuis l'écran de sélection de langue. Disparaît automatiquement après quelques secondes.
		void DrawLoginLanguageBadge(float vpW, float vpH);
		void DrawLangFooterHints(std::string_view left, std::string_view right);
		void DrawAuthGoldField(const engine::client::AuthUiPresenter::RenderField& spec, char* buf, int bufSz, bool password,
			float extraSpacingPx = 0.f);
		void DrawLoginRememberRow(const RenderModel& rm);
		void DrawLoginFooterChips(const RenderModel& rm);
		void DrawFooterChipRow(const std::vector<std::pair<std::string, std::string>>& chips);
		void DrawRegisterFlowHeader(const RenderModel& rm, float vpW);
		void DrawRegisterFooterChips(const RenderModel& rm);
		void DrawField(std::string_view label, char* buf, int bufSz, bool password = false);
		void DrawBanner(std::string_view title, std::string_view msg, float r, float g, float b);
		void DrawKeycapHints(std::initializer_list<std::pair<const char*, const char*>> hints);
		// width = -FLT_MIN (défaut) = pleine largeur. Quand on place plusieurs de ces boutons sur
		// la même ligne avec SameLine, il faut donner une largeur finie à chacun pour éviter que
		// leur rectangle de hit-test ne se chevauche (cas vu dans Terms : Refuser couvrait toute
		// la largeur, donc cliquer sur Accepter déclenchait Refuser → fermeture de l'app).
		bool DrawPrimaryButton(std::string_view label, bool disabled = false, float width = -FLT_MIN);
		bool DrawGhostButton(std::string_view label, bool disabled = false, float width = -FLT_MIN);
		void DrawSeparator();
		void DrawBreadcrumb(std::initializer_list<const char*> steps, int current);
		void DrawBreadcrumb(const std::vector<std::string>& steps, int current);

		void SyncTransientFromModel(const VisualState& vs, const RenderModel& rm);
		void PullLanguageOptionsFromPresenter();
	};
} // namespace engine::render
