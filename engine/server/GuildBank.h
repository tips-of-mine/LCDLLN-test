#pragma once
// M32.4 — Guild bank: shared item storage across 6–8 tabs (98 slots each).
// Each tab has per-rank access permissions (view, deposit, withdraw).
// All withdrawals are appended to an in-memory audit log.

#include "engine/server/ReplicationTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
    /// Access flags for one (rank, tab) pair in the guild bank (M32.4).
    enum class GuildBankTabPermission : uint8_t
    {
        None     = 0,
        View     = 1u << 0, ///< Can see items in this tab.
        Deposit  = 1u << 1, ///< Can deposit items into this tab.
        Withdraw = 1u << 2, ///< Can withdraw items from this tab.
    };

    /// Number of item slots per guild bank tab (M32.4 spec).
    inline constexpr size_t kGuildBankSlotsPerTab = 98u;

    /// Minimum number of tabs a guild bank must have (M32.4 spec: 6–8).
    inline constexpr uint8_t kGuildBankMinTabs = 6u;

    /// Maximum number of tabs a guild bank may have (M32.4 spec: 6–8).
    inline constexpr uint8_t kGuildBankMaxTabs = 8u;

    /// Maximum number of distinct rank indices whose permissions can be stored per tab.
    inline constexpr uint8_t kGuildBankMaxRanks = 8u;

    /// One tab of the guild bank (M32.4).
    struct GuildBankTab
    {
        /// Human-readable tab label (e.g. "Tab 1", "Herbs & Ores").
        std::string name;

        /// Item stacks occupying each slot; ItemStack{0,0} means empty.
        std::array<ItemStack, kGuildBankSlotsPerTab> slots{};

        /// Bitmask permissions indexed by rankId (0..kGuildBankMaxRanks-1).
        /// Each byte stores OR-combined GuildBankTabPermission bits for that rank.
        std::array<uint8_t, kGuildBankMaxRanks> rankPermissions{};
    };

    /// One audit-log entry written when a player withdraws from the guild bank (M32.4).
    struct GuildBankWithdrawEntry
    {
        uint64_t playerId         = 0u; ///< Player who performed the withdrawal.
        uint64_t timestampUnixSec = 0u; ///< Wall-clock seconds since Unix epoch.
        uint8_t  tabIndex         = 0u; ///< Tab from which the item was taken.
        uint8_t  slotIndex        = 0u; ///< Slot from which the item was taken.
        uint32_t itemId           = 0u; ///< Item type withdrawn.
        uint32_t quantity         = 0u; ///< Amount withdrawn.
    };

    /// Server-side guild bank manager (M32.4).
    ///
    /// Manages 6–8 tabs of 98 slots each, enforces per-rank per-tab permissions,
    /// and appends an audit-log entry for every successful withdrawal.
    ///
    /// One GuildBank instance per live guild; lifetime is managed by the caller
    /// (e.g. GuildSystem or a higher-level guild manager).
    class GuildBank final
    {
    public:
        GuildBank() = default;

        /// Non-copyable, non-movable.
        GuildBank(const GuildBank&)            = delete;
        GuildBank& operator=(const GuildBank&) = delete;

        /// Initialize the bank for \p guildId with \p tabCount tabs (clamped to [6,8]).
        /// Guild Master (rankId=0) is granted all permissions on every tab by default.
        /// Emits LOG_INFO on success, LOG_ERROR on invalid guildId.
        bool Init(uint64_t guildId, uint8_t tabCount = kGuildBankMinTabs);

        /// Release all bank state.
        /// Emits LOG_INFO on completion.
        void Shutdown();

        bool IsInitialized() const { return m_initialized; }

        // ------------------------------------------------------------------
        // Deposits / Withdrawals
        // ------------------------------------------------------------------

        /// Deposit \p item into \p tabIndex / \p slotIndex for \p playerId (rank \p rankId).
        /// Validates Deposit permission and that the slot is currently empty.
        /// Returns true on success.
        bool Deposit(uint64_t  playerId,
                     uint8_t   rankId,
                     uint8_t   tabIndex,
                     uint8_t   slotIndex,
                     ItemStack item);

        /// Withdraw up to \p quantity from \p tabIndex / \p slotIndex for \p playerId (rank \p rankId).
        /// Validates Withdraw permission, fills \p outItem with the taken stack,
        /// and appends an audit-log entry.
        /// Pass quantity=0 to take the full stack.
        /// Returns true on success.
        bool Withdraw(uint64_t   playerId,
                      uint8_t    rankId,
                      uint8_t    tabIndex,
                      uint8_t    slotIndex,
                      uint32_t   quantity,
                      ItemStack& outItem);

        // ------------------------------------------------------------------
        // Permissions
        // ------------------------------------------------------------------

        /// Override the permission bitmask for \p rankId on \p tabIndex.
        /// Callers are responsible for verifying that the requesting player has
        /// authority (e.g. Guild Master) before calling this function.
        /// Returns true on success.
        bool SetTabPermission(uint8_t tabIndex, uint8_t rankId, GuildBankTabPermission perms);

        /// Return true when \p rankId holds \p perm on \p tabIndex.
        bool HasTabPermission(uint8_t rankId, uint8_t tabIndex, GuildBankTabPermission perm) const;

        // ------------------------------------------------------------------
        // Queries
        // ------------------------------------------------------------------

        /// Return a const pointer to the tab at \p tabIndex; nullptr when out of range.
        const GuildBankTab* GetTab(uint8_t tabIndex) const;

        /// Return the complete audit log of all withdrawal operations.
        const std::vector<GuildBankWithdrawEntry>& GetWithdrawLog() const { return m_withdrawLog; }

        /// Return the number of active tabs in this bank.
        uint8_t GetTabCount() const { return static_cast<uint8_t>(m_tabs.size()); }

    private:
        uint64_t                            m_guildId     = 0u;
        std::vector<GuildBankTab>           m_tabs;
        std::vector<GuildBankWithdrawEntry> m_withdrawLog;
        bool                                m_initialized = false;
    };

} // namespace engine::server
