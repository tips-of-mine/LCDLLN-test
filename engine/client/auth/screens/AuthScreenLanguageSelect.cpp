#include "engine/client/AuthUi.h"

#include "engine/core/Log.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace engine::client
{
#if defined(_WIN32)
	namespace
	{
		std::string ToUpperAscii(std::string_view s)
		{
			std::string o;
			o.reserve(s.size());
			for (unsigned char ch : s)
			{
				if (ch >= 'a' && ch <= 'z')
				{
					ch = static_cast<unsigned char>(ch - ('a' - 'A'));
				}
				o.push_back(static_cast<char>(ch));
			}
			return o;
		}
	} // namespace

	void AuthUiPresenter::ApplyLocaleSelection(bool firstRun)
	{
		const auto& locales = m_localization.GetAvailableLocales();
		if (locales.empty())
		{
			LOG_WARN(Core, "[AuthUiPresenter] ApplyLocaleSelection ignored: no available locales");
			return;
		}

		if (m_languageSelectionIndex >= locales.size())
		{
			m_languageSelectionIndex = 0;
		}
		m_selectedLocale = locales[m_languageSelectionIndex];
		if (!m_localization.SetLocale(m_selectedLocale))
		{
			LOG_WARN(Core, "[AuthUiPresenter] Locale apply failed for '{}'", m_selectedLocale);
			return;
		}

		SaveRememberPreference();
		if (firstRun)
		{
			if (PatchPersistedLocaleKey(m_selectedLocale))
			{
				m_persistedLocale = m_selectedLocale;
				m_hasPersistedLocale = true;
			}
		}
		m_infoBanner = Tr("language.apply_success", { { "language", LocalizedLanguageName(m_selectedLocale) } });
		LOG_INFO(Core, "[AuthUiPresenter] Locale selection applied (locale={}, first_run={})", m_selectedLocale, firstRun);
		if (firstRun)
		{
			SetPhase(Phase::Login);
			m_activeField = 0;
		}
		else
		{
			SetPhase(m_phaseBeforeOptions);
		}
	}

	void AuthUiPresenter::ImGuiApplyFirstRunLanguageContinue(const engine::core::Config& cfg, std::string_view localeTag)
	{
		(void)cfg;
		if (m_phase != Phase::LanguageSelectionFirstRun || localeTag.empty())
		{
			return;
		}
		const auto& locales = m_localization.GetAvailableLocales();
		if (locales.empty())
		{
			return;
		}
		const std::string tag(localeTag);
		const auto it = std::find(locales.begin(), locales.end(), tag);
		if (it == locales.end())
		{
			LOG_WARN(Core, "[AuthUiPresenter] ImGui: locale '{}' absente des locales disponibles", tag);
			return;
		}
		m_languageSelectionIndex = static_cast<uint32_t>(std::distance(locales.begin(), it));
		m_selectedLocale = *it;
		ApplyLocaleSelection(true);
	}

	void AuthUiPresenter::ImGuiSelectFirstRunLanguageCard(uint32_t cardIndex)
	{
		if (m_phase != Phase::LanguageSelectionFirstRun)
		{
			return;
		}
		const auto& locales = m_localization.GetAvailableLocales();
		if (locales.empty() || cardIndex >= locales.size())
		{
			return;
		}
		m_languageSelectionIndex = cardIndex;
		m_selectedLocale = locales[cardIndex];
	}

	void AuthUiPresenter::BuildModel_LanguageSelect(RenderModel& model) const
	{
		model.languageFirstRunLayout = true;
		model.sectionTitle = Tr("language.first_run.panel_title");
		const auto& localesFr = m_localization.GetAvailableLocales();
		if (!localesFr.empty() && m_languageSelectionIndex < localesFr.size())
		{
			const std::string& selTag = localesFr[m_languageSelectionIndex];
			const std::string welcomeKey = std::string("language.first_run.welcome.") + selTag;
			std::string sub = Tr(welcomeKey);
			if (sub.empty() || sub == welcomeKey)
			{
				sub = Tr("language.first_run.welcome.fr");
			}
			model.languagePanelSubtitle = std::move(sub);
		}
		else
		{
			model.languagePanelSubtitle = Tr("language.first_run.welcome.fr");
		}
		if (!localesFr.empty())
		{
			model.languageVersionLabel = Tr("language.first_run.progress", { { "current", "1" }, { "total", "2" } });
			for (size_t li = 0; li < localesFr.size(); ++li)
			{
				const std::string& locTag = localesFr[li];
				LanguageFirstRunCard card{};
				card.localeTag = locTag;
				const std::string disp = LocalizedLanguageName(locTag);
				card.nameAllCaps = ToUpperAscii(disp);
				const std::string nativeKey = std::string("language.native_line.") + locTag;
				card.nativeLine = Tr(nativeKey);
				if (card.nativeLine.empty() || card.nativeLine == nativeKey)
				{
					card.nativeLine = disp;
				}
				card.selected = (static_cast<uint32_t>(li) == m_languageSelectionIndex);
				card.hovered = (static_cast<int32_t>(li) == m_hoveredLanguageCardIndex);
				model.languageFirstRunCards.push_back(std::move(card));
			}
		}
		model.languageFooterLeft = Tr("language.first_run.footer_left");
		model.languageFooterRight = Tr("language.first_run.footer_right");
	}

	void AuthUiPresenter::Update_LanguageSelect(engine::platform::Input& input, const engine::core::Config& cfg,
		engine::platform::Window& window, bool usingNativeAuth, bool authUiImguiMode)
	{
		(void)window;
		if (usingNativeAuth || m_phase != Phase::LanguageSelectionFirstRun)
		{
			return;
		}
		const auto& locales = m_localization.GetAvailableLocales();
		if (locales.empty())
		{
			return;
		}
		const uint32_t n = static_cast<uint32_t>(locales.size());
		if (input.WasPressed(engine::platform::Key::Left) || input.WasPressed(engine::platform::Key::Up))
		{
			m_languageSelectionIndex = (m_languageSelectionIndex + n - 1u) % n;
			m_selectedLocale = locales[m_languageSelectionIndex];
			LOG_INFO(Core, "[AuthUiPresenter] Locale selection moved to {}", m_selectedLocale);
		}
		if (input.WasPressed(engine::platform::Key::Right) || input.WasPressed(engine::platform::Key::Down))
		{
			m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % n;
			m_selectedLocale = locales[m_languageSelectionIndex];
			LOG_INFO(Core, "[AuthUiPresenter] Locale selection moved to {}", m_selectedLocale);
		}
		if (authUiImguiMode && input.WasPressed(engine::platform::Key::Enter))
		{
			ImGuiApplyFirstRunLanguageContinue(cfg, m_selectedLocale);
		}
	}

#else

	void AuthUiPresenter::ImGuiApplyFirstRunLanguageContinue(const engine::core::Config&, std::string_view) {}

	void AuthUiPresenter::ImGuiSelectFirstRunLanguageCard(uint32_t) {}

	void AuthUiPresenter::BuildModel_LanguageSelect(RenderModel&) const {}

	void AuthUiPresenter::Update_LanguageSelect(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool)
	{
	}

#endif
} // namespace engine::client
