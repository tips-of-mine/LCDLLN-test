// AUTH-UI.6 - overlay Options de la phase LanguageOptions : sidebar de navigation par onglets et panneau principal multi-sections (split depuis AuthImGuiRenderer.cpp).
// Contient RenderOptionsScreen avec ses lambdas internes (sliderVol01, sectionTitle, hintLine, toggleRow, submitOptionsMirror) et les sept onglets de configuration.

#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/auth/AuthImGuiCommon.h"
#include "src/client/render/LnTheme.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;
		using LnTheme::ToU32;

		constexpr int kOptionsRes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}}; ///< Table des resolutions video proposees dans le combo graphique.
		constexpr int kOptionsResCount = sizeof(kOptionsRes) / sizeof(kOptionsRes[0]); ///< Nombre d'entrees dans kOptionsRes, calcule statiquement.

		/// B2/ST6 — Une action gameplay remappable depuis l'onglet Controls.
		/// \c cfgKey : cle config \c controls.keybind.* (lue/ecrite, miroir local) ;
		/// \c def : touche par defaut (nom ASCII) si la cle est absente ;
		/// \c labelKey / \c labelFallback : libelle i18n affiche (avec repli).
		struct RebindAction
		{
			const char* cfgKey;
			const char* def;
			const char* labelKey;
			const char* labelFallback;
		};

		/// B2/ST6 — Table figee des 5 actions remappables, ordre = index dans
		/// \c m_rebindKeys et valeur de \c m_rebindActionIdx. Doit rester aligne sur
		/// la liste portee depuis le mini-panneau in-game (Engine.cpp) et sur le
		/// format d'ecriture de keybinds.json (cf. WriteKeybindsJson plus bas).
		constexpr RebindAction kRebindActions[] = {
			{"controls.keybind.sprint", "Alt", "options.keybind.sprint", "Sprint"},
			{"controls.keybind.crouch", "Ctrl", "options.keybind.crouch", "Accroupi"},
			{"controls.keybind.cast", "R", "options.keybind.cast", "Sort"},
			{"controls.keybind.interact", "E", "options.keybind.interact", "Interagir"},
			{"controls.keybind.punch", "X", "options.keybind.punch", "Coup de poing"},
		};
		constexpr int kRebindActionCount = sizeof(kRebindActions) / sizeof(kRebindActions[0]); ///< Nombre d'actions remappables (= taille de m_rebindKeys).

		/// B2/ST6 — Correspondance \c ImGuiKey -> nom ASCII attendu par keybinds.json.
		/// Cohérent avec \c kRebindableKeys (Engine.cpp) : modificateurs gauches
		/// ("Shift"/"Ctrl"/"Alt"), lettres A..Z, chiffres 0..9, "Espace", "Tab".
		struct ImGuiKeyName
		{
			ImGuiKey key;
			const char* name;
		};
		const ImGuiKeyName kImGuiKeyNames[] = {
			{ImGuiKey_A, "A"}, {ImGuiKey_B, "B"}, {ImGuiKey_C, "C"}, {ImGuiKey_D, "D"},
			{ImGuiKey_E, "E"}, {ImGuiKey_F, "F"}, {ImGuiKey_G, "G"}, {ImGuiKey_H, "H"},
			{ImGuiKey_L, "L"}, {ImGuiKey_M, "M"}, {ImGuiKey_N, "N"}, {ImGuiKey_O, "O"},
			{ImGuiKey_P, "P"}, {ImGuiKey_Q, "Q"}, {ImGuiKey_R, "R"}, {ImGuiKey_S, "S"},
			{ImGuiKey_U, "U"}, {ImGuiKey_V, "V"}, {ImGuiKey_W, "W"}, {ImGuiKey_X, "X"},
			{ImGuiKey_Y, "Y"}, {ImGuiKey_Z, "Z"},
			{ImGuiKey_0, "0"}, {ImGuiKey_1, "1"}, {ImGuiKey_2, "2"}, {ImGuiKey_3, "3"},
			{ImGuiKey_4, "4"}, {ImGuiKey_5, "5"}, {ImGuiKey_6, "6"}, {ImGuiKey_7, "7"},
			{ImGuiKey_8, "8"}, {ImGuiKey_9, "9"},
			{ImGuiKey_LeftCtrl, "Ctrl"}, {ImGuiKey_RightCtrl, "Ctrl"},
			{ImGuiKey_LeftAlt, "Alt"}, {ImGuiKey_RightAlt, "Alt"},
			{ImGuiKey_LeftShift, "Shift"}, {ImGuiKey_RightShift, "Shift"},
			{ImGuiKey_Space, "Espace"}, {ImGuiKey_Tab, "Tab"},
		};

		/// B2/ST6 — Balaie les touches connues et renvoie le nom ASCII de la première
		/// pressée cette frame (front montant, \c repeat=false), ou nullptr si aucune.
		/// Sert à la capture de rebind via la couche clavier d'ImGui (l'écran d'options
		/// n'a pas accès à \c m_input, contrairement au mini-panneau in-game).
		const char* CapturePressedKeyName()
		{
			for (const auto& e : kImGuiKeyNames)
			{
				if (ImGui::IsKeyPressed(e.key, false))
				{
					return e.name;
				}
			}
			return nullptr;
		}

		/// B2/ST6 — Sérialise les 5 touches remappées au format keybinds.json et l'écrit
		/// sur disque (fichier dédié mergé au boot, comme ui_theme.json). \c keys est
		/// indexé comme \c kRebindActions. Les valeurs étant des noms ASCII contrôlés,
		/// aucun échappement JSON n'est requis. Effet de bord : écriture fichier.
		void WriteKeybindsJson(const std::string keys[kRebindActionCount])
		{
			const std::string js =
				std::string("{\n  \"controls\": {\n    \"keybind\": {\n")
				+ "      \"sprint\": \"" + keys[0] + "\",\n"
				+ "      \"crouch\": \"" + keys[1] + "\",\n"
				+ "      \"cast\": \"" + keys[2] + "\",\n"
				+ "      \"interact\": \"" + keys[3] + "\",\n"
				+ "      \"punch\": \"" + keys[4] + "\"\n"
				+ "    }\n  }\n}\n";
			(void)engine::platform::FileSystem::WriteAllText("keybinds.json", js);
		}
	} // namespace

	/// Affiche l'overlay Options complet : sidebar de navigation par onglets a gauche, panneau principal a droite avec les reglages de l'onglet actif, et barre de boutons Retour / Annuler / Appliquer en bas.
	/// \param inGame true quand l'ecran est reutilise en jeu (menu Pause) : on masque alors le decor auth (DrawAuthTweaksPanel « THEME DE RACE »). Le flag est aussi memorise dans m_optionsInGame pour le masquage d'onglets (ST6).
	void AuthImGuiRenderer::RenderOptionsScreen(const RenderModel& rm, float vpW, float vpH, bool inGame)
	{
		// B2/ST4 — memorise le contexte pour les helpers internes (masquage onglets ST6).
		m_optionsInGame = inGame;
		// B2/ST6 — true si une capture de rebind était active au début de la frame.
		// Le remap (onglet Controls) traite Échap pour annuler la capture ; on garde
		// l'info ici pour que le handler Échap global (bas de fonction) ne ferme PAS
		// l'overlay la même frame qu'une annulation de capture.
		const bool rebindWasActive = (m_rebindActionIdx >= 0);
		const auto tr = [this](const char* key, const char* fallback = nullptr) -> std::string {
			if (m_authPresenter == nullptr)
			{
				return fallback && fallback[0] != '\0' ? std::string(fallback) : std::string(key);
			}
			std::string s = m_authPresenter->UiTranslate(key);
			if (s.empty() && fallback != nullptr && fallback[0] != '\0')
			{
				return std::string(fallback);
			}
			return s.empty() ? std::string(key) : s;
		};

		// Sidebar : retour a 220 px (revert du bump 320 px : la sidebar etait OK
		// avant, c'est le main qui doit changer pour qu'on puisse interagir avec
		// les widgets).
		const float sideW = 220.f;
		// Panel main : on conserve le bornage horizontal mais on l'augmente a 1600
		// pour avoir assez de largeur pour les sliders, combos, et le label hint
		// a droite. Centre horizontalement sur ultra-wide.
		const float mainWMax = 1600.f;
		const float mainW = (std::min)(mainWMax, vpW - sideW - 32.f);
		const float mainOriginX = sideW + ((vpW - sideW - mainW) * 0.5f);
		// Hauteurs ajustees PAR sous-menu : on passe les deux child a AutoResizeY,
		// la hauteur s'adapte au contenu reel de l'onglet actif (au lieu d'occuper
		// toute la viewport - 1080 px - et de laisser une grande zone vide).
		// Le caller plus haut (DrawAuthTweaksPanel) reste libre, ces childs ne
		// remplissent plus tout l'ecran.

		static constexpr const char* kTabKeys[] = {"options.imgui.tab.graphics", "options.imgui.tab.audio", "options.imgui.tab.controls",
			"options.imgui.tab.lang", "options.imgui.tab.ui", "options.imgui.tab.net", "options.imgui.tab.account"};
		// Anciennes icones Unicode (carre, note de musique, clavier, etc.) absentes de Windlass
		// + ProggyClean fallback : rendaient toutes des '?'. Suppression au profit du libelle nu.
		static constexpr int tabCount = 7;
		// Indices des onglets : 0 Graphics, 1 Audio, 2 Controls, 3 Lang, 4 UI,
		// 5 Network, 6 Account.
		static constexpr int kTabNetwork = 5;
		static constexpr int kTabAccount = 6;

		// B2/ST6 — Liste des onglets visibles dans le contexte courant. En jeu
		// (m_optionsInGame) on masque Network et Account : ces onglets dépendent du
		// flux auth (serveur préféré, latence, compte connecté) sans objet en jeu.
		// On itère ensuite sur cette liste pour la sidebar ET pour borner m_optionsTab,
		// de sorte qu'aucun onglet masqué ne reste sélectionnable.
		std::vector<int> visibleTabs;
		visibleTabs.reserve(tabCount);
		for (int i = 0; i < tabCount; ++i)
		{
			if (m_optionsInGame && (i == kTabNetwork || i == kTabAccount))
			{
				continue;
			}
			visibleTabs.push_back(i);
		}
		// Si l'onglet courant n'est plus visible (ex. on entre en jeu alors que
		// l'onglet Account était actif), ramener sur le premier onglet visible.
		if (std::find(visibleTabs.begin(), visibleTabs.end(), m_optionsTab) == visibleTabs.end())
		{
			m_optionsTab = visibleTabs.empty() ? 0 : visibleTabs.front();
		}

		// Sidebar haute = main haute (alignement vertical du cadre).
		const float topMargin = 60.f;
		const float bottomMargin = 60.f;
		const float panelH = (std::max)(560.f, vpH - topMargin - bottomMargin);
		ImGui::SetCursorPos(ImVec2(0.f, topMargin));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(LnTheme::kPanel));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::BeginChild("##opts_sidebar", ImVec2(sideW, panelH),
			ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(2);

		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		ImGui::SetWindowFontScale(1.08f);
		{
			const std::string sideTitle = tr("options.imgui.sidebar_title");
			ImGui::TextUnformatted(sideTitle.c_str());
		}
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		DrawSeparator();

		for (int i : visibleTabs)
		{
			const bool active = (m_optionsTab == i);
			const std::string tabLabel = tr(kTabKeys[i]);
			ImGui::PushStyleColor(ImGuiCol_Button, active ? ToImVec4(LnTheme::AccentDim(0.12f)) : ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_Text, active ? ToImVec4(LnTheme::kAccent) : ToImVec4(LnTheme::kText));
			ImGui::PushStyleColor(ImGuiCol_Border, active ? ToImVec4(LnTheme::kAccent) : ToImVec4(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, active ? 1.5f : 0.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
			char btnLine[192];
			std::snprintf(btnLine, sizeof(btnLine), "%s##tab%d", tabLabel.c_str(), i);
			if (ImGui::Button(btnLine, ImVec2(-FLT_MIN, 36.f)))
			{
				m_optionsTab = i;
			}
			if (active)
			{
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 rmin = ImGui::GetItemRectMin();
				const ImVec2 rmax = ImGui::GetItemRectMax();
				dl->AddRectFilled(ImVec2(rmin.x, rmin.y), ImVec2(rmin.x + 3.f, rmax.y), ToU32(LnTheme::kAccent));
			}
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
		}

		// Push le footer hint en bas du sidebar : reserve 56 px pour le footer +
		// remplit l'espace restant. Comportement original (avant le passage en
		// AutoResizeY).
		const float sidebarFooterReserve = 56.f;
		const float sidebarSpacerH = ImGui::GetContentRegionAvail().y - sidebarFooterReserve;
		if (sidebarSpacerH > 2.f)
		{
			ImGui::Dummy(ImVec2(1.f, sidebarSpacerH));
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.82f);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.92f);
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + sideW - 24.f);
		ImGui::TextWrapped("%s", tr("options.imgui.sidebar_footer").c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleVar(1);
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		ImGui::EndChild();

		// Panel main : meme topMargin que la sidebar pour alignement, hauteur =
		// panelH. Le contenu commence dans le coin haut-gauche du child et le
		// scroll interne (##opts_body) prend le relais si trop d'options.
		ImGui::SetCursorPos(ImVec2(mainOriginX, topMargin));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(LnTheme::kBackground));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
		ImGui::BeginChild("##opts_main", ImVec2(mainW, panelH),
			ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(2);

		// Header inline : pas de BeginChild wrapper qui consommait trop de hauteur
		// (ImGui::BeginChild avec hauteur 0 sans flag AutoResizeY peut prendre 100%
		// de la zone disponible -> body invisible, retour utilisateur 'cadre vide').
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
		ImGui::SetWindowFontScale(0.78f);
		ImGui::TextUnformatted(tr("options.imgui.category_label").c_str());
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::SetWindowFontScale(1.12f);
		{
			const std::string mainTabTitle = tr(kTabKeys[m_optionsTab]);
			ImGui::TextUnformatted(mainTabTitle.c_str());
		}
		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleColor();
		if (m_optDirty)
		{
			ImGui::SameLine(0.f, 18.f);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kWarning));
			ImGui::TextUnformatted(tr("options.imgui.dirty_banner_title").c_str());
			ImGui::PopStyleColor();
		}
		DrawSeparator();

		// Body : hauteur 0 = remplit l'espace restant du panel main (ImGui calcule
		// auto en retranchant la taille deja consommee par header + separator).
		// Reserve 60 px en bas pour le footer d'actions (RETOUR / ANNULER / APPLIQUER).
		const float footerReserve = 60.f;
		const float bodyH = (std::max)(120.f, ImGui::GetContentRegionAvail().y - footerReserve);
		ImGui::BeginChild("##opts_body", ImVec2(-FLT_MIN, bodyH),
			false, ImGuiWindowFlags_None);

		const auto markDirty = [this]() { m_optDirty = true; };

		const auto sliderVol01 = [&](const char* label, float* v01) {
			float pct = *v01 * 100.f;
			if (ImGui::SliderFloat(label, &pct, 0.f, 100.f, "%.0f%%"))
			{
				*v01 = pct * 0.01f;
				markDirty();
			}
		};

		const auto sectionTitle = [&](const char* key) {
			ImGui::Spacing();
			const std::string t = tr(key);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::TextUnformatted(t.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::Spacing();
		};

		const auto hintLine = [&](const char* key) {
			const std::string h = tr(key);
			if (h.empty())
			{
				return;
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.78f);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.9f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
			ImGui::TextWrapped("%s", h.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleVar(1);
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		};

		const auto toggleRow = [&](const char* labelKey, bool* v, const char* hintKey) {
			const float fullW = ImGui::GetContentRegionAvail().x;
			const std::string lbl = tr(labelKey);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ToImVec4(LnTheme::kSurface));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ToImVec4(LnTheme::AccentDim(0.12f)));
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ToImVec4(LnTheme::kAccent));
			ImGui::BeginGroup();
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(lbl.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine(fullW - 28.f);
			char cbId[96];
			std::snprintf(cbId, sizeof(cbId), "##cb_%s", labelKey);
			if (ImGui::Checkbox(cbId, v))
			{
				markDirty();
			}
			ImGui::EndGroup();
			ImGui::PopStyleColor(3);
			if (hintKey != nullptr && hintKey[0] != '\0')
			{
				hintLine(hintKey);
			}
			ImGui::Spacing();
		};

		const auto submitOptionsMirror = [&]() {
			if (m_authPresenter == nullptr || m_authCfg == nullptr)
			{
				return;
			}
			m_optResIdx = std::clamp(m_optResIdx, 0, kOptionsResCount - 1);
			engine::client::AuthUiPresenter::LanguageOptionsImGuiMirror mir{};
			mir.videoFullscreen = m_optFullscreen;
			mir.videoVsync = m_optVsync;
			mir.videoResWidth = kOptionsRes[m_optResIdx][0];
			mir.videoResHeight = kOptionsRes[m_optResIdx][1];
			mir.videoQualityPreset = static_cast<int32_t>(std::clamp(m_optQualityPreset, 0, 3));
			mir.videoFovDegrees = std::clamp(m_optFovDegrees, 60.f, 120.f);
			mir.audioMaster01 = m_optAudioMaster;
			mir.audioMusic01 = m_optAudioMusic;
			mir.audioSfx01 = m_optAudioSfx;
			mir.audioUi01 = m_optAudioUi;
			mir.mouseSensitivity = m_optMouseSens;
			mir.invertY = m_optInvertY;
			mir.useZqsd = m_optUseZqsd;
			mir.gameplayUdpEnabled = m_optGameplayUdp;
			mir.allowInsecureDev = m_optAllowInsecureDev;
			mir.authTimeoutMs = std::clamp(m_optAuthTimeoutMs, 2000u, 10000u);
			mir.languageSelectionIndex = static_cast<uint32_t>(m_optLangIndex);
			mir.uiScalePercent = std::clamp(m_optUiScalePct, 80.f, 140.f);
			mir.panelOpacityPercent = std::clamp(m_optPanelOpacityPct, 40.f, 100.f);
			mir.showTooltipUi = m_optShowTooltipsUi;
			mir.preferredServerIndex = static_cast<uint32_t>(std::clamp(m_optPreferredServer, 0, 2));
			m_authPresenter->ImGuiApplyLanguageOptionsMenu(*m_authCfg, mir);
			m_optDirty = false;
		};

		ImGui::PushStyleColor(ImGuiCol_FrameBg, ToImVec4(LnTheme::kSurface));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ToImVec4(LnTheme::AccentDim(0.12f)));
		ImGui::PushStyleColor(ImGuiCol_SliderGrab, ToImVec4(LnTheme::kAccent));
		ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ToImVec4(LnTheme::kAccent));

		if (m_optionsTab == 0)
		{
			sectionTitle("options.imgui.section.display");
			const char* resItems = "1280x720\01600x900\01920x1080\02560x1440\03840x2160\0\0";
			const std::string resLab = tr("options.imgui.resolution");
			if (ImGui::Combo(resLab.c_str(), &m_optResIdx, resItems))
			{
				m_optResIdx = std::clamp(m_optResIdx, 0, kOptionsResCount - 1);
				markDirty();
			}
			std::string qualityItems;
			for (int q = 0; q < 4; ++q)
			{
				char qk[56];
				std::snprintf(qk, sizeof(qk), "options.imgui.quality.%d", q);
				qualityItems += tr(qk);
				qualityItems.push_back('\0');
			}
			qualityItems.push_back('\0');
			const std::string qualLab = tr("options.imgui.quality");
			if (ImGui::Combo(qualLab.c_str(), &m_optQualityPreset, qualityItems.c_str()))
			{
				m_optQualityPreset = std::clamp(m_optQualityPreset, 0, 3);
				markDirty();
			}
			hintLine("options.imgui.quality_hint");

			ImGui::Spacing();
			const std::string fovLabel = tr("options.imgui.fov");
			if (ImGui::SliderFloat(fovLabel.c_str(), &m_optFovDegrees, 60.f, 120.f, "%.0f \xC2\xB0"))
			{
				m_optFovDegrees = std::clamp(m_optFovDegrees, 60.f, 120.f);
				markDirty();
			}

			sectionTitle("options.imgui.section.modes");
			toggleRow("options.imgui.fullscreen", &m_optFullscreen, "options.imgui.fullscreen_hint");
			toggleRow("options.imgui.vsync", &m_optVsync, "options.imgui.vsync_hint");
		}
		else if (m_optionsTab == 1)
		{
			sectionTitle("options.imgui.section.volumes");
			sliderVol01(tr("options.audio.master").c_str(), &m_optAudioMaster);
			sliderVol01(tr("options.audio.music").c_str(), &m_optAudioMusic);
			sliderVol01(tr("options.audio.sfx").c_str(), &m_optAudioSfx);
			sliderVol01(tr("options.audio.voice").c_str(), &m_optAudioUi);
		}
		else if (m_optionsTab == 2)
		{
			sectionTitle("options.imgui.section.mouse_keyboard");
			float sensPct = (m_optMouseSens - 0.001f) / 0.009f * 90.f + 10.f;
			sensPct = std::clamp(sensPct, 10.f, 100.f);
			if (ImGui::SliderFloat(tr("options.controls.mouse_sensitivity").c_str(), &sensPct, 10.f, 100.f, "%.0f %%"))
			{
				m_optMouseSens = 0.001f + (sensPct - 10.f) / 90.f * 0.009f;
				m_optMouseSens = std::clamp(m_optMouseSens, 0.001f, 0.01f);
				markDirty();
			}
			hintLine("options.imgui.hint.mouse_sensitivity_pct");
			toggleRow("options.controls.invert_y", &m_optInvertY, "options.imgui.hint.invert_y");
			toggleRow("options.imgui.zqsd_layout", &m_optUseZqsd, "options.imgui.hint.zqsd");

			sectionTitle("options.imgui.section.keybinds");

			// B2/ST6 — Remap interactif des 5 actions gameplay (Sprint / Accroupi /
			// Sort / Interagir / Coup de poing), actif à l'auth ET en jeu. Porté du
			// mini-panneau in-game (Engine.cpp) mais via la capture clavier d'ImGui :
			// l'écran d'options (couche renderer) n'a pas accès à m_input.
			//
			// 1) Charge les miroirs locaux depuis la config au premier passage (m_authCfg
			//    est const : l'état d'édition vit dans m_rebindKeys). 2) Si une capture
			//    est en cours, affecte la prochaine touche pressée (Échap annule) et
			//    persiste keybinds.json. 3) Affiche une ligne « Action : <touche> [Modifier] »
			//    par action, avec signalement de conflit (touche déjà sur une autre action).
			if (!m_rebindKeysLoaded)
			{
				for (int a = 0; a < kRebindActionCount; ++a)
				{
					m_rebindKeys[a] = (m_authCfg != nullptr)
						? m_authCfg->GetString(kRebindActions[a].cfgKey, kRebindActions[a].def)
						: std::string(kRebindActions[a].def);
				}
				m_rebindKeysLoaded = true;
			}

			// Capture en cours : balaye le clavier ImGui. Échap annule sans modifier.
			if (m_rebindActionIdx >= 0 && m_rebindActionIdx < kRebindActionCount)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				{
					m_rebindActionIdx = -1;
				}
				else if (const char* pressed = CapturePressedKeyName())
				{
					// Détection de conflit : la touche choisie est-elle déjà affectée à
					// une autre action ? (doublon autorisé, simplement signalé.)
					m_rebindWarning.clear();
					for (int a = 0; a < kRebindActionCount; ++a)
					{
						if (a != m_rebindActionIdx && m_rebindKeys[a] == pressed)
						{
							const std::string other = tr(kRebindActions[a].labelKey, kRebindActions[a].labelFallback);
							m_rebindWarning = std::string("Touche '") + pressed + "' deja utilisee par " + other + " (doublon).";
							break;
						}
					}
					m_rebindKeys[m_rebindActionIdx] = pressed;
					WriteKeybindsJson(m_rebindKeys);
					m_rebindActionIdx = -1;
				}
			}

			// Ligne éditable par action : libellé + touche courante, bouton « Modifier ».
			const auto rebindRow = [&](int actionIdx) {
				const RebindAction& act = kRebindActions[actionIdx];
				const std::string label = tr(act.labelKey, act.labelFallback);
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%s : %s", label.c_str(), m_rebindKeys[actionIdx].c_str());
				ImGui::PopStyleColor();
				ImGui::SameLine(220.f);
				ImGui::PushID(actionIdx);
				if (m_rebindActionIdx == actionIdx)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
					ImGui::TextUnformatted(tr("options.keybind.press_key", "appuyez sur une touche (Echap = annuler)").c_str());
					ImGui::PopStyleColor();
				}
				else
				{
					// Bouton compact (largeur fixe) : DrawAuthButtonGhost s'étire sur
					// toute la largeur restante (width -FLT_MIN), inadapté après SameLine.
					ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kSurface));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.12f)));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.18f)));
					ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
					ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
					ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
					if (ImGui::Button(tr("options.keybind.rebind", "Modifier").c_str(), ImVec2(110.f, 28.f)))
					{
						m_rebindActionIdx = actionIdx;
						m_rebindWarning.clear();
					}
					ImGui::PopStyleVar(2);
					ImGui::PopStyleColor(5);
				}
				ImGui::PopID();
			};
			for (int a = 0; a < kRebindActionCount; ++a)
			{
				rebindRow(a);
			}
			if (!m_rebindWarning.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kWarning));
				ImGui::TextWrapped("%s", m_rebindWarning.c_str());
				ImGui::PopStyleColor();
			}
		}
		else if (m_optionsTab == 3)
		{
			sectionTitle("options.imgui.tab.lang");
			if (m_authPresenter != nullptr)
			{
				const auto& locs = m_authPresenter->GetAvailableLocales();
				std::vector<const char*> ptrs;
				ptrs.reserve(locs.size());
				for (const auto& loc : locs)
				{
					ptrs.push_back(loc.c_str());
				}
				if (!ptrs.empty())
				{
					const int n = static_cast<int>(ptrs.size());
					if (m_optLangIndex >= n)
					{
						m_optLangIndex = n - 1;
					}
					const std::string langComboLabel = tr("options.imgui.lang_interface");
					if (ImGui::Combo(langComboLabel.c_str(), &m_optLangIndex, ptrs.data(), n))
					{
						markDirty();
					}
					hintLine("options.imgui.lang_restart_hint");
				}
			}
			else
			{
				ImGui::TextDisabled("(Presenter)");
			}
		}
		else if (m_optionsTab == 4)
		{
			sectionTitle("options.imgui.section.interface");
			if (ImGui::SliderFloat(tr("options.interface.scale").c_str(), &m_optUiScalePct, 80.f, 140.f, "%.0f %%"))
			{
				m_optUiScalePct = std::clamp(m_optUiScalePct, 80.f, 140.f);
				markDirty();
			}
			if (ImGui::SliderFloat(tr("options.interface.panel_opacity").c_str(), &m_optPanelOpacityPct, 40.f, 100.f, "%.0f %%"))
			{
				m_optPanelOpacityPct = std::clamp(m_optPanelOpacityPct, 40.f, 100.f);
				markDirty();
			}
			toggleRow("options.interface.tooltips", &m_optShowTooltipsUi, "options.interface.tooltips_hint");
			// Thème de l'interface : application live (aperçu immédiat sur l'écran
			// auth) + persistance ui_theme.json, comme le sélecteur in-game. Hors du
			// modèle staged (Appliquer/Annuler) : un thème se prévisualise en direct.
			{
				// Clé i18n dérivée de l'id du thème (data-driven) : un nouveau thème
				// dans le registre s'affiche sans toucher ce code dès que sa clé
				// "options.interface.theme.<id>" existe ; sinon tr() renvoie la clé.
				auto themeLabel = [&](std::string_view id) -> std::string {
					return tr(("options.interface.theme." + std::string(id)).c_str());
				};
				const std::string_view curTheme = LnTheme::ActiveName();
				if (ImGui::BeginCombo(tr("options.interface.theme").c_str(), themeLabel(curTheme).c_str()))
				{
					for (std::string_view id : LnTheme::Names())
					{
						const bool selected = (id == curTheme);
						if (ImGui::Selectable(themeLabel(id).c_str(), selected) && !selected)
						{
							LnTheme::SetActive(id);
							const std::string js =
								std::string("{\n  \"ui\": { \"theme\": \"")
								+ std::string(LnTheme::ActiveName()) + "\" }\n}\n";
							(void)engine::platform::FileSystem::WriteAllText("ui_theme.json", js);
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
		}
		else if (m_optionsTab == 5)
		{
			sectionTitle("options.imgui.section.network");
			{
				const std::string srvLab = tr("options.imgui.pref_server");
				std::string srvItems;
				srvItems += tr("options.imgui.server_morne");
				srvItems.push_back('\0');
				srvItems += tr("options.imgui.server_korvath");
				srvItems.push_back('\0');
				srvItems += tr("options.imgui.server_auto");
				srvItems.push_back('\0');
				srvItems.push_back('\0');
				if (ImGui::Combo(srvLab.c_str(), &m_optPreferredServer, srvItems.c_str()))
				{
					m_optPreferredServer = std::clamp(m_optPreferredServer, 0, 2);
					markDirty();
				}
			}
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
			ImGui::TextUnformatted(tr("options.imgui.latency_label").c_str());
			ImGui::SameLine(0.f, 12.f);
			if (m_authPresenter != nullptr)
			{
				const auto& sc = m_authPresenter->GetStatusCache();
				ImGui::PushStyleColor(ImGuiCol_Text, sc.authOk ? ToImVec4(LnTheme::kSuccess) : ToImVec4(LnTheme::kMuted));
				const std::string lat = sc.authOk ? tr("options.imgui.latency_ok") : std::string("--");
				ImGui::TextUnformatted(lat.c_str());
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::TextUnformatted("--");
			}
			ImGui::PopStyleColor();
			ImGui::Spacing();
			toggleRow("options.game.gameplay_udp", &m_optGameplayUdp, "options.imgui.hint.gameplay_udp");
			toggleRow("options.game.allow_insecure_dev", &m_optAllowInsecureDev, "options.imgui.hint.allow_insecure");
			int tmo = static_cast<int>(m_optAuthTimeoutMs);
			if (ImGui::SliderInt(tr("options.game.auth_timeout").c_str(), &tmo, 2000, 10000, "%d ms"))
			{
				m_optAuthTimeoutMs = static_cast<uint32_t>(std::clamp(tmo, 2000, 10000));
				markDirty();
			}
		}
		else if (m_optionsTab == 6)
		{
			sectionTitle("options.imgui.account.section");
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
			ImGui::BeginChild("##acct_card", ImVec2(-FLT_MIN, 92.f), true, ImGuiWindowFlags_None);
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
			const std::string& loginDisp = rm.authOptionsAccountLogin.empty() ? tr("options.imgui.account_anon") : rm.authOptionsAccountLogin;
			ImGui::TextUnformatted(loginDisp.c_str());
			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			const std::string& tagDisp = rm.authOptionsAccountTagId.empty() ? std::string("-") : rm.authOptionsAccountTagId;
			ImGui::TextUnformatted(tagDisp.c_str());
			ImGui::PopStyleColor();
			ImGui::EndChild();
			ImGui::Spacing();
			sectionTitle("options.imgui.account.actions");
			{
				const std::string lblPw = tr("options.imgui.account_change_password");
				if (DrawAuthButtonGhost(lblPw, "##acct_pw") && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiOptionsAccountMenuAction(engine::client::AuthUiPresenter::OptionsAccountMenuAction::ChangePassword);
				}
			}
			{
				const std::string lblMail = tr("options.imgui.account_change_email");
				if (DrawAuthButtonGhost(lblMail, "##acct_mail") && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiOptionsAccountMenuAction(engine::client::AuthUiPresenter::OptionsAccountMenuAction::ChangeEmail);
				}
			}
			{
				const std::string lblOut = tr("options.imgui.account_sign_out");
				if (DrawAuthButtonDanger(lblOut, "##acct_out") && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiOptionsAccountMenuAction(engine::client::AuthUiPresenter::OptionsAccountMenuAction::SignOut);
				}
			}
		}

		ImGui::PopStyleColor(4);

		ImGui::EndChild();

		DrawSeparator();
		const float btnW = 128.f;
		const std::string backStr = tr("options.imgui.button.back");
		const std::string cancelStr = tr("common.cancel");
		const std::string applyStr = tr("common.apply");

		const auto drawSizedGhost = [&](const char* label, float w, bool disabled) -> bool {
			if (disabled)
			{
				ImGui::BeginDisabled();
			}
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.15f)));
			ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
			const bool c = ImGui::Button(label, ImVec2(w, 32.f));
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(5);
			if (disabled)
			{
				ImGui::EndDisabled();
			}
			return c;
		};

		if (drawSizedGhost(backStr.c_str(), btnW, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCloseLanguageOptionsWithoutApply();
		}
		ImGui::SameLine(0.f, 10.f);
		if (drawSizedGhost(cancelStr.c_str(), btnW, !m_optDirty) && m_authPresenter != nullptr)
		{
			PullLanguageOptionsFromPresenter();
		}
		{
			const float applyPosX = ImGui::GetWindowContentRegionMax().x - btnW;
			ImGui::SetCursorPosX(applyPosX);
		}
		if (!m_optDirty)
		{
			ImGui::BeginDisabled();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kAccent));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
		}
		ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(LnTheme::AccentDim(0.25f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(LnTheme::AccentDim(0.35f)));
		ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		const bool applyClick = ImGui::Button(applyStr.c_str(), ImVec2(btnW, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (!m_optDirty)
		{
			ImGui::EndDisabled();
		}
		else
		{
			ImGui::PopStyleVar(1);
			ImGui::PopStyleColor(1);
		}
		if (applyClick)
		{
			submitOptionsMirror();
		}

		ImGui::EndChild();

		// B2/ST6 — ne pas fermer l'overlay si Échap vient d'annuler une capture de
		// rebind (le remap a déjà consommé la touche cette frame).
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !rebindWasActive && m_rebindActionIdx < 0
			&& m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiCloseLanguageOptionsWithoutApply();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && m_optDirty)
		{
			submitOptionsMirror();
		}

		// Decor auth (« THEME DE RACE ») : uniquement hors contexte in-game.
		if (!inGame)
		{
			DrawAuthTweaksPanel(vpW, vpH);
		}
	}

	/// B2/ST4 — Rend l'ecran d'options reutilise EN JEU, independamment de vs.active.
	/// Voir la doc d'en-tete (AuthImGuiRenderer.h) pour le contrat complet (main thread,
	/// frame ImGui ouverte, valeur de retour). Detail d'implementation :
	///  - 1ere frame d'ouverture (m_optionsOverlayWasOpen passe false->true) : on tire les
	///    mirrors d'edition (m_opt*) depuis les *Pending prepares par OpenLanguageOptionsInGame.
	///  - overlay opaque via BeginFullscreenOverlay(.., 1.0f), puis RenderOptionsScreen(inGame=true),
	///    puis ImGui::End() — paire Begin/End equilibree.
	///  - fermeture detectee via le presenter : RenderOptionsScreen appelle
	///    ImGuiCloseLanguageOptionsWithoutApply() (Retour/Echap) qui, en in-game, pose
	///    m_optionsOpenInGame=false ; on relit IsOptionsOpenInGame() pour la valeur de retour.
	bool AuthImGuiRenderer::RenderOptionsOverlay(float vpW, float vpH)
	{
		if (m_authPresenter == nullptr)
		{
			return false;
		}
		// Premiere frame d'ouverture : initialiser les mirrors d'edition depuis les
		// valeurs *Pending (deja preparees par OpenLanguageOptionsInGame au ST3).
		if (!m_optionsOverlayWasOpen)
		{
			PullLanguageOptionsFromPresenter();
			// B2/ST6 — force la relecture des touches remappables depuis la config à
			// chaque ouverture in-game (l'utilisateur a pu les changer au clavier ailleurs).
			m_rebindKeysLoaded = false;
			m_rebindActionIdx = -1;
			m_rebindWarning.clear();
			m_optionsOverlayWasOpen = true;
		}

		// RenderModel minimal : champs par defaut. authOptionsAccountLogin /
		// authOptionsAccountTagId restent vides — l'onglet Account sera masque en jeu (ST6).
		RenderModel rmMinimal{};
		BeginFullscreenOverlay(vpW, vpH, 1.0f);
		RenderOptionsScreen(rmMinimal, vpW, vpH, /*inGame=*/true);
		ImGui::End();

		// Etat d'ouverture cote presenter : true = encore ouvert, false = ferme (Retour/Echap).
		const bool stillOpen = m_authPresenter->IsOptionsOpenInGame();
		if (!stillOpen)
		{
			m_optionsOverlayWasOpen = false;
		}
		return stillOpen;
	}
} // namespace engine::render

#endif
