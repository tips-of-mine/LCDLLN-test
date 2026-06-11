#pragma once
// CMANGOS.23 (Phase 3.23a) — QuestState : machine d'états par joueur+
// quête (None → Available → Accepted → Completed → Rewarded → None).
// Header-only, in-memory (DB persistence + handler en sub-PRs).
//
// Audit 2026-06-10 (Lot B1) — THREAD-SAFE : les handlers du master sont
// dispatchés sur un pool de workers NetServer (défaut 4) ; chaque méthode
// publique verrouille m_mutex et délègue à GetUnlocked (privé) pour la
// lecture interne — jamais de double verrouillage.

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace engine::server::quests
{
	using QuestId   = uint32_t;
	using AccountId = uint64_t;

	enum class QuestStatus : uint8_t
	{
		None       = 0,    ///< pas de relation entre le joueur et la quête
		Available  = 1,    ///< proposable
		Accepted   = 2,    ///< en cours
		Completed  = 3,    ///< objectifs remplis, pas encore récompensé
		Rewarded   = 4,    ///< récompense récupérée
		Failed     = 5,    ///< échec (timeout, mort, etc.)
	};

	enum class QuestOpResult : uint8_t
	{
		OK = 0,
		WrongStatus      = 1,    ///< l'opération n'est pas valide depuis l'état courant
		QuestNotFound    = 2,
	};

	class QuestStateTracker
	{
	public:
		QuestStatus Get(AccountId account, QuestId quest) const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return GetUnlocked(account, quest);
		}

		QuestOpResult Accept(AccountId account, QuestId quest)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto cur = GetUnlocked(account, quest);
			if (cur != QuestStatus::None && cur != QuestStatus::Available)
				return QuestOpResult::WrongStatus;
			m_state[account][quest] = QuestStatus::Accepted;
			return QuestOpResult::OK;
		}

		QuestOpResult Complete(AccountId account, QuestId quest)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto cur = GetUnlocked(account, quest);
			if (cur != QuestStatus::Accepted) return QuestOpResult::WrongStatus;
			m_state[account][quest] = QuestStatus::Completed;
			return QuestOpResult::OK;
		}

		QuestOpResult Reward(AccountId account, QuestId quest)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto cur = GetUnlocked(account, quest);
			if (cur != QuestStatus::Completed) return QuestOpResult::WrongStatus;
			m_state[account][quest] = QuestStatus::Rewarded;
			return QuestOpResult::OK;
		}

		QuestOpResult Fail(AccountId account, QuestId quest)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto cur = GetUnlocked(account, quest);
			if (cur != QuestStatus::Accepted) return QuestOpResult::WrongStatus;
			m_state[account][quest] = QuestStatus::Failed;
			return QuestOpResult::OK;
		}

		void Clear()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_state.clear();
		}

		/// CMANGOS.23 (Phase 5.23 step 3+4) — Liste toutes les quetes connues du
		/// compte. Utilise par QuestHandler::HandleList pour repondre au client.
		/// Retourne une copie ; complexite O(n) sur le nombre de quetes du compte.
		std::unordered_map<QuestId, QuestStatus> ListAll(AccountId account) const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_state.find(account);
			if (it == m_state.end())
				return {};
			return it->second;
		}

	private:
		/// Lecture interne SANS verrou — appelée uniquement sous m_mutex.
		QuestStatus GetUnlocked(AccountId account, QuestId quest) const
		{
			auto itAcc = m_state.find(account);
			if (itAcc == m_state.end()) return QuestStatus::None;
			auto itQ = itAcc->second.find(quest);
			return (itQ == itAcc->second.end()) ? QuestStatus::None : itQ->second;
		}

		/// Audit Lot B1 — protège m_state contre les workers concurrents.
		mutable std::mutex m_mutex;
		std::unordered_map<AccountId, std::unordered_map<QuestId, QuestStatus>> m_state;
	};
}
