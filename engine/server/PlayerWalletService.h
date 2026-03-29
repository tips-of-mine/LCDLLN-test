#pragma once

#include "engine/server/CurrencyConfig.h"

#include <cstdint>
#include <string>

namespace engine::server
{
	struct ConnectedClient;

	/// Stable currency ids (wire + DB currency_id) for M35.1 MVP.
	inline constexpr uint8_t kCurrencyGold = 1;
	inline constexpr uint8_t kCurrencyHonor = 2;
	inline constexpr uint8_t kCurrencyBadges = 3;
	inline constexpr uint8_t kCurrencyPremium = 4;

	/// Server-side wallet operations: caps, unsigned math, transfer (M35.1).
	class PlayerWalletService final
	{
	public:
		/// Construct service bound to a loaded \ref CurrencyConfig lifetime must cover this object.
		explicit PlayerWalletService(const CurrencyConfig& currencyConfig);

		/// Add \p delta to \p currencyId (unsigned; rejects negative semantics via separate API).
		bool AddCurrency(ConnectedClient& client, uint8_t currencyId, uint64_t delta, std::string& outError);

		/// Subtract \p delta from \p currencyId (fails on overdraft).
		bool SubtractCurrency(ConnectedClient& client, uint8_t currencyId, uint64_t delta, std::string& outError);

		/// Atomically move \p amount from \p from to \p to for one currency (single-threaded game shard).
		bool Transfer(
			ConnectedClient& from,
			ConnectedClient& to,
			uint8_t currencyId,
			uint64_t amount,
			std::string& outError);

	private:
		uint32_t* SelectMutableAmount(ConnectedClient& client, uint8_t currencyId);
		const uint32_t* SelectConstAmount(const ConnectedClient& client, uint8_t currencyId) const;

		const CurrencyConfig& m_currencyConfig;
	};
}
