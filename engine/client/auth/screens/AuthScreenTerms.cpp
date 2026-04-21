#include "engine/client/AuthUi.h"

#include "engine/network/NetClient.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/network/RequestResponseDispatcher.h"
#include "engine/network/TermsPayloads.h"
#include "engine/platform/Input.h"
#include "engine/platform/Window.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace engine::client
{
#if defined(_WIN32)

	void AuthUiPresenter::ImGuiNotifyTermsScrollReachedBottom(bool reached)
	{
		if (m_phase != Phase::Terms)
		{
			return;
		}
		m_termsScrolledToBottom = reached;
	}

	void AuthUiPresenter::ImGuiSetTermsAcknowledgeChecked(bool on)
	{
		if (m_phase != Phase::Terms)
		{
			return;
		}
		m_termsAcknowledgeChecked = on;
	}

	void AuthUiPresenter::ImGuiTermsPrimaryClick(const engine::core::Config& cfg)
	{
		if (m_phase != Phase::Terms)
		{
			return;
		}
		SubmitCurrentPhase(cfg);
	}

	void AuthUiPresenter::ImGuiTermsDecline(engine::platform::Window& window)
	{
		window.RequestClose();
	}

	void AuthUiPresenter::BuildModel_Terms(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.panel.terms");
		auto pushBody = [this, &model](std::string text, bool active = false, bool link = false, bool asCheckbox = false,
			bool checkboxChecked = false) {
			RenderBodyLine line{};
			line.text = std::move(text);
			line.active = active;
			line.link = link;
			line.checkbox = asCheckbox;
			line.checkboxChecked = checkboxChecked;
			line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
			model.bodyLines.push_back(std::move(line));
		};
		pushBody(Tr("auth.panel.edition") + " " + std::to_string(m_pendingTermsEditionId));
		pushBody(Tr("auth.panel.version") + " " + m_termsVersionLabel);
		pushBody(Tr("auth.panel.title") + " " + m_termsTitle);
		pushBody(Tr("auth.panel.language") + " " + m_termsLocale);
		{
			const size_t start = static_cast<size_t>(std::min<uint32_t>(m_termsScrollOffset, static_cast<uint32_t>(m_termsContent.size())));
			const size_t count = std::min<size_t>(900u, m_termsContent.size() - start);
			pushBody(std::string(m_termsContent.data() + start, count));
		}
		pushBody(m_termsScrolledToBottom ? Tr("auth.panel.end_reached") : Tr("auth.panel.end_not_reached"));
		pushBody(m_termsAcknowledgeChecked ? Tr("auth.panel.accept_checked") : Tr("auth.panel.accept_unchecked"), true, false, true,
			m_termsAcknowledgeChecked);
		{
			RenderAction action{};
			action.labelKey = "auth.hint.terms.accept";
			action.primary = true;
			action.active = true;
			action.emphasized = false;
			action.hovered = m_hoveredActionIndex == 0;
			model.actions.push_back(std::move(action));
		}
	}

	void AuthUiPresenter::Update_Terms(engine::platform::Input& input, const engine::core::Config&, engine::platform::Window&,
		bool usingNativeAuth, bool authUiImguiMode)
	{
		if (usingNativeAuth || authUiImguiMode || m_phase != Phase::Terms)
		{
			return;
		}
		if (!m_initialized || m_flowComplete || !m_authEnabled || m_viewportW == 0u || m_viewportH == 0u)
		{
			return;
		}
		if (input.MouseScrollDelta() != 0)
		{
			const int scrollDir = input.MouseScrollDelta() > 0 ? -24 : 24;
			const int next = static_cast<int>(m_termsScrollOffset) + scrollDir;
			m_termsScrollOffset = static_cast<uint32_t>(std::max(0, next));
		}
		const uint32_t kStep = 12u;
		if (input.WasPressed(engine::platform::Key::Down))
		{
			m_termsScrollOffset += kStep;
		}
		if (input.WasPressed(engine::platform::Key::PageDown))
		{
			m_termsScrollOffset += kStep * 2u;
		}
		if (input.WasPressed(engine::platform::Key::Up))
		{
			m_termsScrollOffset = (m_termsScrollOffset > kStep) ? (m_termsScrollOffset - kStep) : 0u;
		}
		if (input.WasPressed(engine::platform::Key::PageUp))
		{
			m_termsScrollOffset = (m_termsScrollOffset > (kStep * 2u)) ? (m_termsScrollOffset - (kStep * 2u)) : 0u;
		}
		const uint32_t visibleChars = 900u;
		if (m_termsTotalLength <= visibleChars || m_termsScrollOffset + visibleChars >= m_termsTotalLength)
		{
			m_termsScrolledToBottom = true;
		}
		if (m_termsScrolledToBottom && input.WasPressed(engine::platform::Key::Space))
		{
			m_termsAcknowledgeChecked = !m_termsAcknowledgeChecked;
		}
	}

	void AuthUiPresenter::StartTermsStatusWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0)
		{
			EnterAuthErrorPhase(Phase::Terms, Tr("auth.error.terms_session_inactive"));
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string locale = CurrentLocale();

		m_pendingAsyncKind = AsyncKind::TermsStatus;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs, locale]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			disp.SetSessionId(sessionId);
			bool statusDone = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeTermsStatusRequest, engine::network::BuildTermsStatusRequestPayload(locale),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						statusDone = true;
						if (timeout)
						{
							errMsg = "TERMS status timeout.";
							return;
						}
						auto terms = engine::network::ParseTermsStatusResponsePayload(pl.data(), pl.size());
						if (!terms)
						{
							errMsg = "TERMS status parse failed.";
							return;
						}
						local.termsPendingCount = terms->pending_count;
						local.termsEditionId = terms->next_edition_id;
						local.termsTitle = terms->title;
						local.termsVersionLabel = terms->version_label;
						local.termsLocale = terms->resolved_locale;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send TERMS_STATUS failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!statusDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!statusDone)
			{
				local.ready = true;
				local.message = "TERMS status timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			if (local.termsPendingCount > 0 && local.termsEditionId != 0)
			{
				bool contentDone = false;
				if (!disp.SendRequest(engine::network::kOpcodeTermsContentRequest,
						engine::network::BuildTermsContentRequestPayload(local.termsEditionId, local.termsLocale, 0u, 8192u),
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							contentDone = true;
							if (timeout)
							{
								errMsg = "TERMS content timeout.";
								return;
							}
							auto content = engine::network::ParseTermsContentResponsePayload(pl.data(), pl.size());
							if (!content)
							{
								errMsg = "TERMS content parse failed.";
								return;
							}
							local.totalLength = content->total_length;
							local.termsContent = content->chunk;
						},
						timeoutMs))
				{
					local.ready = true;
					local.message = "Send TERMS_CONTENT failed.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!contentDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				if (!contentDone)
				{
					local.ready = true;
					local.message = errMsg.empty() ? "TERMS content timeout." : errMsg;
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					return;
				}
			}
			local.ready = true;
			local.success = true;
			local.message = local.termsPendingCount > 0 ? "Please review and accept the pending terms." : "No pending terms.";
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartTermsAcceptWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0 || m_pendingTermsEditionId == 0)
		{
			EnterAuthErrorPhase(Phase::Terms, Tr("auth.error.terms_session_inactive"));
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const std::string locale = CurrentLocale();
		const uint64_t editionId = m_pendingTermsEditionId;

		m_pendingAsyncKind = AsyncKind::TermsAccept;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs, locale, editionId]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			disp.SetSessionId(sessionId);
			bool acceptDone = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeTermsAcceptRequest, engine::network::BuildTermsAcceptRequestPayload(editionId, 1u),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						acceptDone = true;
						if (timeout)
						{
							errMsg = "TERMS accept timeout.";
							return;
						}
						if (!pl.empty() && pl[0] == 0)
						{
							errMsg = "TERMS accept failed.";
						}
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send TERMS_ACCEPT failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!acceptDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!acceptDone || !errMsg.empty())
			{
				local.ready = true;
				local.message = errMsg.empty() ? "TERMS accept timeout." : errMsg;
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			bool statusDone = false;
			if (!disp.SendRequest(engine::network::kOpcodeTermsStatusRequest, engine::network::BuildTermsStatusRequestPayload(locale),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						statusDone = true;
						if (timeout)
						{
							errMsg = "TERMS status timeout.";
							return;
						}
						auto terms = engine::network::ParseTermsStatusResponsePayload(pl.data(), pl.size());
						if (!terms)
						{
							errMsg = "TERMS status parse failed.";
							return;
						}
						local.termsPendingCount = terms->pending_count;
						local.termsEditionId = terms->next_edition_id;
						local.termsTitle = terms->title;
						local.termsVersionLabel = terms->version_label;
						local.termsLocale = terms->resolved_locale;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send TERMS_STATUS failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!statusDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!statusDone)
			{
				local.ready = true;
				local.message = "TERMS status timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			if (local.termsPendingCount > 0 && local.termsEditionId != 0)
			{
				bool contentDone = false;
				if (!disp.SendRequest(engine::network::kOpcodeTermsContentRequest,
						engine::network::BuildTermsContentRequestPayload(local.termsEditionId, local.termsLocale, 0u, 8192u),
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							contentDone = true;
							if (timeout)
							{
								errMsg = "TERMS content timeout.";
								return;
							}
							auto content = engine::network::ParseTermsContentResponsePayload(pl.data(), pl.size());
							if (!content)
							{
								errMsg = "TERMS content parse failed.";
								return;
							}
							local.totalLength = content->total_length;
							local.termsContent = content->chunk;
						},
						timeoutMs))
				{
					local.ready = true;
					local.message = "Send TERMS_CONTENT failed.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!contentDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
			}
			local.ready = true;
			local.success = true;
			local.message = local.termsPendingCount > 0 ? "Next pending terms loaded." : "All terms accepted.";
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

#else

	void AuthUiPresenter::ImGuiNotifyTermsScrollReachedBottom(bool) {}
	void AuthUiPresenter::ImGuiSetTermsAcknowledgeChecked(bool) {}
	void AuthUiPresenter::ImGuiTermsPrimaryClick(const engine::core::Config&) {}
	void AuthUiPresenter::ImGuiTermsDecline(engine::platform::Window& window)
	{
		window.RequestClose();
	}

	void AuthUiPresenter::BuildModel_Terms(RenderModel&) const {}

	void AuthUiPresenter::Update_Terms(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool)
	{
	}

#endif
} // namespace engine::client
