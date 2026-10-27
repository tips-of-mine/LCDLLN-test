// ETAPE5.5 — sauvegardes différentielles. Voir CharacterSaveDiff.h.

#include "src/shardd/gameplay/character/CharacterSaveDiff.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace engine::server
{
	namespace
	{
		/// FNV-1a 64 bits incrémental.
		class Fnv1a
		{
		public:
			void Bytes(const void* data, size_t size)
			{
				const auto* p = static_cast<const uint8_t*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					m_h ^= p[i];
					m_h *= 1099511628211ull;
				}
			}
			void U32(uint32_t v) { Bytes(&v, sizeof(v)); }
			void U64(uint64_t v) { Bytes(&v, sizeof(v)); }
			/// Flottant par sa représentation binaire (−0 et +0 diffèrent : écriture prudente).
			void F32(float v) { U32(std::bit_cast<uint32_t>(v)); }
			void Str(std::string_view s)
			{
				U64(static_cast<uint64_t>(s.size()));
				Bytes(s.data(), s.size());
			}
			uint64_t Value() const { return m_h; }

		private:
			uint64_t m_h = 14695981039346656037ull;
		};
	}

	uint32_t CountSaveBlocks(uint32_t blocks)
	{
		return static_cast<uint32_t>(std::popcount(blocks & kSaveBlocksAll));
	}

	SaveBlockHashes ComputeSaveBlockHashes(const PersistedCharacterState& state, float yawRadians,
		std::string_view progressPayload)
	{
		SaveBlockHashes h;
		{
			Fnv1a f;
			f.F32(state.positionMetersX);
			f.F32(state.positionMetersY);
			f.F32(state.positionMetersZ);
			f.F32(yawRadians);
			f.U32(state.level);
			h.position = f.Value();
		}
		{
			Fnv1a f;
			f.U32(state.zoneId);
			f.U32(state.experiencePoints);
			f.U64(static_cast<uint64_t>(state.stats.currentHealth));
			f.U64(static_cast<uint64_t>(state.stats.maxHealth));
			f.U32(state.chatModeratorRole ? 1u : 0u);
			f.Str(progressPayload);
			h.progress = f.Value();
		}
		{
			Fnv1a f;
			f.U32(state.gold);
			f.U32(state.honor);
			f.U32(state.badges);
			f.U32(state.premiumCurrency);
			h.wallet = f.Value();
		}
		{
			Fnv1a f;
			f.U64(static_cast<uint64_t>(state.inventory.size()));
			for (const auto& stack : state.inventory)
			{
				f.U64(static_cast<uint64_t>(stack.itemId));
				f.U64(static_cast<uint64_t>(stack.quantity));
			}
			h.inventory = f.Value();
		}
		{
			Fnv1a f;
			for (uint32_t itemId : state.equipment)
				f.U32(itemId);
			h.equipment = f.Value();
		}
		return h;
	}

	uint32_t DiffSaveBlocks(const SaveBlockHashes& saved, const SaveBlockHashes& current)
	{
		uint32_t blocks = 0;
		if (saved.position != current.position) blocks |= kSaveBlockPosition;
		if (saved.progress != current.progress) blocks |= kSaveBlockProgress;
		if (saved.wallet != current.wallet) blocks |= kSaveBlockWallet;
		if (saved.inventory != current.inventory) blocks |= kSaveBlockInventory;
		if (saved.equipment != current.equipment) blocks |= kSaveBlockEquipment;
		return blocks;
	}

	SavedBlockCache::SavedBlockCache(uint32_t fullEvery)
	{
		SetFullEvery(fullEvery);
	}

	void SavedBlockCache::SetFullEvery(uint32_t fullEvery)
	{
		std::lock_guard lock(m_mutex);
		m_fullEvery = std::clamp(fullEvery, 1u, 1000u);
	}

	uint32_t SavedBlockCache::FullEvery() const
	{
		std::lock_guard lock(m_mutex);
		return m_fullEvery;
	}

	uint32_t SavedBlockCache::PlanBlocks(uint64_t characterKey, const SaveBlockHashes& current) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_entries.find(characterKey);
		if (it == m_entries.end())
			return kSaveBlocksAll;
		// Tour d'écriture complète : la (fullEvery)-ième sauvegarde depuis la dernière complète.
		if (it->second.savesSinceFull + 1u >= m_fullEvery)
			return kSaveBlocksAll;
		return DiffSaveBlocks(it->second.hashes, current);
	}

	void SavedBlockCache::OnCommitted(uint64_t characterKey, const SaveBlockHashes& current, uint32_t written)
	{
		std::lock_guard lock(m_mutex);
		Entry& e = m_entries[characterKey];
		e.hashes = current;
		e.savesSinceFull = (written & kSaveBlocksAll) == kSaveBlocksAll ? 0u : e.savesSinceFull + 1u;
	}

	void SavedBlockCache::Invalidate(uint64_t characterKey)
	{
		std::lock_guard lock(m_mutex);
		m_entries.erase(characterKey);
	}

	size_t SavedBlockCache::Size() const
	{
		std::lock_guard lock(m_mutex);
		return m_entries.size();
	}
}
