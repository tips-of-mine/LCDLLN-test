#include "engine/render/DynamicLightSystem.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::render
{
	// ── Helpers ───────────────────────────────────────────────────────────────

	bool DynamicLightSystem::IsNightHour(float timeHours)
	{
		// Night window: [18:00, 24:00) ∪ [0:00, 6:00)
		return (timeHours >= kNightStartHour) || (timeHours < kNightEndHour);
	}

	void DynamicLightSystem::FadeLight(DynamicPointLight& light,
	                                   bool shouldBeActive,
	                                   float deltaSeconds)
	{
		if (shouldBeActive)
		{
			// Fade in: advance timer toward kFadeDurationSec.
			light.fadeTimer += deltaSeconds;
			if (light.fadeTimer >= kFadeDurationSec)
			{
				light.fadeTimer       = kFadeDurationSec;
				light.active          = true;
			}
		}
		else
		{
			// Fade out: retreat timer toward 0.
			light.fadeTimer -= deltaSeconds;
			if (light.fadeTimer <= 0.0f)
			{
				light.fadeTimer = 0.0f;
				light.active    = false;
			}
		}

		// Smooth step [0, 1] over the fade window, then scale by peak intensity.
		const float t = light.fadeTimer / kFadeDurationSec;
		const float smooth = t * t * (3.0f - 2.0f * t); // smoothstep
		light.currentIntensity = smooth * light.intensity;
	}

	// ── DynamicLightSystem ────────────────────────────────────────────────────

	void DynamicLightSystem::Init(uint32_t maxLightsHint)
	{
		m_lights.reserve(maxLightsHint);
		m_snapshot.visibleLights.reserve(kDefaultMaxVisibleLights);
		m_initialized = true;
		LOG_INFO(Render, "[DynamicLightSystem] Init OK (capacity={})", maxLightsHint);
	}

	void DynamicLightSystem::Shutdown()
	{
		const auto count = m_lights.size();
		m_lights.clear();
		m_snapshot.visibleLights.clear();
		m_initialized = false;
		LOG_INFO(Render, "[DynamicLightSystem] Shutdown (had {} lights)", count);
	}

	uint32_t DynamicLightSystem::AddLight(const DynamicPointLight& light)
	{
		const auto idx = static_cast<uint32_t>(m_lights.size());
		m_lights.push_back(light);
		// Ensure the fade timer starts in the correct state.
		// If Always trigger, pre-activate immediately.
		if (light.trigger == LightTrigger::Always)
		{
			m_lights.back().fadeTimer        = kFadeDurationSec;
			m_lights.back().currentIntensity = light.intensity;
			m_lights.back().active           = true;
		}
		LOG_DEBUG(Render,
		    "[DynamicLightSystem] AddLight idx={} tag='{}' trigger={} intensity={:.2f}",
		    idx, light.tag.c_str(),
		    static_cast<int>(light.trigger), light.intensity);
		return idx;
	}

	void DynamicLightSystem::ClearLights()
	{
		const auto count = m_lights.size();
		m_lights.clear();
		m_snapshot.visibleLights.clear();
		LOG_INFO(Render, "[DynamicLightSystem] ClearLights (removed {})", count);
	}

	void DynamicLightSystem::Update(float timeHours, float deltaSeconds)
	{
		if (!m_initialized) return;

		const bool isNight = IsNightHour(timeHours);

		for (DynamicPointLight& light : m_lights)
		{
			bool shouldBeActive = false;
			switch (light.trigger)
			{
			case LightTrigger::Always:
				// Always lights maintain peak intensity; skip fade logic.
				light.currentIntensity = light.intensity;
				light.active           = true;
				light.fadeTimer        = kFadeDurationSec;
				continue;

			case LightTrigger::NightOnly:
				shouldBeActive = isNight;
				break;

			case LightTrigger::DayOnly:
				shouldBeActive = !isNight;
				break;
			}

			FadeLight(light, shouldBeActive, deltaSeconds);
		}

		// Window emissive multiplier: rises as sun sets, falls at sunrise.
		// Full night = 1.0, full day = 0.0, smooth transition around 18h and 6h.
		// Use a simple cos blend centred on midnight (0 / 24) and noon (12).
		const float angle = (timeHours / 24.0f) * 2.0f * 3.14159265f;
		const float cosVal = std::cos(angle); // +1 at noon, -1 at midnight
		// Remap: noon (cosVal=+1) -> emissive=0; midnight (cosVal=-1) -> emissive=1
		const float raw = (-cosVal + 1.0f) * 0.5f; // [0,1]
		// Smooth so transition is fast near 18h/6h, not gradual all day.
		m_snapshot.windowEmissiveMultiplier = raw * raw * (3.0f - 2.0f * raw);

		LOG_TRACE(Render, "[DynamicLightSystem] Update timeHours={:.2f} night={} emissive={:.3f}",
		    timeHours, isNight ? "true" : "false", m_snapshot.windowEmissiveMultiplier);
	}

	const DynamicLightSnapshot& DynamicLightSystem::BuildSnapshot(
	    float camX, float camY, float camZ, uint32_t maxVisible)
	{
		m_snapshot.visibleLights.clear();

		// Collect lights with non-zero intensity and within radius of the camera.
		// We use squared distance to avoid sqrt per light.
		struct Candidate
		{
			const DynamicPointLight* ptr;
			float distSq;
		};

		static std::vector<Candidate> s_candidates;
		s_candidates.clear();
		s_candidates.reserve(m_lights.size());

		for (const DynamicPointLight& light : m_lights)
		{
			if (light.currentIntensity <= 0.0f) continue;

			const float dx   = light.positionX - camX;
			const float dy   = light.positionY - camY;
			const float dz   = light.positionZ - camZ;
			const float dist2 = dx * dx + dy * dy + dz * dz;

			s_candidates.push_back({ &light, dist2 });
		}

		// Sort front-to-back so the first `maxVisible` are the most relevant.
		std::sort(s_candidates.begin(), s_candidates.end(),
		    [](const Candidate& a, const Candidate& b) {
			    return a.distSq < b.distSq;
		    });

		const uint32_t count = std::min(
		    static_cast<uint32_t>(s_candidates.size()), maxVisible);

		m_snapshot.visibleLights.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
			m_snapshot.visibleLights.push_back(s_candidates[i].ptr);

		LOG_TRACE(Render,
		    "[DynamicLightSystem] BuildSnapshot: {}/{} lights visible",
		    count, s_candidates.size());

		return m_snapshot;
	}

} // namespace engine::render
