#pragma once
// Creature : Unit + ancrage spawn pool. templateEntry pointe vers
// creature_template (donnees statiques : nom, modele, stats base), spawnId
// vers creature_spawn (instance dans le pool). Les deux sont immuables apres
// construction.
//
// L'IA + le motion stack viennent via composition externe (cf. EventAI Wave 9
// existant, HostileRefManager Wave 19 a venir, Navmesh Wave 24 a venir).
// Creature elle-meme ne porte que l'identite stable + heritage Unit.

#include "src/shardd/entities/Unit.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <cstdint>

namespace engine::server::entities
{
	/// Creature : herite Unit, ajoute identite spawn (template + spawn id).
	class Creature : public Unit
	{
	public:
		/// \param guid identifiant immuable de l'instance
		/// \param templateEntry id de creature_template (donnees statiques)
		/// \param spawnId id de creature_spawn (instance dans le pool)
		Creature(ObjectGuid guid, uint32_t templateEntry, uint32_t spawnId)
			: Unit(guid, kCreatureFieldCount)
			, m_templateEntry(kCreatureFieldTemplateEntry, &Mask(), templateEntry)
			, m_spawnId(kCreatureFieldSpawnId, &Mask(), spawnId)
		{
			// Mark template/spawn ids dirty pour la premiere replication.
			m_templateEntry.MarkDirty();
			m_spawnId.MarkDirty();
		}

		~Creature() override = default;

		uint32_t GetTemplateEntry() const noexcept { return m_templateEntry.Get(); }
		uint32_t GetSpawnId() const noexcept { return m_spawnId.Get(); }

	private:
		UpdateField<uint32_t> m_templateEntry;
		UpdateField<uint32_t> m_spawnId;
	};
}
