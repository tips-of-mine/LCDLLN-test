#pragma once

#include <string_view>

#include "engine/client/AuthUi.h"

namespace engine::core
{
	class Config;
}
namespace engine::platform
{
	class Window;
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
		int m_optionsTab = 0;
		uint32_t m_lastSyncedPhaseToken = 0xffffffffu;

		char m_forgotEmail[256]{};
		char m_charName[96]{};

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

		/// Réglages visuels locaux (maquette « Tweaks ») sur l’écran premier lancement — décoratif pour l’instant.
		int m_langTweakRace = 0;
		bool m_langTweakAnimBg = true;

		engine::client::AuthUiPresenter* m_authPresenter = nullptr;
		const engine::core::Config* m_authCfg = nullptr;
		engine::platform::Window* m_authWindow = nullptr;

		void RenderLangScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderLoginScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderRegisterScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderErrorScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderVerifyScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderEmailConfirmationScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderOptionsScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderShardScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderForgotScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderTermsScreen(const RenderModel& rm, float vpW, float vpH);
		void RenderCharCreateScreen(const RenderModel& rm, float vpW, float vpH);

		void BeginFullscreenOverlay(float vpW, float vpH, float windowBgAlpha = 1.f);
		bool BeginPanel(float width, float vpW, float vpH, std::string_view title, std::string_view subtitle,
			std::string_view versionLabel = {}, bool versionLeadingInfoGlyph = false, bool subtitleWelcomeAccent = false);
		void EndPanel();
		int DrawLanguageFirstRunCards(const RenderModel& rm, int selected);
		void DrawLangScreenTweaks(float vpW, float vpH);
		void DrawLangFooterHints(std::string_view left, std::string_view right);
		void DrawField(std::string_view label, char* buf, int bufSz, bool password = false);
		void DrawBanner(std::string_view title, std::string_view msg, float r, float g, float b);
		void DrawKeycapHints(std::initializer_list<std::pair<const char*, const char*>> hints);
		bool DrawPrimaryButton(std::string_view label, bool disabled = false);
		bool DrawGhostButton(std::string_view label, bool disabled = false);
		void DrawSeparator();
		void DrawBreadcrumb(std::initializer_list<const char*> steps, int current);

		void SyncTransientFromModel(const VisualState& vs, const RenderModel& rm);
		void PullLanguageOptionsFromPresenter();
	};
} // namespace engine::render
