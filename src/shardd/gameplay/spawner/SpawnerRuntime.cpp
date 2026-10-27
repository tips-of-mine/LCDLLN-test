#include "src/shardd/gameplay/spawner/SpawnerRuntime.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/world/WorldCells.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine::server
{
	namespace
	{
		enum class JsonType
		{
			Null,
			Bool,
			Number,
			String,
			Object,
			Array
		};

		struct JsonValue
		{
			JsonType type = JsonType::Null;
			bool boolValue = false;
			double numberValue = 0.0;
			std::string stringValue;
			std::unordered_map<std::string, JsonValue> objectValue;
			std::vector<JsonValue> arrayValue;
		};

		/// Minimal JSON parser kept local to the spawner runtime to avoid new dependencies.
		class JsonParser final
		{
		public:
			explicit JsonParser(std::string_view input)
				: m_input(input)
			{
			}

			bool Parse(JsonValue& outRoot, std::string& outError)
			{
				SkipWhitespace();
				if (!ParseValue(outRoot, outError))
				{
					return false;
				}

				SkipWhitespace();
				if (m_pos != m_input.size())
				{
					outError = "unexpected trailing characters";
					return false;
				}

				return true;
			}

		private:
			void SkipWhitespace()
			{
				while (m_pos < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_pos])) != 0)
				{
					++m_pos;
				}
			}

			bool Consume(char expected)
			{
				if (m_pos >= m_input.size() || m_input[m_pos] != expected)
				{
					return false;
				}

				++m_pos;
				return true;
			}

			bool StartsWith(std::string_view token) const
			{
				return m_input.substr(m_pos, token.size()) == token;
			}

			bool ParseValue(JsonValue& outValue, std::string& outError)
			{
				if (m_pos >= m_input.size())
				{
					outError = "unexpected end of input";
					return false;
				}

				switch (m_input[m_pos])
				{
				case '{':
					return ParseObject(outValue, outError);
				case '[':
					return ParseArray(outValue, outError);
				case '"':
					outValue = JsonValue{};
					outValue.type = JsonType::String;
					return ParseString(outValue.stringValue, outError);
				default:
					break;
				}

				if (StartsWith("true"))
				{
					m_pos += 4;
					outValue = JsonValue{};
					outValue.type = JsonType::Bool;
					outValue.boolValue = true;
					return true;
				}

				if (StartsWith("false"))
				{
					m_pos += 5;
					outValue = JsonValue{};
					outValue.type = JsonType::Bool;
					outValue.boolValue = false;
					return true;
				}

				if (StartsWith("null"))
				{
					m_pos += 4;
					outValue = JsonValue{};
					outValue.type = JsonType::Null;
					return true;
				}

				return ParseNumber(outValue, outError);
			}

			bool ParseObject(JsonValue& outValue, std::string& outError)
			{
				if (!Consume('{'))
				{
					outError = "expected '{'";
					return false;
				}

				outValue = JsonValue{};
				outValue.type = JsonType::Object;
				SkipWhitespace();
				if (Consume('}'))
				{
					return true;
				}

				while (true)
				{
					std::string key;
					if (!ParseString(key, outError))
					{
						return false;
					}

					SkipWhitespace();
					if (!Consume(':'))
					{
						outError = "expected ':' after object key";
						return false;
					}

					SkipWhitespace();
					JsonValue child;
					if (!ParseValue(child, outError))
					{
						return false;
					}

					outValue.objectValue.emplace(std::move(key), std::move(child));
					SkipWhitespace();
					if (Consume('}'))
					{
						return true;
					}

					if (!Consume(','))
					{
						outError = "expected ',' between object members";
						return false;
					}

					SkipWhitespace();
				}
			}

			bool ParseArray(JsonValue& outValue, std::string& outError)
			{
				if (!Consume('['))
				{
					outError = "expected '['";
					return false;
				}

				outValue = JsonValue{};
				outValue.type = JsonType::Array;
				SkipWhitespace();
				if (Consume(']'))
				{
					return true;
				}

				while (true)
				{
					JsonValue child;
					if (!ParseValue(child, outError))
					{
						return false;
					}

					outValue.arrayValue.emplace_back(std::move(child));
					SkipWhitespace();
					if (Consume(']'))
					{
						return true;
					}

					if (!Consume(','))
					{
						outError = "expected ',' between array entries";
						return false;
					}

					SkipWhitespace();
				}
			}

			bool ParseString(std::string& outValue, std::string& outError)
			{
				if (!Consume('"'))
				{
					outError = "expected string";
					return false;
				}

				outValue.clear();
				while (m_pos < m_input.size())
				{
					const char current = m_input[m_pos++];
					if (current == '"')
					{
						return true;
					}

					if (current != '\\')
					{
						outValue.push_back(current);
						continue;
					}

					if (m_pos >= m_input.size())
					{
						outError = "unterminated escape sequence";
						return false;
					}

					const char escaped = m_input[m_pos++];
					switch (escaped)
					{
					case '"': outValue.push_back('"'); break;
					case '\\': outValue.push_back('\\'); break;
					case '/': outValue.push_back('/'); break;
					case 'b': outValue.push_back('\b'); break;
					case 'f': outValue.push_back('\f'); break;
					case 'n': outValue.push_back('\n'); break;
					case 'r': outValue.push_back('\r'); break;
					case 't': outValue.push_back('\t'); break;
					default:
						outError = "unsupported escape sequence";
						return false;
					}
				}

				outError = "unterminated string";
				return false;
			}

			bool ParseNumber(JsonValue& outValue, std::string& outError)
			{
				const size_t start = m_pos;
				if (m_input[m_pos] == '-')
				{
					++m_pos;
				}

				bool hasDigit = false;
				while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])) != 0)
				{
					hasDigit = true;
					++m_pos;
				}

				if (m_pos < m_input.size() && m_input[m_pos] == '.')
				{
					++m_pos;
					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])) != 0)
					{
						hasDigit = true;
						++m_pos;
					}
				}

				if (m_pos < m_input.size() && (m_input[m_pos] == 'e' || m_input[m_pos] == 'E'))
				{
					++m_pos;
					if (m_pos < m_input.size() && (m_input[m_pos] == '+' || m_input[m_pos] == '-'))
					{
						++m_pos;
					}

					while (m_pos < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_pos])) != 0)
					{
						hasDigit = true;
						++m_pos;
					}
				}

				if (!hasDigit)
				{
					outError = "expected number";
					return false;
				}

				const std::string token(m_input.substr(start, m_pos - start));
				char* endPtr = nullptr;
				const double parsedValue = std::strtod(token.c_str(), &endPtr);
				if (endPtr == nullptr || *endPtr != '\0')
				{
					outError = "invalid number";
					return false;
				}

				outValue = JsonValue{};
				outValue.type = JsonType::Number;
				outValue.numberValue = parsedValue;
				return true;
			}

			std::string_view m_input;
			size_t m_pos = 0;
		};

		/// Return one object member or `nullptr` when the key is absent.
		const JsonValue* FindObjectMember(const JsonValue& object, std::string_view key)
		{
			if (object.type != JsonType::Object)
			{
				return nullptr;
			}

			const auto it = object.objectValue.find(std::string(key));
			if (it == object.objectValue.end())
			{
				return nullptr;
			}

			return &it->second;
		}

		/// Convert one JSON number into a validated unsigned 32-bit integer.
		bool TryGetUint(const JsonValue& value, uint32_t& outValue)
		{
			if (value.type != JsonType::Number
				|| !std::isfinite(value.numberValue)
				|| value.numberValue < 0.0
				|| value.numberValue > static_cast<double>(std::numeric_limits<uint32_t>::max()))
			{
				return false;
			}

			const double truncated = std::floor(value.numberValue);
			if (std::abs(truncated - value.numberValue) > 0.000001)
			{
				return false;
			}

			outValue = static_cast<uint32_t>(truncated);
			return true;
		}

		/// Convert one JSON number into a validated finite float.
		bool TryGetFloat(const JsonValue& value, float& outValue)
		{
			if (value.type != JsonType::Number || !std::isfinite(value.numberValue))
			{
				return false;
			}

			outValue = static_cast<float>(value.numberValue);
			return true;
		}

		/// Monde vivant — décode `routine` : { kind, schedule, path:[[x,z]…],
		/// pauseMinSec, pauseMaxSec, pauseChance, wanderRadiusMeters, pingPong,
		/// face:[x,z] }. \return false si kind/schedule inconnus ou chemin absent
		/// pour tour/patrol.
		bool ParseRoutine(const JsonValue& v, ambient::RoutineDefinition& out)
		{
			if (v.type != JsonType::Object) return false;
			const JsonValue* kind = FindObjectMember(v, "kind");
			if (kind == nullptr || kind->type != JsonType::String || !ambient::ParseRoutineKind(kind->stringValue, out.kind))
				return false;
			if (const JsonValue* sch = FindObjectMember(v, "schedule"); sch != nullptr)
			{
				if (sch->type != JsonType::String || !ambient::ParseRoutineSchedule(sch->stringValue, out.schedule)) return false;
			}
			auto num = [&](const char* key, float& dst) {
				if (const JsonValue* n = FindObjectMember(v, key); n != nullptr) return TryGetFloat(*n, dst);
				return true;
			};
			if (!num("pauseMinSec", out.pauseMinSec) || !num("pauseMaxSec", out.pauseMaxSec)
				|| !num("pauseChance", out.pauseChance) || !num("wanderRadiusMeters", out.wanderRadiusMeters))
				return false;
			if (const JsonValue* pp = FindObjectMember(v, "pingPong"); pp != nullptr && pp->type == JsonType::Bool)
				out.pingPong = pp->boolValue;
			if (const JsonValue* path = FindObjectMember(v, "path"); path != nullptr)
			{
				if (path->type != JsonType::Array) return false;
				for (const JsonValue& pt : path->arrayValue)
				{
					ambient::RoutinePoint p;
					if (pt.type != JsonType::Array || pt.arrayValue.size() != 2
						|| !TryGetFloat(pt.arrayValue[0], p.x) || !TryGetFloat(pt.arrayValue[1], p.z))
						return false;
					out.path.push_back(p);
				}
			}
			if (const JsonValue* face = FindObjectMember(v, "face"); face != nullptr)
			{
				if (face->type != JsonType::Array || face->arrayValue.size() != 2
					|| !TryGetFloat(face->arrayValue[0], out.facePoint.x) || !TryGetFloat(face->arrayValue[1], out.facePoint.z))
					return false;
				out.hasFacePoint = true;
			}
			if ((out.kind == ambient::RoutineKind::Tour || out.kind == ambient::RoutineKind::Patrol) && out.path.size() < 2)
				return false;
			out.pauseChance = std::clamp(out.pauseChance, 0.0f, 1.0f);
			return true;
		}

		/// ETAPE4.4 — décode `agenda` : { profession, jitterMinutes?, strollRadiusMeters?,
		/// places: { home|work|tavern|market|square|temple : [[x,z]…] } }.
		/// \return false si le métier est inconnu, un lieu inconnu, un point mal formé,
		/// ou si aucun domicile n'est donné (le PNJ doit pouvoir rentrer dormir).
		bool ParseAgenda(const JsonValue& v, ambient::AgendaDefinition& out)
		{
			if (v.type != JsonType::Object) return false;
			const JsonValue* profession = FindObjectMember(v, "profession");
			if (profession == nullptr || profession->type != JsonType::String
				|| ambient::FindProfession(profession->stringValue) == nullptr)
				return false;
			out.profession = profession->stringValue;
			if (const JsonValue* j = FindObjectMember(v, "jitterMinutes"); j != nullptr && !TryGetFloat(*j, out.jitterMinutes))
				return false;
			if (const JsonValue* r = FindObjectMember(v, "strollRadiusMeters"); r != nullptr && !TryGetFloat(*r, out.strollRadiusMeters))
				return false;
			out.jitterMinutes = std::clamp(out.jitterMinutes, 0.0f, 180.0f);
			out.strollRadiusMeters = std::clamp(out.strollRadiusMeters, 0.0f, 30.0f);
			const JsonValue* places = FindObjectMember(v, "places");
			if (places == nullptr || places->type != JsonType::Object)
				return false;
			for (const auto& [name, list] : places->objectValue)
			{
				ambient::AgendaPlace place{};
				if (!ambient::ParseAgendaPlace(name, place) || list.type != JsonType::Array)
					return false;
				for (const JsonValue& pt : list.arrayValue)
				{
					ambient::RoutinePoint p;
					if (pt.type != JsonType::Array || pt.arrayValue.size() != 2
						|| !TryGetFloat(pt.arrayValue[0], p.x) || !TryGetFloat(pt.arrayValue[1], p.z))
						return false;
					out.places[static_cast<size_t>(place)].push_back(p);
				}
			}
			if (out.places[static_cast<size_t>(ambient::AgendaPlace::Home)].empty())
				return false;
			out.enabled = true;
			return true;
		}
	}

	SpawnerRuntime::SpawnerRuntime(const engine::core::Config& config)
		: m_config(config)
	{
		LOG_INFO(Net, "[SpawnerRuntime] Constructed");
	}

	SpawnerRuntime::~SpawnerRuntime()
	{
		Shutdown();
	}

	bool SpawnerRuntime::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[SpawnerRuntime] Init ignored: already initialized");
			return true;
		}

		if (!LoadDefinitions())
		{
			LOG_ERROR(Net, "[SpawnerRuntime] Init FAILED: definition load failed");
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[SpawnerRuntime] Init OK (spawners={})", m_definitions.size());
		return true;
	}

	void SpawnerRuntime::Shutdown()
	{
		if (!m_initialized && m_definitions.empty())
		{
			return;
		}

		const size_t definitionCount = m_definitions.size();
		m_definitions.clear();
		m_initialized = false;
		LOG_INFO(Net, "[SpawnerRuntime] Destroyed (spawners={})", definitionCount);
	}

	bool SpawnerRuntime::LoadDefinitions()
	{
		m_definitions.clear();

		const std::filesystem::path zonesDirectory = engine::platform::FileSystem::ResolveContentPath(m_config, "zones");
		const std::vector<std::filesystem::path> zoneEntries = engine::platform::FileSystem::ListDirectory(zonesDirectory);
		if (zoneEntries.empty())
		{
			LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: no zone directories found ({})", zonesDirectory.string());
			return false;
		}

		std::unordered_set<std::string> spawnerIds;
		for (const std::filesystem::path& zoneEntry : zoneEntries)
		{
			if (!std::filesystem::is_directory(zoneEntry))
			{
				continue;
			}
			// Monde « Royaumes » : seuls les dossiers du monde actif sont chargés
			// (cellules cell_* en monde en cellules, zones historiques sinon).
			if (!engine::world::cells::ZoneDirBelongsToWorld(zoneEntry.filename().string(),
				m_config.GetString("world.active_zone", "")))
			{
				continue;
			}

			const std::string relativePath = "zones/" + zoneEntry.filename().string() + "/spawners.json";
			const std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(m_config, relativePath);
			if (!engine::platform::FileSystem::Exists(fullPath))
			{
				LOG_DEBUG(Net, "[SpawnerRuntime] Zone spawner file skipped: missing {}", relativePath);
				continue;
			}

			const std::string jsonText = engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath);
			if (jsonText.empty())
			{
				LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: empty spawner file ({})", relativePath);
				m_definitions.clear();
				return false;
			}

			JsonValue root;
			std::string parseError;
			JsonParser parser(jsonText);
			if (!parser.Parse(root, parseError))
			{
				LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: parse error '{}' ({})", parseError, relativePath);
				m_definitions.clear();
				return false;
			}

			const JsonValue* zoneIdValue = FindObjectMember(root, "zoneId");
			const JsonValue* spawnersValue = FindObjectMember(root, "spawners");
			uint32_t zoneId = 0;
			if (zoneIdValue == nullptr || !TryGetUint(*zoneIdValue, zoneId))
			{
				LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: {} must define a valid root.zoneId", relativePath);
				m_definitions.clear();
				return false;
			}
			// Monde « Royaumes » : une cellule générée peut n'avoir aucun spawner
			// (désert, mer) — liste vide acceptée pour les dossiers cell_* seulement.
			if (spawnersValue != nullptr && spawnersValue->type == JsonType::Array && spawnersValue->arrayValue.empty()
				&& engine::world::cells::ParseCellDirName(zoneEntry.filename().string()).has_value())
			{
				LOG_INFO(Net, "[SpawnerRuntime] Zone spawner file empty (cellule sans spawner) : {}", relativePath);
				continue;
			}
			if (spawnersValue == nullptr || spawnersValue->type != JsonType::Array || spawnersValue->arrayValue.empty())
			{
				LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: {} must define a non-empty root.spawners array", relativePath);
				m_definitions.clear();
				return false;
			}

			for (size_t spawnerIndex = 0; spawnerIndex < spawnersValue->arrayValue.size(); ++spawnerIndex)
			{
				const JsonValue& spawnerValue = spawnersValue->arrayValue[spawnerIndex];
				if (spawnerValue.type != JsonType::Object)
				{
					LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: {} spawners[{}] must be an object",
						relativePath,
						spawnerIndex);
					m_definitions.clear();
					return false;
				}

				const JsonValue* idValue = FindObjectMember(spawnerValue, "id");
				const JsonValue* archetypeIdValue = FindObjectMember(spawnerValue, "archetypeId");
				const JsonValue* positionValue = FindObjectMember(spawnerValue, "position");
				const JsonValue* countValue = FindObjectMember(spawnerValue, "count");
				const JsonValue* respawnValue = FindObjectMember(spawnerValue, "respawnSec");
				const JsonValue* leashValue = FindObjectMember(spawnerValue, "leashDistanceMeters");
				if (idValue == nullptr || idValue->type != JsonType::String || idValue->stringValue.empty())
				{
					LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: {} spawners[{}].id must be a non-empty string",
						relativePath,
						spawnerIndex);
					m_definitions.clear();
					return false;
				}

				SpawnerDefinition definition{};
				definition.spawnerId = idValue->stringValue;
				definition.zoneId = zoneId;
				if (!spawnerIds.emplace(definition.spawnerId).second)
				{
					LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: duplicate spawner id '{}'", definition.spawnerId);
					m_definitions.clear();
					return false;
				}

				if (archetypeIdValue == nullptr
					|| !TryGetUint(*archetypeIdValue, definition.archetypeId)
					|| definition.archetypeId == 0)
				{
					LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: spawner '{}' archetypeId must be positive",
						definition.spawnerId);
					m_definitions.clear();
					return false;
				}

				if (countValue == nullptr
					|| !TryGetUint(*countValue, definition.count)
					|| definition.count == 0)
				{
					LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: spawner '{}' count must be positive",
						definition.spawnerId);
					m_definitions.clear();
					return false;
				}

				if (respawnValue == nullptr
					|| !TryGetUint(*respawnValue, definition.respawnSeconds)
					|| definition.respawnSeconds == 0)
				{
					LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: spawner '{}' respawnSec must be positive",
						definition.spawnerId);
					m_definitions.clear();
					return false;
				}

				if (positionValue == nullptr
					|| positionValue->type != JsonType::Array
					|| positionValue->arrayValue.size() != 3
					|| !TryGetFloat(positionValue->arrayValue[0], definition.positionMetersX)
					|| !TryGetFloat(positionValue->arrayValue[1], definition.positionMetersY)
					|| !TryGetFloat(positionValue->arrayValue[2], definition.positionMetersZ))
				{
					LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: spawner '{}' position must be a [x, y, z] array",
						definition.spawnerId);
					m_definitions.clear();
					return false;
				}

				if (leashValue != nullptr)
				{
					if (!TryGetFloat(*leashValue, definition.leashDistanceMeters) || definition.leashDistanceMeters <= 0.0f)
					{
						LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: spawner '{}' leashDistanceMeters must be positive",
							definition.spawnerId);
						m_definitions.clear();
						return false;
					}
				}

				// Monde vivant — routine d'ambiance (optionnelle). Politique stricte :
				// une routine présente mais mal formée fait échouer le chargement.
				if (const JsonValue* routineValue = FindObjectMember(spawnerValue, "routine"); routineValue != nullptr)
				{
					if (!ParseRoutine(*routineValue, definition.routine))
					{
						LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: spawner '{}' routine invalide (kind/path/schedule)",
							definition.spawnerId);
						m_definitions.clear();
						return false;
					}
				}
				// ETAPE4.4 — agenda de métier (optionnel), même politique stricte.
				if (const JsonValue* agendaValue = FindObjectMember(spawnerValue, "agenda"); agendaValue != nullptr)
				{
					if (!ParseAgenda(*agendaValue, definition.agenda))
					{
						LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: spawner '{}' agenda invalide (profession/places/home)",
							definition.spawnerId);
						m_definitions.clear();
						return false;
					}
				}

				LOG_DEBUG(Net,
					"[SpawnerRuntime] Loaded spawner definition (id={}, zone_id={}, archetype_id={}, count={}, respawn_sec={})",
					definition.spawnerId,
					definition.zoneId,
					definition.archetypeId,
					definition.count,
					definition.respawnSeconds);
				m_definitions.push_back(std::move(definition));
			}

			LOG_INFO(Net, "[SpawnerRuntime] Zone spawner file loaded (path={}, zone_id={}, spawners={})",
				relativePath,
				zoneId,
				spawnersValue->arrayValue.size());
		}

		if (m_definitions.empty())
		{
			LOG_ERROR(Net, "[SpawnerRuntime] Definition load FAILED: no spawners loaded");
			return false;
		}

		LOG_INFO(Net, "[SpawnerRuntime] Definition load OK (spawners={})", m_definitions.size());
		return true;
	}
}
