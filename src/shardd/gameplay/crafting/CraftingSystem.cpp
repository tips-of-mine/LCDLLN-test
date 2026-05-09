#include "src/shardd/gameplay/crafting/CraftingSystem.h"

#include "src/shared/server_bootstrap/ServerApp.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace engine::server
{
	namespace
	{
		// -----------------------------------------------------------------------
		// Minimal inline JSON parser (same pattern as SpawnerRuntime / GatheringSystem)
		// -----------------------------------------------------------------------

		enum class JType2 { Null, Bool, Number, String, Object, Array };

		struct JVal2
		{
			JType2 type = JType2::Null;
			bool   bVal  = false;
			double nVal  = 0.0;
			std::string sVal;
			std::unordered_map<std::string, JVal2> oVal;
			std::vector<JVal2> aVal;
		};

		class JP2 final
		{
		public:
			explicit JP2(std::string_view src) : m_src(src) {}

			bool Parse(JVal2& out, std::string& err)
			{
				Skip();
				if (!Val(out, err)) return false;
				Skip();
				if (m_pos != m_src.size()) { err = "trailing chars"; return false; }
				return true;
			}

		private:
			void Skip() { while (m_pos < m_src.size() && std::isspace(static_cast<unsigned char>(m_src[m_pos]))) ++m_pos; }
			bool Eat(char c) { if (m_pos < m_src.size() && m_src[m_pos] == c) { ++m_pos; return true; } return false; }
			bool Sw(std::string_view tok) const { return m_src.substr(m_pos, tok.size()) == tok; }

			bool Val(JVal2& out, std::string& err)
			{
				if (m_pos >= m_src.size()) { err = "eof"; return false; }
				switch (m_src[m_pos])
				{
				case '{': return Obj(out, err);
				case '[': return Arr(out, err);
				case '"': out = {}; out.type = JType2::String; return Str(out.sVal, err);
				default: break;
				}
				if (Sw("true"))  { m_pos += 4; out = {}; out.type = JType2::Bool; out.bVal = true;  return true; }
				if (Sw("false")) { m_pos += 5; out = {}; out.type = JType2::Bool; out.bVal = false; return true; }
				if (Sw("null"))  { m_pos += 4; out = {}; out.type = JType2::Null; return true; }
				return Num(out, err);
			}

			bool Obj(JVal2& out, std::string& err)
			{
				if (!Eat('{')) { err = "expected {"; return false; }
				out = {}; out.type = JType2::Object;
				Skip();
				if (Eat('}')) return true;
				for (;;)
				{
					std::string key; if (!Str(key, err)) return false;
					Skip(); if (!Eat(':')) { err = "expected :"; return false; }
					Skip(); JVal2 child; if (!Val(child, err)) return false;
					out.oVal.emplace(std::move(key), std::move(child));
					Skip(); if (Eat('}')) return true;
					if (!Eat(',')) { err = "expected ,"; return false; }
					Skip();
				}
			}

			bool Arr(JVal2& out, std::string& err)
			{
				if (!Eat('[')) { err = "expected ["; return false; }
				out = {}; out.type = JType2::Array;
				Skip(); if (Eat(']')) return true;
				for (;;)
				{
					JVal2 child; if (!Val(child, err)) return false;
					out.aVal.push_back(std::move(child));
					Skip(); if (Eat(']')) return true;
					if (!Eat(',')) { err = "expected ,"; return false; }
					Skip();
				}
			}

			bool Str(std::string& out, std::string& err)
			{
				if (!Eat('"')) { err = "expected string"; return false; }
				out.clear();
				while (m_pos < m_src.size())
				{
					const char c = m_src[m_pos++];
					if (c == '"') return true;
					if (c != '\\') { out.push_back(c); continue; }
					if (m_pos >= m_src.size()) { err = "bad escape"; return false; }
					const char e = m_src[m_pos++];
					switch (e) {
					case '"':  out.push_back('"');  break;
					case '\\': out.push_back('\\'); break;
					case '/':  out.push_back('/');  break;
					case 'n':  out.push_back('\n'); break;
					case 'r':  out.push_back('\r'); break;
					case 't':  out.push_back('\t'); break;
					default:   err = "bad escape"; return false;
					}
				}
				err = "unterminated string"; return false;
			}

			bool Num(JVal2& out, std::string& err)
			{
				const size_t start = m_pos;
				if (m_pos < m_src.size() && m_src[m_pos] == '-') ++m_pos;
				bool hasDigit = false;
				while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) { hasDigit = true; ++m_pos; }
				if (m_pos < m_src.size() && m_src[m_pos] == '.') { ++m_pos; while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) { hasDigit = true; ++m_pos; } }
				if (!hasDigit) { err = "expected number"; return false; }
				const std::string tok(m_src.substr(start, m_pos - start));
				char* ep = nullptr;
				const double v = std::strtod(tok.c_str(), &ep);
				if (ep == nullptr || *ep != '\0') { err = "bad number"; return false; }
				out = {}; out.type = JType2::Number; out.nVal = v;
				return true;
			}

			std::string_view m_src;
			size_t m_pos = 0;
		};

		const JVal2* Mem2(const JVal2& obj, std::string_view key)
		{
			if (obj.type != JType2::Object) return nullptr;
			const auto it = obj.oVal.find(std::string(key));
			return (it != obj.oVal.end()) ? &it->second : nullptr;
		}

		bool GetUint2(const JVal2& v, uint32_t& out)
		{
			if (v.type != JType2::Number || !std::isfinite(v.nVal) || v.nVal < 0.0
				|| v.nVal > static_cast<double>(std::numeric_limits<uint32_t>::max())) return false;
			const double t = std::floor(v.nVal);
			if (std::abs(t - v.nVal) > 0.0001) return false;
			out = static_cast<uint32_t>(t);
			return true;
		}

		bool GetFloat2(const JVal2& v, float& out)
		{
			if (v.type != JType2::Number || !std::isfinite(v.nVal)) return false;
			out = static_cast<float>(v.nVal);
			return true;
		}
	} // anonymous namespace

	// -------------------------------------------------------------------------
	// CraftingSystem — public API
	// -------------------------------------------------------------------------

	bool CraftingSystem::Init(const engine::core::Config& config, uint16_t tickHz)
	{
		if (m_initialized)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] Init ignored: already initialized");
			return true;
		}

		m_tickHz = (tickHz > 0) ? tickHz : 20u;

		if (!LoadRecipes(config))
		{
			LOG_ERROR(Gameplay, "[CraftingSystem] Init FAILED: could not load recipes");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Gameplay, "[CraftingSystem] Init OK (recipes={})", m_recipes.size());
		return true;
	}

	void CraftingSystem::Shutdown()
	{
		m_sessions.clear();
		m_recipes.clear();
		m_initialized = false;
		LOG_INFO(Gameplay, "[CraftingSystem] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Profession management
	// -------------------------------------------------------------------------

	LearnProfessionResult CraftingSystem::LearnProfession(
		ConnectedClient&   client,
		const std::string& professionKey,
		bool               asPrimary)
	{
		/// Check if already known.
		if (FindProfession(client, professionKey) != nullptr)
		{
			LOG_INFO(Gameplay, "[CraftingSystem] LearnProfession: client {} already knows '{}'",
			         client.clientId, professionKey);
			return LearnProfessionResult::AlreadyKnown;
		}

		/// Enforce primary profession cap.
		if (asPrimary)
		{
			uint32_t primaryCount = 0;
			for (const ProfessionEntry& e : client.professions)
			{
				if (e.isPrimary) ++primaryCount;
			}
			if (primaryCount >= kMaxPrimaryProfessions)
			{
				LOG_WARN(Gameplay, "[CraftingSystem] LearnProfession rejected: client {} already has {} primary professions",
				         client.clientId, primaryCount);
				return LearnProfessionResult::TooManyPrimary;
			}
		}

		ProfessionEntry entry{};
		entry.professionKey = professionKey;
		entry.skillLevel    = 1;
		entry.isPrimary     = asPrimary;
		client.professions.push_back(std::move(entry));

		LOG_INFO(Gameplay, "[CraftingSystem] LearnProfession OK: client={} profession='{}' primary={}",
		         client.clientId, professionKey, asPrimary ? 1 : 0);
		return LearnProfessionResult::Ok;
	}

	// -------------------------------------------------------------------------
	// Recipe queries
	// -------------------------------------------------------------------------

	std::vector<const RecipeDefinition*> CraftingSystem::GetVisibleRecipes(
		const std::string& professionKey,
		uint32_t           playerSkillLevel) const
	{
		std::vector<const RecipeDefinition*> result;
		for (const RecipeDefinition& r : m_recipes)
		{
			if (r.professionKey != professionKey) continue;
			/// Show all recipes the player can craft or will be able to craft
			/// within the next 15 skill levels (so they can see upcoming unlocks).
			if (r.skillRequired <= playerSkillLevel + 15u)
				result.push_back(&r);
		}
		return result;
	}

	const RecipeDefinition* CraftingSystem::FindRecipe(const std::string& recipeId) const
	{
		for (const RecipeDefinition& r : m_recipes)
		{
			if (r.recipeId == recipeId) return &r;
		}
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// Craft session management
	// -------------------------------------------------------------------------

	CraftOpResult CraftingSystem::TryStartCraft(
		ConnectedClient&   client,
		const std::string& recipeId,
		uint32_t           currentTick,
		uint32_t&          outDurationTicks)
	{
		outDurationTicks = 0;

		if (FindSession(client.clientId) != nullptr)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] TryStartCraft: client {} already crafting", client.clientId);
			return CraftOpResult::AlreadyCrafting;
		}

		const RecipeDefinition* recipe = FindRecipe(recipeId);
		if (recipe == nullptr)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] TryStartCraft: unknown recipe '{}'", recipeId);
			return CraftOpResult::RecipeNotFound;
		}

		const ProfessionEntry* profession = FindProfessionConst(client, recipe->professionKey);
		if (profession == nullptr)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] TryStartCraft: client {} lacks profession '{}'",
			         client.clientId, recipe->professionKey);
			return CraftOpResult::NoProfession;
		}

		if (profession->skillLevel < recipe->skillRequired)
		{
			LOG_WARN(Gameplay, "[CraftingSystem] TryStartCraft: client {} skill too low ({} < {})",
			         client.clientId, profession->skillLevel, recipe->skillRequired);
			return CraftOpResult::SkillTooLow;
		}

		/// Validate ingredients.
		for (const RecipeIngredient& ing : recipe->ingredients)
		{
			uint32_t totalInBag = 0;
			for (const ItemStack& stack : client.inventory)
			{
				if (stack.itemId == ing.itemId) totalInBag += stack.quantity;
			}
			if (totalInBag < ing.quantity)
			{
				LOG_WARN(Gameplay, "[CraftingSystem] TryStartCraft: client {} missing item {} (have {} need {})",
				         client.clientId, ing.itemId, totalInBag, ing.quantity);
				return CraftOpResult::MissingIngredients;
			}
		}

		const uint32_t durationTicks =
			static_cast<uint32_t>(recipe->craftTimeSec * static_cast<float>(m_tickHz));

		CraftingSessionState session{};
		session.clientId       = client.clientId;
		session.recipeId       = recipeId;
		session.completionTick = currentTick + durationTicks;
		m_sessions.push_back(std::move(session));

		outDurationTicks = durationTicks;
		LOG_INFO(Gameplay, "[CraftingSystem] Craft started: client={} recipe='{}' durationTicks={}",
		         client.clientId, recipeId, durationTicks);
		return CraftOpResult::Ok;
	}

	std::string CraftingSystem::CancelCraft(uint32_t clientId)
	{
		const auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
			[clientId](const CraftingSessionState& s) { return s.clientId == clientId; });
		if (it == m_sessions.end()) return {};
		const std::string recipeId = it->recipeId;
		m_sessions.erase(it);
		LOG_INFO(Gameplay, "[CraftingSystem] Craft cancelled: client={} recipe='{}'", clientId, recipeId);
		return recipeId;
	}

	CraftingSessionState* CraftingSystem::FindSession(uint32_t clientId)
	{
		for (CraftingSessionState& s : m_sessions)
		{
			if (s.clientId == clientId) return &s;
		}
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// Tick
	// -------------------------------------------------------------------------

	void CraftingSystem::Tick(
		uint32_t                      currentTick,
		std::vector<ConnectedClient>& clients,
		std::vector<uint32_t>&        outCompletedClientIds,
		std::vector<std::string>&     outCompletedRecipeIds,
		std::vector<uint8_t>&         outSkillGained,
		std::vector<uint32_t>&        outNewSkillLevel,
		std::vector<CraftQualityTier>& outQualityTier)
	{
		std::vector<CraftingSessionState> remaining;
		remaining.reserve(m_sessions.size());

		for (CraftingSessionState& session : m_sessions)
		{
			if (currentTick < session.completionTick)
			{
				remaining.push_back(session);
				continue;
			}

			/// Find the client.
			ConnectedClient* client = nullptr;
			for (ConnectedClient& c : clients)
			{
				if (c.clientId == session.clientId) { client = &c; break; }
			}
			if (client == nullptr)
			{
				LOG_DEBUG(Gameplay, "[CraftingSystem] Session discarded: client {} disconnected", session.clientId);
				continue;
			}

			const RecipeDefinition* recipe = FindRecipe(session.recipeId);
			if (recipe == nullptr)
			{
				LOG_WARN(Gameplay, "[CraftingSystem] Tick: recipe '{}' no longer exists, discarding session",
				         session.recipeId);
				continue;
			}

			/// Consume ingredients (validated at start; apply best-effort here).
			bool ingredientsOk = true;
			for (const RecipeIngredient& ing : recipe->ingredients)
			{
				if (!RemoveItemFromInventory(*client, ing.itemId, ing.quantity))
				{
					ingredientsOk = false;
					LOG_WARN(Gameplay, "[CraftingSystem] Tick: ingredient missing at completion (item={}); aborting craft",
					         ing.itemId);
					break;
				}
			}

		if (!ingredientsOk)
		{
			/// Don't grant output — session is consumed.
			outCompletedClientIds.push_back(session.clientId);
			outCompletedRecipeIds.push_back(session.recipeId);
			outSkillGained.push_back(0);
			outNewSkillLevel.push_back(0);
			outQualityTier.push_back(CraftQualityTier::Normal);
			continue;
		}

		/// Add output item.
		AddItemToInventory(*client, recipe->outputItemId, recipe->outputQuantity);

		/// M36.3 — Roll critical craft chance and determine quality tier.
		uint32_t playerSkill = 0;
		ProfessionEntry* profession = FindProfession(*client, recipe->professionKey);
		if (profession != nullptr)
		{
			playerSkill = profession->skillLevel;
		}
		const CraftQualityTier quality = RollCriticalCraft(playerSkill);

		/// Roll skill-up.
		uint8_t  gained   = 0;
		uint32_t newLevel = 0;
		if (profession != nullptr)
		{
			const float prob = SkillUpProbability(profession->skillLevel, *recipe);
			if (prob > 0.0f)
			{
				const float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
				if (roll < prob && profession->skillLevel < kMaxProfessionSkillLevel)
				{
					++profession->skillLevel;
					gained = 1;
					LOG_INFO(Gameplay, "[CraftingSystem] Skill-up: client={} profession='{}' newLevel={}",
					         client->clientId, recipe->professionKey, profession->skillLevel);
				}
			}
			newLevel = profession->skillLevel;
		}

		outCompletedClientIds.push_back(session.clientId);
		outCompletedRecipeIds.push_back(session.recipeId);
		outSkillGained.push_back(gained);
		outNewSkillLevel.push_back(newLevel);
		outQualityTier.push_back(quality);

		LOG_INFO(Gameplay,
		         "[CraftingSystem] Craft complete: client={} recipe='{}' quality={} skillGained={}",
		         session.clientId, session.recipeId,
		         static_cast<uint8_t>(quality), gained);
		}

		m_sessions = std::move(remaining);
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	bool CraftingSystem::LoadRecipes(const engine::core::Config& config)
	{
		m_recipes.clear();

		constexpr std::string_view kRelPath = "crafting/recipes.json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(config, std::string(kRelPath));
		if (jsonText.empty())
		{
			LOG_ERROR(Gameplay, "[CraftingSystem] LoadRecipes FAILED: file missing or empty ({})", kRelPath);
			return false;
		}

		JVal2 root;
		std::string parseError;
		JP2 parser(jsonText);
		if (!parser.Parse(root, parseError))
		{
			LOG_ERROR(Gameplay, "[CraftingSystem] LoadRecipes FAILED: parse error '{}' ({})", parseError, kRelPath);
			return false;
		}

		const JVal2* recipesArr = Mem2(root, "recipes");
		if (recipesArr == nullptr || recipesArr->type != JType2::Array || recipesArr->aVal.empty())
		{
			LOG_ERROR(Gameplay, "[CraftingSystem] LoadRecipes FAILED: root.recipes must be a non-empty array");
			return false;
		}

		for (size_t i = 0; i < recipesArr->aVal.size(); ++i)
		{
			const JVal2& entry = recipesArr->aVal[i];
			if (entry.type != JType2::Object) continue;

			const JVal2* idV          = Mem2(entry, "id");
			const JVal2* professionV  = Mem2(entry, "profession");
			const JVal2* skillReqV    = Mem2(entry, "skill_required");
			const JVal2* ingredientsV = Mem2(entry, "ingredients");
			const JVal2* outputV      = Mem2(entry, "output");
			const JVal2* craftTimeV   = Mem2(entry, "craft_time");
			const JVal2* skillUpV     = Mem2(entry, "skill_up_chance");

			if (idV == nullptr || idV->type != JType2::String || idV->sVal.empty())
			{
				LOG_WARN(Gameplay, "[CraftingSystem] Recipe [{}] missing 'id' — skipped", i);
				continue;
			}
			if (professionV == nullptr || professionV->type != JType2::String || professionV->sVal.empty())
			{
				LOG_WARN(Gameplay, "[CraftingSystem] Recipe '{}' missing 'profession' — skipped", idV->sVal);
				continue;
			}

			RecipeDefinition recipe{};
			recipe.recipeId       = idV->sVal;
			recipe.professionKey  = professionV->sVal;

			if (skillReqV != nullptr)  (void)GetUint2(*skillReqV, recipe.skillRequired);
			if (craftTimeV != nullptr) (void)GetFloat2(*craftTimeV, recipe.craftTimeSec);
			if (skillUpV != nullptr)   (void)GetFloat2(*skillUpV, recipe.skillUpChance);

			/// Output
			if (outputV != nullptr && outputV->type == JType2::Object)
			{
				const JVal2* oItemV = Mem2(*outputV, "item_id");
				const JVal2* oQtyV  = Mem2(*outputV, "quantity");
				if (oItemV != nullptr) (void)GetUint2(*oItemV, recipe.outputItemId);
				if (oQtyV  != nullptr) (void)GetUint2(*oQtyV, recipe.outputQuantity);
			}

			/// Ingredients
			if (ingredientsV != nullptr && ingredientsV->type == JType2::Array)
			{
				for (size_t j = 0; j < ingredientsV->aVal.size(); ++j)
				{
					const JVal2& ing = ingredientsV->aVal[j];
					if (ing.type != JType2::Object) continue;
					const JVal2* itemIdV = Mem2(ing, "item_id");
					const JVal2* qtyV    = Mem2(ing, "quantity");
					RecipeIngredient ingEntry{};
					if (itemIdV != nullptr) (void)GetUint2(*itemIdV, ingEntry.itemId);
					if (qtyV    != nullptr) (void)GetUint2(*qtyV, ingEntry.quantity);
					if (ingEntry.itemId != 0 && ingEntry.quantity > 0)
						recipe.ingredients.push_back(ingEntry);
				}
			}

			if (recipe.outputItemId == 0)
			{
				LOG_WARN(Gameplay, "[CraftingSystem] Recipe '{}' has no valid output — skipped", recipe.recipeId);
				continue;
			}

			LOG_INFO(Gameplay,
			         "[CraftingSystem] Recipe loaded (id='{}' profession='{}' skill_req={} ingredients={} output_item={})",
			         recipe.recipeId, recipe.professionKey, recipe.skillRequired,
			         recipe.ingredients.size(), recipe.outputItemId);
			m_recipes.push_back(std::move(recipe));
		}

		LOG_INFO(Gameplay, "[CraftingSystem] LoadRecipes OK (recipes={})", m_recipes.size());
		return !m_recipes.empty();
	}

	float CraftingSystem::SkillUpProbability(uint32_t playerSkillLevel, const RecipeDefinition& recipe) const
	{
		if (playerSkillLevel < recipe.skillRequired)
		{
			return 0.0f;
		}
		const uint32_t diff = playerSkillLevel - recipe.skillRequired;
		if (diff == 0)
		{
			return recipe.skillUpChance;
		}
		if (diff >= kSkillUpGreenWindow)
		{
			return 0.0f;
		}
		/// Linear diminishing returns: 100% at diff=0, 0% at diff=kSkillUpGreenWindow.
		return recipe.skillUpChance * (1.0f - static_cast<float>(diff) / static_cast<float>(kSkillUpGreenWindow));
	}

	ProfessionEntry* CraftingSystem::FindProfession(ConnectedClient& client, const std::string& professionKey)
	{
		for (ProfessionEntry& e : client.professions)
		{
			if (e.professionKey == professionKey) return &e;
		}
		return nullptr;
	}

	const ProfessionEntry* CraftingSystem::FindProfessionConst(const ConnectedClient& client, const std::string& professionKey)
	{
		for (const ProfessionEntry& e : client.professions)
		{
			if (e.professionKey == professionKey) return &e;
		}
		return nullptr;
	}

	bool CraftingSystem::RemoveItemFromInventory(ConnectedClient& client, uint32_t itemId, uint32_t quantity)
	{
		uint32_t remaining = quantity;
		for (ItemStack& stack : client.inventory)
		{
			if (stack.itemId != itemId || remaining == 0) continue;
			const uint32_t take = std::min(stack.quantity, remaining);
			stack.quantity -= take;
			remaining -= take;
		}
		/// Purge empty stacks.
		client.inventory.erase(
			std::remove_if(client.inventory.begin(), client.inventory.end(),
				[](const ItemStack& s) { return s.quantity == 0; }),
			client.inventory.end());
		return remaining == 0;
	}

	void CraftingSystem::AddItemToInventory(ConnectedClient& client, uint32_t itemId, uint32_t quantity)
	{
		for (ItemStack& stack : client.inventory)
		{
			if (stack.itemId == itemId) { stack.quantity += quantity; return; }
		}
		client.inventory.push_back({ itemId, quantity });
	}

	CraftQualityTier CraftingSystem::RollCriticalCraft(uint32_t playerSkillLevel)
	{
		/// Base quality is Normal (M36.3 spec: "Base quality: normal").
		CraftQualityTier quality = CraftQualityTier::Normal;

		/// Crit chance = playerSkillLevel / 300 * 10% (M36.3 spec).
		const float critChance =
			(static_cast<float>(playerSkillLevel) / kCritSkillDivisor) * kCritBaseFraction;

		if (critChance <= 0.0f)
		{
			return quality;
		}

		const float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		if (roll < critChance)
		{
			/// Critical craft: upgrade quality +1, capped at Epic (M36.3 spec: "cap quality, epic max").
			const uint8_t current = static_cast<uint8_t>(quality);
			const uint8_t capped  = static_cast<uint8_t>(kMaxCraftQualityTier);
			quality = static_cast<CraftQualityTier>(std::min(current + 1u, static_cast<uint32_t>(capped)));
			LOG_INFO(Gameplay, "[CraftingSystem] Critical craft! skill={} quality={}",
			         playerSkillLevel, static_cast<uint8_t>(quality));
		}

		return quality;
	}
}
