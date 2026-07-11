#include "src/shardd/gameplay/trade/TradeSystem.h"

#include "src/shared/server_bootstrap/ServerApp.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::server
{
	void TradeSystem::Init(uint16_t tickHz)
	{
		m_tickHz          = (tickHz > 0) ? tickHz : 20;
		m_reviewTickCount = static_cast<uint32_t>(m_tickHz) * kTradeReviewSeconds;
		m_sessions.clear();
		m_pendingRequests.clear();
		LOG_INFO(Gameplay, "[TradeSystem] Init OK (tickHz={}, reviewTicks={})",
		         m_tickHz, m_reviewTickCount);
	}

	void TradeSystem::Shutdown()
	{
		if (!m_sessions.empty())
		{
			LOG_WARN(Gameplay, "[TradeSystem] Shutdown with {} open session(s) — forcing cancel",
			         m_sessions.size());
		}
		m_sessions.clear();
		m_pendingRequests.clear();
		LOG_INFO(Gameplay, "[TradeSystem] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Request / accept / decline flow
	// -------------------------------------------------------------------------

	TradeOpResult TradeSystem::SendTradeRequest(
		const ConnectedClient& initiator,
		const ConnectedClient& target,
		std::string& outError)
	{
		if (FindSession(initiator.clientId) != nullptr)
		{
			outError = "already in a trade";
			LOG_WARN(Gameplay, "[TradeSystem] TradeRequest rejected: client {} already in a trade",
			         initiator.clientId);
			return TradeOpResult::AlreadyInTrade;
		}
		if (FindSession(target.clientId) != nullptr)
		{
			outError = "target is already in a trade";
			LOG_WARN(Gameplay, "[TradeSystem] TradeRequest rejected: target {} already in a trade",
			         target.clientId);
			return TradeOpResult::AlreadyInTrade;
		}

		/// Range check (XZ plane).
		if (Distance2D(initiator, target) > kTradeMaxRangeMeters)
		{
			outError = "target is too far away";
			LOG_WARN(Gameplay, "[TradeSystem] TradeRequest rejected: clients {} and {} out of range",
			         initiator.clientId, target.clientId);
			return TradeOpResult::RangeExceeded;
		}

		/// One pending request per pair is sufficient.
		for (const auto& req : m_pendingRequests)
		{
			if ((req.initiatorClientId == initiator.clientId && req.targetClientId == target.clientId) ||
			    (req.initiatorClientId == target.clientId   && req.targetClientId == initiator.clientId))
			{
				outError = "trade request already pending";
				return TradeOpResult::PendingExists;
			}
		}

		PendingTradeRequest req{};
		req.initiatorClientId = initiator.clientId;
		req.targetClientId    = target.clientId;
		req.initiatorName     = "P" + std::to_string(initiator.clientId);
		m_pendingRequests.push_back(std::move(req));

		LOG_INFO(Gameplay, "[TradeSystem] TradeRequest queued: initiator={} target={}",
		         initiator.clientId, target.clientId);
		return TradeOpResult::Ok;
	}

	TradeOpResult TradeSystem::AcceptTradeRequest(
		const ConnectedClient& acceptor,
		std::string& outError)
	{
		PendingTradeRequest* req = FindPendingRequest(acceptor.clientId);
		if (req == nullptr)
		{
			outError = "no pending trade request";
			return TradeOpResult::InvalidTarget;
		}

		TradeSession session{};
		session.sideAClientId    = req->initiatorClientId;
		session.sideBClientId    = req->targetClientId;
		session.sideA.clientId   = req->initiatorClientId;
		session.sideB.clientId   = req->targetClientId;
		session.phase            = TradePhase::Negotiation;

		/// Remove the pending request before pushing the session.
		m_pendingRequests.erase(
			std::remove_if(m_pendingRequests.begin(), m_pendingRequests.end(),
				[&](const PendingTradeRequest& r) {
					return r.targetClientId == acceptor.clientId;
				}),
			m_pendingRequests.end());

		m_sessions.push_back(std::move(session));

		LOG_INFO(Gameplay, "[TradeSystem] Session opened: A={} B={}",
		         m_sessions.back().sideAClientId, m_sessions.back().sideBClientId);
		return TradeOpResult::Ok;
	}

	TradeOpResult TradeSystem::DeclineTradeRequest(
		const ConnectedClient& decliner,
		std::string& outError)
	{
		PendingTradeRequest* req = FindPendingRequest(decliner.clientId);
		if (req == nullptr)
		{
			outError = "no pending trade request to decline";
			return TradeOpResult::InvalidTarget;
		}

		const uint32_t initiatorId = req->initiatorClientId;
		m_pendingRequests.erase(
			std::remove_if(m_pendingRequests.begin(), m_pendingRequests.end(),
				[&](const PendingTradeRequest& r) {
					return r.targetClientId == decliner.clientId;
				}),
			m_pendingRequests.end());

		LOG_INFO(Gameplay, "[TradeSystem] TradeRequest declined: initiator={} target={}",
		         initiatorId, decliner.clientId);
		return TradeOpResult::Ok;
	}

	// -------------------------------------------------------------------------
	// Active session operations
	// -------------------------------------------------------------------------

	TradeOpResult TradeSystem::AddItem(
		const ConnectedClient& caller,
		uint32_t itemId,
		uint32_t quantity,
		std::string& outError)
	{
		const size_t idx = FindSessionIndex(caller.clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			outError = "not in a trade";
			return TradeOpResult::NotInTrade;
		}
		TradeSession& session = m_sessions[idx];
		if (session.phase != TradePhase::Negotiation)
		{
			outError = "trade is locked — cannot add items";
			return TradeOpResult::WrongPhase;
		}
		TradeSide* side = SelectSide(session, caller.clientId);
		if (side == nullptr)
		{
			outError = "internal error: side not found";
			return TradeOpResult::NotInTrade;
		}
		if (side->locked)
		{
			outError = "your side is already locked";
			return TradeOpResult::WrongPhase;
		}
		if (side->items.size() >= kMaxTradeItemSlots)
		{
			outError = "trade slots full";
			return TradeOpResult::SlotsFull;
		}

		/// Merge with existing stack if same itemId, otherwise append.
		bool merged = false;
		for (ItemStack& stack : side->items)
		{
			if (stack.itemId == itemId)
			{
				stack.quantity += quantity;
				merged = true;
				break;
			}
		}
		if (!merged)
		{
			side->items.push_back({ itemId, quantity });
		}

		LOG_DEBUG(Gameplay, "[TradeSystem] AddItem: client={} itemId={} qty={} (session A={} B={})",
		          caller.clientId, itemId, quantity,
		          session.sideAClientId, session.sideBClientId);
		return TradeOpResult::Ok;
	}

	TradeOpResult TradeSystem::SetGold(
		const ConnectedClient& caller,
		uint32_t goldAmount,
		std::string& outError)
	{
		const size_t idx = FindSessionIndex(caller.clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			outError = "not in a trade";
			return TradeOpResult::NotInTrade;
		}
		TradeSession& session = m_sessions[idx];
		if (session.phase != TradePhase::Negotiation)
		{
			outError = "trade is locked — cannot change gold";
			return TradeOpResult::WrongPhase;
		}
		TradeSide* side = SelectSide(session, caller.clientId);
		if (side == nullptr || side->locked)
		{
			outError = "your side is already locked";
			return TradeOpResult::WrongPhase;
		}
		side->goldAmount = goldAmount;
		LOG_DEBUG(Gameplay, "[TradeSystem] SetGold: client={} gold={}", caller.clientId, goldAmount);
		return TradeOpResult::Ok;
	}

	TradeOpResult TradeSystem::Lock(
		const ConnectedClient& caller,
		uint32_t currentTick,
		std::string& outError)
	{
		const size_t idx = FindSessionIndex(caller.clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			outError = "not in a trade";
			return TradeOpResult::NotInTrade;
		}
		TradeSession& session = m_sessions[idx];
		if (session.phase != TradePhase::Negotiation)
		{
			outError = "already in review or confirming phase";
			return TradeOpResult::WrongPhase;
		}
		TradeSide* side = SelectSide(session, caller.clientId);
		if (side == nullptr)
		{
			outError = "internal error: side not found";
			return TradeOpResult::NotInTrade;
		}
		side->locked = true;

		/// If both sides are now locked, advance to the Review phase.
		if (session.sideA.locked && session.sideB.locked)
		{
			session.phase           = TradePhase::Review;
			session.reviewStartTick = currentTick;
			LOG_INFO(Gameplay, "[TradeSystem] Review phase started: A={} B={} tick={}",
			         session.sideAClientId, session.sideBClientId, currentTick);
		}
		else
		{
			LOG_DEBUG(Gameplay, "[TradeSystem] Lock: client={} (waiting for partner)",
			          caller.clientId);
		}
		return TradeOpResult::Ok;
	}

	TradeOpResult TradeSystem::Confirm(
		ConnectedClient& caller,
		ConnectedClient& partner,
		std::string& outError)
	{
		const size_t idx = FindSessionIndex(caller.clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			outError = "not in a trade";
			return TradeOpResult::NotInTrade;
		}
		TradeSession& session = m_sessions[idx];
		if (session.phase == TradePhase::Negotiation)
		{
			outError = "both sides must lock before confirming";
			return TradeOpResult::WrongPhase;
		}
		TradeSide* side = SelectSide(session, caller.clientId);
		if (side == nullptr)
		{
			outError = "internal error: side not found";
			return TradeOpResult::NotInTrade;
		}
		side->confirmed = true;

		/// Only swap when BOTH sides have confirmed.
		if (!session.sideA.confirmed || !session.sideB.confirmed)
		{
			LOG_DEBUG(Gameplay, "[TradeSystem] Confirm: client={} (waiting for partner)",
			          caller.clientId);
			session.phase = TradePhase::Confirming;
			return TradeOpResult::Ok;
		}

		/// Validate items before the irreversible swap.
		ConnectedClient* clientA = (caller.clientId == session.sideAClientId) ? &caller : &partner;
		ConnectedClient* clientB = (caller.clientId == session.sideBClientId) ? &caller : &partner;

		if (!ValidateItems(*clientA, *clientB, session, outError))
		{
			LOG_WARN(Gameplay, "[TradeSystem] Confirm FAILED validation: A={} B={} reason={}",
			         session.sideAClientId, session.sideBClientId, outError);
			return TradeOpResult::InsufficientItems;
		}

		ApplySwap(*clientA, *clientB, session);
		LOG_INFO(Gameplay, "[TradeSystem] Trade completed: A={} B={}",
		         session.sideAClientId, session.sideBClientId);

		m_sessions.erase(m_sessions.begin() + static_cast<ptrdiff_t>(idx));
		return TradeOpResult::Ok;
	}

	TradeOpResult TradeSystem::Cancel(uint32_t clientId, std::string& outError)
	{
		const size_t idx = FindSessionIndex(clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			outError = "not in a trade";
			return TradeOpResult::NotInTrade;
		}
		RemoveSession(static_cast<uint32_t>(idx), "cancel");
		return TradeOpResult::Ok;
	}

	// -------------------------------------------------------------------------
	// State queries
	// -------------------------------------------------------------------------

	TradeSession* TradeSystem::FindSession(uint32_t clientId)
	{
		const size_t idx = FindSessionIndex(clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			return nullptr;
		}
		return &m_sessions[idx];
	}

	const TradeSession* TradeSystem::FindSession(uint32_t clientId) const
	{
		const size_t idx = FindSessionIndex(clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			return nullptr;
		}
		return &m_sessions[idx];
	}

	PendingTradeRequest* TradeSystem::FindPendingRequest(uint32_t targetClientId)
	{
		for (auto& req : m_pendingRequests)
		{
			if (req.targetClientId == targetClientId)
			{
				return &req;
			}
		}
		return nullptr;
	}

	uint32_t TradeSystem::GetPartnerClientId(uint32_t clientId) const
	{
		const size_t idx = FindSessionIndex(clientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			return 0;
		}
		const TradeSession& s = m_sessions[idx];
		return (s.sideAClientId == clientId) ? s.sideBClientId : s.sideAClientId;
	}

	TradeWindowUpdateMessage TradeSystem::BuildWindowUpdate(
		uint32_t viewerClientId,
		uint32_t currentTick) const
	{
		TradeWindowUpdateMessage msg{};
		const size_t idx = FindSessionIndex(viewerClientId);
		if (idx == std::numeric_limits<size_t>::max())
		{
			return msg;
		}
		const TradeSession& session = m_sessions[idx];

		auto fillSide = [](TradeSideWire& wire, const TradeSide& side)
		{
			wire.clientId   = side.clientId;
			wire.goldAmount = side.goldAmount;
			wire.locked     = side.locked     ? 1u : 0u;
			wire.confirmed  = side.confirmed  ? 1u : 0u;
			wire.items      = side.items;
		};

		const bool viewerIsA = (viewerClientId == session.sideAClientId);
		fillSide(msg.self,  viewerIsA ? session.sideA : session.sideB);
		fillSide(msg.other, viewerIsA ? session.sideB : session.sideA);

		if (session.phase == TradePhase::Review)
		{
			const uint32_t elapsed = currentTick - session.reviewStartTick;
			msg.reviewTicksRemaining =
				(elapsed < m_reviewTickCount) ? (m_reviewTickCount - elapsed) : 0;
		}
		return msg;
	}

	void TradeSystem::Tick(uint32_t currentTick)
	{
		/// Expire review phase once the anti-scam timer has elapsed.
		for (auto& session : m_sessions)
		{
			if (session.phase != TradePhase::Review)
			{
				continue;
			}
			const uint32_t elapsed = currentTick - session.reviewStartTick;
			if (elapsed >= m_reviewTickCount)
			{
				session.phase = TradePhase::Confirming;
				LOG_DEBUG(Gameplay,
				          "[TradeSystem] Review window expired — confirm enabled: A={} B={}",
				          session.sideAClientId, session.sideBClientId);
			}
		}
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	float TradeSystem::Distance2D(const ConnectedClient& a, const ConnectedClient& b)
	{
		const float dx = a.positionMetersX - b.positionMetersX;
		const float dz = a.positionMetersZ - b.positionMetersZ;
		return std::sqrt(dx * dx + dz * dz);
	}

	bool TradeSystem::ValidateItems(
		const ConnectedClient& clientA,
		const ConnectedClient& clientB,
		const TradeSession&    session,
		std::string&           outError) const
	{
		auto hasSufficientItems = [](const ConnectedClient& client,
		                             const TradeSide&       side,
		                             std::string&           err) -> bool
		{
			for (const ItemStack& offered : side.items)
			{
				uint32_t totalInBag = 0;
				for (const ItemStack& bag : client.inventory)
				{
					if (bag.itemId == offered.itemId)
					{
						totalInBag += bag.quantity;
					}
				}
				if (totalInBag < offered.quantity)
				{
					err = "insufficient items in inventory";
					return false;
				}
			}
			return true;
		};

		auto hasSufficientGold = [](const ConnectedClient& client,
		                            const TradeSide&       side,
		                            std::string&           err) -> bool
		{
			if (client.gold < side.goldAmount)
			{
				err = "insufficient gold";
				return false;
			}
			return true;
		};

		return hasSufficientItems(clientA, session.sideA, outError) &&
		       hasSufficientItems(clientB, session.sideB, outError) &&
		       hasSufficientGold(clientA, session.sideA, outError)  &&
		       hasSufficientGold(clientB, session.sideB, outError);
	}

	void TradeSystem::ApplySwap(
		ConnectedClient& clientA,
		ConnectedClient& clientB,
		TradeSession&    session)
	{
		/// Remove offered items from the giver, then add them to the receiver.
		auto removeItems = [](ConnectedClient& client, const TradeSide& side)
		{
			for (const ItemStack& offered : side.items)
			{
				uint32_t remaining = offered.quantity;
				for (ItemStack& bag : client.inventory)
				{
					if (bag.itemId == offered.itemId && remaining > 0)
					{
						const uint32_t take = std::min(bag.quantity, remaining);
						bag.quantity -= take;
						remaining    -= take;
					}
				}
				/// Purge empty stacks.
				client.inventory.erase(
					std::remove_if(client.inventory.begin(), client.inventory.end(),
						[](const ItemStack& s) { return s.quantity == 0; }),
					client.inventory.end());
			}
		};

		auto addItems = [](ConnectedClient& client, const TradeSide& side)
		{
			for (const ItemStack& offered : side.items)
			{
				bool merged = false;
				for (ItemStack& bag : client.inventory)
				{
					if (bag.itemId == offered.itemId)
					{
						bag.quantity += offered.quantity;
						merged = true;
						break;
					}
				}
				if (!merged)
				{
					client.inventory.push_back(offered);
				}
			}
		};

		/// Items: A gives → B, B gives → A.
		removeItems(clientA, session.sideA);
		removeItems(clientB, session.sideB);
		addItems(clientA, session.sideB);
		addItems(clientB, session.sideA);

		/// Gold: subtract from giver, add to receiver.
		clientA.gold -= session.sideA.goldAmount;
		clientB.gold += session.sideA.goldAmount;
		clientB.gold -= session.sideB.goldAmount;
		clientA.gold += session.sideB.goldAmount;

		LOG_INFO(Gameplay,
		         "[TradeSystem] Swap applied: A={} (gave {} gold, {} item types) "
		         "B={} (gave {} gold, {} item types)",
		         clientA.clientId, session.sideA.goldAmount, session.sideA.items.size(),
		         clientB.clientId, session.sideB.goldAmount, session.sideB.items.size());
	}

	void TradeSystem::RemoveSession(uint32_t sessionIndex, std::string_view reason)
	{
		const TradeSession& s = m_sessions[sessionIndex];
		LOG_INFO(Gameplay, "[TradeSystem] Session removed: A={} B={} reason={}",
		         s.sideAClientId, s.sideBClientId, reason);
		m_sessions.erase(m_sessions.begin() + static_cast<ptrdiff_t>(sessionIndex));
	}

	TradeSide* TradeSystem::SelectSide(TradeSession& session, uint32_t clientId)
	{
		if (session.sideAClientId == clientId) { return &session.sideA; }
		if (session.sideBClientId == clientId) { return &session.sideB; }
		return nullptr;
	}

	const TradeSide* TradeSystem::SelectSide(const TradeSession& session, uint32_t clientId) const
	{
		if (session.sideAClientId == clientId) { return &session.sideA; }
		if (session.sideBClientId == clientId) { return &session.sideB; }
		return nullptr;
	}

	size_t TradeSystem::FindSessionIndex(uint32_t clientId) const
	{
		for (size_t i = 0; i < m_sessions.size(); ++i)
		{
			if (m_sessions[i].sideAClientId == clientId ||
			    m_sessions[i].sideBClientId == clientId)
			{
				return i;
			}
		}
		return std::numeric_limits<size_t>::max();
	}
}
