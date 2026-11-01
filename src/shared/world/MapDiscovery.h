#pragma once

// Monde « Royaumes » — carte à découvrir (brouillard de guerre de la carte du monde).
//
// Chaque cellule de 10 km est découpée en cases de 62,5 m (160 × 160) ; une
// case est « découverte » dès que le joueur passe à moins du rayon de
// révélation de son centre. Une cellule = un masque de 25 600 bits (3 200
// octets), persisté par personnage (table character_map_discovery) et échangé
// avec le client par les opcodes 208–210 (cf. MapDiscoveryPayloads.h).
//
// FIX.16 — cases affinées de 125 m (80 × 80, 800 octets) à 62,5 m : avec des
// cases de 125 m, le rayon de 300 m ne couvrait que 4 × 4 cases et la zone
// découverte apparaissait CARRÉE sur la carte. Les anciens masques (800
// octets, en base ou envoyés par un ancien client) restent acceptés :
// chaque ancienne case devient 2 × 2 nouvelles cases (Assign, DecodeBitmap).
//
// Conventions : X = est, Z = sud ; case (0, 0) = coin min (nord-ouest) de la
// cellule ; bit i = tz · 80 + tx (octet i / 8, bit i % 8, poids faible d'abord).
// Pur (aucune dépendance moteur) : partagé client / master / tests.

#include "src/shared/world/WorldCells.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace engine::world::discovery
{
	/// Côté d'une case de découverte (m).
	inline constexpr double kTileM = 62.5;
	/// Cases par côté de cellule (10 000 m / 62,5 m).
	inline constexpr int kTilesPerSide = 160;
	/// Cases par cellule.
	inline constexpr int kTilesPerCell = kTilesPerSide * kTilesPerSide;
	/// Taille du masque brut d'une cellule (octets).
	inline constexpr size_t kBitmapBytes = static_cast<size_t>(kTilesPerCell) / 8u;
	/// FIX.16 — ancien découpage (cases de 125 m), encore accepté en lecture.
	inline constexpr int kLegacyTilesPerSide = 80;
	inline constexpr int kLegacyTilesPerCell = kLegacyTilesPerSide * kLegacyTilesPerSide;
	inline constexpr size_t kLegacyBitmapBytes = static_cast<size_t>(kLegacyTilesPerCell) / 8u;
	/// Rayon de révélation autour du joueur (m).
	inline constexpr double kRevealRadiusM = 300.0;

	/// Masque de découverte d'une cellule.
	class CellBitmap
	{
	public:
		/// Vrai si la case (tx, tz) est découverte (hors bornes : false).
		bool Get(int tx, int tz) const;
		/// Marque la case (tx, tz). \return true si elle ne l'était pas (hors bornes : false).
		bool Set(int tx, int tz);
		/// OU bit à bit avec \p other. \return true si au moins une case a été ajoutée.
		bool Merge(const CellBitmap& other);
		/// Nombre de cases découvertes.
		int Count() const;
		/// Aucune case découverte.
		bool Empty() const;

		const std::array<uint8_t, kBitmapBytes>& Bytes() const { return m_bits; }
		/// Remplace le contenu par \p bytes : kBitmapBytes octets, ou kLegacyBitmapBytes
		/// (ancien masque 80 × 80, agrandi : chaque case → 2 × 2). \return false sinon.
		bool Assign(std::span<const uint8_t> bytes);

	private:
		std::array<uint8_t, kBitmapBytes> m_bits{};
	};

	/// Encodage d'un masque sur le réseau : le plus court entre brut et RLE.
	enum class BitmapEncoding : uint8_t
	{
		Raw = 0, ///< kBitmapBytes octets tels quels
		Rle = 1, ///< suite d'uint16 LE : longueurs de séries alternées, en commençant par des 0
	};

	/// Encode \p bm (choisit l'encodage le plus court). \param outEncoding encodage retenu.
	std::vector<uint8_t> EncodeBitmap(const CellBitmap& bm, BitmapEncoding& outEncoding);
	/// Décode \p data selon \p encoding. \return false si les données sont invalides
	/// (taille brute fausse, séries qui ne couvrent pas exactement kTilesPerCell cases).
	bool DecodeBitmap(BitmapEncoding encoding, std::span<const uint8_t> data, CellBitmap& out);

	/// Case de découverte contenant le point GLOBAL (x, z) : cellule + indices locaux.
	struct TileRef
	{
		engine::world::cells::CellCoord cell{};
		int tx = 0;
		int tz = 0;
	};
	TileRef TileAt(double x, double z);

	/// Centre GLOBAL de la case (tx, tz) de \p cell.
	engine::world::cells::CellPoint TileCenter(engine::world::cells::CellCoord cell, int tx, int tz);

	/// FIX.16 — Rayon d'affichage d'une case découverte (m) : à l'écran, chaque
	/// case découverte dégage un DISQUE de ce rayon autour de son centre (au lieu
	/// d'un carré). L'union de ces disques suit le cercle de révélation
	/// (kRevealRadiusM) au lieu de former des marches d'escalier.
	/// 0,75 case > demi-diagonale (0,707 case) : deux cases voisines découvertes
	/// ne laissent aucun trou entre elles.
	inline constexpr double kDisplayRevealRadiusM = kTileM * 0.75;

	/// FIX.16 — Vrai si le point GLOBAL (x, z) est dans le disque d'affichage
	/// (kDisplayRevealRadiusM) d'au moins une case découverte parmi les 3 × 3
	/// cases autour de lui. \param tileDiscovered(i, j) indique si la case
	/// d'indices GLOBAUX (i = floor(x / kTileM), j = floor(z / kTileM)) est
	/// découverte. Pur (sert au voile de la carte du monde, testé).
	bool IsPointRevealedSmooth(double x, double z, const std::function<bool(long long, long long)>& tileDiscovered);

	/// Appelle \p fn pour chaque case dont le centre est à moins de \p radiusM du
	/// point GLOBAL (x, z), y compris dans les cellules voisines.
	void ForEachTileInDisc(double x, double z, double radiusM, const std::function<void(const TileRef&)>& fn);
}
