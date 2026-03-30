#pragma once

#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <vector>

namespace engine::server
{
    /// Server-side trade session states (M35.3).
    enum class TradeState : uint8_t
    {
        Pending   = 0, ///< Initiator sent /trade; waiting for responder to accept.
        Open      = 1, ///< Both players have the trade window open; items/gold can be modified.
        Reviewing = 2, ///< Both players have locked; 5-second review timer is active.
        Complete  = 3, ///< Server validated and atomically swapped inventories.
        Cancelled = 4  ///< One player cancelled or disconnected; items returned.
    };

    /// One player's side of an open trade offer (M35.3).
    struct TradeOffer
    {
        std::vector<ItemStack> items; ///< Items offered; validated against inventory on lock.
        uint32_t gold      = 0;      ///< Gold offered (validated against wallet on swap).
        bool     locked    = false;  ///< Player clicked "Lock" — enters review phase.
        bool     confirmed = false;  ///< Player clicked "Confirm" — irreversible accept.
    };

    /// Active player-to-player trade session (M35.3).
    struct TradeSession
    {
        uint32_t   sessionId         = 0;
        uint32_t   initiatorClientId = 0; ///< Client who sent /trade.
        uint32_t   responderClientId = 0; ///< Client who accepted.
        TradeOffer initiatorOffer;
        TradeOffer responderOffer;
        TradeState state             = TradeState::Pending;
        uint32_t   reviewEndTick     = 0; ///< Server tick when the 5-second review expires.
    };
}
