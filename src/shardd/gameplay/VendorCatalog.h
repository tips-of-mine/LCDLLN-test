#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// One sellable row in a vendor definition (M35.2).
	struct VendorItemDefinition
	{
		uint32_t itemId = 0;
		uint32_t buyPrice = 0;
		/// -1 = infinite stock (not tracked in runtime map).
		int32_t stock = -1;
	};

	/// One vendor NPC shop table loaded from `config/vendors.json` (paths.content).
	struct VendorDefinition
	{
		uint32_t vendorId = 0;
		std::string displayName;
		std::string kind;
		std::vector<VendorItemDefinition> items;
	};

	/// Runtime stock for finite-quantity offers: key = (vendorId << 32) | itemId.
	class VendorStockBook final
	{
	public:
		/// Reset all finite stocks from catalog defaults.
		void ResetFromCatalog(const std::vector<VendorDefinition>& vendors);

		/// Remaining stock for finite items; nullopt = infinite.
		std::optional<uint32_t> GetRemaining(uint32_t vendorId, uint32_t itemId) const;

		/// Decrement one finite stack after a buy; returns false if would underflow.
		bool TryConsume(uint32_t vendorId, uint32_t itemId, uint32_t quantity, std::string& outError);

	private:
		std::unordered_map<uint64_t, uint32_t> m_remaining{};
	};

	/// Loads vendor definitions and resolves sell prices (25% buy default).
	class VendorCatalog final
	{
	public:
		VendorCatalog() = default;

		/// Load `config/vendors.json` under paths.content; falls back to one default vendor on failure.
		bool Load(const engine::core::Config& config);

		/// Return vendor by id or nullptr.
		const VendorDefinition* FindVendor(uint32_t vendorId) const;

		/// Find item row for a vendor+item pair.
		const VendorItemDefinition* FindVendorItem(uint32_t vendorId, uint32_t itemId) const;

		/// Default sell-back price: floor(25% of buy price); minimum 1 gold when buy_price > 0.
		static uint32_t ComputeSellPrice(uint32_t buyPrice);

		/// All loaded vendors (read-only) for stock book initialization.
		const std::vector<VendorDefinition>& GetVendors() const { return m_vendors; }

	private:
		void ApplyDefaultVendor();

		std::vector<VendorDefinition> m_vendors{};
	};
}
