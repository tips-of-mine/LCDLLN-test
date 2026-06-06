// src/shared/world/WorldClock.h
#pragma once
#include <cstdint>
#include <cmath>

namespace engine::world
{
    /// Parametres de l'horloge monde. Source de verite cote master ; repliques
    /// au client. La formule est PURE et deterministe (memes entrees -> meme
    /// sortie sur master, shard, client) pour garantir la synchronisation.
    struct WorldClockParams
    {
        uint64_t epochRefUnixMs        = 1767225600000ull; ///< 2026-01-01 00:00 UTC.
        float    timeScaleRealMinPerDay = 60.0f;           ///< minutes REELLES par jour de jeu (60 = 1 jour/h).
        double   offsetGameSec          = 0.0;             ///< decalage runtime (/settime). Non persiste.
        bool     paused                 = false;           ///< /pausetime : fige l'horloge.
        double   pausedAtGameSec        = 0.0;             ///< valeur figee quand paused.
        double   lunarPeriodGameSec     = 16.0 * 86400.0;  ///< cycle lunaire en SECONDES DE JEU (16 jours de jeu).
    };

    /// Secondes de jeu ecoulees depuis l'epoch, d'apres l'horloge reelle.
    inline double GameSeconds(uint64_t nowUnixMs, const WorldClockParams& p)
    {
        if (p.paused) return p.pausedAtGameSec;
        const double realSec = (nowUnixMs >= p.epochRefUnixMs)
            ? static_cast<double>(nowUnixMs - p.epochRefUnixMs) / 1000.0 : 0.0;
        // Garde defensive : timeScale <= 0 (config/wire corrompu) retombe sur le
        // defaut 60 plutot que de produire une division par zero (gsPerRs = inf).
        const double scaleMinPerDay = (p.timeScaleRealMinPerDay > 0.0f)
            ? static_cast<double>(p.timeScaleRealMinPerDay) : 60.0;
        const double gsPerRs = 86400.0 / (scaleMinPerDay * 60.0);
        return realSec * gsPerRs + p.offsetGameSec;
    }

    /// Heure du jour [0,24) a partir des secondes de jeu.
    inline float TimeOfDayHours(double gameSec)
    {
        double h = std::fmod(gameSec / 3600.0, 24.0);
        if (h < 0.0) h += 24.0;
        return static_cast<float>(h);
    }

    /// Phase de lune 0..15 a partir des secondes de jeu et de la periode (s de jeu).
    inline uint8_t LunarPhase(double gameSec, double lunarPeriodGameSec)
    {
        if (lunarPeriodGameSec <= 0.0) return 0u;
        double pos = std::fmod(gameSec, lunarPeriodGameSec);
        if (pos < 0.0) pos += lunarPeriodGameSec;
        int phase = static_cast<int>((pos / lunarPeriodGameSec) * 16.0);
        if (phase < 0) phase = 0;
        if (phase > 15) phase = 15;
        return static_cast<uint8_t>(phase);
    }

    /// Phase qualitative de la journee (pour ambiances/logique gameplay).
    enum class DayPhase : uint8_t { Night, Dawn, Day, Dusk };

    /// Phase de la journee a partir de l'heure [0,24). Seuils repris de l'ancien
    /// WorldClock master : Night [22..5), Dawn [5..7), Day [7..19), Dusk [19..22).
    inline DayPhase DayPhaseAt(float hours)
    {
        if (hours >= 22.0f || hours < 5.0f) return DayPhase::Night;
        if (hours < 7.0f)  return DayPhase::Dawn;
        if (hours < 19.0f) return DayPhase::Day;
        return DayPhase::Dusk;
    }
}
