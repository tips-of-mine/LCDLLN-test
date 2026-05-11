#pragma once
// M32.4 — Tabard item constants and guild emblem data definition.
// The tabard is an equippable chest item that carries the guild emblem overlay.
// The emblem is serialized as compact JSON and stored in the guilds table.

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
    /// Item type id for the guild tabard (equippable chest slot, M32.4).
    inline constexpr uint32_t kItemIdTabard = 9001u;

    /// Equipment slot index for the chest (matches M16.3 equip slot layout).
    inline constexpr uint32_t kEquipSlotChest = 4u;

    /// Visual emblem definition stored per-guild (M32.4).
    /// Serialized as compact JSON and persisted in the guilds.emblem column.
    /// Client must cache this value; do NOT re-fetch every frame.
    struct GuildEmblem
    {
        uint32_t bgColor     = 0xFF1A1A1Au; ///< ARGB background fill color.
        uint32_t borderColor = 0xFFFFFFFFu; ///< ARGB border color.
        uint32_t symbolId    = 0u;          ///< Atlas symbol index (0 = none / blank).
    };

    /// Serialize \p emblem to a compact JSON string.
    /// Produces the format: {"bg":<u32>,"border":<u32>,"sym":<u32>}
    std::string SerializeEmblem(const GuildEmblem& emblem);

    /// Parse a GuildEmblem from the JSON string produced by SerializeEmblem.
    /// Returns true on success; leaves \p out unchanged on parse failure.
    bool ParseEmblem(std::string_view json, GuildEmblem& out);

} // namespace engine::server
