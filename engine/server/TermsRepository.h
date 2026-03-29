#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	/// MySQL-backed CGU / terms storage. Safe when pool is null or terms.enforce is false (no blocking).
	class TermsRepository
	{
	public:
		TermsRepository() = default;

		void Init(const engine::core::Config& config, engine::server::db::ConnectionPool* pool);

		bool IsEnforced() const { return m_enforce; }
		bool HasDb() const { return m_pool != nullptr && m_pool->IsInitialized(); }

		/// True if at least one published edition is not accepted by this account.
		bool HasPendingTerms(uint64_t account_id);

		/// Number of published editions not yet accepted (for UI progress).
		uint32_t CountPendingEditions(uint64_t account_id);

		struct PendingHead
		{
			uint64_t    edition_id     = 0;
			std::string version_label;
			std::string title;
			std::string resolved_locale;
		};

		/// First pending edition (by publication order), or edition_id==0 if none.
		bool GetFirstPending(uint64_t account_id, std::string_view locale_pref, PendingHead& out);

		struct ContentChunk
		{
			std::string full_text;
			std::string resolved_locale;
		};

		/// Loads full text from DB (for chunking in handler). Returns false on error / not found.
		bool LoadEditionContent(uint64_t edition_id, std::string_view locale_pref, ContentChunk& out);

		/// True if this edition is published, effective, and not yet accepted by the account.
		bool IsEditionPendingForAccount(uint64_t account_id, uint64_t edition_id);

		/// Display label for an edition (e.g. v2.0).
		bool GetEditionVersionLabel(uint64_t edition_id, std::string& out_label);

		/// Inserts acceptance row. Returns false on DB error.
		bool RecordAcceptance(uint64_t account_id, uint64_t edition_id);

	private:
		bool                       m_enforce = false;
		engine::server::db::ConnectionPool* m_pool = nullptr;
		std::string                m_fallback_locale = "en";
	};
}
