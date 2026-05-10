// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Implementation
// AuctionHousePresenter.

#include "src/client/auction/AuctionUi.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>
#include <cstdio>

namespace engine::client
{
	namespace
	{
		/// Retourne le steady_clock now en ms depuis le boot pour l'horodatage
		/// local des toasts (5s d'affichage).
		uint64_t SteadyMs()
		{
			const auto v = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return static_cast<uint64_t>(v);
		}
	}

	// -------------------------------------------------------------------------
	// FormatCopper / FormatDuration — static helpers.
	// -------------------------------------------------------------------------

	std::string FormatCopper(uint64_t copper)
	{
		if (copper == 0ull)
		{
			return std::string("0c");
		}
		const uint64_t gold   = copper / 10000ull;
		const uint64_t rest1  = copper % 10000ull;
		const uint64_t silver = rest1 / 100ull;
		const uint64_t cop    = rest1 % 100ull;
		char buf[64]{};
		// Composantes non-nulles uniquement.
		if (gold > 0ull && silver > 0ull && cop > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%llug %llus %lluc",
				static_cast<unsigned long long>(gold),
				static_cast<unsigned long long>(silver),
				static_cast<unsigned long long>(cop));
		}
		else if (gold > 0ull && silver > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%llug %llus",
				static_cast<unsigned long long>(gold),
				static_cast<unsigned long long>(silver));
		}
		else if (gold > 0ull && cop > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%llug %lluc",
				static_cast<unsigned long long>(gold),
				static_cast<unsigned long long>(cop));
		}
		else if (gold > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%llug",
				static_cast<unsigned long long>(gold));
		}
		else if (silver > 0ull && cop > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%llus %lluc",
				static_cast<unsigned long long>(silver),
				static_cast<unsigned long long>(cop));
		}
		else if (silver > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%llus",
				static_cast<unsigned long long>(silver));
		}
		else
		{
			std::snprintf(buf, sizeof(buf), "%lluc",
				static_cast<unsigned long long>(cop));
		}
		return std::string(buf);
	}

	std::string FormatDuration(uint64_t seconds)
	{
		if (seconds == 0ull)
		{
			return std::string("expired");
		}
		const uint64_t hours    = seconds / 3600ull;
		const uint64_t restSec  = seconds % 3600ull;
		const uint64_t minutes  = restSec / 60ull;
		char buf[32]{};
		if (hours > 0ull && minutes > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%lluh %llum",
				static_cast<unsigned long long>(hours),
				static_cast<unsigned long long>(minutes));
		}
		else if (hours > 0ull)
		{
			std::snprintf(buf, sizeof(buf), "%lluh",
				static_cast<unsigned long long>(hours));
		}
		else
		{
			// minutes ou less than 1 minute : floor a 1m si > 0.
			const uint64_t shownMinutes = (minutes == 0ull) ? 1ull : minutes;
			std::snprintf(buf, sizeof(buf), "%llum",
				static_cast<unsigned long long>(shownMinutes));
		}
		return std::string(buf);
	}

	// -------------------------------------------------------------------------
	// Presenter lifecycle
	// -------------------------------------------------------------------------

	AuctionHousePresenter::~AuctionHousePresenter()
	{
		Shutdown();
	}

	bool AuctionHousePresenter::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AuctionHousePresenter] Init ignored: already initialized");
			return true;
		}
		m_initialized = true;
		m_state = {};
		m_pendingBidAuctionId = 0u;
		LOG_INFO(Core, "[AuctionHousePresenter] Init OK");
		return true;
	}

	void AuctionHousePresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_initialized = false;
		m_state = {};
		m_pendingBidAuctionId = 0u;
		LOG_INFO(Core, "[AuctionHousePresenter] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Outgoing requests
	// -------------------------------------------------------------------------

	void AuctionHousePresenter::RequestList(uint32_t filter)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[AuctionHousePresenter] RequestList: no send callback");
			return;
		}
		m_state.filterItemTemplateId = filter;
		const auto payload = engine::network::BuildAuctionListRequestPayload(filter);
		if (!m_send(engine::network::kOpcodeAuctionListRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (liste encheres).";
			LOG_WARN(Net, "[AuctionHousePresenter] RequestList: send failed filter={}", filter);
			return;
		}
		LOG_DEBUG(Net, "[AuctionHousePresenter] Auction ListRequest queued filter={}", filter);
	}

	void AuctionHousePresenter::Post(uint32_t itemTemplateId, uint32_t count,
		uint64_t startBidCopper, uint64_t buyoutCopper, uint8_t durationHours)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[AuctionHousePresenter] Post: no send callback");
			return;
		}
		const auto payload = engine::network::BuildAuctionPostRequestPayload(
			itemTemplateId, count, startBidCopper, buyoutCopper, durationHours);
		if (!m_send(engine::network::kOpcodeAuctionPostRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (poste enchere).";
			LOG_WARN(Net, "[AuctionHousePresenter] Post: send failed item={} count={}",
				itemTemplateId, count);
			return;
		}
		LOG_DEBUG(Net, "[AuctionHousePresenter] Auction PostRequest queued item={} count={} duration={}h",
			itemTemplateId, count, static_cast<unsigned>(durationHours));
	}

	void AuctionHousePresenter::Bid(uint64_t auctionId, uint64_t bidAmountCopper)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[AuctionHousePresenter] Bid: no send callback");
			return;
		}
		const auto payload = engine::network::BuildAuctionBidRequestPayload(
			auctionId, bidAmountCopper);
		if (!m_send(engine::network::kOpcodeAuctionBidRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (mise enchere).";
			LOG_WARN(Net, "[AuctionHousePresenter] Bid: send failed auctionId={} amount={}",
				auctionId, bidAmountCopper);
			return;
		}
		m_pendingBidAuctionId = auctionId;
		LOG_DEBUG(Net, "[AuctionHousePresenter] Auction BidRequest queued auctionId={} amount={}",
			auctionId, bidAmountCopper);
	}

	void AuctionHousePresenter::Cancel(uint64_t auctionId)
	{
		if (!m_send)
		{
			LOG_WARN(Net, "[AuctionHousePresenter] Cancel: no send callback");
			return;
		}
		const auto payload = engine::network::BuildAuctionCancelRequestPayload(auctionId);
		if (!m_send(engine::network::kOpcodeAuctionCancelRequest, payload))
		{
			m_state.lastErrorText = "Echec envoi (annulation enchere).";
			LOG_WARN(Net, "[AuctionHousePresenter] Cancel: send failed auctionId={}", auctionId);
			return;
		}
		LOG_DEBUG(Net, "[AuctionHousePresenter] Auction CancelRequest queued auctionId={}", auctionId);
	}

	// -------------------------------------------------------------------------
	// Incoming responses / push
	// -------------------------------------------------------------------------

	void AuctionHousePresenter::OnListResponse(const engine::network::AuctionListResponsePayload& resp)
	{
		using engine::network::AuctionErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<AuctionErrorCode>(resp.error);
			if (err == AuctionErrorCode::Unauthorized)
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
			else
				m_state.lastErrorText = "Erreur Auction inconnue.";
			LOG_WARN(Net, "[AuctionHousePresenter] OnListResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.listings.clear();
		m_state.listings.reserve(resp.listings.size());
		for (const auto& s : resp.listings)
		{
			AuctionListingSummary local;
			local.auctionId              = s.auctionId;
			local.itemTemplateId         = s.itemTemplateId;
			local.itemName               = s.itemName;
			local.count                  = s.count;
			local.currentBidCopper       = s.currentBidCopper;
			local.buyoutCopper           = s.buyoutCopper;
			local.ownerName              = s.ownerName;
			local.secondsUntilExpiration = s.secondsUntilExpiration;
			m_state.listings.push_back(std::move(local));
		}
		m_state.listingsLoaded = true;

		LOG_INFO(Net, "[AuctionHousePresenter] OnListResponse OK count={}",
			m_state.listings.size());
	}

	void AuctionHousePresenter::OnPostResponse(const engine::network::AuctionPostResponsePayload& resp)
	{
		using engine::network::AuctionErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<AuctionErrorCode>(resp.error);
			switch (err)
			{
			case AuctionErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case AuctionErrorCode::InvalidParams:
				m_state.lastErrorText = "Parametres invalides (count/prix/duree).";
				break;
			default:
				m_state.lastErrorText = "Erreur Auction inconnue.";
				break;
			}
			LOG_WARN(Net, "[AuctionHousePresenter] OnPostResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		char buf[64]{};
		std::snprintf(buf, sizeof(buf), "Enchere postee #%llu",
			static_cast<unsigned long long>(resp.auctionId));
		m_state.lastInfoText = buf;
		LOG_INFO(Net, "[AuctionHousePresenter] OnPostResponse OK auctionId={}", resp.auctionId);
		// Refresh la liste suite a un Post reussi.
		RequestList(m_state.filterItemTemplateId);
	}

	void AuctionHousePresenter::OnBidResponse(const engine::network::AuctionBidResponsePayload& resp)
	{
		using engine::network::AuctionErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<AuctionErrorCode>(resp.error);
			switch (err)
			{
			case AuctionErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case AuctionErrorCode::BidTooLow:
				m_state.lastErrorText = "Mise trop basse.";
				break;
			case AuctionErrorCode::AuctionExpired:
				m_state.lastErrorText = "Enchere expiree.";
				break;
			case AuctionErrorCode::OwnAuction:
				m_state.lastErrorText = "Vous ne pouvez pas miser sur votre propre enchere.";
				break;
			case AuctionErrorCode::AuctionNotFound:
				m_state.lastErrorText = "Enchere introuvable.";
				break;
			default:
				m_state.lastErrorText = "Erreur Auction inconnue.";
				break;
			}
			m_pendingBidAuctionId = 0u;
			LOG_WARN(Net, "[AuctionHousePresenter] OnBidResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.lastBidWasBuyout = (resp.isBuyout != 0u);
		m_state.lastBidAuctionId = m_pendingBidAuctionId;
		m_state.lastBidTimeMs    = SteadyMs();
		m_pendingBidAuctionId    = 0u;
		LOG_INFO(Net, "[AuctionHousePresenter] OnBidResponse OK isBuyout={}",
			static_cast<unsigned>(resp.isBuyout));

		// Refresh la liste suite a une Bid reussie.
		RequestList(m_state.filterItemTemplateId);
	}

	void AuctionHousePresenter::OnCancelResponse(const engine::network::AuctionCancelResponsePayload& resp)
	{
		using engine::network::AuctionErrorCode;
		if (resp.error != 0u)
		{
			const auto err = static_cast<AuctionErrorCode>(resp.error);
			switch (err)
			{
			case AuctionErrorCode::Unauthorized:
				m_state.lastErrorText = "Session invalide. Reconnectez-vous.";
				break;
			case AuctionErrorCode::NotOwner:
				m_state.lastErrorText = "Vous n'etes pas le proprietaire.";
				break;
			case AuctionErrorCode::AuctionNotFound:
				m_state.lastErrorText = "Enchere introuvable.";
				break;
			case AuctionErrorCode::AuctionExpired:
				m_state.lastErrorText = "Enchere deja terminee.";
				break;
			default:
				m_state.lastErrorText = "Erreur Auction inconnue.";
				break;
			}
			LOG_WARN(Net, "[AuctionHousePresenter] OnCancelResponse error={}",
				static_cast<unsigned>(resp.error));
			return;
		}

		m_state.lastErrorText.clear();
		m_state.lastInfoText = "Enchere annulee.";
		LOG_INFO(Net, "[AuctionHousePresenter] OnCancelResponse OK");

		// Refresh la liste suite a un Cancel reussi.
		RequestList(m_state.filterItemTemplateId);
	}

	void AuctionHousePresenter::OnExpiredNotification(const engine::network::AuctionExpiredNotificationPayload& note)
	{
		m_state.lastExpirationAuctionId   = note.auctionId;
		m_state.lastExpirationWon         = (note.won != 0u);
		m_state.lastExpirationFinalBid    = note.finalBidCopper;
		m_state.lastExpirationWinnerName  = note.winnerName;
		m_state.lastExpirationTimeMs      = SteadyMs();
		LOG_DEBUG(Net, "[AuctionHousePresenter] OnExpiredNotification auctionId={} won={} finalBid={}",
			note.auctionId, static_cast<unsigned>(note.won), note.finalBidCopper);
	}
}
