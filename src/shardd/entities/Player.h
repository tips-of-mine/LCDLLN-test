#pragma once
// Player : Unit + identite compte (accountId, characterId, name).
// Le name est immuable apres construction (le renaming passe par une
// suppression + creation cote masterd, pas par mutation in-place).
//
// XP : UpdateField pour la replication delta (UI client peut afficher
// progression sans poll DB).
//
// Les ids account/character sont MarkDirty() a la construction pour
// garantir la replication initiale au client.

#include "src/shardd/entities/Unit.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/UpdateFieldIndices.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>

namespace engine::server::entities
{
	/// Player : herite Unit, ajoute identite compte + nom + xp.
	class Player : public Unit
	{
	public:
		/// \param guid identifiant immuable
		/// \param accountId id du compte master proprietaire
		/// \param characterId id de la fiche character (DB primary key)
		/// \param name nom du personnage (immuable apres construction)
		Player(ObjectGuid guid, uint64_t accountId, uint64_t characterId, std::string name)
			: Unit(guid, kPlayerFieldCount)
			, m_accountId(kPlayerFieldAccountId, &Mask(), accountId)
			, m_characterId(kPlayerFieldCharacterId, &Mask(), characterId)
			, m_xp(kPlayerFieldXp, &Mask())
			, m_name(std::move(name))
		{
			// Mark account + character ids dirty pour la premiere replication.
			// (UpdateField::ctor ne flag pas le mask : seul Set() le fait. Ici on
			// veut que le client recoive ces 2 ids des le premier snapshot.)
			m_accountId.MarkDirty();
			m_characterId.MarkDirty();
		}

		~Player() override = default;

		uint64_t GetAccountId() const noexcept { return m_accountId.Get(); }
		uint64_t GetCharacterId() const noexcept { return m_characterId.Get(); }
		const std::string& GetName() const noexcept { return m_name; }

		void SetXp(uint32_t xp) { m_xp.Set(xp); }
		uint32_t GetXp() const noexcept { return m_xp.Get(); }

	private:
		UpdateField<uint64_t> m_accountId;
		UpdateField<uint64_t> m_characterId;
		UpdateField<uint32_t> m_xp;
		const std::string     m_name;
	};
}
