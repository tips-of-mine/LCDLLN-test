// AUTH-UI.8 - rendu ImGui ecran de selection du serveur de jeu (shard) avec liste detaillee et indicateurs de charge (split depuis AuthImGuiRenderer.cpp).
// Contient les helpers d'extraction d'hote/initiale depuis l'endpoint et RenderShardScreen (liste scrollable de shards avec barre de charge, statut et bouton Entrer).

#include "src/client/render/AuthImGuiRenderer.h"
#include "src/client/render/LnTheme.h"

#include "src/client/localization/LocalizationService.h"
#include "src/shared/network/ServerListPayloads.h"

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

		/// Retourne la premiere lettre alphabetique (majuscule) d'une chaine (nom public
		/// du serveur), utilisee comme avatar textuel dans la carte de shard. '?' si aucune.
		/// On n'utilise PLUS l'endpoint ici : l'IP/port ne doit pas transparaitre dans l'UI.
		char ShardInitialFromName(const std::string& name)
		{
			for (unsigned char c : name)
			{
				if (std::isalpha(c) != 0)
				{
					return static_cast<char>(std::toupper(c));
				}
			}
			return '?';
		}
	} // namespace

	/// Affiche l'ecran de selection du shard : liste scrollable des serveurs avec nom, endpoint, barre de charge, ping et statut, puis boutons Retour et Entrer dans le monde.
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

		// Breadcrumb " 01 COMPTE > 02 ROYAUME > ... " retire : il etait dessine en absolu
		// en haut a gauche et apparaissait comme un texte parasite hors de la stage centrale.
		// Le panel principal et son badge " 1/2 / 2/2 " suffisent a indiquer l'etape.

		// Titre/sous-titre via helper unifie (reference visuelle).
		DrawAuthBigTitle(rm, vpW, vpH, "shard");
		const float titleZoneW = vpW * 0.96f;

		const std::string titleStr = rm.sectionTitle.empty() ? tr("auth.shard_pick.panel_title") : rm.sectionTitle;
		const std::string subStr = tr("auth.shard_pick.panel_subtitle");

		uint32_t choice = 0u;
		static const std::vector<engine::network::ServerListEntry> kEmptyShardList{}; ///< Valeur vide statique utilisee en repli quand le presenter est absent.
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

		if (!BeginPanel(820.f, titleZoneW, vpH, std::string_view(titleStr), std::string_view(subStr), std::string_view(verStr), true, false))
		{
			EndPanel();
			ImGui::EndChild();
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
			constexpr float kFlag = 68.f;
			constexpr float kLoadW = 260.f;
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

				// Nom public du serveur (texte) — remplace l'adresse IP en jaune. On
				// n'affiche NI l'IP NI le port (information sensible) dans la liste.
				const bool hasDisplayName = !e.display_name.empty();
				std::string nameUpper = hasDisplayName
					? e.display_name
					: tr("auth.shard_pick.name_fallback", P{{"id", std::to_string(e.shard_id)}});
				for (char& ch : nameUpper)
				{
					if (ch >= 'a' && ch <= 'z')
					{
						ch = static_cast<char>(ch - 'a' + 'A');
					}
				}
				const char initialBuf[4] = {ShardInitialFromName(nameUpper), '\0', '\0', '\0'};
				// Sous-titre : pour un serveur hors-ligne on garde le message de maintenance ;
				// sinon on n'affiche aucune adresse (descLine vide => ligne omise plus bas).
				const std::string descLine =
					(e.endpoint.empty() && e.status != 1u && e.status != 2u)
						? tr("auth.shard_pick.desc_offline")
						: std::string{};

				const ImVec4 borderCol = isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kBorder);
				const float dim = (rowSelectable || e.status == 2u) ? 1.f : 0.48f;

				// On pousse 3 StyleVar (Alpha + ChildRounding + ChildBorderSize) et 2 StyleColor
				// (ChildBg + Border) avant BeginChild. Seuls ChildRounding/ChildBorderSize/ChildBg/Border
				// servent au cadre de la fenetre enfant : on peut les pop juste apres BeginChild. L'Alpha
				// doit rester actif pour DIM le CONTENU de la ligne - on la pop apres EndChild (l. ~292).
				// Avant : on poppait les 3 StyleVar ici ET 1 StyleVar apres EndChild > assertion ImGui
				// " PopStyleVar() too many times ".
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * dim);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, isSelected ? IV(LnTheme::AccentDim(0.1f)) : IV(LnTheme::kSurface));
				ImGui::PushStyleColor(ImGuiCol_Border, borderCol);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
				ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isSelected ? 2.f : 1.f);
				char rowId[40];
				std::snprintf(rowId, sizeof(rowId), "##shard%u", e.shard_id);
				ImGui::BeginChild(rowId, ImVec2(-FLT_MIN, kRowH), true, ImGuiWindowFlags_NoScrollbar);
				ImGui::PopStyleVar(2);
				ImGui::PopStyleColor(2);

				// Geometrie de la ligne (V3) : drapeau agrandi + colonnes centrees
				// verticalement dans la hauteur de cellule. Chaque colonne est placee en
				// absolu (SetCursorPos) au lieu de SameLine : cela permet un centrage
				// vertical independant par colonne et d'ancrer le statut a droite.
				const ImVec2 cellStart = ImGui::GetCursorPos();
				const float availW = ImGui::GetContentRegionAvail().x;
				const float availH = ImGui::GetContentRegionAvail().y;
				const float fs = ImGui::GetFontSize();
				const float sp = ImGui::GetStyle().ItemSpacing.y;

				constexpr float kGapFlagName = 16.f;
				constexpr float kGapNameLoad = 32.f;   ///< Espace nom <-> bloc charge.
				constexpr float kGapLoadStatus = 32.f; ///< Espace bloc charge <-> statut.
				constexpr float kStatusW = 120.f;
				constexpr float kPadR = 20.f;          ///< Marge du statut au bord droit.
				constexpr float kBarH = 12.f;          ///< Hauteur de la barre de charge (agrandie).
				constexpr float kInitFontPx = 32.f;    ///< Taille de l'initiale dans le drapeau.

				// Bornes X des colonnes : statut ancre a droite, charge a sa gauche,
				// nom occupe l'espace restant.
				const float flagX = cellStart.x;
				const float nameX = flagX + kFlag + kGapFlagName;
				const float statusX = cellStart.x + availW - kPadR - kStatusW;
				const float loadX = statusX - kGapLoadStatus - kLoadW;
				const float nameW = (std::max)(120.f, loadX - kGapNameLoad - nameX);

				// Offset Y de centrage vertical pour un bloc de hauteur \p blockH.
				auto centeredY = [&](float blockH) {
					return cellStart.y + (std::max)(0.f, (availH - blockH) * 0.5f);
				};

				// --- Drapeau : carre 68x68 + initiale agrandie, centre verticalement ----
				ImGui::SetCursorPos(ImVec2(flagX, centeredY(kFlag)));
				const ImVec2 flagP0 = ImGui::GetCursorScreenPos();
				const ImVec2 flagP1 = ImVec2(flagP0.x + kFlag, flagP0.y + kFlag);
				ImDrawList* dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(flagP0, flagP1, U32(LnTheme::kPanel), 6.f);
				dl->AddRect(flagP0, flagP1, isSelected ? U32(LnTheme::kAccent) : U32(LnTheme::kBorder), 6.f, 0, 1.5f);
				{
					ImFont* font = ImGui::GetFont();
					const ImVec2 ts = font->CalcTextSizeA(kInitFontPx, FLT_MAX, 0.f, initialBuf);
					dl->AddText(font, kInitFontPx,
						ImVec2(flagP0.x + (kFlag - ts.x) * 0.5f, flagP0.y + (kFlag - ts.y) * 0.5f),
						U32(LnTheme::kText), initialBuf);
				}

				// --- Colonne nom (nom + [desc] + mode + [event]), centree verticalement -
				float nameBlockH = fs * 1.05f;
				if (!descLine.empty())
					nameBlockH += sp + fs * 0.82f;
				nameBlockH += sp + fs * 0.76f;
				if (e.character_count > 0u)
					nameBlockH += sp + fs * 0.76f;
				ImGui::SetCursorPos(ImVec2(nameX, centeredY(nameBlockH)));
				ImGui::BeginGroup();
				ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? IV(LnTheme::kAccent) : IV(LnTheme::kText));
				ImGui::SetWindowFontScale(1.05f);
				ImGui::TextUnformatted(nameUpper.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				if (!descLine.empty())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
					ImGui::SetWindowFontScale(0.82f);
					ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + nameW);
					ImGui::TextWrapped("%s", descLine.c_str());
					ImGui::PopTextWrapPos();
					ImGui::SetWindowFontScale(1.f);
					ImGui::PopStyleColor();
				}
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.76f);
				// Ligne « MODE  REGLE » construite a partir des enums (game_mode + ruleset),
				// localisee. Remplace le texte « PvE  COOPERATIVE » fige. tr() prend un
				// const char* -> on materialise les cles avant d'appeler .c_str().
				const std::string modeKey = std::string("auth.shard_pick.mode.") + std::string(engine::network::GameModeToken(e.game_mode));
				const std::string rulesetKey = std::string("auth.shard_pick.ruleset.") + std::string(engine::network::RulesetToken(e.ruleset));
				const std::string modeLine = tr(modeKey.c_str()) + "  " + tr(rulesetKey.c_str());
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

				// --- Colonne charge (label + barre 260x12 + ratio), centree verticalement
				const float loadBlockH = fs * 0.72f + sp + kBarH + sp + fs * 0.78f;
				ImGui::SetCursorPos(ImVec2(loadX, centeredY(loadBlockH)));
				ImGui::BeginGroup();
				const std::string loadLbl = tr("auth.shard_pick.load_line", P{{"percent", std::to_string(pct)}});
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.72f);
				ImGui::TextUnformatted(loadLbl.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				const LnTheme::Rgba barCol = (vis == RowVis::Offline) ? LnTheme::kMuted
					: (vis == RowVis::Saturated) ? LnTheme::kWarning : LnTheme::kSuccess;
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IV(barCol));
				ImGui::ProgressBar(loadFrac, ImVec2(kLoadW, kBarH), "");
				ImGui::PopStyleColor();
				const std::string pl = tr("auth.shard_pick.players",
					P{{"current", std::to_string(e.current_load)}, {"max", std::to_string(e.max_capacity)}});
				ImGui::PushStyleColor(ImGuiCol_Text, IV(LnTheme::kMuted));
				ImGui::SetWindowFontScale(0.78f);
				ImGui::TextUnformatted(pl.c_str());
				ImGui::SetWindowFontScale(1.f);
				ImGui::PopStyleColor();
				ImGui::EndGroup();

				// --- Colonne statut (latence + etat), ancree a droite, centree ----------
				const float statusBlockH = fs * 0.85f + sp + fs * 0.78f;
				ImGui::SetCursorPos(ImVec2(statusX, centeredY(statusBlockH)));
				ImGui::BeginGroup();
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

		/// Dessine un bouton fantome (bordure fine, fond transparent) de largeur fixee ; gere l'etat desactive.
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
		ImGui::EndChild();

		DrawAuthTweaksPanel(vpW, vpH);
	}
} // namespace engine::render

#endif
