#pragma once

// SND.4 — Ambiances sonores : quelles boucles jouer, à quel volume, selon
// l'endroit où se trouve le joueur, l'heure et la météo.
//
// Chaque COUCHE est une boucle (forêt de jour, forêt de nuit, plaine, marais,
// montagne, neige, désert, côte, rivière, ville, pluie, orage). Le module calcule
// un volume cible par couche ; Engine (EngineAudio.cpp) démarre, fond et arrête
// les boucles en conséquence (fondus de quelques secondes : on ne « saute » pas
// d'une ambiance à l'autre en franchissant une frontière).
//
// Données utilisées :
//   - biome du lore sous le joueur : grille exportée par tools/world_gen dans le
//     zone.json de chaque cellule (audio.biome_grid, blocs de 250 m) ;
//   - densité d'arbres autour du joueur (décor de la cellule) ;
//   - eau proche (mer = niveau de la mer, sinon rivière/lac) ;
//   - ville (rayon d'une agglomération de places.json) ;
//   - altitude, couche de sol dominante (neige), heure du jour, météo.
//
// Module PUR : testé par ambience_tests.

#include <array>
#include <cstdint>
#include <string_view>

namespace engine::audio
{
	enum class AmbienceLayer : uint8_t
	{
		ForestDay = 0,
		ForestNight,
		PlainsDay,
		PlainsNight,
		Swamp,
		Mountain,
		Snow,
		Desert,
		Coast,
		River,
		Town,
		RainLight,
		RainHeavy,
		Count
	};

	inline constexpr size_t kAmbienceLayerCount = static_cast<size_t>(AmbienceLayer::Count);

	/// Identifiant du son (catalogue zone_audio.json, bus Ambience) d'une couche.
	std::string_view AmbienceSoundId(AmbienceLayer layer);

	/// Grandes familles de biomes du lore (légende grid.legend du world_master).
	enum class BiomeFamily : uint8_t
	{
		Unknown = 0,
		Plain,
		Forest,
		Desert,
		Mountain,
		SnowMountain,
		Swamp,
		Coast,
	};

	/// Famille d'un code de biome du lore (« T » forêt, « ~ » désert, « % » marais…).
	BiomeFamily BiomeFamilyOf(char code);
	/// Vrai si le code de biome comporte une rivière (« r », « R », « & »).
	bool BiomeHasRiver(char code);

	/// Code de biome au point GLOBAL (x, z) d'une grille `size`×`size` couvrant la
	/// cellule de coin min (minX, minZ) et de côté `sizeM`. '\0' si hors grille.
	char BiomeAt(std::string_view grid, int size, double x, double z, double minX, double minZ, double sizeM);

	/// Ce que le joueur entend autour de lui.
	struct AmbienceInputs
	{
		char biome = '\0';           ///< code du lore sous le joueur ('\0' = inconnu)
		float treeDensity = 0.0f;    ///< 0..1 (arbres dans un rayon de ~30 m)
		bool seaNearby = false;      ///< mer à moins de ~40 m
		bool waterNearby = false;    ///< rivière ou lac à moins de ~40 m
		bool inTown = false;         ///< dans une agglomération
		float altitude = 0.0f;       ///< m (monde)
		bool snowGround = false;     ///< couche dominante du sol = neige
		float daylight = 1.0f;       ///< 0 nuit … 1 plein jour
		float rain = 0.0f;           ///< intensité de pluie 0..1
		bool storm = false;          ///< orage (pluie forte, tonnerre)
	};

	/// Volume cible (0..1) de chaque couche.
	std::array<float, kAmbienceLayerCount> AmbienceTargets(const AmbienceInputs& in);

	/// Lumière du jour (0..1) à l'heure `hours` (0–24) : nuit avant 5 h et après
	/// 21 h, transitions d'1 h 30 à l'aube et au crépuscule.
	float DaylightAt(float hours);
}
