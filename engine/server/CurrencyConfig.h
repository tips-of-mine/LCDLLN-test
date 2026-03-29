#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	/// One currency definition loaded from `config/currencies.json` (paths.content).
	struct CurrencyDefinition
	{
		uint8_t id = 0;
		std::string key;
		std::string displayName;
		uint64_t maxAmount = 0;
		/// Relative to paths.content (e.g. textures/ui/currency_gold.texr).
		std::string iconRelativePath;
	};

	/// Optional copper/silver/gold breakdown (display / future exchange).
	struct CurrencyConversion
	{
		uint64_t copperPerSilver = 100;
		uint64_t silverPerGold = 100;
	};

	/// Authoritative currency table for caps and UI metadata (M35.1).
	class CurrencyConfig final
	{
	public:
		/// Construct an empty config (use \ref Load).
		CurrencyConfig() = default;

		/// Load `config/currencies.json` under paths.content; falls back to built-in defaults on failure.
		bool Load(const engine::core::Config& config);

		/// Return max allowed amount for \p currencyId, or 0 if unknown.
		uint64_t GetMaxAmount(uint8_t currencyId) const;

		/// Lookup definition by stable id (1..4 for MVP).
		const CurrencyDefinition* FindById(uint8_t currencyId) const;

		/// Conversion hints from JSON (optional).
		const CurrencyConversion& GetConversion() const { return m_conversion; }

	private:
		void ApplyDefaults();

		std::vector<CurrencyDefinition> m_definitions{};
		CurrencyConversion m_conversion{};
	};
}
