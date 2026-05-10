#pragma once
// UpdateMask : bit-mask sur N champs (un bit par UpdateField). Permet le
// delta replication : ne serialiser / envoyer QUE les champs modifies depuis
// la derniere snapshot. Pattern WoW-like.
// Header-only.
//
// Stockage en chunks de uint32_t : pour N champs, on alloue ceil(N/32) chunks.
// SetBit / ClearBit / TestBit sont O(1). PopCount parcourt tous les chunks
// (O(N/32)) avec Brian Kernighan's bit count.
//
// Hors plage : SetBit/ClearBit/TestBit hors [0, FieldCount) sont des no-ops
// (defensif, plutot que asserts qui crashent en prod). Les tests verifient ce
// comportement.

#include <cstdint>
#include <cstddef>
#include <vector>

namespace engine::server::entities
{
	/// Mask de bits sur N champs. \p fieldCount est defini soit au constructeur,
	/// soit a posteriori via Resize().
	class UpdateMask
	{
	public:
		/// Construit un mask vide (0 champs). Appeler Resize() avant utilisation.
		UpdateMask() = default;

		/// Construit un mask dimensionne pour \p fieldCount champs, tous a 0.
		explicit UpdateMask(size_t fieldCount) { Resize(fieldCount); }

		/// Redimensionne le mask. Tous les bits sont remis a 0 apres l'appel,
		/// y compris pour les indices < fieldCount precedent.
		void Resize(size_t fieldCount)
		{
			m_fieldCount = fieldCount;
			const size_t chunks = (fieldCount + 31u) / 32u;
			m_chunks.assign(chunks, 0u);
		}

		/// Remet tous les bits a 0. La taille (FieldCount) est conservee.
		void Clear() noexcept
		{
			for (auto& c : m_chunks) c = 0u;
		}

		/// Met le bit \p fieldIdx a 1. No-op si fieldIdx >= FieldCount.
		void SetBit(size_t fieldIdx) noexcept
		{
			if (fieldIdx >= m_fieldCount) return;
			m_chunks[fieldIdx / 32u] |= (1u << (fieldIdx % 32u));
		}

		/// Met le bit \p fieldIdx a 0. No-op si fieldIdx >= FieldCount.
		void ClearBit(size_t fieldIdx) noexcept
		{
			if (fieldIdx >= m_fieldCount) return;
			m_chunks[fieldIdx / 32u] &= ~(1u << (fieldIdx % 32u));
		}

		/// Retourne true si le bit \p fieldIdx est a 1. False si hors plage.
		bool TestBit(size_t fieldIdx) const noexcept
		{
			if (fieldIdx >= m_fieldCount) return false;
			return (m_chunks[fieldIdx / 32u] & (1u << (fieldIdx % 32u))) != 0u;
		}

		/// Nombre total de champs declares.
		size_t FieldCount() const noexcept { return m_fieldCount; }

		/// Acces direct aux chunks (utile pour serialisation reseau).
		const std::vector<uint32_t>& Chunks() const noexcept { return m_chunks; }

		/// True si aucun bit n'est a 1.
		bool Empty() const noexcept
		{
			for (auto c : m_chunks)
				if (c != 0u) return false;
			return true;
		}

		/// Compte le nombre de bits a 1 (Brian Kernighan, O(set bits)).
		size_t PopCount() const noexcept
		{
			size_t count = 0u;
			for (auto c : m_chunks)
			{
				while (c)
				{
					c &= (c - 1u); // efface le bit a 1 le plus bas
					++count;
				}
			}
			return count;
		}

	private:
		size_t                  m_fieldCount = 0;
		std::vector<uint32_t>   m_chunks;
	};
}
