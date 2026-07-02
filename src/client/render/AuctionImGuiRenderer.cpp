// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — AuctionImGuiRenderer
// implementation.

#include "src/client/render/AuctionImGuiRenderer.h"

#include "src/client/auction/AuctionUi.h"
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

		/// Retourne le steady_clock now en ms (pour les toasts 5s).
		uint64_t SteadyNowMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	void AuctionImGuiRenderer::Render()
	{
		if (m_presenter == nullptr)
			return;
		if (!m_presenter->IsInitialized())
			return;

		// Le panel n'est rendu que si IsEnabled().
		if (m_enabled)
			RenderMainPanel();

		// Les toasts sont rendus independamment.
		RenderBidToast();
		RenderExpiredToast();
	}

	void AuctionImGuiRenderer::RenderMainPanel()
	{
		const auto& state = m_presenter->GetState();

		// Geometrie : panneau ancre droite, 640x720.
		const float panelW = 640.f;
		const float panelH = 720.f;
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
		if (ImGui::Begin("Hotel des ventes (F6)##ln_auction_panel", nullptr, flags))
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

			// Top : filter + Refresh.
			ImGui::TextUnformatted("Filtre :");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.f);
			ImGui::InputInt("##filter_item", &m_inputFilter);
			if (m_inputFilter < 0) m_inputFilter = 0;
			ImGui::SameLine();
			if (ImGui::Button("Rafraichir"))
			{
				m_presenter->RequestList(static_cast<uint32_t>(m_inputFilter));
			}
			ImGui::SameLine();
			if (ImGui::Button("Toutes"))
			{
				m_inputFilter = 0;
				m_presenter->RequestList(0u);
			}
			ImGui::Separator();

			// Middle : table des listings.
			ImGui::TextUnformatted("Encheres actives :");
			if (!state.listingsLoaded)
			{
				ImGui::TextUnformatted("(non chargees)");
			}
			else if (state.listings.empty())
			{
				ImGui::TextWrapped("Aucune enchere correspondante.");
			}
			else
			{
				const float tableHeight = 360.f;
				if (ImGui::BeginChild("##auction_list_child", ImVec2(0.f, tableHeight), true,
					ImGuiWindowFlags_HorizontalScrollbar))
				{
					if (ImGui::BeginTable("##auction_table", 7,
						ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders
						| ImGuiTableFlags_SizingStretchProp))
					{
						ImGui::TableSetupColumn("Objet");
						ImGui::TableSetupColumn("Qte");
						ImGui::TableSetupColumn("Vendeur");
						ImGui::TableSetupColumn("Enchere");
						ImGui::TableSetupColumn("Achat imm.");
						ImGui::TableSetupColumn("Reste");
						ImGui::TableSetupColumn("Action");
						ImGui::TableHeadersRow();

						for (const auto& a : state.listings)
						{
							ImGui::TableNextRow();
							ImGui::PushID(static_cast<int>(a.auctionId & 0x7FFFFFFFu));
							ImGui::TableNextColumn();
							ImGui::Text("%s", a.itemName.c_str());
							ImGui::TableNextColumn();
							ImGui::Text("%u", static_cast<unsigned>(a.count));
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(a.ownerName.c_str());
							ImGui::TableNextColumn();
							const std::string cur = engine::client::FormatCopper(a.currentBidCopper);
							ImGui::TextUnformatted(cur.c_str());
							ImGui::TableNextColumn();
							if (a.buyoutCopper == 0ull)
							{
								ImGui::TextUnformatted("-");
							}
							else
							{
								const std::string buy = engine::client::FormatCopper(a.buyoutCopper);
								ImGui::TextUnformatted(buy.c_str());
							}
							ImGui::TableNextColumn();
							const std::string dur = engine::client::FormatDuration(a.secondsUntilExpiration);
							ImGui::TextUnformatted(dur.c_str());
							ImGui::TableNextColumn();
							if (ImGui::SmallButton("Miser"))
							{
								m_bidTargetAuctionId = a.auctionId;
								m_inputBidAmount = static_cast<int>(std::min<uint64_t>(
									a.currentBidCopper + 1ull, static_cast<uint64_t>(0x7FFFFFFF)));
								m_bidPopupOpen = true;
							}
							ImGui::PopID();
						}
						ImGui::EndTable();
					}
				}
				ImGui::EndChild();
			}

			ImGui::Separator();

			// Bottom : formulaire Post.
			ImGui::TextUnformatted("Poster une enchere :");
			ImGui::SetNextItemWidth(120.f);
			ImGui::InputInt("ID objet##post_item", &m_inputPostItem);
			if (m_inputPostItem < 1) m_inputPostItem = 1;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80.f);
			ImGui::InputInt("Qte##post_count", &m_inputPostCount);
			if (m_inputPostCount < 1) m_inputPostCount = 1;

			ImGui::SetNextItemWidth(140.f);
			ImGui::InputInt("Enchere initiale (cuivre)##post_startbid", &m_inputPostStartBid);
			if (m_inputPostStartBid < 1) m_inputPostStartBid = 1;
			ImGui::SameLine();
			ImGui::SetNextItemWidth(140.f);
			ImGui::InputInt("Achat imm. (cuivre)##post_buyout", &m_inputPostBuyout);
			if (m_inputPostBuyout < 0) m_inputPostBuyout = 0;

			ImGui::TextUnformatted("Duree :");
			ImGui::SameLine();
			ImGui::RadioButton("12h##dur12", &m_inputPostDurationIdx, 0);
			ImGui::SameLine();
			ImGui::RadioButton("24h##dur24", &m_inputPostDurationIdx, 1);
			ImGui::SameLine();
			ImGui::RadioButton("48h##dur48", &m_inputPostDurationIdx, 2);

			if (ImGui::Button("Poster", ImVec2(160.f, 28.f)))
			{
				const uint8_t durationHours = (m_inputPostDurationIdx == 0) ? 12u
					: (m_inputPostDurationIdx == 2) ? 48u : 24u;
				m_presenter->Post(
					static_cast<uint32_t>(m_inputPostItem),
					static_cast<uint32_t>(m_inputPostCount),
					static_cast<uint64_t>(m_inputPostStartBid),
					static_cast<uint64_t>(m_inputPostBuyout),
					durationHours);
			}

			// Popup de confirmation Bid.
			if (m_bidPopupOpen)
			{
				ImGui::OpenPopup("##bid_popup");
				m_bidPopupOpen = false;
			}
			if (ImGui::BeginPopupModal("##bid_popup", nullptr,
				ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
			{
				ImGui::Text("Miser sur enchere #%llu",
					static_cast<unsigned long long>(m_bidTargetAuctionId));
				ImGui::SetNextItemWidth(180.f);
				ImGui::InputInt("Montant (copper)##bid_amount", &m_inputBidAmount);
				if (m_inputBidAmount < 1) m_inputBidAmount = 1;
				ImGui::Separator();
				if (ImGui::Button("Valider", ImVec2(120.f, 0.f)))
				{
					m_presenter->Bid(m_bidTargetAuctionId,
						static_cast<uint64_t>(m_inputBidAmount));
					m_bidTargetAuctionId = 0u;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Annuler", ImVec2(120.f, 0.f)))
				{
					m_bidTargetAuctionId = 0u;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}

	void AuctionImGuiRenderer::RenderBidToast()
	{
		const auto& state = m_presenter->GetState();
		if (!state.lastBidTimeMs.has_value())
			return;

		constexpr uint64_t kToastDurationMs = 5000ull;
		const uint64_t nowSteady = SteadyNowMs();
		if (nowSteady < *state.lastBidTimeMs)
			return;
		const uint64_t age = nowSteady - *state.lastBidTimeMs;
		if (age > kToastDurationMs)
			return;

		char buf[80]{};
		if (state.lastBidWasBuyout)
		{
			std::snprintf(buf, sizeof(buf), "Achat immediat #%llu",
				static_cast<unsigned long long>(state.lastBidAuctionId));
		}
		else
		{
			std::snprintf(buf, sizeof(buf), "Mise placee sur #%llu",
				static_cast<unsigned long long>(state.lastBidAuctionId));
		}

		// Geometrie toast : bottom-right, 320x60, marge 16.
		const float toastW = 320.f;
		const float toastH = 60.f;
		const float margin = 16.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - toastW - margin);
		const float posY = std::max(0.f, vpH - toastH - margin);

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

		if (ImGui::Begin("##ln_auction_bid_toast", nullptr, toastFlags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.45f, 1.f));
			ImGui::TextWrapped("%s", buf);
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}

	void AuctionImGuiRenderer::RenderExpiredToast()
	{
		const auto& state = m_presenter->GetState();
		if (!state.lastExpirationTimeMs.has_value())
			return;

		constexpr uint64_t kToastDurationMs = 5000ull;
		const uint64_t nowSteady = SteadyNowMs();
		if (nowSteady < *state.lastExpirationTimeMs)
			return;
		const uint64_t age = nowSteady - *state.lastExpirationTimeMs;
		if (age > kToastDurationMs)
			return;

		std::string text;
		char buf[160]{};
		if (state.lastExpirationWon)
		{
			const std::string finalAmt = engine::client::FormatCopper(state.lastExpirationFinalBid);
			std::snprintf(buf, sizeof(buf), "Enchere #%llu expiree -- vendue a %s pour %s",
				static_cast<unsigned long long>(state.lastExpirationAuctionId),
				state.lastExpirationWinnerName.empty() ? "?" : state.lastExpirationWinnerName.c_str(),
				finalAmt.c_str());
		}
		else
		{
			std::snprintf(buf, sizeof(buf), "Enchere #%llu expiree sans acheteur",
				static_cast<unsigned long long>(state.lastExpirationAuctionId));
		}
		text = buf;

		// Geometrie toast : bottom-right, decale au-dessus du bid toast.
		const float toastW = 360.f;
		const float toastH = 64.f;
		const float margin = 16.f;
		const float spacing = 76.f;  // au-dessus du bid toast.
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = std::max(0.f, vpW - toastW - margin);
		const float posY = std::max(0.f, vpH - toastH - margin - spacing);

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

		if (ImGui::Begin("##ln_auction_expired_toast", nullptr, toastFlags))
		{
			const ImVec4 color = state.lastExpirationWon
				? ImVec4(0.55f, 0.95f, 0.55f, 1.f)
				: ImVec4(0.85f, 0.6f, 0.45f, 1.f);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}
}

#else // !_WIN32

namespace engine::render
{
	void AuctionImGuiRenderer::Render()             {}
	void AuctionImGuiRenderer::RenderMainPanel()    {}
	void AuctionImGuiRenderer::RenderBidToast()     {}
	void AuctionImGuiRenderer::RenderExpiredToast() {}
}

#endif
