#pragma once

#include <cstdint>

#include "src/shared/world/WorldClock.h"

namespace engine::render
{
	/// CPU-side day/night cycle system (M38.1).
	///
	/// Advances a 0-24 h clock each frame at a configurable speed, then derives:
	///   - Sun elevation and azimuth (celestial mechanics, simplified).
	///   - Directional light direction (toward-sun unit vector).
	///   - Light colour and intensity (lerped through dawn/noon/dusk/night keyframes).
	///   - Ambient colour (scaled by sun elevation; minimum 0.1 to avoid pitch-black nights).
	///   - Sky gradient colours (zenith and horizon, used by the sky shader).
	///
	/// Moon position is the sun offset by 12 h (opposite side of the sky).
	/// When the sun is below the horizon the moon becomes the active directional light.
	class DayNightCycle
	{
	public:
		/// Initialisation parameters (configurable from game/config.json or defaults).
		struct Params
		{
			/// Starting time in hours [0, 24).  Default: 8.0 (early morning).
			float initialTimeOfDay = 8.0f;
			/// Real-world seconds elapsed per in-game hour.
			/// timeScale = 1  → 1 real second = 1 game second (24 h real-time day).
			/// timeScale = 60 → 1 real minute  = 1 game hour  (24 min real-time day).
			float timeScale = 60.0f;
		};

		/// All outputs computed by Advance(); consumed by Engine::Update() and sky shaders.
		struct State
		{
			/// Current in-game time [0, 24).
			float timeOfDay = 8.0f;

			// ---- Directional light (active celestial body) ----

			/// Normalised direction *toward* the active light source (sun or moon).
			/// Matches the convention used by LightingPass::LightParams::lightDir.
			float lightDir[3] = { 0.5774f, 0.5774f, 0.5774f };

			/// RGB colour of the directional light (intensity pre-multiplied).
			/// Day = 1.0; night = 0.1 (dim moonlight).
			float lightColor[3] = { 1.0f, 0.95f, 0.85f };

			/// RGB constant ambient colour (minimum 0.1 to avoid pitch-black nights).
			float ambientColor[3] = { 0.03f, 0.03f, 0.06f };

			// ---- Sky gradient (fed to sky.frag push-constants) ----

			/// Colour at the zenith (top of the sky dome).
			float skyZenith[3] = { 0.08f, 0.14f, 0.38f };

			/// Colour at the horizon.
			float skyHorizon[3] = { 0.55f, 0.72f, 0.90f };

			// ---- Debug / gameplay helpers ----

			/// True when the sun is above the horizon (elevation > 0).
			bool isDaytime = true;

			// ---- Lunar phase (master-driven via OnLunarPhaseChange callback) ----

			/// Index de la phase lunaire courante (0..15). 0=NewMoon, 7=FullMoon, 15=Earthshine late.
			/// Mis a jour uniquement via OnLunarPhaseChange (master autoritaire).
			uint8_t moonPhase = 0;

			/// Fraction de la lune eclairee (0..1). 0 = noir (NewMoon), 1 = pleine.
			/// Mis a jour uniquement via OnLunarPhaseChange (master autoritaire).
			float moonIllumination = 0.0f;
		};

		DayNightCycle() = default;

		/// Initialise the cycle with the given parameters.
		/// Immediately computes the initial State for timeOfDay = params.initialTimeOfDay.
		/// @param params  Configuration (initial time, time scale).
		void Init(const Params& params);

		/// Advance the clock by \p deltaSeconds real-world seconds and recompute State.
		/// Call once per frame from Engine::Update().
		/// @param deltaSeconds  Real elapsed time since the last frame (seconds).
		void Advance(float deltaSeconds);

		/// Force the in-game time to \p hours [0, 24).
		/// Useful for the `/time set <hours>` debug command.
		void SetTime(float hours);

		/// Update the time scale (real seconds per in-game hour).
		/// Used by the editor's "Atmosphere" panel slider. Clamps to [0.1, 1000.0]
		/// to avoid divide-by-zero or wraparound issues.
		void SetTimeScale(float realSecondsPerHour);

