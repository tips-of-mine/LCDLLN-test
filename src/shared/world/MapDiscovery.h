#pragma once

// Monde « Royaumes » — carte à découvrir (brouillard de guerre de la carte du monde).
//
// Chaque cellule de 10 km est découpée en cases de 125 m (80 × 80) ; une case
// est « découverte » dès que le joueur passe à moins du rayon de révélation de
// son centre. Une cellule = un masque de 6400 bits (800 octets), persisté par
// personnage côté master (table character_map_discovery) et échangé avec le
// client par les opcodes 208–210 (cf. MapDiscoveryPayloads.h).
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
	inline constexpr double kTileM = 125.0;
	/// Cases par côté de cellule (10 000 m / 125 m).
	inline constexpr int kTilesPerSide = 80;
	/// Cases par cellule.
	inline constexpr int kTilesPerCell = kTilesPerSide * kTilesPerSide;
	/// Taille du masque brut d'une cellule (octets).
	inline constexpr size_t kBitmapBytes = static_cast<size_t>(kTilesPerCell) / 8u;
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
		/// Remplace le contenu par \p bytes (taille kBitmapBytes exigée). \return false sinon.
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

	/// Appelle \p fn pour chaque case dont le centre est à moins de \p radiusM du
	/// point GLOBAL (x, z), y compris dans les cellules voisines.
	void ForEachTileInDisc(double x, double z, double radiusM, const std::function<void(const TileRef&)>& fn);
}
