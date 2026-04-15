#include "engine/server/CraftingSystem.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine::server
{
	namespace
	{
		// -------------------------------------------------------------------------
		// Minimal JSON parser (same pattern as SpawnerRuntime.cpp / ResourceNodeRuntime.cpp)
		// -------------------------------------------------------------------------

		enum class JT { Null, Bool, Number, String, Object, Array };

		struct JV
		{
			JT type = JT::Null;
			bool boolVal = false;
			double numVal = 0.0;
			std::string strVal;
			std::unordered_map<std::string, JV> obj;
			std::vector<JV> arr;
		};

		class JP final
		{
		public:
			explicit JP(std::string_view s) : m_s(s) {}

			bool Parse(JV& root, std::string& err)
			{
				Skip();
				if (!Val(root, err)) return false;
				Skip();
				if (m_p != m_s.size()) { err = "trailing characters"; return false; }
				return true;
			}

		private:
			void Skip()
			{
				while (m_p < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_p])))
					++m_p;
			}
			bool Eat(char c)
			{
				if (m_p >= m_s.size() || m_s[m_p] != c) return false;
				++m_p; return true;
			}
			bool Sw(std::string_view t) const
			{
				return m_s.substr(m_p, t.size()) == t;
			}
			bool Val(JV& v, std::string& e)
			{
				if (m_p >= m_s.size()) { e = "unexpected end"; return false; }
				switch (m_s[m_p])
				{
				case '{': return Obj(v, e);
				case '[': return Arr(v, e);
				case '"': v = {}; v.type = JT::String; return Str(v.strVal, e);
				default: break;
				}
				if (Sw("true"))  { m_p += 4; v = {}; v.type = JT::Bool; v.boolVal = true;  return true; }
				if (Sw("false")) { m_p += 5; v = {}; v.type = JT::Bool; v.boolVal = false; return true; }
				if (Sw("null"))  { m_p += 4; v = {}; v.type = JT::Null;                    return true; }
				return Num(v, e);
			}
			bool Obj(JV& v, std::string& e)
			{
				if (!Eat('{')) { e = "expected '{'"; return false; }
				v = {}; v.type = JT::Object; Skip();
				if (Eat('}')) return true;
				while (true)
				{
					std::string k; if (!Str(k, e)) return false;
					Skip(); if (!Eat(':')) { e = "expected ':'"; return false; }
					Skip(); JV ch; if (!Val(ch, e)) return false;
					v.obj.emplace(std::move(k), std::move(ch)); Skip();
					if (Eat('}')) return true;
					if (!Eat(',')) { e = "expected ','"; return false; }
					Skip();
				}
			}
			bool Arr(JV& v, std::string& e)
			{
				if (!Eat('[')) { e = "expected '['"; return false; }
				v = {}; v.type = JT::Array; Skip();
				if (Eat(']')) return true;
				while (true)
				{
					JV ch; if (!Val(ch, e)) return false;
					v.arr.emplace_back(std::move(ch)); Skip();
					if (Eat(']')) return true;
					if (!Eat(',')) { e = "expected ','"; return false; }
					Skip();
				}
			}
			bool Str(std::string& out, std::string& e)
			{
				if (!Eat('"')) { e = "expected string"; return false; }
				out.clear();
				while (m_p < m_s.size())
				{
					const char c = m_s[m_p++];
					if (c == '"') return true;
					if (c != '\\') { out.push_back(c); continue; }
					if (m_p >= m_s.size()) { e = "bad escape"; return false; }
					const char x = m_s[m_p++];
					switch (x)
					{
					case '"': out.push_back('"');  break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/');  break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					default: e = "unsupported escape"; return false;
					}
				}
				e = "unterminated string"; return false;
			}
			bool Num(JV& v, std::string& e)
			{
				const size_t s = m_p;
				if (m_p < m_s.size() && m_s[m_p] == '-') ++m_p;
				bool has = false;
				while (m_p < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_p]))) { has = true; ++m_p; }
				if (m_p < m_s.size() && m_s[m_p] == '.')
				{
					++m_p;
					while (m_p < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_p]))) { has = true; ++m_p; }
				}
				if (m_p < m_s.size() && (m_s[m_p] == 'e' || m_s[m_p] == 'E'))
				{
					++m_p;
					if (m_p < m_s.size() && (m_s[m_p] == '+' || m_s[m_p] == '-')) ++m_p;
					while (m_p < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_p]))) { has = true; ++m_p; }
				}
				if (!has) { e = "expected number"; return false; }
				const std::string tok(m_s.substr(s, m_p - s));
				char* end = nullptr;
				const double n = std::strtod(tok.c_str(), &end);
				if (!end || *end != '\0') { e = "invalid number"; return false; }
				v = {}; v.type = JT::Number; v.numVal = n; return true;
			}

			std::string_view m_s;
			size_t m_p = 0;
		};

		const JV* Get(const JV& obj, std::string_view k)
		{
			if (obj.type != JT::Object) return nullptr;
			const auto it = obj.obj.find(std::string(k));
			return it == obj.obj.end() ? nullptr : &it->second;
		}

		bool ToU32(const JV& v, uint32_t& out)
		{
			if (v.type != JT::Number || !std::isfinite(v.numVal) || v.numVal < 0.0
				|| v.numVal > static_cast<double>(std::numeric_limits<uint32_t>::max()))
				return false;
			const double t = std::floor(v.numVal);
			if (std::abs(t - v.numVal) > 1e-6) return false;
			out = static_cast<uint32_t>(t);
			return true;
		}

		bool ToF32(const JV& v, float& out)
		{
			if (v.type != JT::Number || !std::isfinite(v.numVal)) return false;
			out = static_cast<float>(v.numVal);
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// CraftingSystem
	// -------------------------------------------------------------------------

	CraftingSystem::CraftingSystem(const engine::core::Config& config)
		: m_config(config)
		, m_rng(static_cast<uint32_t>(std::time(nullptr)))
	{
		LOG_INFO(Net, "[CraftingSystem] Constructed");
	}

	CraftingSystem::~CraftingSystem()
	{
		Shutdown();
	}

	bool CraftingSystem::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[CraftingSystem] Init ignored: already initialized");
			return true;
		}

		if (!LoadFromContent())
		{
			LOG_ERROR(Net, "[CraftingSystem] Init FAILED: recipe load failed");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[CraftingSystem] Init OK (professions={}, recipes={})",
			m_professions.size(), m_recipes.size());
		return true;
	}

	void CraftingSystem::Shutdown()
	{
		if (!m_initialized && m_professions.empty() && m_recipes.empty())
			return;

		const size_t prof = m_professions.size();
		const size_t rec  = m_recipes.size();
		m_professions.clear();
		m_recipes.clear();
		m_sessions.clear();
		m_initialized = false;
		LOG_INFO(Net, "[CraftingSystem] Destroyed (professions={}, recipes={})", prof, rec);
	}

	const CraftProfessionDefinition* CraftingSystem::FindProfession(std::string_view professionId) const
	{
		for (const CraftProfessionDefinition& p : m_professions)
			if (p.professionId == professionId) return &p;
		return nullptr;
	}

	bool CraftingSystem::LearnProfession(
		std::vector<PlayerProfessionState>& clientProfessions,
		std::string_view professionId,
		std::string& outError) const
	{
		const CraftProfessionDefinition* def = FindProfession(professionId);
		if (def == nullptr)
		{
			outError = "unknown_profession";
			LOG_WARN(Net, "[CraftingSystem] LearnProfession rejected: unknown profession '{}'", professionId);
			return false;
		}

		for (const PlayerProfessionState& p : clientProfessions)
		{
			if (p.professionId == professionId)
			{
				outError = "already_known";
				LOG_WARN(Net, "[CraftingSystem] LearnProfession rejected: already known '{}'", professionId);
				return false;
			}
		}

		if (def->isPrimary)
		{
			uint32_t primaryCount = 0;
			for (const PlayerProfessionState& p : clientProfessions)
			{
				if (p.isPrimary) ++primaryCount;
			}
			if (primaryCount >= kMaxPrimaryProfessions)
			{
				outError = "primary_slots_full";
				LOG_WARN(Net, "[CraftingSystem] LearnProfession rejected: primary slots full (max {})",
					kMaxPrimaryProfessions);
				return false;
			}
		}

		PlayerProfessionState state{};
		state.professionId = std::string(professionId);
		state.skillLevel   = kInitialCraftingSkill;
		state.isPrimary    = def->isPrimary;
		clientProfessions.push_back(std::move(state));

		LOG_INFO(Net, "[CraftingSystem] Profession learned (profession='{}', isPrimary={})",
			professionId, def->isPrimary ? "true" : "false");
		return true;
	}

	const RecipeDefinition* CraftingSystem::FindRecipe(std::string_view recipeId) const
	{
		for (const RecipeDefinition& r : m_recipes)
			if (r.recipeId == recipeId) return &r;
		return nullptr;
	}

	std::vector<std::string> CraftingSystem::GetCraftableRecipeIds(
		std::string_view professionId,
		uint32_t skillLevel) const
	{
		std::vector<std::string> result;
		for (const RecipeDefinition& r : m_recipes)
		{
			if (r.professionId == professionId && r.skillRequired <= skillLevel)
				result.push_back(r.recipeId);
		}
		return result;
	}

	const ActiveCraftSession* CraftingSystem::FindSession(uint32_t clientId) const
	{
		for (const ActiveCraftSession& s : m_sessions)
			if (s.clientId == clientId) return &s;
		return nullptr;
	}

	bool CraftingSystem::StartCraft(uint32_t clientId, std::string_view recipeId, uint32_t currentTick)
	{
		if (FindSession(clientId) != nullptr)
		{
			LOG_WARN(Net, "[CraftingSystem] StartCraft rejected: client {} already crafting", clientId);
			return false;
		}
		ActiveCraftSession session{};
		session.clientId  = clientId;
		session.recipeId  = std::string(recipeId);
		session.startTick = currentTick;
		m_sessions.push_back(std::move(session));
		LOG_INFO(Net, "[CraftingSystem] Craft started (client_id={}, recipe='{}')", clientId, recipeId);
		return true;
	}

	void CraftingSystem::CancelCraft(uint32_t clientId)
	{
		const auto it = std::remove_if(
			m_sessions.begin(), m_sessions.end(),
			[clientId](const ActiveCraftSession& s) { return s.clientId == clientId; });
		if (it != m_sessions.end())
		{
			LOG_INFO(Net, "[CraftingSystem] Craft cancelled (client_id={})", clientId);
			m_sessions.erase(it, m_sessions.end());
		}
	}

	void CraftingSystem::CancelCraftsForClient(uint32_t clientId)
	{
		CancelCraft(clientId);
	}

	void CraftingSystem::Tick(
		uint32_t currentTick,
		uint16_t tickHz,
		std::vector<CraftCompletionResult>& outResults)
	{
		for (auto it = m_sessions.begin(); it != m_sessions.end();)
		{
			const RecipeDefinition* recipe = FindRecipe(it->recipeId);
			if (recipe == nullptr)
			{
				LOG_WARN(Net, "[CraftingSystem] Craft cancelled: missing recipe '{}' (client_id={})",
					it->recipeId, it->clientId);
				it = m_sessions.erase(it);
				continue;
			}

			const uint32_t durationTicks = static_cast<uint32_t>(
				recipe->craftTimeSec * static_cast<float>(tickHz));
			if (durationTicks == 0 || currentTick >= it->startTick + durationTicks)
			{
				CraftCompletionResult result{};
				result.clientId = it->clientId;
				result.recipeId = it->recipeId;
				outResults.push_back(std::move(result));

				LOG_INFO(Net, "[CraftingSystem] Craft completed (client_id={}, recipe='{}')",
					it->clientId, it->recipeId);
				it = m_sessions.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	uint8_t CraftingSystem::GetProgressPercent(
		uint32_t clientId,
		uint32_t currentTick,
		uint16_t tickHz) const
	{
		const ActiveCraftSession* session = FindSession(clientId);
		if (session == nullptr) return 0;

		const RecipeDefinition* recipe = FindRecipe(session->recipeId);
		if (recipe == nullptr) return 0;

		const uint32_t durationTicks = static_cast<uint32_t>(
			recipe->craftTimeSec * static_cast<float>(tickHz));
		if (durationTicks == 0) return 100;

		const uint32_t elapsed = (currentTick > session->startTick)
			? (currentTick - session->startTick) : 0u;
		const uint32_t pct = (elapsed * 100u) / durationTicks;
		return static_cast<uint8_t>(pct < 100u ? pct : 100u);
	}

	ItemQualityTier CraftingSystem::RollCriticalQuality(uint32_t playerSkill) const
	{
		// Formula: chance = playerSkill / kMaxCraftingSkill * kCritCraftMaxChance
		// At skill 300: 10%; at skill 1: ~0.033%.
		const float critChance =
			(static_cast<float>(playerSkill) / static_cast<float>(kMaxCraftingSkill))
			* kCritCraftMaxChance;

		if (critChance <= 0.0f)
			return ItemQualityTier::Normal;

		std::uniform_real_distribution<float> dist(0.0f, 1.0f);
		const bool isCrit = dist(m_rng) < critChance;

		if (!isCrit)
			return ItemQualityTier::Normal;

		// Crit: upgrade by +1 tier, cap at Epic.
		// Base is always Normal (no recipe can start above Normal via normal craft).
		const uint8_t nextTier = static_cast<uint8_t>(ItemQualityTier::Normal) + 1u;
		const uint8_t maxTier  = static_cast<uint8_t>(kMaxCraftQualityTier);
		return static_cast<ItemQualityTier>(nextTier < maxTier ? nextTier : maxTier);
	}

	bool CraftingSystem::RollSkillUp(
		const RecipeDefinition& recipe,
		uint32_t playerSkill) const
	{
		if (recipe.skillUpChance <= 0.0f) return false;

		float chance = recipe.skillUpChance;
		if (playerSkill > recipe.skillRequired)
		{
			const float penalty = static_cast<float>(playerSkill - recipe.skillRequired) / kSkillUpWindow;
			chance *= std::max(0.0f, 1.0f - penalty);
		}
		if (chance <= 0.0f) return false;
		if (chance >= 1.0f) return true;

		std::uniform_real_distribution<float> dist(0.0f, 1.0f);
		return dist(m_rng) < chance;
	}

	// -------------------------------------------------------------------------
	// Private: JSON loading
	// -------------------------------------------------------------------------

	bool CraftingSystem::LoadFromContent()
	{
		constexpr std::string_view kRelPath = "crafting/recipes.json";

		const std::string text = engine::platform::FileSystem::ReadAllTextContent(m_config, kRelPath);
		if (text.empty())
		{
			LOG_ERROR(Net, "[CraftingSystem] Load FAILED: file missing or empty ({})", kRelPath);
			return false;
		}

		JV root;
		std::string parseErr;
		JP parser(text);
		if (!parser.Parse(root, parseErr))
		{
			LOG_ERROR(Net, "[CraftingSystem] Load FAILED: JSON parse error '{}' ({})", parseErr, kRelPath);
			return false;
		}

		// ------------------------------------------------------------------
		// Load profession definitions
		// ------------------------------------------------------------------
		m_professions.clear();
		const JV* profArr = Get(root, "professions");
		if (profArr == nullptr || profArr->type != JT::Array)
		{
			LOG_ERROR(Net, "[CraftingSystem] Load FAILED: missing 'professions' array in {}", kRelPath);
			return false;
		}

		std::unordered_set<std::string> seenProfIds;
		for (size_t i = 0; i < profArr->arr.size(); ++i)
		{
			const JV& e = profArr->arr[i];
			if (e.type != JT::Object) continue;

			const JV* idV  = Get(e, "id");
			const JV* priV = Get(e, "isPrimary");
			if (idV == nullptr || idV->type != JT::String || idV->strVal.empty()) continue;
			if (!seenProfIds.emplace(idV->strVal).second) continue;

			CraftProfessionDefinition pd{};
			pd.professionId = idV->strVal;
			pd.isPrimary    = (priV != nullptr && priV->type == JT::Bool) ? priV->boolVal : true;
			m_professions.push_back(std::move(pd));
			LOG_INFO(Net, "[CraftingSystem] Profession loaded (id='{}', isPrimary={})",
				m_professions.back().professionId,
				m_professions.back().isPrimary ? "true" : "false");
		}

		if (m_professions.empty())
		{
			LOG_ERROR(Net, "[CraftingSystem] Load FAILED: no professions defined in {}", kRelPath);
			return false;
		}

		// ------------------------------------------------------------------
		// Load recipe definitions
		// ------------------------------------------------------------------
		m_recipes.clear();
		const JV* recArr = Get(root, "recipes");
		if (recArr == nullptr || recArr->type != JT::Array)
		{
			LOG_WARN(Net, "[CraftingSystem] No 'recipes' array found in {} — continuing with empty list",
				kRelPath);
			return true;
		}

		std::unordered_set<std::string> seenRecipeIds;
		for (size_t i = 0; i < recArr->arr.size(); ++i)
		{
			const JV& e = recArr->arr[i];
			if (e.type != JT::Object) continue;

			const JV* idV       = Get(e, "id");
			const JV* profV     = Get(e, "profession");
			const JV* skillReqV = Get(e, "skill_required");
			const JV* ingV      = Get(e, "ingredients");
			const JV* outV      = Get(e, "output");
			const JV* timeV     = Get(e, "craft_time");
			const JV* skillUpV  = Get(e, "skill_up_chance");

			if (idV == nullptr || idV->type != JT::String || idV->strVal.empty())
			{
				LOG_WARN(Net, "[CraftingSystem] recipes[{}] missing id — skipped", i);
				continue;
			}
			if (!seenRecipeIds.emplace(idV->strVal).second)
			{
				LOG_WARN(Net, "[CraftingSystem] Duplicate recipe id '{}' — skipped", idV->strVal);
				continue;
			}
			if (profV == nullptr || profV->type != JT::String || profV->strVal.empty())
			{
				LOG_WARN(Net, "[CraftingSystem] Recipe '{}' missing profession — skipped", idV->strVal);
				continue;
			}

			RecipeDefinition rd{};
			rd.recipeId     = idV->strVal;
			rd.professionId = profV->strVal;

			if (skillReqV != nullptr) (void)ToU32(*skillReqV, rd.skillRequired);
			if (rd.skillRequired == 0) rd.skillRequired = 1;

			if (timeV != nullptr) (void)ToF32(*timeV, rd.craftTimeSec);
			if (rd.craftTimeSec <= 0.0f) rd.craftTimeSec = 3.0f;

			if (skillUpV != nullptr) (void)ToF32(*skillUpV, rd.skillUpChance);
			rd.skillUpChance = std::max(0.0f, std::min(1.0f, rd.skillUpChance));

			// Output item
			if (outV != nullptr && outV->type == JT::Object)
			{
				const JV* outIdV  = Get(*outV, "item_id");
				const JV* outQtyV = Get(*outV, "quantity");
				if (outIdV != nullptr) (void)ToU32(*outIdV, rd.outputItemId);
				if (outQtyV != nullptr) (void)ToU32(*outQtyV, rd.outputQuantity);
			}
			if (rd.outputQuantity == 0) rd.outputQuantity = 1;

			// Ingredients
			if (ingV != nullptr && ingV->type == JT::Array)
			{
				for (size_t j = 0; j < ingV->arr.size(); ++j)
				{
					const JV& ing = ingV->arr[j];
					if (ing.type != JT::Object) continue;
					const JV* ingIdV  = Get(ing, "item_id");
					const JV* ingQtyV = Get(ing, "quantity");
					if (ingIdV == nullptr) continue;

					RecipeIngredient ri{};
					(void)ToU32(*ingIdV, ri.itemId);
					if (ingQtyV != nullptr) (void)ToU32(*ingQtyV, ri.quantity);
					if (ri.quantity == 0) ri.quantity = 1;
					if (ri.itemId != 0)
						rd.ingredients.push_back(ri);
				}
			}

			m_recipes.push_back(std::move(rd));
			LOG_INFO(Net, "[CraftingSystem] Recipe loaded (id='{}', profession='{}', skill_req={}, ingredients={})",
				m_recipes.back().recipeId,
				m_recipes.back().professionId,
				m_recipes.back().skillRequired,
				m_recipes.back().ingredients.size());
		}

		LOG_INFO(Net, "[CraftingSystem] Load OK (professions={}, recipes={})",
			m_professions.size(), m_recipes.size());
		return true;
	}
}
