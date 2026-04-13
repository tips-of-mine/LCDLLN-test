#include "engine/server/ServerRegistry.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
    namespace
    {
        std::string EscapeMysql(MYSQL* mysql, std::string_view v)
        {
            if (!mysql)
                return {};
            std::vector<char> buf(v.size() * 2 + 1);
            unsigned long w = mysql_real_escape_string(mysql, buf.data(), v.data(), static_cast<unsigned long>(v.size()));
            return std::string(buf.data(), w);
        }
    } // namespace

    bool ServerRegistry::Connect(const engine::core::Config& cfg)
    {
        const std::string host     = cfg.GetString("db.host", "");
        const std::string user     = cfg.GetString("db.user", "");
        const std::string password = cfg.GetString("db.password", "");
        const std::string database = cfg.GetString("db.database", "lcdlln_master");
        const unsigned int port    = static_cast<unsigned int>(cfg.GetInt("db.port", 3306));

        if (host.empty())
        {
            LOG_WARN(Core, "[ServerRegistry] Connect: db.host est vide, enregistrement ignoré");
            return false;
        }

        MYSQL* mysql = mysql_init(nullptr);
        if (!mysql)
        {
            LOG_ERROR(Core, "[ServerRegistry] Connect: mysql_init échoué");
            return false;
        }

        bool reconnect = false;
        mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);

        if (!mysql_real_connect(mysql, host.c_str(), user.c_str(),
                password.empty() ? nullptr : password.c_str(),
                database.c_str(), port, nullptr, 0))
        {
            LOG_ERROR(Core, "[ServerRegistry] Connect: mysql_real_connect échoué: {}", mysql_error(mysql));
            mysql_close(mysql);
            return false;
        }

        m_conn = static_cast<void*>(mysql);
        LOG_INFO(Core, "[ServerRegistry] Connecté à {}:{}/{}", host, port, database);
        return true;
    }

    void ServerRegistry::Disconnect()
    {
        if (m_conn)
        {
            mysql_close(static_cast<MYSQL*>(m_conn));
            m_conn = nullptr;
        }
    }

    bool ServerRegistry::RegisterSelf(const engine::core::Config& cfg)
    {
        m_name       = cfg.GetString("server.name", "Serveur Principal");
        m_host       = cfg.GetString("server.public_host", "127.0.0.1");
        m_port       = static_cast<uint16_t>(cfg.GetInt("server.listen_port", 3840));
        m_maxPlayers = static_cast<uint32_t>(cfg.GetInt("server.max_players", 1000));

        if (!Connect(cfg))
        {
            LOG_WARN(Core, "[ServerRegistry] RegisterSelf: connexion DB échouée, le serveur continue sans enregistrement");
            return false;
        }

        MYSQL* mysql = static_cast<MYSQL*>(m_conn);

        const std::string esc_name       = EscapeMysql(mysql, m_name);
        const std::string esc_host       = EscapeMysql(mysql, m_host);
        const std::string port_str       = std::to_string(m_port);
        const std::string max_pl_str     = std::to_string(m_maxPlayers);

        // UPSERT : INSERT ... ON DUPLICATE KEY UPDATE
        // La table game_servers doit avoir UNIQUE KEY (host, port).
        std::string sql =
            "INSERT INTO game_servers (name, host, port, max_players, status) VALUES ('"
            + esc_name + "','"
            + esc_host + "',"
            + port_str + ","
            + max_pl_str + ",'online') "
            "ON DUPLICATE KEY UPDATE "
            "name=VALUES(name), max_players=VALUES(max_players), status='online'";

        if (mysql_real_query(mysql, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
        {
            LOG_ERROR(Core, "[ServerRegistry] RegisterSelf UPSERT échoué: {}", mysql_error(mysql));
            return false;
        }

        // Récupérer le server_id.
        // mysql_insert_id retourne l'AUTO_INCREMENT de l'INSERT ou le dernier UPDATE si applicable.
        // En cas de ON DUPLICATE KEY UPDATE, MySQL retourne LAST_INSERT_ID() seulement si on appelle
        // LAST_INSERT_ID(id) dans le UPDATE. On fait donc un SELECT pour être robuste.
        const uint64_t insert_id = mysql_insert_id(mysql);
        if (insert_id != 0)
        {
            m_serverId = static_cast<uint32_t>(insert_id);
        }
        else
        {
            // Cas UPDATE (duplicate key) : récupérer l'id existant via SELECT
            const std::string sel =
                "SELECT id FROM game_servers WHERE host='"
                + esc_host + "' AND port=" + port_str + " LIMIT 1";
            if (mysql_real_query(mysql, sel.c_str(), static_cast<unsigned long>(sel.size())) == 0)
            {
                MYSQL_RES* res = mysql_store_result(mysql);
                if (res)
                {
                    MYSQL_ROW row = mysql_fetch_row(res);
                    if (row && row[0])
                        m_serverId = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
                    mysql_free_result(res);
                }
            }
            else
            {
                LOG_WARN(Core, "[ServerRegistry] RegisterSelf SELECT id échoué: {}", mysql_error(mysql));
            }
        }

        m_registered = true;
        LOG_INFO(Core, "[ServerRegistry] Enregistré dans game_servers (server_id={}, name='{}', host={}, port={}, max_players={})",
            m_serverId, m_name, m_host, m_port, m_maxPlayers);
        return true;
    }

    void ServerRegistry::SetOffline()
    {
        if (!m_conn || m_host.empty())
            return;

        MYSQL* mysql = static_cast<MYSQL*>(m_conn);

        const std::string esc_host = EscapeMysql(mysql, m_host);
        const std::string port_str = std::to_string(m_port);

        const std::string sql =
            "UPDATE game_servers SET status='offline' WHERE host='"
            + esc_host + "' AND port=" + port_str;

        if (mysql_real_query(mysql, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
        {
            LOG_WARN(Core, "[ServerRegistry] SetOffline UPDATE échoué: {}", mysql_error(mysql));
            return;
        }

        m_registered = false;
        LOG_INFO(Core, "[ServerRegistry] Statut 'offline' enregistré pour {}:{}", m_host, m_port);
    }

} // namespace engine::server
