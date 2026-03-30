#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	/// One item sold by a vendor with its base buy price and optional stock limit.
	struct VendorItemDefinition
	{
		uint32_t itemId   = 0;
		uint32_t buyPrice = 0;  ///< Gold cost to buy from this vendor.
		int32_t  stock    = -1; ///< Initial stock; -1 means infinite (never depleted).
	};

	/// One vendor NPC loaded from content data (M35.2).
	struct VendorDefinition
	{
		std::string                    vendorId;   ///< Stable unique identifier (e.g. "vendor_blacksmith_z1").
		std::string                    vendorType; ///< Semantic type for UI grouping (e.g. "weapons", "potions").
		std::vector<VendorItemDefinition> items;
	};

	/// Server-side vendor runtime: loads vendor JSON definitions from `paths.content` (M35.2).
	class VendorRuntime final
	{
	public:
		/// Capture the config used to resolve vendor definition files.
		explicit VendorRuntime(const engine::core::Config& config);

		/// Emit shutdown logs when the vendor runtime is destroyed.
		~VendorRuntime();

		/// Load all vendor definitions from `vendors/vendor_definitions.json`.
		bool Init();

		/// Release loaded definitions and emit shutdown logs.
		void Shutdown();

		/// Return a pointer to the definition matching \p vendorId, or nullptr when absent.
		const VendorDefinition* FindVendor(std::string_view vendorId) const;

		/// Return the full list of loaded vendor definitions.
		const std::vector<VendorDefinition>& GetDefinitions() const { return m_definitions; }

	private:
		/// Parse and load vendor definitions from the configured content-relative JSON path.
		bool LoadDefinitions();

		engine::core::Config           m_config;
		std::vector<VendorDefinition>  m_definitions;
		bool                           m_initialized = false;
	};
}
