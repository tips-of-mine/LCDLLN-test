/**
 * @file CharacterDb.cpp
 * @brief SQLite character persistence implementation (M14.4).
 */

#include "engine/persistence/CharacterDb.h"
#include <cstdio>
#include <cstring>
#include <sqlite3.h>

namespace engine::persistence {

namespace {
sqlite3* g_db = nullptr;
}

bool Open(const std::string& path) {
    if (g_db) return false;
    if (sqlite3_open(path.c_str(), &g_db) != SQLITE_OK) {
        if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
        return false;
    }
    return true;
}

void Close() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

static bool Exec(const char* sql) {
    if (!g_db) return false;
    char* err = nullptr;
    if (sqlite3_exec(g_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        if (err) { sqlite3_free(err); }
        return false;
    }
    return true;
}

bool CreateTablesIfNeeded() {
    if (!g_db) return false;
    if (!Exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"))
        return false;
    int existing = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT version FROM schema_version LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            existing = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (existing >= kSchemaVersion)
        return true;
    if (existing == 0) {
        if (!Exec("INSERT INTO schema_version (version) VALUES (0)"))
            return false;
    }
    if (existing < 1) {
        if (!Exec("CREATE TABLE IF NOT EXISTS characters (character_id INTEGER PRIMARY KEY AUTOINCREMENT, zone_id INTEGER NOT NULL DEFAULT 0, pos_x REAL NOT NULL DEFAULT 0, pos_y REAL NOT NULL DEFAULT 0, pos_z REAL NOT NULL DEFAULT 0, hp INTEGER NOT NULL DEFAULT 100, max_hp INTEGER NOT NULL DEFAULT 100)"))
            return false;
        if (!Exec("CREATE TABLE IF NOT EXISTS inventory_items (character_id INTEGER NOT NULL, item_id INTEGER NOT NULL, count INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (character_id, item_id), FOREIGN KEY (character_id) REFERENCES characters(character_id))"))
            return false;
        if (!Exec("UPDATE schema_version SET version = 1"))
            return false;
    }
    return true;
}

int64_t InsertCharacter(const CharacterState& state) {
    if (!g_db) return 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "INSERT INTO characters (zone_id, pos_x, pos_y, pos_z, hp, max_hp) VALUES (?1,?2,?3,?4,?5,?6)", -1, &stmt, nullptr) != SQLITE_OK)
        return 0;
    sqlite3_bind_int(stmt, 1, state.zoneId);
    sqlite3_bind_double(stmt, 2, static_cast<double>(state.posX));
    sqlite3_bind_double(stmt, 3, static_cast<double>(state.posY));
    sqlite3_bind_double(stmt, 4, static_cast<double>(state.posZ));
    sqlite3_bind_int(stmt, 5, static_cast<int>(state.hp));
    sqlite3_bind_int(stmt, 6, static_cast<int>(state.maxHp));
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return 0;
    return sqlite3_last_insert_rowid(g_db);
}

bool LoadCharacter(int64_t characterId, CharacterState& out) {
    if (!g_db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT zone_id, pos_x, pos_y, pos_z, hp, max_hp FROM characters WHERE character_id = ?1", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, characterId);
    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out.zoneId = sqlite3_column_int(stmt, 0);
        out.posX = static_cast<float>(sqlite3_column_double(stmt, 1));
        out.posY = static_cast<float>(sqlite3_column_double(stmt, 2));
        out.posZ = static_cast<float>(sqlite3_column_double(stmt, 3));
        out.hp = static_cast<uint32_t>(sqlite3_column_int(stmt, 4));
        out.maxHp = static_cast<uint32_t>(sqlite3_column_int(stmt, 5));
        ok = true;
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool SaveCharacter(int64_t characterId, const CharacterState& state) {
    if (!g_db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "UPDATE characters SET zone_id=?1, pos_x=?2, pos_y=?3, pos_z=?4, hp=?5, max_hp=?6 WHERE character_id=?7", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int(stmt, 1, state.zoneId);
    sqlite3_bind_double(stmt, 2, static_cast<double>(state.posX));
    sqlite3_bind_double(stmt, 3, static_cast<double>(state.posY));
    sqlite3_bind_double(stmt, 4, static_cast<double>(state.posZ));
    sqlite3_bind_int(stmt, 5, static_cast<int>(state.hp));
    sqlite3_bind_int(stmt, 6, static_cast<int>(state.maxHp));
    sqlite3_bind_int64(stmt, 7, characterId);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool LoadInventory(int64_t characterId, std::unordered_map<uint32_t, uint32_t>& out) {
    if (!g_db) return false;
    out.clear();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "SELECT item_id, count FROM inventory_items WHERE character_id = ?1", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(stmt, 1, characterId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint32_t itemId = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
        uint32_t count = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
        out[itemId] = count;
    }
    sqlite3_finalize(stmt);
    return true;
}

bool SaveInventory(int64_t characterId, const std::unordered_map<uint32_t, uint32_t>& items) {
    if (!g_db) return false;
    sqlite3_stmt* delStmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "DELETE FROM inventory_items WHERE character_id = ?1", -1, &delStmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int64(delStmt, 1, characterId);
    if (sqlite3_step(delStmt) != SQLITE_DONE) {
        sqlite3_finalize(delStmt);
        return false;
    }
    sqlite3_finalize(delStmt);
    sqlite3_stmt* insStmt = nullptr;
    if (sqlite3_prepare_v2(g_db, "INSERT INTO inventory_items (character_id, item_id, count) VALUES (?1,?2,?3)", -1, &insStmt, nullptr) != SQLITE_OK)
        return false;
    for (const auto& p : items) {
        if (p.second == 0u) continue;
        sqlite3_bind_int64(insStmt, 1, characterId);
        sqlite3_bind_int(insStmt, 2, static_cast<int>(p.first));
        sqlite3_bind_int(insStmt, 3, static_cast<int>(p.second));
        if (sqlite3_step(insStmt) != SQLITE_DONE) {
            sqlite3_finalize(insStmt);
            return false;
        }
        sqlite3_reset(insStmt);
    }
    sqlite3_finalize(insStmt);
    return true;
}

} // namespace engine::persistence
