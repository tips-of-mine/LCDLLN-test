#pragma once
// CMANGOS.25 (Phase 3.25a) — IgnoreList : extension du systeme social
// au-dela de FriendSystem. Permet a un joueur d'ignorer un autre joueur :
// les whispers + invites + chat / mail de cette personne sont silencieusement
// drop cote master.
//
// **Pur** : pas de DB persistence ni d'integration ChatGate dans cette
// PR. Le store abstrait permet le test in-memory ; production wirera
// MysqlIgnoreStore + integration avec ChatGate (#486) en sub-PR.

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::server::social
{
	/// Limite : 50 ignored par account (audit + cmangos parity).
	inline constexpr size_t kMaxIgnoredPerAccount = 50;

	enum class IgnoreOpResult : uint8_t
	{
		OK = 0,
		AlreadyIgnored = 1,
		NotIgnored     = 2,
		ListFull       = 3,
		SelfIgnore     = 4,    ///< Pas le droit de s'ignorer soi-meme.
	};

	/// Interface du store. Production = MysqlIgnoreStore, tests = in-memory.
	class IIgnoreStore
	{
	public:
		virtual ~IIgnoreStore() = default;

		virtual bool Add(uint64_t ownerAccountId, uint64_t targetAccountId) = 0;
		virtual bool Remove(uint64_t ownerAccountId, uint64_t targetAccountId) = 0;
		virtual bool IsIgnored(uint64_t ownerAccountId, uint64_t targetAccountId) const = 0;
		virtual std::vector<uint64_t> List(uint64_t ownerAccountId) const = 0;
		virtual size_t Size(uint64_t ownerAccountId) const = 0;
	};

	class IgnoreListManager
	{
	public:
		explicit IgnoreListManager(IIgnoreStore* store) : m_store(store) {}

		IgnoreOpResult Ignore(uint64_t ownerAccountId, uint64_t targetAccountId)
		{
			if (!m_store) return IgnoreOpResult::NotIgnored;
			if (ownerAccountId == targetAccountId)
				return IgnoreOpResult::SelfIgnore;
			if (m_store->IsIgnored(ownerAccountId, targetAccountId))
				return IgnoreOpResult::AlreadyIgnored;
			if (m_store->Size(ownerAccountId) >= kMaxIgnoredPerAccount)
				return IgnoreOpResult::ListFull;
			m_store->Add(ownerAccountId, targetAccountId);
			return IgnoreOpResult::OK;
		}

		IgnoreOpResult Unignore(uint64_t ownerAccountId, uint64_t targetAccountId)
		{
			if (!m_store) return IgnoreOpResult::NotIgnored;
			if (!m_store->IsIgnored(ownerAccountId, targetAccountId))
				return IgnoreOpResult::NotIgnored;
			m_store->Remove(ownerAccountId, targetAccountId);
			return IgnoreOpResult::OK;
		}

		/// Hot path : appele par ChatGate / WhisperHandler / MailManager pour
		/// silently drop. Retourne true si \p ownerAccountId a ignore
		/// \p targetAccountId.
		bool IsIgnored(uint64_t ownerAccountId, uint64_t targetAccountId) const
		{
			return m_store && m_store->IsIgnored(ownerAccountId, targetAccountId);
		}

		std::vector<uint64_t> List(uint64_t ownerAccountId) const
		{
			return m_store ? m_store->List(ownerAccountId) : std::vector<uint64_t>{};
		}

	private:
		IIgnoreStore* m_store;
	};

	/// Implementation RAM pour tests + dev.
	class InMemoryIgnoreStore final : public IIgnoreStore
	{
	public:
		bool Add(uint64_t owner, uint64_t target) override
		{
			return m_data[owner].insert(target).second;
		}
		bool Remove(uint64_t owner, uint64_t target) override
		{
			auto it = m_data.find(owner);
			if (it == m_data.end()) return false;
			return it->second.erase(target) > 0;
		}
		bool IsIgnored(uint64_t owner, uint64_t target) const override
		{
			auto it = m_data.find(owner);
			if (it == m_data.end()) return false;
			return it->second.count(target) != 0;
		}
		std::vector<uint64_t> List(uint64_t owner) const override
		{
			std::vector<uint64_t> out;
			auto it = m_data.find(owner);
			if (it == m_data.end()) return out;
			out.reserve(it->second.size());
			for (auto t : it->second) out.push_back(t);
			return out;
		}
		size_t Size(uint64_t owner) const override
		{
			auto it = m_data.find(owner);
			return (it == m_data.end()) ? 0 : it->second.size();
		}

	private:
		std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_data;
	};
}
