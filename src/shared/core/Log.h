#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
namespace engine::core
{
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
        Off = 6
    };

    /// M45 — Filtres bitmask par catégorie (inspiré cmangos Log.h LogFilters).
    /// Permet de tracer une seule sous-feature en debug sans polluer les autres.
    /// Un filtre est un seul bit ; le masque global est un \c uint64_t.
    /// Activation via \c log.filters.<name> = true dans \c config.json
    /// (où \c <name> est en snake_case, voir \c LogFilterFromName).
    enum class LogFilter : uint64_t
    {
        None              = 0,
        TransportMoves    = 1ull << 0,
        CreatureMoves     = 1ull << 1,
        VisibilityChanges = 1ull << 2,
        Weather           = 1ull << 3,
        PlayerStats       = 1ull << 4,
        SqlText           = 1ull << 5,
        PlayerMoves       = 1ull << 6,
        Damage            = 1ull << 7,
        Combat            = 1ull << 8,
        SpellCast         = 1ull << 9,
        Pathfinding       = 1ull << 10,
        MapLoading        = 1ull << 11,
        EventAiDev        = 1ull << 12,
        DbScriptsDev      = 1ull << 13,
        PacketIo          = 1ull << 14,
        ChatRelay         = 1ull << 15,
        Auth              = 1ull << 16,
        Session           = 1ull << 17,
        Db                = 1ull << 18,
        Migration         = 1ull << 19,
        Custom            = 1ull << 20,
    };

    /// Nom snake_case → bit. Renvoie \c LogFilter::None si inconnu.
    LogFilter LogFilterFromName(std::string_view name);

    struct LogSettings
    {
        /// Minimum level that will be emitted (console + file).
        LogLevel level = LogLevel::Info;
        /// Seuil distinct pour le fichier principal (cmangos LogFileLevel).
        /// Si \c LogLevel::Off (par défaut), suit \c level.
        LogLevel fileLevel = LogLevel::Off;
        /// Relative path to the log file to append to (created if missing).
        /// Empty string means no file output.
        std::string filePath;
        /// If true, also write logs to stdout/stderr.
        bool console = true;
        /// If true, flush file output after each line (recommended for Debug builds).
        bool flushAlways = false;
        /// Max size per log file in MB before rotation (0 = no rotation).
        size_t rotation_size_mb = 10;
        /// Number of rotated files to retain (used as max_files for rotating sink).
        int retention_days = 7;
        /// Fichiers journaux additionnels : nom du sous-système (argument des macros \c LOG_* , ex. \c Smtp) → nom de fichier relatif au dossier du fichier principal \c filePath (ou \c "." si \c filePath n'a pas de dossier). Les lignes sont dupliquées : elles vont aussi dans le fichier principal et la console si actifs.
        std::unordered_map<std::string, std::string> subsystemFiles;
        /// M44.4 — Si \c true, les lignes sont émises au format JSON (un objet JSON par ligne,
        /// jsonl). Active via la clé config \c log.json en prod pour ingestion par Loki/ELK.
        /// Format : \c {"timestamp":"YYYY-MM-DDTHH:MM:SSZ","level":"INFO","subsystem":"Net","message":"..."}.
        /// Le timestamp est en UTC ISO-8601. Les caractères spéciaux JSON (quote, backslash,
        /// newline, contrôle) sont échappés dans \c subsystem et \c message.
        bool jsonOutput = false;
        /// M45 — Masque initial des filtres bitmask. Mettre les bits de \c LogFilter à 1 pour
        /// activer la catégorie correspondante. Voir \c LOG_FILTERED.
        uint64_t enabledFilters = 0;
        /// M45 — Couleurs ANSI (POSIX) ou attributs Win32 sur la console pour différencier
        /// les niveaux. Désactivé automatiquement si stdout n'est pas un TTY (pipe, fichier, Docker).
        bool consoleColors = true;
        /// M45 — Fichier dédié aux commandes GM (mode global). Vide = pas de log GM dédié.
        /// Ignoré si \c gmLogPerAccount est true.
        std::string gmLogFile;
        /// M45 — Si \c true, chaque commande GM est écrite dans un fichier nommé
        /// \c <gmLogDir>/account_<id>.log (un fichier par compte GM).
        bool gmLogPerAccount = false;
        /// M45 — Dossier des logs GM per-account (créé s'il n'existe pas). Défaut : \c gmlogs.
        std::string gmLogDir;
        /// M45 — Fichier dédié aux événements personnages (création / suppression / login).
        std::string charLogFile;
        /// M45 — Fichier dédié aux erreurs DB (mauvais data, contraintes, etc.).
        std::string dbErrorLogFile;
        /// M45 — Fichier dédié aux dumps de paquets réseau (16 octets/ligne + ASCII).
        /// Si vide, les dumps vont dans le log principal sous le sous-système \c "Packet".
        std::string packetLogFile;
        /// M45 — Fichier dédié aux logs custom (Log::WriteCustom).
        std::string customLogFile;
    };
    class Log final
    {
    public:
        /// Returns a filename with timestamp suffix: prefix-YYYYMMDD-HHMMSS.log
        static std::string MakeTimestampedFilename(std::string_view prefix);
        /// Initialize logging. Safe to call once; subsequent calls overwrite settings.
        static void Init(const LogSettings& settings);
        /// Shutdown logging (closes file handle). Safe to call multiple times.
        static void Shutdown();
        /// Returns true if the logger has been successfully initialized and is ready to write.
        static bool IsActive();
        /// Get current minimum log level.
        static LogLevel GetLevel();
        /// Set current minimum log level.
        static void SetLevel(LogLevel level);

        /// Type d'un observateur de lignes de log : reçoit `(niveau, sous-système,
        /// message)` pour chaque ligne émise (au-dessus du seuil), AVANT l'écriture
        /// fichier/console et **hors** du verrou de log. Sert à alimenter une
        /// console in-app (éditeur monde). Sous-projet 1, bloc E.
        using LogSink = std::function<void(LogLevel, const char* /*subsystem*/, std::string_view /*message*/)>;

        /// Installe (ou retire avec un foncteur vide) l'observateur global de
        /// lignes de log. Contrat : appeler **une seule fois au démarrage**, sur
        /// le thread principal, avant que d'autres threads ne loggent (le sink
        /// est ensuite invoqué sans verrou par tous les threads ; l'implémentation
        /// du sink doit être thread-safe). Le serveur ne pose jamais de sink :
        /// comportement inchangé. Effet de bord : remplace le sink courant.
        static void SetSink(LogSink sink);

        /// M45 — Test si un filtre bitmask est actif (atomique, sans verrou).
        /// Utiliser via la macro \c LOG_FILTERED qui court-circuite le \c std::format si false.
        static bool HasFilter(LogFilter f);
        /// M45 — Active ou désactive un filtre (bit unique).
        static void SetFilter(LogFilter f, bool enabled);
        /// M45 — Remplace tout le masque des filtres.
        static void SetFilters(uint64_t mask);
        /// M45 — Renvoie le masque courant.
        static uint64_t GetFilters();

        /// Write a log line for the given level/subsystem and already-formatted message.
        static void WriteLine(LogLevel level, const char* subsystem, std::string_view message);
        /// Format + write a log line.
        template <typename... Args>
        static void Write(LogLevel level, const char* subsystem, std::format_string<Args...> fmt, Args&&... args)
        {
            if (!s_active.load(std::memory_order_acquire))
                return;
            if (level < s_level.load(std::memory_order_relaxed))
                return;
            WriteLine(level, subsystem, std::format(fmt, std::forward<Args>(args)...));
        }

        /// M45 — Log d'une commande GM. Mode global (gmLogFile) ou per-account (gmLogDir/account_<id>.log)
        /// selon \c LogSettings::gmLogPerAccount. La ligne va aussi dans le log principal sous \c "GM".
        /// \param accountId Identifiant compte (utilisé pour le fichier per-account).
        /// \param characterName Nom du personnage qui exécute la commande (peut être vide hors world).
        /// \param command Texte intégral de la commande (ex. \c ".tele Stormwind").
        static void WriteGmCommand(uint32_t accountId, std::string_view characterName, std::string_view command);
        /// M45 — Log d'un événement personnage (création, suppression, EnterWorld, save).
        static void WriteCharLog(uint32_t accountId, uint64_t characterId, std::string_view event);
        /// M45 — Erreur DB persistée dans un fichier dédié (\c dbErrorLogFile).
        static void WriteDbError(std::string_view message);
        /// M45 — Log custom libre (\c customLogFile).
        static void WriteCustom(std::string_view message);
        /// M45 — Dump hex d'un paquet réseau (cmangos outWorldPacketDump).
        /// Format : timestamp + entête (\c connId, direction, opcode, taille) puis hex 16 octets/ligne
        /// avec gutter ASCII. Émis uniquement si \c LogFilter::PacketIo est actif.
        /// \param direction \c "IN" ou \c "OUT".
        /// \param opcode Code opération (numéro logique du paquet).
        /// \param data Pointeur sur le payload (peut être nul si size=0).
        /// \param size Taille en octets.
        /// \param connId Identifiant connexion (0 si inconnu).
        static void WritePacketDump(std::string_view direction, uint16_t opcode,
                                    const void* data, size_t size, uint32_t connId = 0);

    private:
        static std::atomic<LogLevel> s_level;
        static std::atomic<bool>     s_active;
        static std::atomic<uint64_t> s_filters;
    };
}
#define LOG_TRACE(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Trace, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Debug, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(subsystem, format, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Info,  #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(subsystem, format, ...)  ::engine::core::Log::Write(::engine::core::LogLevel::Warn,  #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_ERROR(subsystem, format, ...) ::engine::core::Log::Write(::engine::core::LogLevel::Error, #subsystem, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_FATAL(subsystem, format, ...)                    \
    do                                                       \
    {                                                        \
        ::engine::core::Log::Write(::engine::core::LogLevel::Fatal, #subsystem, format __VA_OPT__(,) __VA_ARGS__); \
        ::std::abort();                                      \
    } while (false)

/// M45 — Émet une ligne uniquement si le filtre est actif ET le niveau passe le seuil.
/// Le test \c HasFilter est atomique sans verrou ; le \c std::format est court-circuité
/// quand le filtre est désactivé (zéro coût en hot path).
/// Usage : \c LOG_FILTERED(Debug, CreatureMoves, AI, "creature {} -> {}", id, pos);
#define LOG_FILTERED(level, filter, subsystem, format, ...)                                          \
    do                                                                                               \
    {                                                                                                \
        if (::engine::core::Log::HasFilter(::engine::core::LogFilter::filter))                       \
            ::engine::core::Log::Write(::engine::core::LogLevel::level, #subsystem, format __VA_OPT__(,) __VA_ARGS__); \
    } while (false)
