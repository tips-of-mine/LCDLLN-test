#pragma once

#include <cstdint>
#include <string>

namespace engine::core { class Config; }

namespace engine::server
{
    /// Enregistre le serveur master dans la table game_servers au démarrage.
    /// Un seul enregistrement par (host, port) — UNIQUE KEY garantit l'idempotence.
    class ServerRegistry
    {
    public:
        ServerRegistry() = default;
        ~ServerRegistry() { Disconnect(); }

        /// Lit name/host/port/max_players depuis config et fait un UPSERT dans game_servers.
        /// Retourne false si la connexion DB échoue (non bloquant : le serveur continue).
        bool RegisterSelf(const engine::core::Config& cfg);

        /// Passe le statut à 'offline' dans game_servers. Appelé à l'arrêt propre.
        void SetOffline();

        uint32_t GetServerId() const { return m_serverId; }
        const std::string& GetName() const { return m_name; }
        const std::string& GetHost() const { return m_host; }
        uint16_t GetPort() const { return m_port; }
        uint32_t GetMaxPlayers() const { return m_maxPlayers; }
        bool IsRegistered() const { return m_registered; }

    private:
        uint32_t    m_serverId   = 0;
        std::string m_name;
        std::string m_host;
        uint16_t    m_port       = 0;
        uint32_t    m_maxPlayers = 0;
        bool        m_registered = false;

        // Connexion MySQL stockée comme void* pour éviter mysql.h dans le header
        void* m_conn = nullptr;

        bool Connect(const engine::core::Config& cfg);
        void Disconnect();
    };
}
