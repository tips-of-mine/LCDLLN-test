#pragma once
// UpdateField<T> : wrapper d'un champ stockant valeur courante + index dans
// l'UpdateMask du parent. Set() met le bit a 1 si la valeur change ;
// MarkDirty() force le flag dirty meme si la valeur est inchangee (utile
// pour la replication initiale).
// Header-only.
//
// Pattern WoW-like : un objet expose N UpdateField<T>, chaque setter passe
// par Set() qui marque automatiquement le champ comme modifie. Le code de
// replication consulte ensuite l'UpdateMask pour serialiser uniquement les
// champs flagges.

#include "src/shardd/entities/UpdateMask.h"

#include <cstddef>
#include <utility>

namespace engine::server::entities
{
	/// Wrapper de champ avec auto-flag de modification. T doit etre
	/// comparable via operator!= et move-constructible.
	template<typename T>
	class UpdateField
	{
	public:
		/// Construit un field non-attache (mask=nullptr, Set() ne flag rien).
		/// Utile pour les conteneurs temporaires.
		UpdateField() = default;

		/// Construit un field attache a un mask externe. Le mask doit survivre
		/// a l'UpdateField (typiquement detenu par l'Object parent).
		/// \param fieldIdx index du bit dans \p mask (< mask->FieldCount()).
		/// \param mask pointeur vers le mask parent (peut etre nullptr pour
		///        un field detache).
		/// \param initial valeur initiale (le mask N'EST PAS flagge a la construction).
		UpdateField(size_t fieldIdx, UpdateMask* mask, T initial = T{})
			: m_value(std::move(initial))
			, m_fieldIdx(fieldIdx)
			, m_mask(mask)
		{}

		/// Lecture de la valeur courante.
		const T& Get() const noexcept { return m_value; }

		/// Set la nouvelle valeur. Si elle differe de la valeur courante,
		/// le bit fieldIdx du mask parent est mis a 1.
		void Set(T newValue)
		{
			if (m_value != newValue)
			{
				m_value = std::move(newValue);
				if (m_mask) m_mask->SetBit(m_fieldIdx);
			}
		}

		/// Force flag dirty meme si la valeur est inchangee. Sert pour l'init
		/// initiale (premier envoi a un client qui n'a pas encore l'objet) ou
		/// pour une replication forcee.
		void MarkDirty() noexcept
		{
			if (m_mask) m_mask->SetBit(m_fieldIdx);
		}

	private:
		T            m_value{};
		size_t       m_fieldIdx = 0;
		UpdateMask*  m_mask     = nullptr;
	};
}
