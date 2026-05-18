// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - Implementation LootRollUiPresenter.

#include "src/client/loot/LootRollUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>

namespace engine::client
{
	namespace
	{
		/// Retourne le steady_clock now en ms depuis le boot pour l'horodatage
		/// local des toasts (5s d'affichage) et du countdown des rolls
		/// pending.
		uint64_t SteadyMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	// -------------------------------------------------------------------------
	// LootChoiceName - static helper.
	// -------------------------------------------------------------------------

	const char* LootChoiceName(uint8_t choice)
	{
		switch (choice)
		{
		case 0u: return "Pass";
		case 1u: return "Greed";
		case 2u: return "Need";
		default: return "?";
		}
	}

	// -------------------------------------------------------------------------
	// Presenter lifecycle
	// -------------------------------------------------------------------------

	LootRollUiPresenter::~LootRollUiPresenter()
	{
		Shutdown();
	}

	bool LootRollUiPresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[LootRollUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		LOG_INFO(Core, "[LootRollUiPresenter] Init OK");
		return true;
	}

	void LootRollUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[LootRollUiPresenter] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void LootRollUiPresenter::Choose(uint64_t rollId, uint8_t choice)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[LootRollUiPresenter] Choose: no send callback");
			return;
		}
		// Audit 2026-05-18 : double-submit guard. Avant ce check, `myChoice` etait
		// pose APRES `m_send` -> double-clic envoyait deux ChoiceRequest.
		// `myChoice` est `std::optional<uint8_t>` : empty = pas encore choisi,
		// has_value() = choix deja envoye (Pass=0, Greed=1, Need=2). On bloque
		// donc sur `has_value()` (et non sur `!= 0` qui collisionnerait avec Pass).
		// On reserve l'optional AVANT l'envoi pour fermer la fenetre de race.
		std::optional<uint8_t> previousChoice;
		bool reserved = false;
		for (auto& p : m_state.pendingRolls)
		{
			if (p.rollId == rollId)
			{
				if (p.myChoice.has_value())
				{
					LOG_DEBUG(Net, "[LootRollUiPresenter] Choose: choix deja envoye rollId={}, ignore", rollId);
					return;
				}
				previousChoice = p.myChoice; // toujours empty ici par la branche has_value() au-dessus
				p.myChoice = choice;
				reserved = true;
				break;
			}
		}
		const auto payload = engine::network::BuildLootRollChoiceRequestPayload(rollId, choice);
		if (!m_send(engine::network::kOpcodeLootRollChoiceRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (loot choice).";
			LOG_WARN(Net, "[LootRollUiPresenter] Choose: send failed rollId={}", rollId);
			if (reserved) // rollback de l'optional pour permettre un retry
			{
				for (auto& p : m_state.pendingRolls)
				{
					if (p.rollId == rollId)
					{
						p.myChoice = previousChoice;
						break;
					}
				}
			}
			return;
		}
		LOG_DEBUG(Net, "[LootRollUiPresenter] Choice queued rollId={} choice={}",
			rollId, static_cast<unsigned>(choice));
	}

	void LootRollUiPresenter::SimulateRoll()
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[LootRollUiPresenter] SimulateRoll: no send callback");
			return;
		}
		const auto payload = engine::network::BuildLootSimulateRollRequestPayload();
		if (!m_send(engine::network::kOpcodeLootSimulateRollRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (simulate roll).";
			LOG_WARN(Net, "[LootRollUiPresenter] SimulateRoll: send failed");
			return;
		}
		LOG_DEBUG(Net, "[LootRollUiPresenter] SimulateRoll queued");
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void LootRollUiPresenter::OnChoiceResponse(const engine::network::LootRollChoiceResponsePayload& resp)
	{
		using engine::network::LootResponseStatus;
		if (resp.status != 0u)
		{
			const auto err = static_cast<LootResponseStatus>(resp.status);
			switch (err)
			{
			case LootResponseStatus::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case LootResponseStatus::InvalidChoice:
				m_state.lastErrorText = "Choix invalide.";
				break;
			case LootResponseStatus::RollNotFound:
				m_state.lastErrorText = "Roll inconnue (deja terminee ?).";
				break;
			case LootResponseStatus::RollEnded:
				m_state.lastErrorText = "Roll deja terminee.";
				break;
			default:
				m_state.lastErrorText = "Erreur Loot inconnue.";
				break;
			}
			LOG_WARN(Net, "[LootRollUiPresenter] OnChoiceResponse error={}",
				static_cast<unsigned>(resp.status));
			return;
		}

		m_state.lastErrorText.clear();
		LOG_DEBUG(Net, "[LootRollUiPresenter] OnChoiceResponse OK");
	}

	void LootRollUiPresenter::OnSimulateRollResponse(const engine::network::LootSimulateRollResponsePayload& resp)
	{
		using engine::network::LootResponseStatus;
		if (resp.status != 0u)
		{
			const auto err = static_cast<LootResponseStatus>(resp.status);
			if (err == LootResponseStatus::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur Loot inconnue.";
			LOG_WARN(Net, "[LootRollUiPresenter] OnSimulateRollResponse error={}",
				static_cast<unsigned>(resp.status));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.lastInfoText = "Roll simulee.";
		LOG_INFO(Net, "[LootRollUiPresenter] OnSimulateRollResponse OK rollId={}", resp.rollId);
	}

	void LootRollUiPresenter::OnRollNotification(const engine::network::LootRollNotificationPayload& note)
	{
		// Si une PendingRoll avec le meme rollId existe deja, on l'ignore
		// (cas pathologique : double push).
		for (const auto& p : m_state.pendingRolls)
		{
			if (p.rollId == note.rollId)
			{
				LOG_WARN(Net, "[LootRollUiPresenter] OnRollNotification: duplicate rollId={} ignored",
					note.rollId);
				return;
			}
		}

		PendingRoll p;
		p.rollId         = note.rollId;
		p.itemTemplateId = note.itemTemplateId;
		p.itemName       = note.itemName;
		p.count          = note.count;
		p.expiresAtMs    = SteadyMs() + static_cast<uint64_t>(note.durationSec) * 1000ull;
		// myChoice reste std::nullopt jusqu'au click.
		m_state.pendingRolls.push_back(std::move(p));

		LOG_INFO(Net, "[LootRollUiPresenter] OnRollNotification rollId={} item='{}' count={} duration={}s",
			note.rollId, note.itemName, note.count, note.durationSec);
	}

	void LootRollUiPresenter::OnRollResultNotification(const engine::network::LootRollResultNotificationPayload& note)
	{
		// Retire la pending roll du cache (par rollId).
		for (auto it = m_state.pendingRolls.begin(); it != m_state.pendingRolls.end(); ++it)
		{
			if (it->rollId == note.rollId)
			{
				m_state.pendingRolls.erase(it);
				break;
			}
		}

		// Met a jour lastResult* pour le toast.
		m_state.lastResultTimeMs       = SteadyMs();
		m_state.lastResultRollId       = note.rollId;
		m_state.lastResultWinnerName   = note.winnerName;
		m_state.lastResultWinnerChoice = note.winnerChoice;
		m_state.lastResultWinnerRoll   = note.winnerRoll;
		m_state.lastResultItemName     = note.itemName;
		m_state.lastResultCount        = note.count;

		LOG_INFO(Net, "[LootRollUiPresenter] OnRollResultNotification rollId={} winner='{}' choice={} roll={}",
			note.rollId, note.winnerName, static_cast<unsigned>(note.winnerChoice),
			static_cast<unsigned>(note.winnerRoll));
	}
}
