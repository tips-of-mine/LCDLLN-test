#pragma once

#include "engine/core/Config.h"
#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	/// Maximum number of primary professions a player may learn simultaneously (M36.2).
	inline constexpr uint32_t kMaxPrimaryProfessions = 2;

	/// Maximum player crafting skill (M36.2).
	inline constexpr uint32_t kMaxCraftingSkill = 300;

	/// Minimum player crafting skill granted on learn (M36.2).
	inline constexpr uint32_t kInitialCraftingSkill = 1;

	/// Skill window above the recipe requirement within which skill-up can occur (M36.2).
	inline constexpr float kSkillUpWindow = 75.0f;

	// -------------------------------------------------------------------------
	// Quality tiers (M36.3)
	// -------------------------------------------------------------------------

	/// Item quality tiers produced by the crafting system (M36.3).
	/// Tier order is significant: higher index = higher quality.
	enum class ItemQualityTier : uint8_t
	{
		Normal   = 0,  ///< White  — base quality, no bonus.
		Uncommon = 1,  ///< Green  — +5% stat multiplier.
		Rare     = 2,  ///< Blue   — +10% stat multiplier.
		Epic     = 3   ///< Purple — +15% stat multiplier.
	};

	/// Maximum craftable quality tier (craft can never exceed Epic) (M36.3).
	inline constexpr ItemQualityTier kMaxCraftQualityTier = ItemQualityTier::Epic;

	/// Per-tier stat multiplier (base × TierMultiplier(tier)) (M36.3).
	/// Each step above Normal adds +5 % to base stats.
	inline constexpr float kQualityTierStatStep = 0.05f;

	/// Maximum critical craft chance (at skill 300) (M36.3).
	inline constexpr float kCritCraftMaxChance = 0.10f;

	/// Return the stat multiplier for \p tier (1.0 for Normal, 1.05 for Uncommon, …) (M36.3).
	inline constexpr float QualityTierMultiplier(ItemQualityTier tier)
	{
		return 1.0f + static_cast<float>(static_cast<uint8_t>(tier)) * kQualityTierStatStep;
	}

	/// Return the display name string for \p tier (M36.3).
	inline constexpr const char* QualityTierName(ItemQualityTier tier)
	{
		switch (tier)
		{
		case ItemQualityTier::Normal:   return "Normal";
		case ItemQualityTier::Uncommon: return "Uncommon";
		case ItemQualityTier::Rare:     return "Rare";
		case ItemQualityTier::Epic:     return "Epic";
		}
		return "Normal";
	}

	/// Return the CSS/UI color token for \p tier (M36.3).
	inline constexpr const char* QualityTierColor(ItemQualityTier tier)
	{
		switch (tier)
		{
		case ItemQualityTier::Normal:   return "white";
		case ItemQualityTier::Uncommon: return "green";
		case ItemQualityTier::Rare:     return "blue";
		case ItemQualityTier::Epic:     return "purple";
		}
		return "white";
	}

	// -------------------------------------------------------------------------
	// Profession state (runtime + persistence)
	// -------------------------------------------------------------------------

	/// One learned profession entry stored per player (M36.2).
	/// Defined here so CharacterPersistence.h can include this header.
	struct PlayerProfessionState
	{
		/// Stable profession identifier, e.g. "blacksmithing".
		std::string professionId;
		/// Current skill level (1–300).
		uint32_t skillLevel = kInitialCraftingSkill;
		/// True when this profession counts toward the primary-slot limit.
		bool isPrimary = false;
	};

	// -------------------------------------------------------------------------
	// Recipe definitions (loaded from crafting/recipes.json)
	// -------------------------------------------------------------------------

	/// One material required to craft a recipe (M36.2).
	struct RecipeIngredient
	{
		/// Numeric item id used for inventory lookup.
		uint32_t itemId = 0;
		uint32_t quantity = 0;
	};

	/// Data-driven recipe definition loaded from `crafting/recipes.json` (M36.2).
	struct RecipeDefinition
	{
		/// Stable recipe identifier, e.g. "iron_sword".
		std::string recipeId;
		/// References a known profession id.
		std::string professionId;
		/// Minimum skill level required to craft this recipe.
		uint32_t skillRequired = 1;
		/// All required ingredients.
		std::vector<RecipeIngredient> ingredients;
		/// Output item granted on successful craft.
		uint32_t outputItemId = 0;
		uint32_t outputQuantity = 1;
		/// Cast-bar duration in seconds (1–5 s).
		float craftTimeSec = 3.0f;
		/// Base skill-up probability when player skill == skillRequired (0.0–1.0).
		float skillUpChance = 1.0f;
	};

	/// One crafting profession definition loaded from `crafting/recipes.json` (M36.2).
	struct CraftProfessionDefinition
	{
		/// Stable profession identifier, e.g. "blacksmithing".
		std::string professionId;
		/// True when the profession counts toward the primary-slot limit.
		bool isPrimary = true;
	};

	// -------------------------------------------------------------------------
	// Runtime crafting session
	// -------------------------------------------------------------------------

	/// One in-progress crafting cast session tracked by the server (M36.2).
	struct ActiveCraftSession
	{
		uint32_t clientId = 0;
		std::string recipeId;
		uint32_t startTick = 0;
	};

	/// Result of one completed craft, returned by CraftingSystem::Tick (M36.2).
	/// Skill-up is applied by the ServerApp after inspecting the player profession state.
	struct CraftCompletionResult
	{
		uint32_t clientId = 0;
		std::string recipeId;
	};

	// -------------------------------------------------------------------------
	// CraftingSystem
	// -------------------------------------------------------------------------

	/// Server-side crafting manager: profession definitions + recipes + active cast sessions (M36.2).
	class CraftingSystem final
	{
	public:
		/// Capture the config used to resolve content paths.
		explicit CraftingSystem(const engine::core::Config& config);

		CraftingSystem(const CraftingSystem&) = delete;
		CraftingSystem& operator=(const CraftingSystem&) = delete;

		~CraftingSystem();

		/// Load profession definitions and recipes from `crafting/recipes.json`.
		bool Init();

		/// Release all state and emit shutdown log.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ------------------------------------------------------------------
		// Profession management
		// ------------------------------------------------------------------

		/// Return the definition for \p professionId, or nullptr when unknown.
		const CraftProfessionDefinition* FindProfession(std::string_view professionId) const;

		/// Learn a new profession for \p client.
		/// Enforces the max-primary-profession cap.
		/// Returns false with \p outError set when the constraint is violated.
		bool LearnProfession(
			std::vector<PlayerProfessionState>& clientProfessions,
			std::string_view professionId,
			std::string& outError) const;

		// ------------------------------------------------------------------
		// Recipe queries
		// ------------------------------------------------------------------

		/// Return a recipe definition by id, or nullptr when not found.
		const RecipeDefinition* FindRecipe(std::string_view recipeId) const;

		/// Return all recipe ids craftable for \p professionId at \p skillLevel.
		std::vector<std::string> GetCraftableRecipeIds(
			std::string_view professionId,
			uint32_t skillLevel) const;

		// ------------------------------------------------------------------
		// Active craft sessions
		// ------------------------------------------------------------------

		/// Return the active craft session for \p clientId, or nullptr when not crafting.
		const ActiveCraftSession* FindSession(uint32_t clientId) const;

		/// Begin a crafting cast.  Caller must have validated prerequisites.
		/// Returns false when \p clientId already has an active session.
		bool StartCraft(uint32_t clientId, std::string_view recipeId, uint32_t currentTick);

		/// Cancel the active craft for \p clientId.  No-op when not crafting.
		void CancelCraft(uint32_t clientId);

		/// Cancel all crafts for a disconnecting client.
		void CancelCraftsForClient(uint32_t clientId);

		/// Advance cast timers and collect completions into \p outResults.
		/// Skill-up and item grant are handled by the ServerApp after Tick returns.
		void Tick(
			uint32_t currentTick,
			uint16_t tickHz,
			std::vector<CraftCompletionResult>& outResults);

		/// Compute cast-bar progress (0–100) for the active craft of \p clientId.
		uint8_t GetProgressPercent(uint32_t clientId, uint32_t currentTick, uint16_t tickHz) const;

		/// Roll the skill-up for one craft result using diminishing-returns formula.
		bool RollSkillUp(
			const RecipeDefinition& recipe,
			uint32_t playerSkill) const;

		/// Roll critical craft quality for \p playerSkill (M36.3).
		/// Formula: chance = playerSkill / kMaxCraftingSkill * kCritCraftMaxChance.
		/// Returns the granted quality tier (Normal + optional +1 tier on crit).
		/// Result is capped at kMaxCraftQualityTier (Epic).
		ItemQualityTier RollCriticalQuality(uint32_t playerSkill) const;

	private:
		/// Load all definitions from crafting/recipes.json.
		bool LoadFromContent();

		engine::core::Config m_config;
		std::vector<CraftProfessionDefinition> m_professions;
		std::vector<RecipeDefinition> m_recipes;
		std::vector<ActiveCraftSession> m_sessions;
		mutable std::mt19937 m_rng;
		bool m_initialized = false;
	};
}
