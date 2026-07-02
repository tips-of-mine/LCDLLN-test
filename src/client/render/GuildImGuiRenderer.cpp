// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — GuildImGuiRenderer implementation.

#include "src/client/render/GuildImGuiRenderer.h"

#include "src/client/guild/GuildUi.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;

		/// Couleur de l'indicateur online (vert) / offline (gris).
		ImVec4 OnlineColor(bool online)
		{
			return online ? ImVec4(0.40f, 0.95f, 0.40f, 1.f)
			              : ImVec4(0.55f, 0.55f, 0.60f, 1.f);
		}

		/// Retourne le steady_clock now en ms (pour les toasts 5s).
		uint64_t SteadyNowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	void GuildImGuiRenderer::Render()
	{
		if (m_presenter == nullptr)
			return;
		if (!m_presenter->IsInitialized())
			return;

		// Le panel n'est rendu que si IsEnabled().
		if (m_enabled)
			RenderMainPanel();

		// Le toast est rendu independamment, si un MotdUpdate recent existe.
		RenderToast();
	}

	void GuildImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 560x600.
		const float panelW = 560.f;
		const float panelH = 600.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - panelW - margin);
		const float posY = std::max(0.f, (vpH - panelH) * 0.5f);

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Guildes (F5)##ln_guilds_panel", nullptr, flags))
		{
			// Erreur transitoire (rouge).
			if (!state.lastErrorText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.4f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastErrorText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}
			// Info transitoire (vert leger).
			if (!state.lastInfoText.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.f, 0.4f, 1.f));
				ImGui::TextWrapped("%s", state.lastInfoText.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}

			// Top : liste des guildes.
			ImGui::TextUnformatted("Guildes :");
			ImGui::Separator();

			if (!state.guildsLoaded || state.guilds.empty())
			{
				if (ImGui::Button("Charger les guildes", ImVec2(-FLT_MIN, 28.f)))
					m_presenter->RequestList();
				if (state.guildsLoaded && state.guilds.empty())
				{
					ImGui::TextWrapped("Aucune guilde.");
				}
			}
			else
			{
				const float topHeight = 200.f;
				if (ImGui::BeginChild("##guilds_list_child", ImVec2(0.f, topHeight), true,
					ImGuiWindowFlags_HorizontalScrollbar))
				{
					if (ImGui::BeginTable("##guilds_table", 4,
						ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
						| ImGuiTableFlags_SizingStretchProp))
					{
						ImGui::TableSetupColumn("Nom");
						ImGui::TableSetupColumn("Leader");
						ImGui::TableSetupColumn("Membres");
						ImGui::TableSetupColumn("Action");
						ImGui::TableHeadersRow();

						for (const auto& g : state.guilds)
						{
							ImGui::TableNextRow();
							ImGui::PushID(static_cast<int>(g.guildId));
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(g.name.c_str());
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(g.leaderName.c_str());
							ImGui::TableNextColumn();
							ImGui::Text("%u", static_cast<unsigned>(g.memberCount));
							ImGui::TableNextColumn();
							const bool isSelected = state.selectedGuildId.has_value()
								&& *state.selectedGuildId == g.guildId;
							const char* btnLbl = isSelected ? "Selectionnee" : "Selectionner";
							if (ImGui::SmallButton(btnLbl) && !isSelected)
							{
								m_presenter->SelectGuild(g.guildId);
							}
							ImGui::PopID();
						}
						ImGui::EndTable();
					}
				}
				ImGui::EndChild();

				ImGui::Spacing();
				if (ImGui::Button("Rafraichir", ImVec2(-FLT_MIN, 24.f)))
					m_presenter->RequestList();
			}

			ImGui::Separator();

			// Bottom : 4 tabs detail si une guilde est selectionnee.
			if (!state.selectedGuildId.has_value())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.f));
				ImGui::TextWrapped("Selectionnez une guilde pour voir ses membres, ses permissions, sa banque et son MOTD.");
				ImGui::PopStyleColor();
			}
			else
			{
				const uint32_t selId = *state.selectedGuildId;

				// Resolve nom de la guilde selectionnee.
				const char* selName = "?";
				const char* selMotd = "";
				for (const auto& g : state.guilds)
				{
					if (g.guildId == selId)
					{
						selName = g.name.c_str();
						selMotd = g.motd.c_str();
						break;
					}
				}

				ImGui::Text("Guilde selectionnee : %s (id=%u)", selName, static_cast<unsigned>(selId));
				ImGui::Spacing();

				if (ImGui::BeginTabBar("##guild_detail_tabs", ImGuiTabBarFlags_None))
				{
					// Tab Members.
					if (ImGui::BeginTabItem("Membres"))
					{
						if (!state.membersLoaded)
						{
							ImGui::TextUnformatted("Chargement...");
						}
						else if (state.selectedMembers.empty())
						{
							ImGui::TextUnformatted("Aucun membre.");
						}
						else
						{
							if (ImGui::BeginTable("##members_table", 3,
								ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
								| ImGuiTableFlags_SizingStretchProp))
							{
								ImGui::TableSetupColumn("Nom");
								ImGui::TableSetupColumn("Rang");
								ImGui::TableSetupColumn("Statut");
								ImGui::TableHeadersRow();
								for (const auto& m : state.selectedMembers)
								{
									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::TextUnformatted(m.accountName.c_str());
									ImGui::TableNextColumn();
									ImGui::Text("%u %s", static_cast<unsigned>(m.rankId), m.rankName.c_str());
									ImGui::TableNextColumn();
									ImGui::PushStyleColor(ImGuiCol_Text, OnlineColor(m.online));
									ImGui::TextUnformatted(m.online ? "En ligne" : "Hors ligne");
									ImGui::PopStyleColor();
								}
								ImGui::EndTable();
							}
						}
						ImGui::EndTabItem();
					}

					// Tab Permissions.
					if (ImGui::BeginTabItem("Permissions"))
					{
						if (!state.ranksLoaded)
						{
							ImGui::TextUnformatted("Chargement...");
						}
						else if (state.selectedRanks.empty())
						{
							ImGui::TextUnformatted("Aucun rang.");
						}
						else
						{
							for (const auto& p : state.selectedRanks)
							{
								ImGui::PushID(static_cast<int>(p.rankId));
								ImGui::Text("Rang %u : %s", static_cast<unsigned>(p.rankId), p.rankName.c_str());
								// Chip-list : permissions actives.
								auto perms = engine::client::ExpandPermissionMask(p.mask);
								if (perms.empty())
								{
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.f));
									ImGui::SameLine();
									ImGui::TextUnformatted(" -- aucune --");
									ImGui::PopStyleColor();
								}
								else
								{
									ImGui::Indent();
									for (const auto& s : perms)
									{
										ImGui::SameLine();
										ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.85f, 1.0f, 1.f));
										ImGui::Text("[%s]", s.c_str());
										ImGui::PopStyleColor();
									}
									ImGui::Unindent();
									ImGui::NewLine();
								}
								ImGui::Separator();
								ImGui::PopID();
							}
						}
						ImGui::EndTabItem();
					}

					// Tab Bank (V1 : tab 0 only).
					if (ImGui::BeginTabItem("Banque"))
					{
						ImGui::TextUnformatted("Onglet 0 (V1)");
						ImGui::Separator();
						if (!state.bankLoaded)
						{
							ImGui::TextUnformatted("Chargement...");
						}
						else if (state.selectedBank.empty())
						{
							ImGui::TextUnformatted("Banque vide.");
						}
						else
						{
							if (ImGui::BeginTable("##bank_table", 3,
								ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
								| ImGuiTableFlags_SizingStretchProp))
							{
								ImGui::TableSetupColumn("Slot");
								ImGui::TableSetupColumn("Item");
								ImGui::TableSetupColumn("Quantite");
								ImGui::TableHeadersRow();
								for (const auto& it : state.selectedBank)
								{
									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::Text("%u", static_cast<unsigned>(it.slotIndex));
									ImGui::TableNextColumn();
									ImGui::TextUnformatted(it.itemName.c_str());
									ImGui::TableNextColumn();
									ImGui::Text("%u", static_cast<unsigned>(it.count));
								}
								ImGui::EndTable();
							}
						}
						ImGui::EndTabItem();
					}

					// Tab MOTD.
					if (ImGui::BeginTabItem("MOTD"))
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.f));
						ImGui::TextWrapped("Message du jour :");
						ImGui::PopStyleColor();
						ImGui::Spacing();
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.7f, 1.f));
						ImGui::TextWrapped("%s", *selMotd ? selMotd : "(vide)");
						ImGui::PopStyleColor();
						ImGui::EndTabItem();
					}

					ImGui::EndTabBar();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void GuildImGuiRenderer::RenderToast()
	{
		const auto& state = m_presenter->GetState();
		if (!state.lastMotdChangeTimeMs.has_value())
			return;

		// Toast actif 5s apres reception.
		constexpr uint64_t kToastDurationMs = 5000ull;
		const uint64_t nowSteady = SteadyNowMs();
		if (nowSteady < *state.lastMotdChangeTimeMs)
			return;
		const uint64_t age = nowSteady - *state.lastMotdChangeTimeMs;
		if (age > kToastDurationMs)
			return;

		// Cherche le nom de la guilde dans le cache. Fallback "Guilde N".
		std::string guildName;
		for (const auto& g : state.guilds)
		{
			if (g.guildId == state.lastMotdGuildId)
			{
				guildName = g.name;
				break;
			}
		}
		if (guildName.empty())
		{
			char buf[32]{};
			std::snprintf(buf, sizeof(buf), "Guilde %u",
				static_cast<unsigned>(state.lastMotdGuildId));
			guildName = buf;
		}

		std::string text = guildName + " : nouveau MOTD";

		// Geometrie toast : bottom-right, 320x60, marge 16.
		const float toastW = 320.f;
		const float toastH = 60.f;
		const float margin = 16.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - toastW - margin);
		const float posY = std::max(0.f, vpH - toastH - margin);

		// Fade out sur les 1000 dernieres ms.
		float alpha = 0.95f;
		if (age > kToastDurationMs - 1000ull)
		{
			const float remain = static_cast<float>(kToastDurationMs - age) / 1000.0f;
			alpha = std::max(0.0f, std::min(0.95f, remain * 0.95f));
		}

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(toastW, toastH), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(alpha);

		const ImGuiWindowFlags toastFlags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoInputs;

		if (ImGui::Begin("##ln_guilds_toast", nullptr, toastFlags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1.f));
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}
}

#else // !_WIN32

namespace engine::render
{
	void GuildImGuiRenderer::Render()           {}
	void GuildImGuiRenderer::RenderMainPanel()  {}
	void GuildImGuiRenderer::RenderToast()      {}
}

#endif
