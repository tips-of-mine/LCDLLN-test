// src/shared/network/WorldClockPayloads.h
#pragma once
#include <cstdint>
#include <vector>

namespace engine::network::worldclock
{
    enum class WorldClockStatus : uint8_t { Ok = 0, Unauthorized = 1 };

    struct WorldClockStateRequest {}; // vide

    /// Etat complet de l'horloge (reponse 204 ET notification 205 : memes champs).
    struct WorldClockStateResponse
    {
        WorldClockStatus status = WorldClockStatus::Ok;
        uint64_t serverTimeUnixMs    = 0;
        uint64_t epochRefUnixMs      = 0;
        float    timeScaleRealMinPerDay = 60.0f;
        double   offsetGameSec       = 0.0;
        uint8_t  paused              = 0;
        double   pausedAtGameSec     = 0.0;
        double   lunarPeriodGameSec  = 0.0;
    };

    void BuildWorldClockStateRequestPayload(std::vector<uint8_t>& out);
    void BuildWorldClockStateResponsePayload(const WorldClockStateResponse& msg, std::vector<uint8_t>& out);
    void BuildWorldClockChangeNotificationPayload(const WorldClockStateResponse& msg, std::vector<uint8_t>& out);

    bool ParseWorldClockStateRequestPayload(const uint8_t* data, size_t size, WorldClockStateRequest& out);
    bool ParseWorldClockStateResponsePayload(const uint8_t* data, size_t size, WorldClockStateResponse& out);
    bool ParseWorldClockChangeNotificationPayload(const uint8_t* data, size_t size, WorldClockStateResponse& out);
}
