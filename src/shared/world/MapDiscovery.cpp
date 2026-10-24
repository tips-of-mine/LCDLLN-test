// Monde « Royaumes » — carte à découvrir. Voir MapDiscovery.h.

#include "src/shared/world/MapDiscovery.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace engine::world::discovery
{
	namespace cells = engine::world::cells;

	bool CellBitmap::Get(int tx, int tz) const
	{
		if (tx < 0 || tz < 0 || tx >= kTilesPerSide || tz >= kTilesPerSide)
			return false;
		const int i = tz * kTilesPerSide + tx;
		return (m_bits[static_cast<size_t>(i >> 3)] >> (i & 7)) & 1u;
	}

	bool CellBitmap::Set(int tx, int tz)
	{
		if (tx < 0 || tz < 0 || tx >= kTilesPerSide || tz >= kTilesPerSide)
			return false;
		const int i = tz * kTilesPerSide + tx;
		uint8_t& b = m_bits[static_cast<size_t>(i >> 3)];
		const uint8_t mask = static_cast<uint8_t>(1u << (i & 7));
		if (b & mask)
			return false;
		b = static_cast<uint8_t>(b | mask);
		return true;
	}

	bool CellBitmap::Merge(const CellBitmap& other)
	{
		bool changed = false;
		for (size_t i = 0; i < kBitmapBytes; ++i)
		{
			const uint8_t merged = static_cast<uint8_t>(m_bits[i] | other.m_bits[i]);
			changed = changed || merged != m_bits[i];
			m_bits[i] = merged;
		}
		return changed;
	}

	int CellBitmap::Count() const
	{
		int n = 0;
		for (uint8_t b : m_bits)
			n += std::popcount(static_cast<unsigned>(b));
		return n;
	}

	bool CellBitmap::Empty() const
	{
		return std::all_of(m_bits.begin(), m_bits.end(), [](uint8_t b) { return b == 0u; });
	}

	bool CellBitmap::Assign(std::span<const uint8_t> bytes)
	{
		if (bytes.size() != kBitmapBytes)
			return false;
		std::copy(bytes.begin(), bytes.end(), m_bits.begin());
		return true;
	}

	std::vector<uint8_t> EncodeBitmap(const CellBitmap& bm, BitmapEncoding& outEncoding)
	{
		// Séries alternées (0 d'abord) ; une série de longueur 0 en tête si la
		// première case est découverte.
		std::vector<uint8_t> rle;
		rle.reserve(64);
		auto push = [&rle](uint32_t len) {
			rle.push_back(static_cast<uint8_t>(len & 0xFFu));
			rle.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
		};
		bool current = false;
		uint32_t run = 0;
		for (int i = 0; i < kTilesPerCell; ++i)
		{
			const bool bit = (bm.Bytes()[static_cast<size_t>(i >> 3)] >> (i & 7)) & 1u;
			if (bit == current)
			{
				++run;
				continue;
			}
			push(run);
			current = bit;
			run = 1;
			if (rle.size() >= kBitmapBytes)
				break; // déjà plus long que le brut : inutile de continuer
		}
		if (rle.size() < kBitmapBytes)
		{
			push(run);
			if (rle.size() < kBitmapBytes)
			{
				outEncoding = BitmapEncoding::Rle;
				return rle;
			}
		}
		outEncoding = BitmapEncoding::Raw;
		return std::vector<uint8_t>(bm.Bytes().begin(), bm.Bytes().end());
	}

	bool DecodeBitmap(BitmapEncoding encoding, std::span<const uint8_t> data, CellBitmap& out)
	{
		if (encoding == BitmapEncoding::Raw)
			return out.Assign(data);
		if (encoding != BitmapEncoding::Rle || (data.size() % 2u) != 0u)
			return false;
		std::array<uint8_t, kBitmapBytes> bits{};
		int pos = 0;
		bool current = false;
		for (size_t k = 0; k + 1 < data.size(); k += 2)
		{
			const int len = static_cast<int>(data[k]) | (static_cast<int>(data[k + 1]) << 8);
			if (len > kTilesPerCell - pos)
				return false;
			if (current)
				for (int i = pos; i < pos + len; ++i)
					bits[static_cast<size_t>(i >> 3)] = static_cast<uint8_t>(bits[static_cast<size_t>(i >> 3)] | (1u << (i & 7)));
			pos += len;
			current = !current;
		}
		if (pos != kTilesPerCell)
			return false;
		return out.Assign(bits);
	}

	TileRef TileAt(double x, double z)
	{
		TileRef t;
		t.cell = cells::CellFromPosition(x, z);
		const cells::CellPoint mn = cells::CellMinCorner(t.cell);
		t.tx = std::clamp(static_cast<int>(std::floor((x - mn.x) / kTileM)), 0, kTilesPerSide - 1);
		t.tz = std::clamp(static_cast<int>(std::floor((z - mn.z) / kTileM)), 0, kTilesPerSide - 1);
		return t;
	}

	cells::CellPoint TileCenter(cells::CellCoord cell, int tx, int tz)
	{
		const cells::CellPoint mn = cells::CellMinCorner(cell);
		return { mn.x + (tx + 0.5) * kTileM, mn.z + (tz + 0.5) * kTileM };
	}

	void ForEachTileInDisc(double x, double z, double radiusM, const std::function<void(const TileRef&)>& fn)
	{
		if (!fn || radiusM < 0.0)
			return;
		// Balayage sur la grille GLOBALE des cases (alignée sur les coins de
		// cellule : la cellule fait un nombre entier de cases).
		const double r2 = radiusM * radiusM;
		const long long i0 = static_cast<long long>(std::floor((x - radiusM) / kTileM));
		const long long i1 = static_cast<long long>(std::floor((x + radiusM) / kTileM));
		const long long j0 = static_cast<long long>(std::floor((z - radiusM) / kTileM));
		const long long j1 = static_cast<long long>(std::floor((z + radiusM) / kTileM));
		for (long long j = j0; j <= j1; ++j)
			for (long long i = i0; i <= i1; ++i)
			{
				const double cx = (static_cast<double>(i) + 0.5) * kTileM;
				const double cz = (static_cast<double>(j) + 0.5) * kTileM;
				const double dx = cx - x, dz = cz - z;
				if (dx * dx + dz * dz > r2)
					continue;
				fn(TileAt(cx, cz));
			}
	}
}
