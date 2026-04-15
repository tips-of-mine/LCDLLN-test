#include "engine/server/ResourceNodeRuntime.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine::server
{
	namespace
	{
		// -------------------------------------------------------------------------
		// Minimal JSON parser (mirrors SpawnerRuntime pattern; no new deps)
		// -------------------------------------------------------------------------

		enum class JsonType { Null, Bool, Number, String, Object, Array };

		struct JsonValue
		{
			JsonType type = JsonType::Null;
			bool boolValue = false;
			double numberValue = 0.0;
			std::string stringValue;
			std::unordered_map<std::string, JsonValue> objectValue;
			std::vector<JsonValue> arrayValue;
		};

		class JsonParser final
		{
		public:
			explicit JsonParser(std::string_view input) : m_input(input) {}

			bool Parse(JsonValue& outRoot, std::string& outError)
			{
				Skip();
				if (!ParseValue(outRoot, outError)) return false;
				Skip();
				if (m_pos != m_input.size())
				{
					outError = "unexpected trailing characters";
					return false;
				}
				return true;
			}

		private:
			void Skip()
			{
				while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos])))
					++m_pos;
			}

			bool Eat(char c)
			{
				if (m_pos >= m_input.size() || m_input[m_pos] != c) return false;
				++m_pos;
				return true;
			}

			bool StartsWith(std::string_view tok) const
			{
				return m_input.substr(m_pos, tok.size()) == tok;
			}

			bool ParseValue(JsonValue& v, std::string& err)
			{
				if (m_pos >= m_input.size()) { err = "unexpected end"; return false; }
				switch (m_input[m_pos])
				{
				case '{': return ParseObject(v, err);
				case '[': return ParseArray(v, err);
				case '"':
					v = {}; v.type = JsonType::String;
					return ParseString(v.stringValue, err);
				default: break;
				}
				if (StartsWith("true"))  { m_pos += 4; v = {}; v.type = JsonType::Bool; v.boolValue = true;  return true; }
				if (StartsWith("false")) { m_pos += 5; v = {}; v.type = JsonType::Bool; v.boolValue = false; return true; }
				if (StartsWith("null"))  { m_pos += 4; v = {}; v.type = JsonType::Null;                      return true; }
				return ParseNumber(v, err);
			}

			bool ParseObject(JsonValue& v, std::string& err)
			{
				if (!Eat('{')) { err = "expected '{'"; return false; }
				v = {}; v.type = JsonType::Object;
				Skip();
				if (Eat('}')) return true;
				while (true)
				{
					std::string key;
					if (!ParseString(key, err)) return false;
					Skip();
					if (!Eat(':')) { err = "expected ':'"; return false; }
					Skip();
					JsonValue child;
					if (!ParseValue(child, err)) return false;
					v.objectValue.emplace(std::move(key), std::move(child));
					Skip();
					if (Eat('}')) return true;
					if (!Eat(',')) { err = "expected ','"; return false; }
					Skip();
				}
			}

			bool ParseArray(JsonValue& v, std::string& err)
			{
				if (!Eat('[')) { err = "expected '['"; return false; }
				v = {}; v.type = JsonType::Array;
				Skip();
				if (Eat(']')) return true;
				while (true)
				{
					JsonValue child;
					if (!ParseValue(child, err)) return false;
					v.arrayValue.emplace_back(std::move(child));
					Skip();
					if (Eat(']')) return true;
					if (!Eat(',')) { err = "expected ','"; return false; }
					Skip();
				}
			}

			bool ParseString(std::string& out, std::string& err)
			{
				if (!Eat('"')) { err = "expected string"; return false; }
				out.clear();
				while (m_pos < m_input.size())
				{
					const char c = m_input[m_pos++];
					if (c == '"') return true;
					if (c != '\\') { out.push_back(c); continue; }
					if (m_pos >= m_input.size()) { err = "unterminated escape"; return false; }
					const char e = m_input[m_pos++];
					switch (e)
					{
					case '"':  out.push_back('"');  break;
					case '\\': out.push_back('\\'); break;
					case '/':  out.push_back('/');  break;
					case 'b':  out.push_back('\b'); break;
					case 'f':  out.push_back('\f'); break;
					case 'n':  out.push_back('\n'); break;
					case 'r':  out.push_back('\r'); break;
					case 't':  out.push_back('\t'); break;
					default:   err = "unsupported escape"; return false;
					}
				}
				err = "unterminated string"; return false;
			}

			bool ParseNumber(JsonValue& v, std::string& err)
			{
				const size_t start = m_pos;
				if (m_pos < m_input.size() && m_input[m_pos] == '-') ++m_pos;
				bool hasDigit = false;
				while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
				{ hasDigit = true; ++m_pos; }
				if (m_pos < m_input.size() && m_input[m_pos] == '.')
				{
					++m_pos;
					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
					{ hasDigit = true; ++m_pos; }
				}
				if (m_pos < m_input.size() && (m_input[m_pos] == 'e' || m_input[m_pos] == 'E'))
				{
					++m_pos;
					if (m_pos < m_input.size() && (m_input[m_pos] == '+' || m_input[m_pos] == '-')) ++m_pos;
					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])))
					{ hasDigit = true; ++m_pos; }
				}
				if (!hasDigit) { err = "expected number"; return false; }
				const std::string tok(m_input.substr(start, m_pos - start));
				char* end = nullptr;
				const double val = std::strtod(tok.c_str(), &end);
				if (end == nullptr || *end != '\0') { err = "invalid number"; return false; }
				v = {}; v.type = JsonType::Number; v.numberValue = val;
				return true;
			}

			std::string_view m_input;
			size_t m_pos = 0;
		};

		const JsonValue* ObjGet(const JsonValue& obj, std::string_view key)
		{
			if (obj.type != JsonType::Object) return nullptr;
			const auto it = obj.objectValue.find(std::string(key));
			return it == obj.objectValue.end() ? nullptr : &it->second;
		}

		bool ToUint(const JsonValue& v, uint32_t& out)
		{
			if (v.type != JsonType::Number || !std::isfinite(v.numberValue)
				|| v.numberValue < 0.0
				|| v.numberValue > static_cast<double>(std::numeric_limits<uint32_t>::max()))
				return false;
			const double t = std::floor(v.numberValue);
			if (std::abs(t - v.numberValue) > 0.000001) return false;
			out = static_cast<uint32_t>(t);
			return true;
		}

		bool ToFloat(const JsonValue& v, float& out)
		{
			if (v.type != JsonType::Number || !std::isfinite(v.numberValue)) return false;
			out = static_cast<float>(v.numberValue);
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// ResourceNodeRuntime
	// -------------------------------------------------------------------------

	ResourceNodeRuntime::ResourceNodeRuntime(const engine::core::Config& config)
		: m_config(config)
		, m_rng(static_cast<uint32_t>(std::time(nullptr)))
	{
		LOG_INFO(Net, "[ResourceNodeRuntime] Constructed");
	}

	ResourceNodeRuntime::~ResourceNodeRuntime()
	{
		Shutdown();
	}

	bool ResourceNodeRuntime::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[ResourceNodeRuntime] Init ignored: already initialized");
			return true;
		}

		if (!LoadDefinitions())
		{
			LOG_ERROR(Net, "[ResourceNodeRuntime] Init FAILED: definition load failed");
			return false;
		}

		LoadAllZoneNodes();

		m_initialized = true;
		LOG_INFO(Net, "[ResourceNodeRuntime] Init OK (definitions={}, instances={})",
			m_definitions.size(), m_instances.size());
		return true;
	}

	void ResourceNodeRuntime::Shutdown()
	{
		if (!m_initialized && m_definitions.empty() && m_instances.empty())
			return;

		const size_t defCount  = m_definitions.size();
		const size_t instCount = m_instances.size();
		m_definitions.clear();
		m_instances.clear();
		m_initialized = false;
		LOG_INFO(Net, "[ResourceNodeRuntime] Destroyed (definitions={}, instances={})", defCount, instCount);
	}

	ResourceNodeInstance* ResourceNodeRuntime::FindInstance(uint32_t instanceId)
	{
		for (ResourceNodeInstance& n : m_instances)
			if (n.instanceId == instanceId) return &n;
		return nullptr;
	}

	const ResourceNodeInstance* ResourceNodeRuntime::FindInstance(uint32_t instanceId) const
	{
		for (const ResourceNodeInstance& n : m_instances)
			if (n.instanceId == instanceId) return &n;
		return nullptr;
	}

	const ResourceNodeDefinition* ResourceNodeRuntime::FindDefinition(std::string_view typeId) const
	{
		for (const ResourceNodeDefinition& d : m_definitions)
			if (d.typeId == typeId) return &d;
		return nullptr;
	}

	uint32_t ResourceNodeRuntime::FindHarvestingInstanceId(uint32_t clientId) const
	{
		for (const ResourceNodeInstance& n : m_instances)
			if (n.state == ResourceNodeState::Harvesting && n.harvesterClientId == clientId)
				return n.instanceId;
		return 0;
	}

	bool ResourceNodeRuntime::StartHarvest(
		uint32_t instanceId,
		uint32_t clientId,
		uint32_t currentTick,
		float clientX,
		float clientZ)
	{
		ResourceNodeInstance* node = FindInstance(instanceId);
		if (node == nullptr)
		{
			LOG_WARN(Net, "[ResourceNodeRuntime] StartHarvest rejected: instance not found (id={})", instanceId);
			return false;
		}
		if (node->state != ResourceNodeState::Available)
		{
			LOG_WARN(Net, "[ResourceNodeRuntime] StartHarvest rejected: node not available (id={}, state={})",
				instanceId, static_cast<int>(node->state));
			return false;
		}

		node->state = ResourceNodeState::Harvesting;
		node->harvesterClientId = clientId;
		node->harvestStartTick = currentTick;
		node->harvesterStartX = clientX;
		node->harvesterStartZ = clientZ;
		node->lastSentProgressPercent = 255;

		LOG_INFO(Net, "[ResourceNodeRuntime] Harvest started (instance_id={}, type={}, client_id={})",
			instanceId, node->typeId, clientId);
		return true;
	}

	bool ResourceNodeRuntime::CancelHarvest(uint32_t instanceId, std::string_view reason)
	{
		ResourceNodeInstance* node = FindInstance(instanceId);
		if (node == nullptr || node->state != ResourceNodeState::Harvesting)
			return false;

		const uint32_t prevClient = node->harvesterClientId;
		node->state = ResourceNodeState::Available;
		node->harvesterClientId = 0;
		node->harvestStartTick = 0;
		node->harvesterStartX = 0.0f;
		node->harvesterStartZ = 0.0f;
		node->lastSentProgressPercent = 255;

		LOG_INFO(Net, "[ResourceNodeRuntime] Harvest cancelled (instance_id={}, reason={}, prev_client_id={})",
			instanceId, reason, prevClient);
		return true;
	}

	void ResourceNodeRuntime::CancelHarvestsForClient(uint32_t clientId, std::string_view reason)
	{
		for (ResourceNodeInstance& node : m_instances)
		{
			if (node.state == ResourceNodeState::Harvesting && node.harvesterClientId == clientId)
			{
				node.state = ResourceNodeState::Available;
				node.harvesterClientId = 0;
				node.harvestStartTick = 0;
				node.harvesterStartX = 0.0f;
				node.harvesterStartZ = 0.0f;
				node.lastSentProgressPercent = 255;
				LOG_INFO(Net, "[ResourceNodeRuntime] Harvest cancelled for disconnected client (instance_id={}, reason={})",
					node.instanceId, reason);
			}
		}
	}

	void ResourceNodeRuntime::Tick(
		uint32_t currentTick,
		uint16_t tickHz,
		std::vector<ResourceHarvestResult>& outResults)
	{
		for (ResourceNodeInstance& node : m_instances)
		{
			if (node.state == ResourceNodeState::Depleted)
			{
				if (node.respawnAtTick != 0 && currentTick >= node.respawnAtTick)
				{
					node.state = ResourceNodeState::Available;
					node.respawnAtTick = 0;
					LOG_INFO(Net, "[ResourceNodeRuntime] Node respawned (instance_id={}, type={})",
						node.instanceId, node.typeId);
				}
			}
			else if (node.state == ResourceNodeState::Harvesting)
			{
				const ResourceNodeDefinition* def = FindDefinition(node.typeId);
				if (def == nullptr)
				{
					LOG_WARN(Net, "[ResourceNodeRuntime] Harvest cancelled: missing definition (instance_id={}, type={})",
						node.instanceId, node.typeId);
					(void)CancelHarvest(node.instanceId, "missing_definition");
					continue;
				}

				const uint32_t durationTicks = static_cast<uint32_t>(
					def->harvestTimeSec * static_cast<float>(tickHz));
				if (durationTicks == 0) continue;

				if (currentTick >= node.harvestStartTick + durationTicks)
				{
					ResourceHarvestResult result{};
					result.instanceId = node.instanceId;
					result.clientId = node.harvesterClientId;
					result.items = RollLoot(*def);

					const uint32_t respawnTicks = def->respawnTimeSec * static_cast<uint32_t>(tickHz);
					node.state = ResourceNodeState::Depleted;
					node.harvesterClientId = 0;
					node.harvestStartTick = 0;
					node.respawnAtTick = currentTick + respawnTicks;

					outResults.push_back(std::move(result));
					LOG_INFO(Net, "[ResourceNodeRuntime] Harvest completed (instance_id={}, respawn_tick={})",
						node.instanceId, node.respawnAtTick);
				}
			}
		}
	}

	uint8_t ResourceNodeRuntime::GetProgressPercent(
		uint32_t instanceId,
		uint32_t currentTick,
		uint16_t tickHz) const
	{
		const ResourceNodeInstance* node = FindInstance(instanceId);
		if (node == nullptr || node->state != ResourceNodeState::Harvesting)
			return 0;

		const ResourceNodeDefinition* def = FindDefinition(node->typeId);
		if (def == nullptr) return 0;

		const uint32_t durationTicks = static_cast<uint32_t>(
			def->harvestTimeSec * static_cast<float>(tickHz));
		if (durationTicks == 0) return 100;

		const uint32_t elapsed = (currentTick > node->harvestStartTick)
			? (currentTick - node->harvestStartTick) : 0u;
		const uint32_t pct = (elapsed * 100u) / durationTicks;
		return static_cast<uint8_t>(pct < 100u ? pct : 100u);
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	bool ResourceNodeRuntime::LoadDefinitions()
	{
		constexpr std::string_view kRelativePath = "gathering/nodes.json";

		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(
			m_config, kRelativePath);
		if (jsonText.empty())
		{
			LOG_ERROR(Net, "[ResourceNodeRuntime] Definition load FAILED: file missing or empty ({})", kRelativePath);
			return false;
		}

		JsonValue root;
		std::string parseError;
		JsonParser parser(jsonText);
		if (!parser.Parse(root, parseError))
		{
			LOG_ERROR(Net, "[ResourceNodeRuntime] Definition load FAILED: JSON parse error '{}' ({})",
				parseError, kRelativePath);
			return false;
		}

		const JsonValue* nodesArr = ObjGet(root, "nodes");
		if (nodesArr == nullptr || nodesArr->type != JsonType::Array || nodesArr->arrayValue.empty())
		{
			LOG_ERROR(Net, "[ResourceNodeRuntime] Definition load FAILED: {} must define a non-empty 'nodes' array",
				kRelativePath);
			return false;
		}

		m_definitions.clear();
		std::unordered_set<std::string> seenTypeIds;

		for (size_t i = 0; i < nodesArr->arrayValue.size(); ++i)
		{
			const JsonValue& entry = nodesArr->arrayValue[i];
			if (entry.type != JsonType::Object)
			{
				LOG_ERROR(Net, "[ResourceNodeRuntime] nodes[{}] must be an object", i);
				m_definitions.clear();
				return false;
			}

			ResourceNodeDefinition def{};

			const JsonValue* typeIdVal      = ObjGet(entry, "typeId");
			const JsonValue* archetypeIdVal = ObjGet(entry, "archetypeId");
			const JsonValue* harvestTimeVal = ObjGet(entry, "harvestTimeSec");
			const JsonValue* respawnVal     = ObjGet(entry, "respawnTimeSec");
			const JsonValue* minItemsVal    = ObjGet(entry, "minItems");
			const JsonValue* maxItemsVal    = ObjGet(entry, "maxItems");
			const JsonValue* lootTableVal   = ObjGet(entry, "lootTable");

			if (typeIdVal == nullptr || typeIdVal->type != JsonType::String || typeIdVal->stringValue.empty())
			{
				LOG_ERROR(Net, "[ResourceNodeRuntime] nodes[{}].typeId must be a non-empty string", i);
				m_definitions.clear(); return false;
			}
			def.typeId = typeIdVal->stringValue;
			if (!seenTypeIds.emplace(def.typeId).second)
			{
				LOG_ERROR(Net, "[ResourceNodeRuntime] Duplicate typeId '{}' in nodes[{}]", def.typeId, i);
				m_definitions.clear(); return false;
			}

			if (archetypeIdVal == nullptr || !ToUint(*archetypeIdVal, def.archetypeId) || def.archetypeId == 0)
			{
				LOG_ERROR(Net, "[ResourceNodeRuntime] nodes[{}].archetypeId must be a positive integer", i);
				m_definitions.clear(); return false;
			}

			if (harvestTimeVal != nullptr)
				(void)ToFloat(*harvestTimeVal, def.harvestTimeSec);
			if (def.harvestTimeSec <= 0.0f) def.harvestTimeSec = 3.0f;

			if (respawnVal != nullptr)
				(void)ToUint(*respawnVal, def.respawnTimeSec);
			if (def.respawnTimeSec == 0) def.respawnTimeSec = 600;

			if (minItemsVal != nullptr) (void)ToUint(*minItemsVal, def.minItems);
			if (maxItemsVal != nullptr) (void)ToUint(*maxItemsVal, def.maxItems);
			if (def.minItems == 0) def.minItems = 1;
			if (def.maxItems < def.minItems) def.maxItems = def.minItems;

			if (lootTableVal != nullptr && lootTableVal->type == JsonType::Array)
			{
				for (size_t j = 0; j < lootTableVal->arrayValue.size(); ++j)
				{
					const JsonValue& lEntry = lootTableVal->arrayValue[j];
					if (lEntry.type != JsonType::Object) continue;

					ResourceNodeLootEntry le{};
					const JsonValue* itemIdV  = ObjGet(lEntry, "itemId");
					const JsonValue* minQtyV  = ObjGet(lEntry, "minQty");
					const JsonValue* maxQtyV  = ObjGet(lEntry, "maxQty");
					const JsonValue* weightV  = ObjGet(lEntry, "weight");

					if (itemIdV == nullptr || !ToUint(*itemIdV, le.itemId) || le.itemId == 0) continue;
					if (minQtyV != nullptr) (void)ToUint(*minQtyV, le.minQuantity);
					if (maxQtyV != nullptr) (void)ToUint(*maxQtyV, le.maxQuantity);
					if (weightV != nullptr) (void)ToUint(*weightV, le.weight);
					if (le.minQuantity == 0) le.minQuantity = 1;
					if (le.maxQuantity < le.minQuantity) le.maxQuantity = le.minQuantity;
					if (le.weight == 0) le.weight = 1;

					def.lootTable.push_back(le);
				}
			}

			LOG_INFO(Net, "[ResourceNodeRuntime] Loaded definition (typeId={}, archetypeId={}, harvestTimeSec={:.1f}, "
				"respawnTimeSec={}, lootEntries={})",
				def.typeId, def.archetypeId, def.harvestTimeSec,
				def.respawnTimeSec, def.lootTable.size());
			m_definitions.push_back(std::move(def));
		}

		LOG_INFO(Net, "[ResourceNodeRuntime] Definitions load OK (count={})", m_definitions.size());
		return true;
	}

	void ResourceNodeRuntime::LoadAllZoneNodes()
	{
		const std::filesystem::path zonesDir = engine::platform::FileSystem::ResolveContentPath(
			m_config, "zones");
		const std::vector<std::filesystem::path> entries =
			engine::platform::FileSystem::ListDirectory(zonesDir);

		std::unordered_set<uint32_t> seenInstanceIds;
		size_t totalLoaded = 0;

		for (const std::filesystem::path& zoneEntry : entries)
		{
			if (!std::filesystem::is_directory(zoneEntry)) continue;

			const std::string relativePath =
				"zones/" + zoneEntry.filename().string() + "/gathering_nodes.json";
			const std::filesystem::path fullPath =
				engine::platform::FileSystem::ResolveContentPath(m_config, relativePath);

			if (!engine::platform::FileSystem::Exists(fullPath))
			{
				LOG_DEBUG(Net, "[ResourceNodeRuntime] No gathering nodes for zone (path={})", relativePath);
				continue;
			}

			const std::string jsonText =
				engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
			if (jsonText.empty())
			{
				LOG_WARN(Net, "[ResourceNodeRuntime] Empty gathering_nodes.json skipped ({})", relativePath);
				continue;
			}

			JsonValue root;
			std::string parseError;
			JsonParser parser(jsonText);
			if (!parser.Parse(root, parseError))
			{
				LOG_WARN(Net, "[ResourceNodeRuntime] JSON parse error '{}' in {} — skipped",
					parseError, relativePath);
				continue;
			}

			const JsonValue* zoneIdVal = ObjGet(root, "zoneId");
			const JsonValue* nodesArr  = ObjGet(root, "nodes");
			uint32_t zoneId = 0;
			if (zoneIdVal == nullptr || !ToUint(*zoneIdVal, zoneId))
			{
				LOG_WARN(Net, "[ResourceNodeRuntime] {} missing valid zoneId — skipped", relativePath);
				continue;
			}
			if (nodesArr == nullptr || nodesArr->type != JsonType::Array)
			{
				LOG_WARN(Net, "[ResourceNodeRuntime] {} missing 'nodes' array — skipped", relativePath);
				continue;
			}

			size_t zoneLoaded = 0;
			for (size_t i = 0; i < nodesArr->arrayValue.size(); ++i)
			{
				const JsonValue& entry = nodesArr->arrayValue[i];
				if (entry.type != JsonType::Object) continue;

				const JsonValue* instanceIdVal = ObjGet(entry, "instanceId");
				const JsonValue* typeIdVal     = ObjGet(entry, "typeId");
				const JsonValue* posVal        = ObjGet(entry, "position");

				uint32_t instanceId = 0;
				if (instanceIdVal == nullptr || !ToUint(*instanceIdVal, instanceId) || instanceId == 0)
				{
					LOG_WARN(Net, "[ResourceNodeRuntime] {} nodes[{}] missing valid instanceId — skipped",
						relativePath, i);
					continue;
				}
				if (!seenInstanceIds.emplace(instanceId).second)
				{
					LOG_WARN(Net, "[ResourceNodeRuntime] Duplicate instanceId {} in {} — skipped",
						instanceId, relativePath);
					continue;
				}
				if (typeIdVal == nullptr || typeIdVal->type != JsonType::String || typeIdVal->stringValue.empty())
				{
					LOG_WARN(Net, "[ResourceNodeRuntime] {} nodes[{}] missing typeId — skipped",
						relativePath, i);
					continue;
				}
				if (FindDefinition(typeIdVal->stringValue) == nullptr)
				{
					LOG_WARN(Net, "[ResourceNodeRuntime] {} nodes[{}] unknown typeId '{}' — skipped",
						relativePath, i, typeIdVal->stringValue);
					continue;
				}

				ResourceNodeInstance node{};
				node.instanceId = instanceId;
				node.typeId     = typeIdVal->stringValue;
				node.zoneId     = zoneId;

				if (posVal != nullptr && posVal->type == JsonType::Array && posVal->arrayValue.size() >= 3)
				{
					(void)ToFloat(posVal->arrayValue[0], node.positionMetersX);
					(void)ToFloat(posVal->arrayValue[1], node.positionMetersY);
					(void)ToFloat(posVal->arrayValue[2], node.positionMetersZ);
				}

				m_instances.push_back(std::move(node));
				++zoneLoaded;
				++totalLoaded;
			}

			LOG_INFO(Net, "[ResourceNodeRuntime] Zone nodes loaded (path={}, zone_id={}, nodes={})",
				relativePath, zoneId, zoneLoaded);
		}

		if (totalLoaded == 0)
		{
			LOG_WARN(Net, "[ResourceNodeRuntime] No gathering node instances found in any zone");
		}
		else
		{
			LOG_INFO(Net, "[ResourceNodeRuntime] All zone nodes loaded (total={})", totalLoaded);
		}
	}

	std::vector<ItemStack> ResourceNodeRuntime::RollLoot(const ResourceNodeDefinition& def)
	{
		if (def.lootTable.empty() || def.minItems == 0)
			return {};

		uint32_t totalWeight = 0;
		for (const ResourceNodeLootEntry& e : def.lootTable)
			totalWeight += e.weight;
		if (totalWeight == 0)
			return {};

		std::uniform_int_distribution<uint32_t> countDist(def.minItems, def.maxItems);
		const uint32_t dropCount = countDist(m_rng);

		std::vector<ItemStack> result;
		for (uint32_t drop = 0; drop < dropCount; ++drop)
		{
			std::uniform_int_distribution<uint32_t> weightDist(0, totalWeight - 1);
			uint32_t roll = weightDist(m_rng);

			for (const ResourceNodeLootEntry& e : def.lootTable)
			{
				if (roll < e.weight)
				{
					std::uniform_int_distribution<uint32_t> qtyDist(e.minQuantity, e.maxQuantity);
					const uint32_t qty = qtyDist(m_rng);

					bool merged = false;
					for (ItemStack& s : result)
					{
						if (s.itemId == e.itemId) { s.quantity += qty; merged = true; break; }
					}
					if (!merged)
						result.push_back({ e.itemId, qty });
					break;
				}
				roll -= e.weight;
			}
		}
		return result;
	}
}
