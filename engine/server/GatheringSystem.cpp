#include "engine/server/GatheringSystem.h"

#include "engine/server/ServerApp.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace engine::server
{
	namespace
	{
		// -----------------------------------------------------------------------
		// Minimal inline JSON parser (same approach as SpawnerRuntime)
		// -----------------------------------------------------------------------

		enum class JType { Null, Bool, Number, String, Object, Array };

		struct JVal
		{
			JType type = JType::Null;
			bool   bVal  = false;
			double nVal  = 0.0;
			std::string sVal;
			std::unordered_map<std::string, JVal> oVal;
			std::vector<JVal> aVal;
		};

		class JP final
		{
		public:
			explicit JP(std::string_view src) : m_src(src) {}

			bool Parse(JVal& out, std::string& err)
			{
				Skip();
				if (!Val(out, err)) return false;
				Skip();
				if (m_pos != m_src.size()) { err = "trailing chars"; return false; }
				return true;
			}

		private:
			void Skip()
			{
				while (m_pos < m_src.size() && std::isspace(static_cast<unsigned char>(m_src[m_pos])))
					++m_pos;
			}

			bool Eat(char c)
			{
				if (m_pos < m_src.size() && m_src[m_pos] == c) { ++m_pos; return true; }
				return false;
			}

			bool Sw(std::string_view tok) const
			{
				return m_src.substr(m_pos, tok.size()) == tok;
			}

			bool Val(JVal& out, std::string& err)
			{
				if (m_pos >= m_src.size()) { err = "unexpected eof"; return false; }
				switch (m_src[m_pos])
				{
				case '{': return Obj(out, err);
				case '[': return Arr(out, err);
				case '"':
					out = {}; out.type = JType::String;
					return Str(out.sVal, err);
				default: break;
				}
				if (Sw("true"))  { m_pos += 4; out = {}; out.type = JType::Bool; out.bVal = true;  return true; }
				if (Sw("false")) { m_pos += 5; out = {}; out.type = JType::Bool; out.bVal = false; return true; }
				if (Sw("null"))  { m_pos += 4; out = {}; out.type = JType::Null; return true; }
				return Num(out, err);
			}

			bool Obj(JVal& out, std::string& err)
			{
				if (!Eat('{')) { err = "expected {"; return false; }
				out = {}; out.type = JType::Object;
				Skip();
				if (Eat('}')) return true;
				for (;;)
				{
					std::string key;
					if (!Str(key, err)) return false;
					Skip();
					if (!Eat(':')) { err = "expected :"; return false; }
					Skip();
					JVal child;
					if (!Val(child, err)) return false;
					out.oVal.emplace(std::move(key), std::move(child));
					Skip();
					if (Eat('}')) return true;
					if (!Eat(',')) { err = "expected ,"; return false; }
					Skip();
				}
			}

			bool Arr(JVal& out, std::string& err)
			{
				if (!Eat('[')) { err = "expected ["; return false; }
				out = {}; out.type = JType::Array;
				Skip();
				if (Eat(']')) return true;
				for (;;)
				{
					JVal child;
					if (!Val(child, err)) return false;
					out.aVal.push_back(std::move(child));
					Skip();
					if (Eat(']')) return true;
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
					switch (e)
					{
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/'); break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					default: err = "bad escape"; return false;
					}
				}
				err = "unterminated string"; return false;
			}

			bool Num(JVal& out, std::string& err)
			{
				const size_t start = m_pos;
				if (m_pos < m_src.size() && m_src[m_pos] == '-') ++m_pos;
				bool hasDigit = false;
				while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos])))
				{ hasDigit = true; ++m_pos; }
				if (m_pos < m_src.size() && m_src[m_pos] == '.')
				{
					++m_pos;
					while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos])))
					{ hasDigit = true; ++m_pos; }
				}
				if (!hasDigit) { err = "expected number"; return false; }
				const std::string tok(m_src.substr(start, m_pos - start));
				char* ep = nullptr;
				const double v = std::strtod(tok.c_str(), &ep);
				if (ep == nullptr || *ep != '\0') { err = "bad number"; return false; }
				out = {}; out.type = JType::Number; out.nVal = v;
				return true;
			}

			std::string_view m_src;
			size_t m_pos = 0;
		};

		const JVal* Mem(const JVal& obj, std::string_view key)
		{
			if (obj.type != JType::Object) return nullptr;
			const auto it = obj.oVal.find(std::string(key));
			return (it != obj.oVal.end()) ? &it->second : nullptr;
		}

		bool GetUint(const JVal& v, uint32_t& out)
		{
			if (v.type != JType::Number || !std::isfinite(v.nVal) || v.nVal < 0.0
				|| v.nVal > static_cast<double>(std::numeric_limits<uint32_t>::max()))
				return false;
			const double t = std::floor(v.nVal);
			if (std::abs(t - v.nVal) > 0.0001) return false;
			out = static_cast<uint32_t>(t);
			return true;
		}

		bool GetFloat(const JVal& v, float& out)
		{
			if (v.type != JType::Number || !std::isfinite(v.nVal)) return false;
			out = static_cast<float>(v.nVal);
			return true;
		}
	} // anonymous namespace

	// -------------------------------------------------------------------------
	// GatheringSystem — public API
	// -------------------------------------------------------------------------

	bool GatheringSystem::Init(
		const engine::core::Config& config,
		uint16_t                    tickHz,
		EntityId&                   inOutNextEntityId)
	{
		if (m_initialized)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] Init ignored: already initialized");
			return true;
		}

		m_tickHz = (tickHz > 0) ? tickHz : 20u;

		if (!LoadNodeTypes(config))
		{
			LOG_ERROR(Gameplay, "[GatheringSystem] Init FAILED: could not load node type definitions");
			return false;
		}

		if (!LoadZoneNodes(config, inOutNextEntityId))
		{
			LOG_WARN(Gameplay, "[GatheringSystem] No zone gathering nodes loaded — world will have no resource nodes");
		}

		m_initialized = true;
		LOG_INFO(Gameplay, "[GatheringSystem] Init OK (types={}, nodes={})",
		         m_nodeTypes.size(), m_nodes.size());
		return true;
	}

	void GatheringSystem::Shutdown()
	{
		m_sessions.clear();
		m_nodes.clear();
		m_nodeTypes.clear();
		m_initialized = false;
		LOG_INFO(Gameplay, "[GatheringSystem] Destroyed");
	}

	HarvestOpResult GatheringSystem::TryStartHarvest(
		const ConnectedClient& client,
		EntityId               nodeEntityId,
		uint32_t               currentTick,
		uint32_t&              outDurationTicks)
	{
		outDurationTicks = 0;

		/// One session per player — reject if already harvesting.
		if (FindSession(client.clientId) != nullptr)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] TryStartHarvest rejected: client {} already harvesting",
			         client.clientId);
			return HarvestOpResult::AlreadyHarvesting;
		}

		ResourceNodeRuntimeState* node = FindNode(nodeEntityId);
		if (node == nullptr)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] TryStartHarvest rejected: node {} not found",
			         nodeEntityId);
			return HarvestOpResult::NodeNotFound;
		}

		if (!node->available)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] TryStartHarvest rejected: node {} not available",
			         nodeEntityId);
			return HarvestOpResult::NodeNotAvailable;
		}

		const ResourceNodeTypeDefinition* typeDef = FindNodeType(node->definition.typeId);
		if (typeDef == nullptr)
		{
			LOG_WARN(Gameplay, "[GatheringSystem] TryStartHarvest rejected: unknown typeId {} on node {}",
			         node->definition.typeId, nodeEntityId);
			return HarvestOpResult::NodeNotFound;
		}

		/// Proximity check (XZ plane, prevent teleport-harvesting).
		const float dist = Distance2D(
			client.positionMetersX, client.positionMetersZ,
			node->definition.positionMetersX, node->definition.positionMetersZ);
		if (dist > typeDef->harvestRangeMeters)
		{
			LOG_WARN(Gameplay,
			         "[GatheringSystem] TryStartHarvest rejected: client {} out of range (dist={:.1f} > {:.1f})",
			         client.clientId, dist, typeDef->harvestRangeMeters);
			return HarvestOpResult::OutOfRange;
		}

		const uint32_t durationTicks =
			static_cast<uint32_t>(typeDef->harvestTimeSeconds * static_cast<float>(m_tickHz));

		HarvestSessionState session{};
		session.clientId       = client.clientId;
		session.nodeEntityId   = nodeEntityId;
		session.completionTick = currentTick + durationTicks;
		session.startPositionX = client.positionMetersX;
		session.startPositionZ = client.positionMetersZ;
		m_sessions.push_back(session);

		outDurationTicks = durationTicks;
		LOG_INFO(Gameplay, "[GatheringSystem] Harvest started: client={} node={} durationTicks={}",
		         client.clientId, nodeEntityId, durationTicks);
		return HarvestOpResult::Ok;
	}

	EntityId GatheringSystem::CancelHarvest(uint32_t clientId)
	{
		const auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
			[clientId](const HarvestSessionState& s) { return s.clientId == clientId; });
		if (it == m_sessions.end())
		{
			return 0;
		}
		const EntityId nodeId = it->nodeEntityId;
		m_sessions.erase(it);
		LOG_INFO(Gameplay, "[GatheringSystem] Harvest cancelled: client={} node={}", clientId, nodeId);
		return nodeId;
	}

	void GatheringSystem::Tick(
		uint32_t                             currentTick,
		const std::vector<ConnectedClient>&  clients,
		std::vector<EntityId>&               outCompletedNodeEntityIds,
		std::vector<uint32_t>&               outCompletedClientIds,
		std::vector<std::vector<ItemStack>>& outCompletedItems,
		std::vector<EntityId>&               outMoveCancelledNodeEntityIds,
		std::vector<uint32_t>&               outMoveCancelledClientIds)
	{
		/// Refresh respawn timers.
		for (ResourceNodeRuntimeState& node : m_nodes)
		{
			if (!node.available && node.respawnTick > 0 && currentTick >= node.respawnTick)
			{
				node.available   = true;
				node.respawnTick = 0;
				LOG_INFO(Gameplay, "[GatheringSystem] Node respawned: entityId={}", node.entityId);
			}
		}

		/// Advance harvest sessions — detect movement cancellations and completions.
		std::vector<HarvestSessionState> remaining;
		remaining.reserve(m_sessions.size());

		for (HarvestSessionState& session : m_sessions)
		{
			/// Find the client to check movement.
			const ConnectedClient* client = nullptr;
			for (const ConnectedClient& c : clients)
			{
				if (c.clientId == session.clientId)
				{
					client = &c;
					break;
				}
			}

			if (client == nullptr)
			{
				/// Client disconnected — silently discard.
				LOG_DEBUG(Gameplay, "[GatheringSystem] Session discarded: client {} disconnected",
				          session.clientId);
				continue;
			}

			/// Movement cancellation check.
			const float moved = Distance2D(
				client->positionMetersX, client->positionMetersZ,
				session.startPositionX, session.startPositionZ);
			if (moved > kHarvestMoveCancelThresholdMeters)
			{
				outMoveCancelledNodeEntityIds.push_back(session.nodeEntityId);
				outMoveCancelledClientIds.push_back(session.clientId);
				LOG_INFO(Gameplay,
				         "[GatheringSystem] Harvest interrupted (moved): client={} node={} dist={:.2f}",
				         session.clientId, session.nodeEntityId, moved);
				continue;
			}

			/// Completion check.
			if (currentTick >= session.completionTick)
			{
				ResourceNodeRuntimeState* node = FindNode(session.nodeEntityId);
				std::vector<ItemStack> loot;
				if (node != nullptr && node->available)
				{
					const ResourceNodeTypeDefinition* typeDef = FindNodeType(node->definition.typeId);
					if (typeDef != nullptr)
					{
						loot = RollLoot(*typeDef);
					}
					node->available   = false;
					node->respawnTick = currentTick
						+ (typeDef ? (typeDef->respawnSeconds * static_cast<uint32_t>(m_tickHz)) : 0u);
					LOG_INFO(Gameplay,
					         "[GatheringSystem] Harvest complete: client={} node={} loot_items={}",
					         session.clientId, session.nodeEntityId, loot.size());
				}
				outCompletedNodeEntityIds.push_back(session.nodeEntityId);
				outCompletedClientIds.push_back(session.clientId);
				outCompletedItems.push_back(std::move(loot));
				continue;
			}

			remaining.push_back(session);
		}

		m_sessions = std::move(remaining);
	}

	ResourceNodeRuntimeState* GatheringSystem::FindNode(EntityId entityId)
	{
		for (ResourceNodeRuntimeState& n : m_nodes)
		{
			if (n.entityId == entityId)
			{
				return &n;
			}
		}
		return nullptr;
	}

	const ResourceNodeRuntimeState* GatheringSystem::FindNode(EntityId entityId) const
	{
		for (const ResourceNodeRuntimeState& n : m_nodes)
		{
			if (n.entityId == entityId)
			{
				return &n;
			}
		}
		return nullptr;
	}

	HarvestSessionState* GatheringSystem::FindSession(uint32_t clientId)
	{
		for (HarvestSessionState& s : m_sessions)
		{
			if (s.clientId == clientId)
			{
				return &s;
			}
		}
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// Private — load definitions
	// -------------------------------------------------------------------------

	bool GatheringSystem::LoadNodeTypes(const engine::core::Config& config)
	{
		m_nodeTypes.clear();

		constexpr std::string_view kRelPath = "gathering/resource_nodes.json";
		const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(config, std::string(kRelPath));
		if (jsonText.empty())
		{
			LOG_ERROR(Gameplay, "[GatheringSystem] LoadNodeTypes FAILED: file missing or empty ({})", kRelPath);
			return false;
		}

		JVal root;
		std::string parseError;
		JP parser(jsonText);
		if (!parser.Parse(root, parseError))
		{
			LOG_ERROR(Gameplay, "[GatheringSystem] LoadNodeTypes FAILED: parse error '{}' ({})", parseError, kRelPath);
			return false;
		}

		const JVal* typesArr = Mem(root, "node_types");
		if (typesArr == nullptr || typesArr->type != JType::Array || typesArr->aVal.empty())
		{
			LOG_ERROR(Gameplay, "[GatheringSystem] LoadNodeTypes FAILED: root.node_types must be a non-empty array");
			return false;
		}

		for (size_t i = 0; i < typesArr->aVal.size(); ++i)
		{
			const JVal& entry = typesArr->aVal[i];
			if (entry.type != JType::Object)
			{
				LOG_ERROR(Gameplay, "[GatheringSystem] LoadNodeTypes FAILED: node_types[{}] must be an object", i);
				return false;
			}

			ResourceNodeTypeDefinition typeDef{};

			const JVal* idV         = Mem(entry, "id");
			const JVal* nodeTypeV   = Mem(entry, "node_type");
			const JVal* harvestSecV = Mem(entry, "harvest_time_sec");
			const JVal* respawnSecV = Mem(entry, "respawn_sec");
			const JVal* rangeV      = Mem(entry, "harvest_range_meters");
			const JVal* lootV       = Mem(entry, "loot_table");

			if (idV == nullptr || !GetUint(*idV, typeDef.typeId) || typeDef.typeId == 0)
			{
				LOG_ERROR(Gameplay, "[GatheringSystem] LoadNodeTypes FAILED: node_types[{}].id must be a positive integer", i);
				return false;
			}
			if (nodeTypeV == nullptr || nodeTypeV->type != JType::String || nodeTypeV->sVal.empty())
			{
				LOG_ERROR(Gameplay, "[GatheringSystem] LoadNodeTypes FAILED: node_types[{}].node_type must be a non-empty string", i);
				return false;
			}
			typeDef.nodeType = nodeTypeV->sVal;

			if (harvestSecV != nullptr)
			{
				(void)GetFloat(*harvestSecV, typeDef.harvestTimeSeconds);
			}
			if (respawnSecV != nullptr)
			{
				(void)GetUint(*respawnSecV, typeDef.respawnSeconds);
			}
			if (rangeV != nullptr)
			{
				(void)GetFloat(*rangeV, typeDef.harvestRangeMeters);
			}

			if (lootV != nullptr && lootV->type == JType::Array)
			{
				for (size_t j = 0; j < lootV->aVal.size(); ++j)
				{
					const JVal& lEntry = lootV->aVal[j];
					if (lEntry.type != JType::Object) continue;
					const JVal* itemIdV = Mem(lEntry, "item_id");
					const JVal* minQtyV = Mem(lEntry, "min_qty");
					const JVal* maxQtyV = Mem(lEntry, "max_qty");
					ResourceNodeLootEntry loot{};
					if (itemIdV != nullptr) (void)GetUint(*itemIdV, loot.itemId);
					if (minQtyV != nullptr) (void)GetUint(*minQtyV, loot.minQty);
					if (maxQtyV != nullptr) (void)GetUint(*maxQtyV, loot.maxQty);
					if (loot.itemId != 0 && loot.maxQty >= loot.minQty && loot.minQty > 0)
					{
						typeDef.lootTable.push_back(loot);
					}
				}
			}

			LOG_INFO(Gameplay,
			         "[GatheringSystem] Node type loaded (id={}, type={}, harvest_sec={:.1f}, respawn_sec={}, loot_entries={})",
			         typeDef.typeId, typeDef.nodeType, typeDef.harvestTimeSeconds,
			         typeDef.respawnSeconds, typeDef.lootTable.size());
			m_nodeTypes.push_back(std::move(typeDef));
		}

		LOG_INFO(Gameplay, "[GatheringSystem] LoadNodeTypes OK (types={})", m_nodeTypes.size());
		return !m_nodeTypes.empty();
	}

	bool GatheringSystem::LoadZoneNodes(const engine::core::Config& config, EntityId& inOutNextEntityId)
	{
		m_nodes.clear();

		const std::filesystem::path zonesDir =
			engine::platform::FileSystem::ResolveContentPath(config, "zones");
		const std::vector<std::filesystem::path> entries =
			engine::platform::FileSystem::ListDirectory(zonesDir);

		for (const std::filesystem::path& entry : entries)
		{
			if (!std::filesystem::is_directory(entry))
			{
				continue;
			}

			const std::string relPath = "zones/" + entry.filename().string() + "/gathering_nodes.json";
			const std::filesystem::path fullPath =
				engine::platform::FileSystem::ResolveContentPath(config, relPath);
			if (!engine::platform::FileSystem::Exists(fullPath))
			{
				LOG_DEBUG(Gameplay, "[GatheringSystem] No gathering_nodes.json for zone ({})", relPath);
				continue;
			}

			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(config, relPath);
			if (jsonText.empty())
			{
				LOG_WARN(Gameplay, "[GatheringSystem] Empty gathering_nodes.json ({})", relPath);
				continue;
			}

			JVal root;
			std::string parseError;
			JP parser(jsonText);
			if (!parser.Parse(root, parseError))
			{
				LOG_ERROR(Gameplay, "[GatheringSystem] LoadZoneNodes FAILED: parse error '{}' ({})",
				          parseError, relPath);
				continue;
			}

			const JVal* zoneIdV = Mem(root, "zoneId");
			const JVal* nodesV  = Mem(root, "nodes");
			uint32_t zoneId = 0;
			if (zoneIdV == nullptr || !GetUint(*zoneIdV, zoneId))
			{
				LOG_ERROR(Gameplay, "[GatheringSystem] LoadZoneNodes FAILED: {} must define root.zoneId", relPath);
				continue;
			}
			if (nodesV == nullptr || nodesV->type != JType::Array || nodesV->aVal.empty())
			{
				LOG_WARN(Gameplay, "[GatheringSystem] LoadZoneNodes: no nodes in {}", relPath);
				continue;
			}

			for (size_t i = 0; i < nodesV->aVal.size(); ++i)
			{
				const JVal& nEntry = nodesV->aVal[i];
				if (nEntry.type != JType::Object) continue;

				const JVal* nodeIdV = Mem(nEntry, "id");
				const JVal* typeIdV = Mem(nEntry, "type_id");
				const JVal* posV    = Mem(nEntry, "position");

				ResourceNodeDefinition def{};
				def.zoneId = zoneId;

				if (nodeIdV != nullptr && nodeIdV->type == JType::String)
					def.nodeId = nodeIdV->sVal;
				if (typeIdV != nullptr)
					(void)GetUint(*typeIdV, def.typeId);
				if (posV != nullptr && posV->type == JType::Array && posV->aVal.size() >= 3)
				{
					(void)GetFloat(posV->aVal[0], def.positionMetersX);
					(void)GetFloat(posV->aVal[1], def.positionMetersY);
					(void)GetFloat(posV->aVal[2], def.positionMetersZ);
				}

				if (def.typeId == 0 || def.nodeId.empty())
				{
					LOG_WARN(Gameplay, "[GatheringSystem] Node entry [{}] in {} skipped: missing id or type_id", i, relPath);
					continue;
				}

				ResourceNodeRuntimeState state{};
				state.definition = std::move(def);
				state.entityId   = inOutNextEntityId++;
				state.available  = true;
				m_nodes.push_back(std::move(state));
			}

			LOG_INFO(Gameplay, "[GatheringSystem] Zone gathering nodes loaded (path={}, zone_id={}, nodes={})",
			         relPath, zoneId, nodesV->aVal.size());
		}

		LOG_INFO(Gameplay, "[GatheringSystem] LoadZoneNodes OK (total_nodes={})", m_nodes.size());
		return true;
	}

	const ResourceNodeTypeDefinition* GatheringSystem::FindNodeType(uint32_t typeId) const
	{
		for (const ResourceNodeTypeDefinition& t : m_nodeTypes)
		{
			if (t.typeId == typeId)
			{
				return &t;
			}
		}
		return nullptr;
	}

	std::vector<ItemStack> GatheringSystem::RollLoot(const ResourceNodeTypeDefinition& typeDef) const
	{
		std::vector<ItemStack> result;
		if (typeDef.lootTable.empty())
		{
			return result;
		}

		/// Simple pseudo-random based on pointer address + system clock seed.
		/// Production would use a seeded RNG; here we use rand() as a stand-in
		/// consistent with the rest of the codebase.
		for (const ResourceNodeLootEntry& entry : typeDef.lootTable)
		{
			if (entry.itemId == 0) continue;
			const uint32_t range = entry.maxQty - entry.minQty;
			const uint32_t qty   = entry.minQty + (range > 0 ? (static_cast<uint32_t>(std::rand()) % (range + 1u)) : 0u);

			bool merged = false;
			for (ItemStack& stack : result)
			{
				if (stack.itemId == entry.itemId)
				{
					stack.quantity += qty;
					merged = true;
					break;
				}
			}
			if (!merged)
			{
				result.push_back({ entry.itemId, qty });
			}
		}
		return result;
	}

	float GatheringSystem::Distance2D(float ax, float az, float bx, float bz)
	{
		const float dx = ax - bx;
		const float dz = az - bz;
		return std::sqrt(dx * dx + dz * dz);
	}
}
