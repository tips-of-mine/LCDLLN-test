// M32.4 — Guild bank implementation.
// Enforces per-rank per-tab permissions and appends an audit-log entry
// for every successful withdrawal.

#include "engine/server/GuildBank.h"
#include "engine/core/Log.h"

#include <ctime>

namespace engine::server
{
    // =========================================================================
    // Init / Shutdown
    // =========================================================================

    bool GuildBank::Init(uint64_t guildId, uint8_t tabCount)
    {
        if (guildId == 0u)
        {
            LOG_ERROR(Server, "[GuildBank] Init FAILED: guildId must be non-zero");
            return false;
        }

        // Clamp tabCount to allowed range.
        if (tabCount < kGuildBankMinTabs)
            tabCount = kGuildBankMinTabs;
        else if (tabCount > kGuildBankMaxTabs)
            tabCount = kGuildBankMaxTabs;

        m_guildId = guildId;
        m_tabs.clear();
        m_tabs.resize(tabCount);

        // Default: give Guild Master (rankId=0) full permissions on every tab.
        constexpr uint8_t kAllPerms =
            static_cast<uint8_t>(GuildBankTabPermission::View)    |
            static_cast<uint8_t>(GuildBankTabPermission::Deposit)  |
            static_cast<uint8_t>(GuildBankTabPermission::Withdraw);

        for (uint8_t i = 0u; i < tabCount; ++i)
        {
            m_tabs[i].name                 = std::string("Tab ") + std::to_string(i + 1u);
            m_tabs[i].rankPermissions[0u]  = kAllPerms; // Guild Master has full access.
        }

        m_withdrawLog.clear();
        m_initialized = true;
        LOG_INFO(Server, "[GuildBank] Init OK (guild={}, tabs={})", guildId, tabCount);
        return true;
    }

    void GuildBank::Shutdown()
    {
        m_tabs.clear();
        m_withdrawLog.clear();
        m_initialized = false;
        LOG_INFO(Server, "[GuildBank] Shutdown (guild={})", m_guildId);
    }

    // =========================================================================
    // Deposit
    // =========================================================================

    bool GuildBank::Deposit(uint64_t  playerId,
                             uint8_t   rankId,
                             uint8_t   tabIndex,
                             uint8_t   slotIndex,
                             ItemStack item)
    {
        if (!m_initialized)
        {
            LOG_ERROR(Server, "[GuildBank] Deposit: bank not initialized (guild={})", m_guildId);
            return false;
        }

        if (tabIndex >= static_cast<uint8_t>(m_tabs.size()))
        {
            LOG_WARN(Server, "[GuildBank] Deposit: tabIndex {} out of range (guild={}, tabs={})",
                     tabIndex, m_guildId, m_tabs.size());
            return false;
        }

        if (!HasTabPermission(rankId, tabIndex, GuildBankTabPermission::Deposit))
        {
            LOG_WARN(Server,
                     "[GuildBank] Deposit: player {} rank {} lacks Deposit on tab {} (guild={})",
                     playerId, rankId, tabIndex, m_guildId);
            return false;
        }

        if (slotIndex >= kGuildBankSlotsPerTab)
        {
            LOG_WARN(Server, "[GuildBank] Deposit: slotIndex {} out of range (guild={}, tab={})",
                     slotIndex, m_guildId, tabIndex);
            return false;
        }

        if (item.itemId == 0u || item.quantity == 0u)
        {
            LOG_WARN(Server,
                     "[GuildBank] Deposit: invalid item (id={}, qty={}) by player {} (guild={})",
                     item.itemId, item.quantity, playerId, m_guildId);
            return false;
        }

        ItemStack& slot = m_tabs[tabIndex].slots[slotIndex];
        if (slot.itemId != 0u)
        {
            LOG_WARN(Server,
                     "[GuildBank] Deposit: slot {}/{} already occupied by item {} (guild={})",
                     tabIndex, slotIndex, slot.itemId, m_guildId);
            return false;
        }

        slot = item;
        LOG_INFO(Server,
                 "[GuildBank] Deposit: player {} deposited item {} x{} into tab {}/slot {} (guild={})",
                 playerId, item.itemId, item.quantity, tabIndex, slotIndex, m_guildId);
        return true;
    }

    // =========================================================================
    // Withdraw
    // =========================================================================

