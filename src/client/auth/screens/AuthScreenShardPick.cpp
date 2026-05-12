// AUTH-UI.8 — Couche modèle pour l'écran de choix du royaume (shard).

// Couche modèle : BuildModel_ShardPick liste les shards disponibles, Update_ShardPick gère la navigation clavier.
#include "src/client/auth/AuthUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/network/RequestResponseDispatcher.h"
#include "src/shared/network/ServerListPayloads.h"
#include "src/shared/platform/Input.h"
#include "src/shared/platform/Window.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace engine::client
{
#if defined(_WIN32)

	/// Enregistre le shard sélectionné par l'utilisateur dans le renderer ImGui.
	void AuthUiPresenter::ImGuiSetShardPickChoiceShardId(uint32_t shardId)
	{
		if (m_phase != Phase::ShardPick)
		{
			return;
		}
		m_shardPickChoiceShardId = shardId;
	}

	/// Soumet le shard choisi et lance la connexion au royaume.
	void AuthUiPresenter::ImGuiSubmitShardPick(const engine::core::Config& cfg)
	{
		if (m_phase != Phase::ShardPick)
		{
			return;
		}
		SubmitCurrentPhase(cfg);
	}

	/// Annule le choix de shard et retourne à la connexion en réinitialisant la liste.
	void AuthUiPresenter::ImGuiBackFromShardPickToLogin()
	{
		if (m_phase != Phase::ShardPick)
		{
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] ImGui: ShardPick -> Login");
		m_userErrorText.clear();
		SetPhase(Phase::Login);
		m_shardPickChoiceShardId = 0;
		m_shardPickEntries.clear();
		m_chosenShardId = 0;
		m_postRegistrationCharacterCreatePending = false;
	}

	/// Lance un worker qui re-emet SERVER_LIST_REQUEST sur la connexion master deja AUTH
	/// pour rafraichir la liste des shards (nb joueurs courant) pendant que l'utilisateur
	/// reste sur l'ecran ShardPick. Ne fait rien si aucune connexion master vivante
	/// (\ref m_masterClient absent ou \ref m_masterSessionId nul) ni si un worker est deja
	/// en vol (l'appelant doit verifier \c !m_worker.joinable() avant d'appeler). Le resultat
	/// (vecteur des entrees a jour) est publie via \c AsyncResult::serverListForPick sous
	/// la kind \c AsyncKind::RefreshShardList ; \ref PollAsyncResult applique le resultat
	/// sur \ref m_shardPickEntries en preservant le choix utilisateur courant.
	void AuthUiPresenter::StartRefreshShardListWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		if (!m_masterClient || m_masterSessionId == 0u)
		{
			// Pas de session master vivante : pas de refresh possible. On reste sur la
			// snapshot precedente ; le bouton "Retour" puis re-login reconstruira l'etat.
			return;
		}
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.shard_pick_refresh_timeout_ms", 3000));

		m_pendingAsyncKind = AsyncKind::RefreshShardList;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		const uint64_t sessionId = m_masterSessionId;
		m_worker = std::thread([this, masterClient, sessionId, timeoutMs]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(masterClient);
			disp.SetSessionId(sessionId);
			bool done = false;
			std::vector<engine::network::ServerListEntry> entries;
			if (!disp.SendRequest(engine::network::kOpcodeServerListRequest, {},
					[&](uint32_t, bool timeout, std::vector<uint8_t> payload) {
						done = true;
						if (timeout || payload.empty())
						{
							return;
						}
						entries = engine::network::ParseServerListResponsePayload(payload.data(), payload.size());
					},
					timeoutMs))
			{
				local.ready = true;
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			local.ready = true;
			local.success = done && !entries.empty();
			local.serverListForPick = std::move(entries);
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	/// Peuple la liste des shards disponibles (nom, charge, endpoint) avec indication du shard sélectionné.
	void AuthUiPresenter::BuildModel_ShardPick(RenderModel& model) const
	{
		model.sectionTitle = Tr("auth.phase.shard_pick");
		{
			RenderBodyLine hint{};
			hint.text = Tr("auth.shard_pick.hint");
			model.bodyLines.push_back(std::move(hint));
		}
		for (const auto& e : m_shardPickEntries)
		{
			RenderBodyLine line{};
			line.text = Tr("auth.shard_pick.line",
				{ { "id", std::to_string(e.shard_id) },
					{ "load", std::to_string(e.current_load) + "/" + std::to_string(e.max_capacity) },
					{ "endpoint", e.endpoint.empty() ? std::string("-") : e.endpoint } });
			const bool rowSelectable = (e.status == 1u && !e.endpoint.empty());
			line.active = (m_shardPickChoiceShardId == e.shard_id);
			line.link = rowSelectable;
			model.bodyLines.push_back(std::move(line));
		}
		{
			RenderAction submit{};
			submit.labelKey = "common.submit";
			submit.primary = true;
			submit.active = (m_shardPickChoiceShardId != 0u);
			submit.emphasized = false;
			submit.hovered = (m_hoveredActionIndex == 0);
			model.actions.push_back(std::move(submit));
		}
		{
			RenderAction back{};
			back.labelKey = "common.back";
			back.primary = false;
			back.active = true;
			back.emphasized = false;
			back.hovered = (m_hoveredActionIndex == 1);
			model.actions.push_back(std::move(back));
		}
	}

	/// Gère la navigation clavier (flèches) dans la liste des shards et Entrée pour valider (hors ImGui).
	void AuthUiPresenter::Update_ShardPick(engine::platform::Input& input, const engine::core::Config& cfg,
		engine::platform::Window& window, bool usingNativeAuth, bool authUiImguiMode)
	{
		(void)window;
		if (usingNativeAuth || m_phase != Phase::ShardPick)
		{
			return;
		}
		const auto& entries = m_shardPickEntries;
		auto countEligible = [&entries]() -> uint32_t {
			uint32_t n = 0;
			for (const auto& e : entries)
			{
				if (e.status == 1u && !e.endpoint.empty())
				{
					++n;
				}
			}
			return n;
		};
		const uint32_t nElig = countEligible();
		if (nElig > 0u && (input.WasPressed(engine::platform::Key::Up) || input.WasPressed(engine::platform::Key::Left)))
		{
			uint32_t idx = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (e.shard_id == m_shardPickChoiceShardId)
				{
					break;
				}
				++idx;
			}
			idx = (idx == 0u) ? (nElig - 1u) : (idx - 1u);
			uint32_t j = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (j == idx)
				{
					m_shardPickChoiceShardId = e.shard_id;
					break;
				}
				++j;
			}
		}
		if (nElig > 0u && (input.WasPressed(engine::platform::Key::Down) || input.WasPressed(engine::platform::Key::Right)))
		{
			uint32_t idx = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (e.shard_id == m_shardPickChoiceShardId)
				{
					break;
				}
				++idx;
			}
			idx = (idx + 1u) % nElig;
			uint32_t j = 0;
			for (const auto& e : entries)
			{
				if (e.status != 1u || e.endpoint.empty())
				{
					continue;
				}
				if (j == idx)
				{
					m_shardPickChoiceShardId = e.shard_id;
					break;
				}
				++j;
			}
		}
		if (authUiImguiMode && m_shardPickChoiceShardId != 0u && input.WasPressed(engine::platform::Key::Enter))
		{
			ImGuiSubmitShardPick(cfg);
		}
	}

// Stubs Linux/Mac — aucune UI d'auth sur ces plateformes.
#else

	void AuthUiPresenter::BuildModel_ShardPick(RenderModel&) const {}

	void AuthUiPresenter::Update_ShardPick(engine::platform::Input&, const engine::core::Config&, engine::platform::Window&, bool, bool)
	{
	}

	void AuthUiPresenter::StartRefreshShardListWorker(const engine::core::Config&) {}

#endif
} // namespace engine::client
