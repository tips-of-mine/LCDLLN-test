#pragma once
// Wave 5 Persistence (Phase 4.39b) - MysqlSkillStore : persiste la
// skill book per-character (V1 = per-account ; voir migration 0053).
// Cible UNIX (master).
//
// La table character_skills a une cle composite (character_id, skill_id).
// V1 : character_id contient l'account_id ; le rename arrivera quand
// le CharacterStore sera branche (Wave 6).
//
// Lifecycle :
//   - LoadForCharacter(charId) appele au 1er HandlePacket par account
//     (sinon le seed in-memory du handler s'execute).
//   - Upsert(row) apres Learn ou Use gain effectif.

#include <cstdint>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::skills
{
	struct SkillRow
	{
		uint64_t characterId = 0;
		uint32_t skillId     = 0;
		uint32_t value       = 0;
		uint32_t cap         = 0;
		uint32_t bonus       = 0;
	};

	class MysqlSkillStore final
	{
	public:
		explicit MysqlSkillStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		bool IsAvailable() const noexcept;

		/// Charge toutes les lignes pour un character. Vide si DB indisponible
		/// ou aucune ligne. Caller (SkillHandler) seed son starter set en
		/// fallback.
		std::vector<SkillRow> LoadForCharacter(uint64_t characterId) const;

		/// Upsert d'un skill : INSERT ... ON DUPLICATE KEY UPDATE value/cap/bonus.
		bool Upsert(const SkillRow& row);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
