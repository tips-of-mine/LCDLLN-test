#include "engine/server/PlayerWalletService.h"

#include "engine/server/ServerApp.h" // ConnectedClient layout (defined here; header only forward-declares).

#include "engine/core/Log.h"

namespace engine::server
{
	namespace
	{
		/// Clamp \p base + \p delta to \p maxCap without signed overflow (unsigned only).
		bool TryAddClamped(uint32_t base, uint64_t delta, uint64_t maxCap, uint32_t& outResult, std::string& outError)
		{
			if (delta == 0u)
			{
				outResult = base;
				return true;
			}
			const uint64_t sum = static_cast<uint64_t>(base) + delta;
			if (sum > maxCap)
			{
				outError = "currency_cap_exceeded";
				LOG_WARN(Net, "[PlayerWalletService] AddCurrency blocked: cap (sum={} max={})", sum, maxCap);
				return false;
			}
			outResult = static_cast<uint32_t>(sum);
			return true;
		}
	}

	PlayerWalletService::PlayerWalletService(const CurrencyConfig& currencyConfig)
		: m_currencyConfig(currencyConfig)
	{
	}

	uint32_t* PlayerWalletService::SelectMutableAmount(ConnectedClient& client, uint8_t currencyId)
	{
		switch (currencyId)
		{
		case kCurrencyGold:
			return &client.gold;
		case kCurrencyHonor:
			return &client.honor;
		case kCurrencyBadges:
			return &client.badges;
		case kCurrencyPremium:
			return &client.premiumCurrency;
		default:
			return nullptr;
		}
	}

	const uint32_t* PlayerWalletService::SelectConstAmount(const ConnectedClient& client, uint8_t currencyId) const
	{
		switch (currencyId)
		{
		case kCurrencyGold:
			return &client.gold;
		case kCurrencyHonor:
			return &client.honor;
		case kCurrencyBadges:
			return &client.badges;
		case kCurrencyPremium:
			return &client.premiumCurrency;
		default:
			return nullptr;
		}
	}

	bool PlayerWalletService::AddCurrency(
		ConnectedClient& client,
		uint8_t currencyId,
		uint64_t delta,
		std::string& outError)
	{
		if (delta == 0u)
		{
			return true;
		}
		const uint64_t maxCap = m_currencyConfig.GetMaxAmount(currencyId);
		if (maxCap == 0u)
		{
			outError = "unknown_currency";
			LOG_WARN(Net, "[PlayerWalletService] AddCurrency FAILED: unknown currency_id={}", currencyId);
			return false;
		}
		uint32_t* slot = SelectMutableAmount(client, currencyId);
		if (slot == nullptr)
		{
			outError = "unknown_currency";
			LOG_WARN(Net, "[PlayerWalletService] AddCurrency FAILED: no slot (currency_id={})", currencyId);
			return false;
		}
		uint32_t newValue = *slot;
		if (!TryAddClamped(*slot, delta, maxCap, newValue, outError))
		{
			return false;
		}
		*slot = newValue;
		LOG_INFO(Net, "[PlayerWalletService] AddCurrency OK (client_id={}, currency_id={}, new_balance={})",
			client.clientId,
			currencyId,
			newValue);
		return true;
	}

	bool PlayerWalletService::SubtractCurrency(
		ConnectedClient& client,
		uint8_t currencyId,
		uint64_t delta,
		std::string& outError)
	{
		if (delta == 0u)
		{
			return true;
		}
		const uint64_t maxCap = m_currencyConfig.GetMaxAmount(currencyId);
		if (maxCap == 0u)
		{
			outError = "unknown_currency";
			LOG_WARN(Net, "[PlayerWalletService] SubtractCurrency FAILED: unknown currency_id={}", currencyId);
			return false;
		}
		uint32_t* slot = SelectMutableAmount(client, currencyId);
		if (slot == nullptr)
		{
			outError = "unknown_currency";
			LOG_WARN(Net, "[PlayerWalletService] SubtractCurrency FAILED: no slot (currency_id={})", currencyId);
			return false;
		}
		if (static_cast<uint64_t>(*slot) < delta)
		{
			outError = "overdraft";
			LOG_WARN(Net,
				"[PlayerWalletService] SubtractCurrency blocked: overdraft (client_id={}, currency_id={}, have={}, need={})",
				client.clientId,
				currencyId,
				*slot,
				delta);
			return false;
		}
		*slot = static_cast<uint32_t>(static_cast<uint64_t>(*slot) - delta);
		LOG_INFO(Net, "[PlayerWalletService] SubtractCurrency OK (client_id={}, currency_id={}, new_balance={})",
			client.clientId,
			currencyId,
			*slot);
		return true;
	}

	bool PlayerWalletService::Transfer(
		ConnectedClient& from,
		ConnectedClient& to,
		uint8_t currencyId,
		uint64_t amount,
		std::string& outError)
	{
		if (amount == 0u)
		{
			return true;
		}
		if (from.clientId == to.clientId)
		{
			outError = "same_client";
			LOG_WARN(Net, "[PlayerWalletService] Transfer ignored: same client_id={}", from.clientId);
			return false;
		}
		std::string subErr;
		if (!SubtractCurrency(from, currencyId, amount, subErr))
		{
			outError = subErr;
			return false;
		}
		if (!AddCurrency(to, currencyId, amount, subErr))
		{
			std::string rollbackErr;
			if (!AddCurrency(from, currencyId, amount, rollbackErr))
			{
				LOG_ERROR(Net,
					"[PlayerWalletService] Transfer FAILED: rollback impossible (from={}, currency_id={}, amount={})",
					from.clientId,
					currencyId,
					amount);
			}
			outError = "add_recipient_failed";
			return false;
		}
		LOG_INFO(Net,
			"[PlayerWalletService] Transfer OK (from={}, to={}, currency_id={}, amount={})",
			from.clientId,
			to.clientId,
			currencyId,
			amount);
		return true;
	}
}
