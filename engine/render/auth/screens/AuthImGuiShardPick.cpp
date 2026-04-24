// AUTH-UI.8 — rendu ImGui écran de sélection du serveur de jeu (shard) avec liste détaillée et indicateurs de charge (split depuis AuthImGuiRenderer.cpp).
// Contient les helpers d'extraction d'hôte/initiale depuis l'endpoint et RenderShardScreen (liste scrollable de shards avec barre de charge, statut et bouton Entrer).

#include "engine/render/AuthImGuiRenderer.h"
#include "engine/render/LnTheme.h"

#include "engine/client/LocalizationService.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Convertit une couleur LnTheme::Rgba en ImVec4 pour les appels de style ImGui.
		ImVec4 IV(const LnTheme::Rgba& c)
		{
			return ImVec4(c.r, c.g, c.b, c.a);
		}

		/// Convertit une couleur LnTheme::Rgba en ImU32 pour les appels de draw list ImGui.
		ImU32 U32(const LnTheme::Rgba& c)
		{
			return ImGui::ColorConvertFloat4ToU32(IV(c));
		}

		/// Extrait la partie hôte d'un endpoint "host:port" ; retourne l'endpoint brut si aucun ':' n'est trouvé.
		std::string ShardEndpointHost(const std::string& endpoint)
		{
			if (endpoint.empty())
			{
				return {};
			}
			const auto colon = endpoint.find(':');
			return (colon == std::string::npos) ? endpoint : endpoint.substr(0u, colon);
		}

		/// Retourne la première lettre alphabétique (majuscule) de l'hôte d'un endpoint, utilisée comme avatar textuel dans la carte de shard.
		char ShardInitialFromEndpoint(const std::string& endpoint)
		{
			const std::string host = ShardEndpointHost(endpoint);
			const std::string& scan = host.empty() ? endpoint : host;
			for (unsigned char c : scan)
			{
				if (std::isalpha(c) != 0)
				{
					return static_cast<char>(std::toupper(c));
				}
			}
			return '?';
		}
	} // namespace

	/// Affiche l'écran de sélection du shard : liste scrollable des serveurs avec nom, endpoint, barre de charge, ping et statut, puis boutons Retour et Entrer dans le monde.
	void AuthImGuiRenderer::RenderShardScreen(const RenderModel& rm, float vpW, float vpH)
	{
		using P = engine::client::LocalizationService::Params;
		auto tr = [this](const char* key, const P& p = {}) -> std::string {
			if (m_authPresenter == nullptr)
			{
				return std::string(key);
			}
			std::string s = m_authPresenter->UiTranslate(key, p);
			return s.empty() ? std::string(key) : s;
		};

		const std::vector<std::string> breadcrumb = {tr("auth.shard_pick.breadcrumb.account"), tr("auth.shard_pick.breadcrumb.realm"),
			tr("auth.shard_pick.breadcrumb.character"), tr("auth.shard_pick.breadcrumb.enter")};
		DrawBreadcrumb(breadcrumb, 1);

		const std::string titleStr = rm.sectionTitle.empty() ? tr("auth.shard_pick.panel_title") : rm.sectionTitle;
		const std::string subStr = tr("auth.shard_pick.panel_subtitle");

		uint32_t choice = 0u;
		static const std::vector<engine::network::ServerListEntry> kEmptyShardList{}; ///< Valeur vide statique utilisée en repli quand le presenter est absent.
		const std::vector<engine::network::ServerListEntry>& entries =
			m_authPresenter != nullptr ? m_authPresenter->ShardPickEntries() : kEmptyShardList;
		if (m_authPresenter != nullptr)
		{
			choice = m_authPresenter->ShardPickChoiceShardId();
		}

		size_t onlineCount = 0u;
		for (const auto& e : entries)
		{
			if (e.status == 1u || e.status == 2u)
			{
				++onlineCount;
			}
		}
		const std::string verStr = tr("auth.shard_pick.version_online",
			P{{"online", std::to_string(onlineCount)}, {"total", std::to_string(entries.size())}});

		if (!BeginPanel(820.f, vpW, vpH, std::string_view(titleStr), std::string_view(subStr), std::string_view(verStr), true, false))
		{
			EndPanel();
			return;
		}

		const std::string infoStr = tr("auth.shard_pick.footer_info");
		if (!infoStr.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 760.f);
			ImGui::TextWrapped("%s", infoStr.c_str());
			ImGui::PopTextWrapPos();
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		const float listH = (std::max)(200.f, vpH * 0.42f);
		ImGui::BeginChild("##shard_scroll", ImVec2(-FLT_MIN, listH), true, ImGuiWindowFlags_None);

		if (entries.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::TextWrapped("%s", tr("auth.shard_pick.none_online").c_str());
			ImGui::PopStyleColor();
		}
		else
		{
			constexpr float kFlag = 50.f;
			constexpr float kLoadW = 200.f;
			constexpr float kPingW = 112.f;
			constexpr float kRowH = 114.f;

			for (const auto& e : entries)
			{
				const bool rowSelectable = (e.status == 1u && !e.endpoint.empty());
				const bool isSelected = (choice == e.shard_id);
				const float loadFrac = e.max_capacity > 0u
					? static_cast<float>(e.current_load) / static_cast<float>(e.max_capacity)
					: 0.f;
				const int pct = static_cast<int>(std::lround(loadFrac * 100.f));
				const bool saturated = rowSelectable && loadFrac > 0.85f;

				enum class RowVis : uint8_t
				{
					Offline,
					Saturated,
					Online
				};
				RowVis vis = RowVis::Offline;
				if (rowSelectable)
				{
					vis = saturated ? RowVis::Saturated : RowVis::Online;
				}
				else if (e.status == 2u)
				{
					vis = RowVis::Saturated;
				}

				const std::string host = ShardEndpointHost(e.endpoint);
				std::string nameUpper = host.empty()
					? tr("auth.shard_pick.name_fallback", P{{"id", std::to_string(e.shard_id)}})
					: host;
				if (!host.empty())
				{
					for (char& ch : nameUpper)
					{
						if (ch >= 'a' && ch <= 'z')
						{
							ch = static_cast<char>(ch - 'a' + 'A');
						}
					}
				}
				const char initialBuf[4] = {ShardInitialFromEndpoint(e.endpoint), '\0', '\0', '\0'};
				const std::string descLine =
					e.endpoint.empty() ? tr("auth.shard_pick.desc_offline") : e.endpoint;

				const ImVec4 borderCol = isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder);
				const float dim = (rowSelectable || e.status == 2u) ? 1.f : 0.48f;

				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * dim);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, isSelected ? IV(LnTheme::AccentDim(0.1f)) : IV(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);
				char rowId[40];
				std::snprintf(rowId, sizeof(rowId), "##shard%u", e.shard_id);
				ImGui::BeginChild(rowId, ImVec2(-FLT_MIN, kRowH), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(3);
				ImGui::PopStyleColor(2);

				const float innerW = ImGui::GetContentRegionAvail().x;
				const float textW = (std::max)(120.f, innerW - kFlag - 18.f - kLoadW - kPingW - 16.f);

				ImGui::BeginGroup();
				const ImVec2 flagP0 = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(kFlag, kFlag));
				const ImVec2 flagP1 = ImGui::GetItemRectMax();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(flagP0, flagP1, U32(LnTheme::kPanel), 4.f);
				dl->AddRect(flagP0, flagP1, isSelected ? U32(LnTheme::kAccent) : U32(LnTheme::kBorder), 4.f, 0, 1.5f);
				{
					const ImVec2 ts = ImGui::CalcTextSize(initialBuf);
					dl->AddText(ImVec2(flagP0.x + (kFlag - ts.x) * 0.5f, flagP0.y + (kFlag - ts.y) * 0.5f), U32(LnTheme::kText), initialBuf);
				}
				ImGui::SameLine(0.f, 14.f);
				ImGui::BeginGroup();
				ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
				ImGui::SetWindowFontScale(1.05f);
				ImGui::TextUnformatted(nameUpper.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.82f);
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textW);
				ImGui::TextWrapped("%s", descLine.c_str());
				ImGui::PopTextWrapPos();
				ImGui::SetWindowFontScale(0.76f);
				const std::string modeLine = tr("auth.shard_pick.mode_default");
				ImGui::TextUnformatted(modeLine.c_str());
				if (e.character_count > 0u)
				{
					const std::string ev = tr("auth.shard_pick.event_characters",
						P{{"count", std::to_string(e.character_count)}});
					ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kAccent));
					ImGui::TextUnformatted(ev.c_str());
					ImGui::PopStyleColor();
				}
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::EndGroup();
				ImGui::EndGroup();

				ImGui::SameLine(0.f, 10.f);
				ImGui::BeginGroup();
				ImGui::Dummy(ImVec2(kLoadW, 1.f));
				const std::string loadLbl = tr("auth.shard_pick.load_line", P{{"percent", std::to_string(pct)}});
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.72f);
				ImGui::TextUnformatted(loadLbl.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				const LnTheme::Rgba barCol = (vis == RowVis::Offline) ? LnTheme::kMuted
					: (vis == RowVis::Saturated) ? LnTheme::kWarning : LnTheme::kSuccess;
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IV(barCol));
				ImGui::ProgressBar(loadFrac, ImVec2(kLoadW, 6.f), "");
				ImGui::PopStyleColor();
				const std::string pl = tr("auth.shard_pick.players",
					P{{"current", std::to_string(e.current_load)}, {"max", std::to_string(e.max_capacity)}});
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.78f);
				ImGui::TextUnformatted(pl.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::EndGroup();

				ImGui::SameLine(0.f, 10.f);
				ImGui::BeginGroup();
				ImGui::Dummy(ImVec2(kPingW, 1.f));
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.85f);
				ImGui::TextUnformatted(tr("auth.shard_pick.latency_na").c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				const char* statKey = (vis == RowVis::Online) ? "auth.shard_pick.status_online"
					: (vis == RowVis::Saturated) ? "auth.shard_pick.status_saturated" : "auth.shard_pick.status_offline";
				const LnTheme::Rgba stCol =
					(vis == RowVis::Online) ? LnTheme::kSuccess : (vis == RowVis::Saturated) ? LnTheme::kWarning : LnTheme::kErrorCol;
				ImGui::PushStyleColor(ImGuiCol_Text, IV(stCol));
				ImGui::SetWindowFontScale(0.78f);
				ImGui::TextUnformatted(tr(statKey).c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::EndGroup();

				ImGui::SetCursorPos(ImVec2(0.f, 0.f));
				char invId[48];
				std::snprintf(invId, sizeof(invId), "##sinv%u", e.shard_id);
				ImGui::InvisibleButton(invId, ImVec2(ImGui::GetWindowWidth() - 8.f, kRowH));
				if (ImGui::IsItemClicked() && rowSelectable && m_authPresenter != nullptr)
				{
					m_authPresenter->ImGuiSetShardPickChoiceShardId(e.shard_id);
				}

				ImGui::EndChild();
				ImGui::Spacing();
				ImGui::PopStyleVar(1);
			}
		}

		ImGui::EndChild();
		ImGui::Spacing();

		const bool canEnter = (m_authPresenter != nullptr && m_authPresenter->ShardPickChoiceShardId() != 0u);
		const float backW = 148.f;
		const float enterW = 228.f;
		const std::string backStr = tr("auth.shard_pick.button_back");
		const std::string enterStr = tr("auth.shard_pick.enter_world");

		/// Dessine un bouton fantôme (bordure fine, fond transparent) de largeur fixée ; gère l'état désactivé.
		auto drawSizedGhost = [&](const char* label, float w, bool disabled) -> bool {
			if (disabled)
			{
				ImGui::BeginDisabled();
			}
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.08f)));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.15f)));
			ImGui::PushStyleColor(ImGuiCol_Border, IV(LnTheme::kBorder));
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
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

		if (drawSizedGhost(backStr.c_str(), backW, false) && m_authPresenter != nullptr)
		{
			m_authPresenter->ImGuiBackFromShardPickToLogin();
		}
		ImGui::SameLine(0.f, 14.f);
		{
			const std::string navRow = tr("auth.shard_pick.hint_nav_row");
			ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
			ImGui::SetWindowFontScale(0.82f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(navRow.c_str());
			ImGui::SetWindowFontScale(1.f);
			ImGui::PopStyleColor();
		}
		ImGui::SameLine(0.f, 0.f);
		ImGui::Dummy(ImVec2((std::max)(0.f, ImGui::GetContentRegionAvail().x - enterW - 4.f), 1.f));
		ImGui::SameLine(0.f, 0.f);
		if (!canEnter)
		{
			ImGui::BeginDisabled();
		}
		ImGui::PushStyleColor(ImGuiCol_Button, IV(LnTheme::kPrimary));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IV(LnTheme::AccentDim(0.25f)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, IV(LnTheme::AccentDim(0.35f)));
		ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kText));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
		const bool enterClick = ImGui::Button(enterStr.c_str(), ImVec2(enterW, 32.f));
		ImGui::PopStyleVar(1);
		ImGui::PopStyleColor(4);
		if (!canEnter)
		{
			ImGui::EndDisabled();
		}
		if (enterClick && m_authPresenter != nullptr && m_authCfg != nullptr)
		{
			m_authPresenter->ImGuiSubmitShardPick(*m_authCfg);
		}

		EndPanel();

		DrawAuthTweaksPanel(vpW, vpH);
	}
} // namespace engine::render

#endif
