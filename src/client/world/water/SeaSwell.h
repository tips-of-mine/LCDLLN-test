// src/client/world/water/SeaSwell.h
#pragma once

// EAU.6 — Houle de la mer et vagues de bord (jet de rive), côté CPU.
//
// La surface de la mer n'est plus plate : water.vert la soulève (houle au large,
// vague qui monte puis se retire sur la plage). Ce module est la MÊME formule en
// C++ : la nage (TerrainCollider::QueryWater) et le son du ressac s'en servent
// pour rester calés sur ce que le joueur voit.
//
// RÈGLE DE PARITÉ : toute constante ou formule de ce fichier a son double dans
// game/data/shaders/water.vert (fonctions seaSwellHeight / seaSurge /
// seaSwellDepthScale). Les deux doivent changer ENSEMBLE. Le test
// sea_swell_tests relit water.vert et vérifie les constantes.
//
// Repère : positions XZ du repère de RENDU (celui de la scène d'eau et du
// collisionneur), temps en secondes (Engine::WaterTimeSeconds).
//
// Lacs et rivières ne sont JAMAIS concernés : seule une nappe `isOcean` reçoit
// ces décalages (EAU.6, décision du propriétaire : pas de vagues dans les lacs).

namespace engine::world::water
{
	/// Réglages de la houle. Défauts = mer calme (décision EAU.6, Q2-A).
	struct SeaSwellParams
	{
		/// Direction du vent dominant dans le plan XZ (normalisée à l'usage).
		float windDirX = 0.8f;
		float windDirZ = 0.6f;
		/// Élévation maximale de la houle au-dessus du niveau moyen (m). La
		/// hauteur crête-creux atteint au plus le double. Clé client
		/// `render.water.sea_swell_m`.
		float amplitudeM = 0.35f;
	};

	/// Amplitude de référence (m) : à cette valeur, l'éclairage de la houle
	/// (water.frag) est exactement celui d'avant EAU.6.
	inline constexpr float kSeaSwellReferenceAmplitudeM = 0.35f;
	/// Montée maximale (m, verticale) de l'eau sur la plage au jet de rive.
	inline constexpr float kSeaRunUpM = 0.30f;
	/// Période (s) entre deux vagues qui arrivent sur la plage.
	inline constexpr float kSeaSurfPeriodS = 9.0f;
	/// Avance de phase (rad) par mètre de profondeur : la vague atteint d'abord
	/// le large puis remonte vers le rivage (≈ 3 s sur la zone de déferlement).
	inline constexpr float kSeaSurfPhasePerMeter = 0.9f;
	/// Profondeur (m) au-delà de laquelle il n'y a plus de jet de rive.
	inline constexpr float kSeaSurfZoneDepthM = 2.5f;
	/// Profondeur (m) en dessous de laquelle la houle commence à s'éteindre,
	/// et profondeur où elle est entière.
	inline constexpr float kSeaSwellShallowM = 0.2f;
	inline constexpr float kSeaSwellFullDepthM = 5.0f;
	/// Profondeur conventionnelle (m) d'un point sans terrain connu : large.
	inline constexpr float kSeaDeepWaterM = 100.0f;

	/// Les 4 trains de houle : longueur d'onde (m), poids relatif (Σ = 1,13,
	/// mêmes poids que l'éclairage historique EAU.2), angle par rapport au vent.
	inline constexpr float kSeaSwellLengthM[4] = { 21.0f, 13.0f, 8.0f, 5.0f };
	inline constexpr float kSeaSwellWeight[4]  = { 0.55f, 0.30f, 0.18f, 0.10f };
	inline constexpr float kSeaSwellAngle[4]   = { 0.0f, 0.45f, -0.60f, 1.10f };
	inline constexpr float kSeaSwellWeightSum  = 1.13f;

	/// Facteur 0..1 de la houle selon la profondeur d'eau \p depthM (m ; < 0 =
	/// terrain émergé) : nulle au bord, entière au large.
	float SeaSwellDepthScale(float depthM);

	/// Hauteur (m, moyenne nulle) de la houle au large au point (x, z) à
	/// l'instant \p timeS. Somme de 4 sinusoïdes autour du vent, sans
	/// déplacement horizontal (la nage lit exactement la même hauteur).
	float SeaSwellHeight(float x, float z, float timeS, const SeaSwellParams& params);

	/// Phase (rad) du jet de rive à la profondeur \p depthM et à l'instant \p timeS.
	float SeaSurfPhase(float depthM, float timeS);

	/// Montée (m, ≥ 0) de l'eau due à la vague de bord : nulle au large
	/// (profondeur > kSeaSurfZoneDepthM), jusqu'à kSeaRunUpM sur la plage.
	float SeaSurge(float x, float z, float depthM, float timeS);

	/// Décalage vertical total (m) de la surface de la mer par rapport à son
	/// niveau moyen : houle atténuée par la profondeur + jet de rive.
	/// \param depthM niveau de la mer − hauteur du sol (m).
	float SeaSurfaceOffset(float x, float z, float depthM, float timeS, const SeaSwellParams& params);

	/// Numéro de la dernière vague arrivée sur le rivage (sommet du jet de rive
	/// à profondeur 0) à l'instant \p timeS. Change d'une unité à chaque vague :
	/// sert à déclencher le son du ressac au bon moment.
	long long SeaSurfBreakIndex(float timeS);
}
