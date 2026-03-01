#pragma once

/**
 * @file CharacterDb.h
 * @brief SQLite persistence for character state and inventory (M14.4). Schema version + migrations.
 */

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::persistence {

/** @brief Schema version for migrations. */
constexpr int kSchemaVersion = 1;

/** @brief Character state (zone, position, hp). */
struct CharacterState {
    int32_t zoneId = 0;
    float posX = 0.f;
    float posY = 0.f;
    float posZ = 0.f;
    uint32_t hp = 100u;
    uint32_t maxHp = 100u;
};

/** @brief Opens DB at path; creates file if missing. */
bool Open(const std::string& path);
/** @brief Closes the DB. */
void Close();
/** @brief Creates tables and runs migrations. */
bool CreateTablesIfNeeded();
/** @brief Inserts new character; returns character_id (0 on error). */
int64_t InsertCharacter(const CharacterState& state);
/** @brief Loads character by id. */
bool LoadCharacter(int64_t characterId, CharacterState& out);
/** @brief Saves character state. */
bool SaveCharacter(int64_t characterId, const CharacterState& state);
/** @brief Loads inventory (itemId -> count). */
bool LoadInventory(int64_t characterId, std::unordered_map<uint32_t, uint32_t>& out);
/** @brief Replaces inventory for character. */
bool SaveInventory(int64_t characterId, const std::unordered_map<uint32_t, uint32_t>& items);

} // namespace engine::persistence
