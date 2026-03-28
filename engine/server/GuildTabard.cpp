// M32.4 — Guild tabard item constants and emblem serialization.
// Uses only standard C++20 facilities; no external JSON library required.

#include "engine/server/GuildTabard.h"
#include "engine/core/Log.h"

#include <charconv>
#include <format>

namespace engine::server
{
    // =========================================================================
    // SerializeEmblem
    // =========================================================================

    std::string SerializeEmblem(const GuildEmblem& emblem)
    {
        // Produces: {"bg":<u32>,"border":<u32>,"sym":<u32>}
        return std::format(R"({{"bg":{},"border":{},"sym":{}}})",
                           emblem.bgColor, emblem.borderColor, emblem.symbolId);
    }

    // =========================================================================
    // ParseEmblem
    // =========================================================================

    bool ParseEmblem(std::string_view json, GuildEmblem& out)
    {
        // Minimal parser for the exact format produced by SerializeEmblem.
        // Not a general JSON parser: only handles the three known keys.
        auto parseField = [](std::string_view src,
                             std::string_view key,
                             uint32_t&        value) -> bool
        {
            // Build search needle: "key":
            std::string needle;
            needle.reserve(key.size() + 3u);
            needle += '"';
            needle += key;
            needle += "\":";

            const auto pos = src.find(needle);
            if (pos == std::string_view::npos)
                return false;

            const char* start = src.data() + pos + needle.size();
            const char* end   = src.data() + src.size();

            auto [ptr, ec] = std::from_chars(start, end, value);
            if (ec != std::errc{})
                return false;

            return true;
        };

        GuildEmblem tmp = out; // keep original on partial failure
        if (!parseField(json, "bg",     tmp.bgColor))     { LOG_WARN(Server, "[GuildTabard] ParseEmblem: missing 'bg' field");     return false; }
        if (!parseField(json, "border", tmp.borderColor)) { LOG_WARN(Server, "[GuildTabard] ParseEmblem: missing 'border' field"); return false; }
        if (!parseField(json, "sym",    tmp.symbolId))    { LOG_WARN(Server, "[GuildTabard] ParseEmblem: missing 'sym' field");    return false; }

        out = tmp;
        return true;
    }

} // namespace engine::server
