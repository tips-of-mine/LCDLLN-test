#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render
{
	/// Light trigger behaviour for auto-night-light entities (M38.3).
	enum class LightTrigger : uint8_t
	{
		Always    = 0, ///< Always active regardless of time.
		NightOnly = 1, ///< Active during night hours (18:00–06:00); fade 60 s.
		DayOnly   = 2  ///< Active during day hours (06:00–18:00); fade 60 s.
	};

	/// One dynamic point light instance (M38.3).
	struct DynamicPointLight
	{
		/// World-space position (XYZ).
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;

		/// Maximum influence radius in metres.
		float radius = 5.0f;

		/// Base colour (RGB, linear).
		float colorR = 1.0f;
		float colorG = 0.85f;
		float colorB = 0.60f;

		/// Configured peak intensity.
		float intensity = 1.0f;

		/// Current effective intensity after fade [0, intensity].
		float currentIntensity = 0.0f;

		/// Trigger behaviour (Always / NightOnly / DayOnly).
		LightTrigger trigger = LightTrigger::NightOnly;

		/// Optional tag used to identify the light source type
		/// (e.g. "streetlamp", "torch", "window").
		std::string tag;

		/// True while the light is in the fully-active state (not fading in/out).
		bool active = false;

		/// Fade timer in seconds [0, kFadeDurationSec].
		float fadeTimer = 0.0f;
	};

	/// Per-frame snapshot of active point lights consumed by the renderer (M38.3).
	struct DynamicLightSnapshot
	{
		/// Point lights with non-zero effective intensity (pre-sorted near-to-far
		/// if BuildSnapshot was called with a camera position).
		std::vector<const DynamicPointLight*> visibleLights;

		/// Window emissive multiplier [0, 1]: 0 at noon, 1 at night.
		/// Drives the "boost albedo at night" material parameter (spec step 6).
		float windowEmissiveMultiplier = 0.0f;
	};

	/// CPU-side dynamic lighting manager: manages NightOnly/DayOnly point lights,
	/// fades them in/out based on time-of-day, and produces a per-frame snapshot
	/// of active lights for the deferred renderer (M38.3).
	///
	/// Usage:
	///   DynamicLightSystem lights;
	///   lights.Init(maxLightsHint);
	///   lights.AddLight(def);          // called per-zone on load
	///   // Each frame:
	///   lights.Update(timeHours, dt);
	///   const auto& snap = lights.BuildSnapshot(camX, camY, camZ, maxVisible);
	class DynamicLightSystem final
	{
	public:
		/// Maximum number of simultaneously visible point lights sent to the GPU.
		static constexpr uint32_t kDefaultMaxVisibleLights = 128u;

		/// Fade duration in seconds for auto-night-light transitions (spec: 1 min).
		static constexpr float kFadeDurationSec = 60.0f;

		/// Night hours threshold: lights with NightOnly trigger activate at this hour.
		static constexpr float kNightStartHour = 18.0f;
		/// Night hours threshold: lights with NightOnly trigger deactivate at this hour.
		static constexpr float kNightEndHour   =  6.0f;

		DynamicLightSystem()                               = default;
		DynamicLightSystem(const DynamicLightSystem&)      = delete;
		DynamicLightSystem& operator=(const DynamicLightSystem&) = delete;

		/// Initialise the system and reserve storage.
		/// \param maxLightsHint  Expected maximum number of registered lights.
		void Init(uint32_t maxLightsHint = 512u);

		/// Shutdown and release all light records.
		void Shutdown();

		/// Register a point light.  Returns the index of the new light.
		uint32_t AddLight(const DynamicPointLight& light);

		/// Remove all registered lights (call on zone unload).
		void ClearLights();

		/// Advance the lighting simulation.
		/// \param timeHours  Current in-game time [0, 24) from DayNightCycle.
		/// \param deltaSeconds  Real frame delta.
		void Update(float timeHours, float deltaSeconds);

		/// Build the per-frame snapshot.
		/// Selects up to \p maxVisible lights by closest distance to the camera.
		/// \param camX/Y/Z      Camera world position.
		/// \param maxVisible    Hard cap on visible lights.
		const DynamicLightSnapshot& BuildSnapshot(
		    float camX, float camY, float camZ,
		    uint32_t maxVisible = kDefaultMaxVisibleLights);

		/// Access the current snapshot (valid after BuildSnapshot).
		const DynamicLightSnapshot& GetSnapshot() const { return m_snapshot; }

		/// Total number of registered lights.
		uint32_t LightCount() const { return static_cast<uint32_t>(m_lights.size()); }

		bool IsInitialized() const { return m_initialized; }

	private:
		/// Returns true when the given hour is within the night window [18, 24) ∪ [0, 6).
		static bool IsNightHour(float timeHours);

		/// Advance the fade timer and current intensity for one light.
		static void FadeLight(DynamicPointLight& light, bool shouldBeActive, float deltaSeconds);

		std::vector<DynamicPointLight> m_lights;
		DynamicLightSnapshot           m_snapshot{};
		bool                           m_initialized = false;
	};

} // namespace engine::render
