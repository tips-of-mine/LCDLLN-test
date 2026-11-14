#include "src/client/render/DynamicLightSystem.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace engine::render
{
	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	namespace
	{
		/// True when an indexed key exists in the config (same helper used by
		/// ParticleSystem::LoadDefinitions).
		bool HasIndexedKey(const engine::core::Config& cfg,
		                   const std::string& prefix,
		                   size_t             index)
		{
			return cfg.Has(prefix + "[" + std::to_string(index) + "].id");
		}

		/// Parse a DynamicLightType string; defaults to Streetlamp on unknown values.
		DynamicLightType ParseLightType(const std::string& s)
		{
			if (s == "torch")   return DynamicLightType::Torch;
			if (s == "window")  return DynamicLightType::Window;
			return DynamicLightType::Streetlamp;
		}
	}

	// -------------------------------------------------------------------------
	// DynamicLightSystem::IsNight
	// -------------------------------------------------------------------------

	/*static*/ bool DynamicLightSystem::IsNight(float timeOfDay)
	{
		// Night = [18, 24) ∪ [0, 6)  (spec: "activate si time 18:00-6:00")
		return timeOfDay >= kNightStartHour || timeOfDay < kNightEndHour;
	}

	// -------------------------------------------------------------------------
	// DynamicLightSystem::Init
	// -------------------------------------------------------------------------

	bool DynamicLightSystem::Init(const engine::core::Config& config)
	{
		Shutdown(); // Re-entrant safety.

		const std::string relPath =
		    config.GetString("world.dynamic_lights_path", "lights/dynamic_lights.json");

		// LoadDefinitions logs its own diagnostics.
		const bool loaded = LoadDefinitions(config, relPath);

		m_activeLights.reserve(m_lights.size());
		m_windowEmissive = 0.0f;
		m_initialized    = true;

		if (loaded)
		{
			LOG_INFO(Render, "[DynamicLightSystem] Init OK (lights={}, path='{}')",
			         m_lights.size(), relPath);
		}
		else
		{
			// File missing or empty – the system is still valid (no lights).
			LOG_WARN(Render,
			         "[DynamicLightSystem] Init with 0 lights (file absent or empty: '{}')",
			         relPath);
		}

		return true; // Non-fatal: engine runs without dynamic lights.
	}

	// -------------------------------------------------------------------------
	// DynamicLightSystem::Shutdown
	// -------------------------------------------------------------------------

	void DynamicLightSystem::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_lights.clear();
		m_activeLights.clear();
		m_windowEmissive = 0.0f;
		m_initialized    = false;

		LOG_INFO(Render, "[DynamicLightSystem] Shutdown complete");
	}

	// -------------------------------------------------------------------------
	// DynamicLightSystem::Tick
	// -------------------------------------------------------------------------

	void DynamicLightSystem::Tick(float timeOfDay, float deltaSeconds)
	{
		if (!m_initialized || m_lights.empty())
		{
			return;
		}

		const bool night           = IsNight(timeOfDay);
		const float fadeStep       = (kFadeDurationSeconds > 0.0f)
		                           ? deltaSeconds / kFadeDurationSeconds
		                           : 1.0f;

		m_activeLights.clear();

		float windowFadeSum   = 0.0f;
		uint32_t windowCount  = 0;

		for (LightRuntime& lr : m_lights)
		{
			// ---- Determine target state ----------------------------------------
			if (lr.def.autoNightLight)
			{
				lr.targetOn = night;
			}
			else
			{
				// Non-auto lights are always on.
				lr.targetOn = true;
			}

			// ---- Advance fade timer (spec: 0→1 or 1→0 over 60 s) ---------------
			if (lr.targetOn)
			{
				// Fade in.
				lr.currentFade = std::min(lr.currentFade + fadeStep, 1.0f);
			}
			else
			{
				// Fade out.
				lr.currentFade = std::max(lr.currentFade - fadeStep, 0.0f);
			}

			// ---- Accumulate window emissive multiplier --------------------------
			if (lr.def.type == DynamicLightType::Window)
			{
				windowFadeSum += lr.currentFade;
				++windowCount;
			}

			// ---- Emit active light entry (only if visible intensity > 0) --------
			if (lr.currentFade > 0.0f)
			{
				ActivePointLight al{};
				al.position[0] = lr.def.position[0];
				al.position[1] = lr.def.position[1];
				al.position[2] = lr.def.position[2];
				al.radius       = lr.def.radius;
				al.color[0]     = lr.def.color[0];
				al.color[1]     = lr.def.color[1];
				al.color[2]     = lr.def.color[2];
				al.intensity    = lr.def.baseIntensity * lr.currentFade;
				m_activeLights.push_back(al);
			}
		}

		// ---- Window emissive multiplier ----------------------------------------
		m_windowEmissive = (windowCount > 0)
		                 ? windowFadeSum / static_cast<float>(windowCount)
		                 : 0.0f;
	}

	// -------------------------------------------------------------------------
	// v1.0.118 — lumières des cellules du monde (zone.json, world.point_lights)
	// -------------------------------------------------------------------------

	void DynamicLightSystem::AddZoneLights(uint32_t zoneId, const std::vector<PointLightDefinition>& defs)
	{
		if (!m_initialized || zoneId == 0 || defs.empty())
			return;
		for (const PointLightDefinition& d : defs)
		{
			LightRuntime lr{};
			lr.def = d;
			lr.zoneId = zoneId;
			// Une lumière allumée en permanence (torche d'un fond sans soleil) l'est déjà
			// quand le joueur arrive ; une lumière de nuit suit le fondu habituel.
			lr.currentFade = d.autoNightLight ? 0.0f : 1.0f;
			lr.targetOn = !d.autoNightLight;
			m_lights.push_back(std::move(lr));
		}
		m_activeLights.reserve(m_lights.size());
		LOG_INFO(Render, "[DynamicLightSystem] Cellule {} : {} lumiere(s) ajoutee(s) (total {})",
		         zoneId, defs.size(), m_lights.size());
	}

	void DynamicLightSystem::ClearZoneLights()
	{
		const size_t before = m_lights.size();
		m_lights.erase(std::remove_if(m_lights.begin(), m_lights.end(),
		                              [](const LightRuntime& lr) { return lr.zoneId != 0; }),
		               m_lights.end());
		if (m_lights.size() != before)
			LOG_INFO(Render, "[DynamicLightSystem] {} lumiere(s) de cellule retiree(s)", before - m_lights.size());
	}

	void DynamicLightSystem::TranslateZoneLights(float dx, float dz)
	{
		for (LightRuntime& lr : m_lights)
		{
			if (lr.zoneId == 0)
				continue;
			lr.def.position[0] += dx;
			lr.def.position[2] += dz;
		}
	}

	size_t DynamicLightSystem::GetZoneLightCount() const
	{
		return static_cast<size_t>(std::count_if(m_lights.begin(), m_lights.end(),
		                                         [](const LightRuntime& lr) { return lr.zoneId != 0; }));
	}

	/*static*/ std::vector<ActivePointLight> DynamicLightSystem::SelectNearest(
		const std::vector<ActivePointLight>& lights, float x, float y, float z, size_t maxCount)
	{
		if (lights.size() <= maxCount)
			return lights;
		std::vector<std::pair<float, size_t>> order;
		order.reserve(lights.size());
		for (size_t i = 0; i < lights.size(); ++i)
		{
			const ActivePointLight& l = lights[i];
			const float dx = l.position[0] - x, dy = l.position[1] - y, dz = l.position[2] - z;
			order.emplace_back(std::sqrt(dx * dx + dy * dy + dz * dz) - l.radius, i);
		}
		std::stable_sort(order.begin(), order.end(),
		                 [](const auto& a, const auto& b) { return a.first < b.first; });
		order.resize(maxCount);
		std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
		std::vector<ActivePointLight> out;
		out.reserve(maxCount);
		for (const auto& o : order)
			out.push_back(lights[o.second]);
		return out;
	}

	// -------------------------------------------------------------------------
	// DynamicLightSystem::LoadDefinitions
	// -------------------------------------------------------------------------

	bool DynamicLightSystem::LoadDefinitions(const engine::core::Config& config,
	                                         const std::string&          relPath)
	{
		m_lights.clear();

		// Resolve the full path via paths.content (same pattern as ParticleSystem).
		const std::string contentRoot = config.GetString("paths.content", "game/data");
		const std::string fullPath    = contentRoot + "/" + relPath;

		engine::core::Config defCfg;
		if (!defCfg.LoadFromFile(fullPath))
		{
			// Non-fatal: game runs without dynamic lights.
			LOG_WARN(Render,
			         "[DynamicLightSystem] Definitions file not found or unreadable: '{}'",
			         fullPath);
			return false;
		}

		size_t index = 0;
		while (HasIndexedKey(defCfg, "lights", index))
		{
			const std::string pfx = "lights[" + std::to_string(index) + "]";

			PointLightDefinition def{};
			def.id            = defCfg.GetString(pfx + ".id", "");
			def.type          = ParseLightType(defCfg.GetString(pfx + ".type", "streetlamp"));
			def.position[0]   = static_cast<float>(defCfg.GetDouble(pfx + ".position[0]", 0.0));
			def.position[1]   = static_cast<float>(defCfg.GetDouble(pfx + ".position[1]", 0.0));
			def.position[2]   = static_cast<float>(defCfg.GetDouble(pfx + ".position[2]", 0.0));
			def.radius        = static_cast<float>(defCfg.GetDouble(pfx + ".radius",        8.0));
			def.color[0]      = static_cast<float>(defCfg.GetDouble(pfx + ".color[0]",      1.0));
			def.color[1]      = static_cast<float>(defCfg.GetDouble(pfx + ".color[1]",      0.85));
			def.color[2]      = static_cast<float>(defCfg.GetDouble(pfx + ".color[2]",      0.4));
			def.baseIntensity = static_cast<float>(defCfg.GetDouble(pfx + ".intensity",     2.0));
			def.autoNightLight = defCfg.GetBool(pfx + ".auto_night_light", true);

			if (def.id.empty())
			{
				LOG_WARN(Render,
				         "[DynamicLightSystem] Skipped light at index {} (empty id)",
				         static_cast<uint32_t>(index));
				++index;
				continue;
			}

			LightRuntime lr{};
			lr.def         = std::move(def);
			lr.currentFade = 0.0f;
			lr.targetOn    = false;
			m_lights.push_back(std::move(lr));

			LOG_INFO(Render, "[DynamicLightSystem] Loaded light '{}' (type={}, radius={:.1f})",
			         m_lights.back().def.id,
			         static_cast<uint8_t>(m_lights.back().def.type),
			         m_lights.back().def.radius);

			++index;
		}

		if (m_lights.empty())
		{
			LOG_WARN(Render,
			         "[DynamicLightSystem] No valid light definitions found in '{}'",
			         fullPath);
			return false;
		}

		LOG_INFO(Render,
		         "[DynamicLightSystem] Loaded {} light definition(s) from '{}'",
		         m_lights.size(), fullPath);
		return true;
	}

} // namespace engine::render
