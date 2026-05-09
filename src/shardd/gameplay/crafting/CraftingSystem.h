#pragma once

#include "src/shared/core/Config.h"
#include "src/shared/network/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	struct ConnectedClient;

	// -------------------------------------------------------------------------
	// Data types shared between server and client (M36.2)
	// -------------------------------------------------------------------------

	/// One ingredient required by a crafting recipe (M36.2).
	struct RecipeIngredient
	{
		uint32_t itemId   = 0;
		uint32_t quantity = 0;
	};

	/// Data-driven crafting recipe definition loaded from JSON (M36.2).
	struct RecipeDefinition
	{
		std::string recipeId;
		/// Key matching a ProfessionEntry::professionKey (e.g. "blacksmithing").
		std::string professionKey;
		/// Minimum skill level required to craft this recipe.
		uint32_t skillRequired    = 1;
		std::vector<RecipeIngredient> ingredients;
		uint32_t outputItemId     = 0;
		uint32_t outputQuantity   = 1;
		/// Craft cast time in seconds (1-5 s per spec).
		float craftTimeSec        = 3.0f;
		/// Base skill-up chance when at the recipe's exact skill requirement level.
		float skillUpChance       = 1.0f;
	};

	/// One profession slot on a character (M36.2).
	struct ProfessionEntry
	{
		std::string professionKey;
		uint32_t    skillLevel = 1;  ///< 1-300
		bool        isPrimary  = false;
	};

	/// Active crafting session for one player (M36.2).
	struct CraftingSessionState
	{
		uint32_t    clientId       = 0;
		std::string recipeId;
		uint32_t    completionTick = 0;
	};

	/// Result of a crafting attempt (M36.2).
	enum class CraftOpResult : uint8_t
	{
		Ok                  = 0,
		RecipeNotFound      = 1,
		NoProfession        = 2,
		SkillTooLow         = 3,
		MissingIngredients  = 4,
		AlreadyCrafting     = 5,
	};

	/// Result of learning a profession (M36.2).
	enum class LearnProfessionResult : uint8_t
	{
		Ok                  = 0,
		AlreadyKnown        = 1,
		TooManyPrimary      = 2,
	};

	/// Skill-up thresholds for display/breakpoint text (M36.2).
	inline constexpr uint32_t kSkillBreakpointJourneyman = 75u;
	inline constexpr uint32_t kSkillBreakpointExpert     = 150u;
	inline constexpr uint32_t kSkillBreakpointArtisan    = 225u;
	inline constexpr uint32_t kSkillBreakpointMaster     = 300u;

	/// Number of skill levels above recipe requirement at which skill-up chance drops to 0 (M36.2).
	inline constexpr uint32_t kSkillUpGreenWindow = 50u;

	// -------------------------------------------------------------------------
	// M36.3 — Craft quality tiers + critical crafts
	// -------------------------------------------------------------------------

	/// Quality tiers for crafted items (M36.3).
	enum class CraftQualityTier : uint8_t
	{
		Normal   = 0, ///< White  — base stats, no bonus.
		Uncommon = 1, ///< Green  — +5% stats.
		Rare     = 2, ///< Blue   — +10% stats.
		Epic     = 3, ///< Purple — +15% stats (cap; no legendary via craft).
	};

	/// Tier colour tags used by the UI (M36.3).
	inline constexpr uint8_t kQualityTierCount = 4u;

	/// Stat multiplier (as a percentage) per quality tier (M36.3).
	/// Index matches the CraftQualityTier value.
	inline constexpr uint32_t kQualityStatMultiplierPct[kQualityTierCount] =
	{
		100u, ///< Normal   = 100% (no bonus)
		105u, ///< Uncommon = +5%
		110u, ///< Rare     = +10%
		115u, ///< Epic     = +15%
	};

	/// Maximum quality tier achievable via crafting (M36.3).
	inline constexpr CraftQualityTier kMaxCraftQualityTier = CraftQualityTier::Epic;

	/// Base critical craft probability multiplier (M36.3).
	/// Crit chance = playerSkillLevel / kCritSkillDivisor * kCritBaseFraction (max 10%).
	inline constexpr float kCritSkillDivisor  = 300.0f;
	inline constexpr float kCritBaseFraction  = 0.10f;

	/// Server-side crafting system: loads recipe definitions, manages craft sessions (M36.2).
	class CraftingSystem final
	{
	public:
		CraftingSystem() = default;

		/// Initialize and load recipe definitions from content JSON.
		/// @param config   Engine config used to resolve content paths.
		/// @param tickHz   Server tick rate — used to convert seconds to ticks.
		bool Init(const engine::core::Config& config, uint16_t tickHz);

		/// Shutdown the system and discard active sessions.
		void Shutdown();

		// ------------------------------------------------------------------
		// Profession management
		// ------------------------------------------------------------------

		/// Try to add \p professionKey to \p client (max 2 primary).
		LearnProfessionResult LearnProfession(
			ConnectedClient&   client,
			const std::string& professionKey,
			bool               asPrimary);

		// ------------------------------------------------------------------
		// Recipe queries
		// ------------------------------------------------------------------

		/// Return all recipe definitions for \p professionKey that are within reach of
		/// \p playerSkillLevel (i.e. skillRequired <= playerSkillLevel or learnable soon).
		std::vector<const RecipeDefinition*> GetVisibleRecipes(
			const std::string& professionKey,
			uint32_t           playerSkillLevel) const;

		/// Return the full recipe definition for \p recipeId, or nullptr.
		const RecipeDefinition* FindRecipe(const std::string& recipeId) const;

		// ------------------------------------------------------------------
		// Craft session management
		// ------------------------------------------------------------------

		/// Validate and start a crafting session.
		/// @param outDurationTicks  Filled with the cast duration on success.
		CraftOpResult TryStartCraft(
			ConnectedClient&   client,
			const std::string& recipeId,
			uint32_t           currentTick,
			uint32_t&          outDurationTicks);

		/// Cancel an active crafting session for \p clientId.
		/// Returns the recipeId that was cancelled (empty string if none was active).
		std::string CancelCraft(uint32_t clientId);

		/// Return the active crafting session for \p clientId, or nullptr.
		CraftingSessionState* FindSession(uint32_t clientId);

		// ------------------------------------------------------------------
		// Tick — advances sessions and fires completion callbacks
		// ------------------------------------------------------------------

		/// Advance all crafting sessions.  For each session that completed this tick:
		/// - Consumes ingredients from the client's inventory.
		/// - Adds output item to the client's inventory.
		/// - Rolls critical craft chance and computes the quality tier (M36.3).
		/// - Rolls skill-up chance and applies it to the player's profession.
		///
		/// Callers receive one entry per completed session and must send
		/// CraftComplete + InventoryDelta + ProfessionUpdate.
		void Tick(
			uint32_t                          currentTick,
			std::vector<ConnectedClient>&     clients,
			std::vector<uint32_t>&            outCompletedClientIds,
			std::vector<std::string>&         outCompletedRecipeIds,
			std::vector<uint8_t>&             outSkillGained,
			std::vector<uint32_t>&            outNewSkillLevel,
			std::vector<CraftQualityTier>&    outQualityTier);

		bool IsInitialized() const { return m_initialized; }
		size_t RecipeCount()  const { return m_recipes.size(); }

	private:
		/// Load recipe definitions from `crafting/recipes.json` (via paths.content).
		bool LoadRecipes(const engine::core::Config& config);

		/// Compute the actual skill-up probability for \p playerSkillLevel vs \p recipe.
		float SkillUpProbability(uint32_t playerSkillLevel, const RecipeDefinition& recipe) const;

		/// Roll a critical craft for \p playerSkillLevel and return the resulting quality tier (M36.3).
		/// Crit chance = playerSkillLevel / 300 * 10%; on crit, base quality is upgraded +1 (capped at Epic).
		static CraftQualityTier RollCriticalCraft(uint32_t playerSkillLevel);

		/// Find the ProfessionEntry for \p professionKey in \p client, or nullptr.
		static ProfessionEntry* FindProfession(ConnectedClient& client, const std::string& professionKey);
		static const ProfessionEntry* FindProfessionConst(const ConnectedClient& client, const std::string& professionKey);

		/// Remove \p quantity of \p itemId from \p client's inventory.
		static bool RemoveItemFromInventory(ConnectedClient& client, uint32_t itemId, uint32_t quantity);

		/// Add \p quantity of \p itemId to \p client's inventory (merge or append).
		static void AddItemToInventory(ConnectedClient& client, uint32_t itemId, uint32_t quantity);

		std::vector<RecipeDefinition>   m_recipes;
		std::vector<CraftingSessionState> m_sessions;
		uint16_t m_tickHz       = 20;
		bool     m_initialized  = false;
	};
}