		/// Met a jour la phase lunaire courante (recue depuis le master via
		/// opcode 193 LunarStateResponse ou 194 LunarPhaseChangeNotification).
		/// L'appel est idempotent et thread-safe au sens "appel main thread"
		/// (pas de mutex : le main thread reception du Engine appelle ce
		/// setter, le main thread render lit moonPhase/moonIllumination via
		/// GetState()). Si \p phase >= 16, l'appel est ignore (defense V1).
		/// Si \p illumination est hors [0..1], il est clampe.
		/// \param phase        0..15
		/// \param illumination 0..1 (clamp si hors plage)
		void OnLunarPhaseChange(uint8_t phase, float illumination);

		/// Branche l'horloge serveur (autoritaire) sur le cycle jour/nuit.
		///
		/// Une fois appelee, le cycle passe en mode "driven" : Advance() ne fait
		/// plus avancer un compteur local, il RECALCULE timeOfDay + phase lunaire
		/// chaque frame depuis l'horloge serveur synchronisee (formule pure
		/// engine::world::GameSeconds). Garantit qu'aucune derive ne s'accumule
		/// (le calcul part toujours de l'instant present corrige de l'offset).
		///
		/// Appel typique : reception des opcodes 204 (WorldClockStateResponse) et
		/// 205 (WorldClockChangeNotification). Les parametres admin (offset via
		/// /settime, pause via /pausetime) arrivent dans \p p et s'appliquent
		/// IMMEDIATEMENT au prochain Advance — comportement intentionnel.
		///
		/// \param p                 Parametres horloge (epoch, timeScale, offset,
		///                          pause, periode lunaire) — autorite master.
		/// \param serverTimeUnixMs  Horodatage Unix (ms) cote serveur a l'emission.
		/// \param clientRecvUnixMs  Horodatage Unix (ms) cote client a la reception.
		///                          L'offset (serverTimeUnixMs - clientRecvUnixMs)
		///                          corrige l'ecart d'horloge client/serveur.
		/// \note Effet de bord : passe m_driven a true (le fallback local est
		///       desactive tant que ce mode est actif).
		void SetServerClock(const engine::world::WorldClockParams& p,
		                    uint64_t serverTimeUnixMs, uint64_t clientRecvUnixMs);

		/// Return the current time scale (real seconds per in-game hour).
		float GetTimeScale() const { return m_timeScale; }

		/// Return the current computed state.
		const State& GetState() const { return m_state; }

	private:
		/// Recompute m_state from the current m_timeOfDay.
		void ComputeState();

		/// Linear interpolation of a 3-component colour.
		static void LerpColor3(const float a[3], const float b[3], float t, float out[3]);

		/// Clamp \p v to [lo, hi].
		static float Clamp(float v, float lo, float hi);

		/// Horodatage Unix courant (ms) cote client (system_clock).
		/// Utilise pour reconstruire l'instant serveur en mode driven.
		static uint64_t NowMsClient();

		float  m_timeOfDay = 8.0f;   ///< Current in-game time [0, 24).
		float  m_timeScale = 60.0f;  ///< Real seconds per in-game hour.
		State  m_state{};

		// ---- Mode "driven" par l'horloge serveur (Task 6.1) ----

		/// true quand SetServerClock a ete appele : Advance recalcule depuis
		/// l'horloge serveur. false (defaut) => fallback local inchange (solo /
		/// serveur muet), AUCUNE regression.
		bool   m_driven = false;
		/// Parametres horloge serveur (autorite master) recus via SetServerClock.
		engine::world::WorldClockParams m_serverParams{};
		/// Decalage d'horloge client->serveur (ms) = serverTimeUnixMs - clientRecvUnixMs.
		/// Ajoute a NowMsClient() pour reconstruire l'instant serveur courant.
		int64_t m_clockOffsetMs = 0;
	};

} // namespace engine::render