    bool GuildBank::Withdraw(uint64_t   playerId,
                              uint8_t    rankId,
                              uint8_t    tabIndex,
                              uint8_t    slotIndex,
                              uint32_t   quantity,
                              ItemStack& outItem)
    {
        if (!m_initialized)
        {
            LOG_ERROR(Server, "[GuildBank] Withdraw: bank not initialized (guild={})", m_guildId);
            return false;
        }

        if (tabIndex >= static_cast<uint8_t>(m_tabs.size()))
        {
            LOG_WARN(Server, "[GuildBank] Withdraw: tabIndex {} out of range (guild={}, tabs={})",
                     tabIndex, m_guildId, m_tabs.size());
            return false;
        }

        if (!HasTabPermission(rankId, tabIndex, GuildBankTabPermission::Withdraw))
        {
            LOG_WARN(Server,
                     "[GuildBank] Withdraw: player {} rank {} lacks Withdraw on tab {} (guild={})",
                     playerId, rankId, tabIndex, m_guildId);
            return false;
        }

        if (slotIndex >= kGuildBankSlotsPerTab)
        {
            LOG_WARN(Server, "[GuildBank] Withdraw: slotIndex {} out of range (guild={}, tab={})",
                     slotIndex, m_guildId, tabIndex);
            return false;
        }

        ItemStack& slot = m_tabs[tabIndex].slots[slotIndex];
        if (slot.itemId == 0u || slot.quantity == 0u)
        {
            LOG_WARN(Server,
                     "[GuildBank] Withdraw: slot {}/{} is empty (guild={})",
                     tabIndex, slotIndex, m_guildId);
            return false;
        }

        // Quantity=0 means "take the full stack".
        const uint32_t taken = (quantity == 0u || quantity > slot.quantity)
                                   ? slot.quantity
                                   : quantity;

        outItem = { slot.itemId, taken };
        slot.quantity -= taken;
        if (slot.quantity == 0u)
            slot.itemId = 0u;

        // Append audit-log entry.
        GuildBankWithdrawEntry entry;
        entry.playerId         = playerId;
        entry.timestampUnixSec = static_cast<uint64_t>(std::time(nullptr));
        entry.tabIndex         = tabIndex;
        entry.slotIndex        = slotIndex;
        entry.itemId           = outItem.itemId;
        entry.quantity         = taken;
        m_withdrawLog.push_back(entry);

        LOG_INFO(Server,
                 "[GuildBank] Withdraw: player {} withdrew item {} x{} from tab {}/slot {} (guild={})",
                 playerId, outItem.itemId, taken, tabIndex, slotIndex, m_guildId);
        return true;
    }

    // =========================================================================
    // Permissions
    // =========================================================================

    bool GuildBank::SetTabPermission(uint8_t tabIndex, uint8_t rankId, GuildBankTabPermission perms)
    {
        if (!m_initialized)
        {
            LOG_ERROR(Server, "[GuildBank] SetTabPermission: bank not initialized (guild={})", m_guildId);
            return false;
        }

        if (tabIndex >= static_cast<uint8_t>(m_tabs.size()))
        {
            LOG_WARN(Server,
                     "[GuildBank] SetTabPermission: tabIndex {} out of range (guild={}, tabs={})",
                     tabIndex, m_guildId, m_tabs.size());
            return false;
        }

        if (rankId >= kGuildBankMaxRanks)
        {
            LOG_WARN(Server,
                     "[GuildBank] SetTabPermission: rankId {} out of range (guild={})",
                     rankId, m_guildId);
            return false;
        }

        m_tabs[tabIndex].rankPermissions[rankId] = static_cast<uint8_t>(perms);
        LOG_DEBUG(Server,
                  "[GuildBank] SetTabPermission: tab {} rank {} perms={} (guild={})",
                  tabIndex, rankId, static_cast<uint32_t>(perms), m_guildId);
        return true;
    }

    bool GuildBank::HasTabPermission(uint8_t rankId,
                                      uint8_t tabIndex,
                                      GuildBankTabPermission perm) const
    {
        if (tabIndex >= static_cast<uint8_t>(m_tabs.size()))
            return false;
        if (rankId >= kGuildBankMaxRanks)
            return false;
        return (m_tabs[tabIndex].rankPermissions[rankId] & static_cast<uint8_t>(perm)) != 0u;
    }

    // =========================================================================
    // Queries
    // =========================================================================

    const GuildBankTab* GuildBank::GetTab(uint8_t tabIndex) const
    {
        if (tabIndex >= static_cast<uint8_t>(m_tabs.size()))
            return nullptr;
        return &m_tabs[tabIndex];
    }

} // namespace engine::server
