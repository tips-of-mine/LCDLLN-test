// Carte à découvrir : masques de cases, encodage brut/RLE, révélation en disque
// (y compris à cheval sur deux cellules), payloads 208–210.

#include "src/shared/network/MapDiscoveryPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/world/MapDiscovery.h"

#include <cstdio>
#include <set>
#include <span>
#include <utility>

namespace disc = engine::world::discovery;
namespace cells = engine::world::cells;
using namespace engine::network;

namespace
{
	int g_fail = 0;
	void Check(bool cond, const char* msg)
	{
		if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; }
	}

	bool RoundTrip(const disc::CellBitmap& bm, disc::BitmapEncoding* encOut = nullptr)
	{
		disc::BitmapEncoding enc = disc::BitmapEncoding::Raw;
		const std::vector<uint8_t> data = disc::EncodeBitmap(bm, enc);
		if (encOut) *encOut = enc;
		disc::CellBitmap back;
		return disc::DecodeBitmap(enc, std::span<const uint8_t>(data.data(), data.size()), back)
			&& back.Bytes() == bm.Bytes();
	}
}

int main()
{
	// Masque : Set / Get / Count / Merge / bornes.
	{
		disc::CellBitmap a;
		Check(a.Empty() && a.Count() == 0, "masque vide");
		Check(a.Set(3, 5) && !a.Set(3, 5), "Set : nouveau puis deja present");
		Check(a.Get(3, 5) && !a.Get(5, 3), "Get");
		Check(!a.Set(-1, 0) && !a.Set(0, disc::kTilesPerSide) && !a.Get(80, 80), "hors bornes");
		disc::CellBitmap b;
		b.Set(79, 79);
		Check(a.Merge(b) && a.Count() == 2 && !a.Merge(b), "Merge OU, idempotent");
	}

	// Encodage : vide et creux en RLE (court), damier en brut, aller-retour exact.
	{
		disc::CellBitmap empty;
		disc::BitmapEncoding enc{};
		Check(RoundTrip(empty, &enc) && enc == disc::BitmapEncoding::Rle, "vide : RLE");
		disc::CellBitmap disk;
		for (int z = 10; z < 15; ++z) for (int x = 20; x < 26; ++x) disk.Set(x, z);
		Check(RoundTrip(disk, &enc) && enc == disc::BitmapEncoding::Rle, "zone compacte : RLE");
		disc::CellBitmap first;
		first.Set(0, 0);
		Check(RoundTrip(first), "premiere case decouverte (serie nulle en tete)");
		disc::CellBitmap checker;
		for (int z = 0; z < disc::kTilesPerSide; ++z) for (int x = 0; x < disc::kTilesPerSide; ++x) if ((x + z) % 2 == 0) checker.Set(x, z);
		Check(RoundTrip(checker, &enc) && enc == disc::BitmapEncoding::Raw, "damier : brut");
		disc::CellBitmap full;
		for (int z = 0; z < disc::kTilesPerSide; ++z) for (int x = 0; x < disc::kTilesPerSide; ++x) full.Set(x, z);
		Check(RoundTrip(full, &enc) && enc == disc::BitmapEncoding::Rle && full.Count() == disc::kTilesPerCell, "plein : RLE");
	}

	// Décodage refusé : RLE incomplet / débordant, brut de mauvaise taille, encodage inconnu.
	{
		disc::CellBitmap out;
		const uint8_t shortRle[] = { 0x10, 0x00 };
		Check(!disc::DecodeBitmap(disc::BitmapEncoding::Rle, shortRle, out), "RLE trop court refuse");
		const uint8_t overRle[] = { 0xFF, 0xFF };
		Check(!disc::DecodeBitmap(disc::BitmapEncoding::Rle, overRle, out), "RLE debordant refuse");
		const uint8_t oddRle[] = { 0x00 };
		Check(!disc::DecodeBitmap(disc::BitmapEncoding::Rle, oddRle, out), "RLE impair refuse");
		const uint8_t raw[10] = {};
		Check(!disc::DecodeBitmap(disc::BitmapEncoding::Raw, raw, out), "brut de mauvaise taille refuse");
		Check(!disc::DecodeBitmap(static_cast<disc::BitmapEncoding>(9), raw, out), "encodage inconnu refuse");
	}

	// Cases : coin min de cellule = case (0, 0), centre de case, révélation en disque.
	{
		const cells::CellCoord c{ 3, -9 };
		const cells::CellPoint mn = cells::CellMinCorner(c);
		const disc::TileRef t0 = disc::TileAt(mn.x + 1.0, mn.z + 1.0);
		Check(t0.cell == c && t0.tx == 0 && t0.tz == 0, "coin min = case (0,0)");
		const disc::TileRef t1 = disc::TileAt(mn.x + 9999.0, mn.z + 130.0);
		Check(t1.cell == c && t1.tx == 79 && t1.tz == 1, "case (79,1)");
		const cells::CellPoint ctr = disc::TileCenter(c, 79, 1);
		Check(ctr.x == mn.x + 9937.5 && ctr.z == mn.z + 187.5, "centre de case");

		int n = 0;
		disc::ForEachTileInDisc(cells::CellCenter(c).x, cells::CellCenter(c).z, disc::kRevealRadiusM, [&](const disc::TileRef& t) {
			++n;
			Check(t.cell == c, "disque central : meme cellule");
		});
		Check(n >= 16 && n <= 24, "disque de 300 m : ~20 cases");

		// À cheval sur la frontière est : cases des deux cellules.
		std::set<std::pair<int, int>> seen;
		disc::ForEachTileInDisc(mn.x + 10000.0, cells::CellCenter(c).z, disc::kRevealRadiusM, [&](const disc::TileRef& t) {
			seen.insert({ t.cell.n, t.cell.e });
		});
		Check(seen.size() == 2 && seen.count({ 3, -9 }) && seen.count({ 3, -8 }), "frontiere : deux cellules");
	}

	// Payloads : requête, réponse (pages), mise à jour, erreurs, refus des paquets malformés.
	{
		const auto reqBytes = BuildMapDiscoveryRequestPayload(32);
		const auto req = ParseMapDiscoveryRequestPayload(reqBytes.data(), reqBytes.size());
		Check(req && req->startIndex == 32, "requete : startIndex");
		const auto reqEmpty = ParseMapDiscoveryRequestPayload(nullptr, 0);
		Check(reqEmpty && reqEmpty->startIndex == 0, "requete vide : premiere page");

		std::vector<MapDiscoveryCell> cellsIn;
		for (uint32_t k = 0; k < kMapDiscoveryCellsPerPacket; ++k)
		{
			MapDiscoveryCell mc;
			mc.cellZoneId = cells::CellZoneId({ 3, static_cast<int>(k) - 9 });
			// Pire cas (brut) pour vérifier que la page tient dans un paquet.
			for (int z = 0; z < disc::kTilesPerSide; ++z) for (int x = 0; x < disc::kTilesPerSide; ++x) if ((x + z) % 2 == 0) mc.bitmap.Set(x, z);
			cellsIn.push_back(mc);
		}
		const auto respBytes = BuildMapDiscoveryResponsePayload(0u, 16u, cellsIn);
		Check(!respBytes.empty() && respBytes.size() + kProtocolV1HeaderSize <= kProtocolV1MaxPacketSize, "reponse pleine : tient dans un paquet");
		const auto resp = ParseMapDiscoveryResponsePayload(respBytes.data(), respBytes.size());
		Check(resp && resp->error == 0 && resp->nextIndex == 16 && resp->cells.size() == cellsIn.size(), "reponse : entete");
		Check(resp && resp->cells[5].cellZoneId == cellsIn[5].cellZoneId && resp->cells[5].bitmap.Bytes() == cellsIn[5].bitmap.Bytes(), "reponse : masque intact");
		const auto pkt = BuildMapDiscoveryResponsePacket(0u, 0u, cellsIn, 7u, 99u);
		Check(!pkt.empty() && pkt.size() <= kProtocolV1MaxPacketSize, "paquet de reponse");

		const auto errBytes = BuildMapDiscoveryResponsePayload(static_cast<uint8_t>(MapDiscoveryErrorCode::Unauthorized), 0u, {});
		const auto err = ParseMapDiscoveryResponsePayload(errBytes.data(), errBytes.size());
		Check(err && err->error == 6 && err->cells.empty(), "reponse en erreur");

		std::vector<MapDiscoveryCell> two(cellsIn.begin(), cellsIn.begin() + 2);
		const auto updBytes = BuildMapDiscoveryUpdatePayload(two);
		const auto upd = ParseMapDiscoveryUpdatePayload(updBytes.data(), updBytes.size());
		Check(upd && upd->cells.size() == 2 && upd->cells[1].bitmap.Bytes() == two[1].bitmap.Bytes(), "mise a jour : aller-retour");
		std::vector<MapDiscoveryCell> tooMany(kMapDiscoveryCellsPerPacket + 1u);
		Check(BuildMapDiscoveryUpdatePayload(tooMany).empty(), "mise a jour : trop de cellules refusee");

		std::vector<uint8_t> truncated(updBytes.begin(), updBytes.end() - 3);
		Check(!ParseMapDiscoveryUpdatePayload(truncated.data(), truncated.size()), "mise a jour tronquee refusee");
	}

	if (g_fail == 0) std::printf("map_discovery_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
