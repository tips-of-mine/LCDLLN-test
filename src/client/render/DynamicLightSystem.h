#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render
{
	/// Type of a dynamic light source (M38.3).
	enum class DynamicLightType : uint8_t
	{
		Streetlamp = 0, ///< Outdoor lamp post; activated at night.
		Torch      = 1, ///< Flame light; activated at night.
		Window     = 2, ///< Indoor window glow; emissive multiplier, activated at night.
	};

	/// Immutable definition loaded from the content JSON (M38.3).
	struct PointLightDefinition
	{
		std::string     id;
		DynamicLightType type            = DynamicLightType::Streetlamp;
		float           position[3]     = { 0.0f, 0.0f, 0.0f };
		float           radius          = 8.0f;  ///< Influence radius in world units.
		float           color[3]        = { 1.0f, 0.85f, 0.4f }; ///< Linear RGB.
		float           baseIntensity   = 2.0f;  ///< Peak intensity (reached at fade = 1).
		bool            autoNightLight  = true;  ///< If true: on 18:00-6:00, off 6:00-18:00.
	};

	/// One active point light with its current fade-adjusted intensity (M38.3).
	/// Consumed by the rendering layer (e.g. a future clustered-light pass).
	struct ActivePointLight
	{
		float position[3]  = { 0.0f, 0.0f, 0.0f };
		float radius        = 8.0f;
		float color[3]      = { 1.0f, 0.85f, 0.4f };
		float intensity     = 0.0f; ///< baseIntensity * currentFade.
	};

	/// CPU-side dynamic point-light system (M38.3).
	///
	/// Loads point-light definitions from a content-relative JSON file,
	/// then each frame:
	///   - Determines whether each "auto_night_light" source should be on or off
	///     based on the current in-game time (night = 18:00-6:00).
	///   - Smoothly fades intensity in (0→1 over kFadeDurationSeconds) or out
	///     (1→0 over kFadeDurationSeconds).
	///   - Exposes `GetActiveLights()` for the deferred/forward rendering pass.
	///   - Exposes `GetWindowEmissiveMultiplier()` for the material emissive shader
	///     (window lights boost albedo intensity at night).
	///
	/// Night is defined as time-of-day ∈ [18, 24) ∪ [0, 6) (spec: 18:00-6:00).
	/// Fade duration: 60 s (spec: 1 minute).
	class DynamicLightSystem final
	{
	public:
		/// Duration of the fade-in / fade-out transition in seconds (spec: 1 min).
		static constexpr float kFadeDurationSeconds = 60.0f;

		/// Night start hour (inclusive, 24-h clock).
		static constexpr float kNightStartHour = 18.0f;

		/// Night end hour (exclusive, 24-h clock).
		static constexpr float kNightEndHour   = 6.0f;

		DynamicLightSystem() = default;
		DynamicLightSystem(const DynamicLightSystem&) = delete;
		DynamicLightSystem& operator=(const DynamicLightSystem&) = delete;

		/// Load light definitions and initialise runtime state.
		///
		/// The definitions JSON path is resolved via `config`'s `paths.content`
		/// and the key `"world.dynamic_lights_path"` (default:
		/// `"lights/dynamic_lights.json"`).
		///
		/// @param config   Engine configuration (paths.content, optional overrides).
		/// @return true on success (even if the file is absent — that logs a warning
		///         but is not fatal, allowing the game to run without lights).
		bool Init(const engine::core::Config& config);

		/// Release all light state.
		void Shutdown();

		/// Advance the system by \p deltaSeconds given the current in-game
		/// \p timeOfDay (hours, [0, 24)).
		///
		/// Updates per-light fade timers and re-builds m_activeLights.
		void Tick(float timeOfDay, float deltaSeconds);

		/// Currently active lights with fade-adjusted intensities.
		/// Valid after the first Tick() call.
		const std::vector<ActivePointLight>& GetActiveLights() const
		{
			return m_activeLights;
		}

		/// Normalised emissive multiplier for window materials [0, 1].
		///
		/// Equals the average fade of all Window-type lights.
		/// The material shader multiplies its emissive texture by this value
		/// so windows glow at night and are dark during the day.
		float GetWindowEmissiveMultiplier() const { return m_windowEmissive; }

		bool IsInitialized() const { return m_initialized; }

		/// Total number of loaded light definitions.
		size_t GetLightCount() const { return m_lights.size(); }

	private:
		/// Per-light runtime state.
		struct LightRuntime
		{
			PointLightDefinition def{};
			float  currentFade  = 0.0f; ///< [0, 1] current fade value.
			bool   targetOn     = false; ///< true when night (light should be on).
		};

		/// Returns true when \p timeOfDay is in the night window [18, 24) ∪ [0, 6).
		static bool IsNight(float timeOfDay);

		/// Load definitions from a JSON file using the Config indexed-key API.
		bool LoadDefinitions(const engine::core::Config& config,
		                     const std::string& relPath);

		std::vector<LightRuntime>   m_lights;
		std::vector<ActivePointLight> m_activeLights;
		float                       m_windowEmissive = 0.0f;
		bool                        m_initialized    = false;
	};

} // namespace engine::render
