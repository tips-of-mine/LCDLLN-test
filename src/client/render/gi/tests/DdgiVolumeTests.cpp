/**
 * M45.6 : tests unitaires CPU PURS pour la structure DDGI (DdgiVolume).
 * Aucune dépendance Vulkan runtime : on n'appelle QUE les méthodes CPU
 * (indexation, positions monde, layout d'atlas). Jamais Allocate/Destroy.
 *
 * Convention identique à ProtocolV1Tests.cpp : main() renvoie 0 si tout passe,
 * non-zero (+ message sur stderr) au premier échec.
 */

#include "src/client/render/gi/DdgiVolume.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	template<typename T>
	void AssertEq(T a, T b, const char* msg)
	{
		if (a != b)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << " (attendu " << static_cast<uint64_t>(b)
				<< " obtenu " << static_cast<uint64_t>(a) << ")" << std::endl;
		}
	}
}

using engine::render::gi::DdgiGridConfig;
using engine::render::gi::DdgiVolume;

static bool TestProbeCount()
{
	DdgiVolume vol; // config par défaut 8x8x4
	AssertEq(vol.ProbeCount(), 8u * 8u * 4u, "ProbeCount defaut");

	DdgiGridConfig cfg;
	cfg.counts[0] = 5u;
	cfg.counts[1] = 3u;
	cfg.counts[2] = 7u;
	vol.SetConfig(cfg);
	AssertEq(vol.ProbeCount(), 5u * 3u * 7u, "ProbeCount custom");
	return s_failCount == 0;
}

static bool TestIndexRoundTrip()
{
	DdgiVolume vol;
	DdgiGridConfig cfg;
	cfg.counts[0] = 6u;
	cfg.counts[1] = 4u;
	cfg.counts[2] = 5u;
	vol.SetConfig(cfg);

	// Round-trip ProbeIndex <-> GridCoord sur quelques triplets.
	const uint32_t samples[][3] = {
		{ 0u, 0u, 0u },
		{ 5u, 3u, 4u },
		{ 1u, 2u, 3u },
		{ 3u, 0u, 4u },
		{ 0u, 3u, 0u },
	};
	for (const auto& s : samples)
	{
		const uint32_t idx = vol.ProbeIndex(s[0], s[1], s[2]);
		uint32_t ix = 99u, iy = 99u, iz = 99u;
		vol.GridCoord(idx, ix, iy, iz);
		AssertEq(ix, s[0], "round-trip ix");
		AssertEq(iy, s[1], "round-trip iy");
		AssertEq(iz, s[2], "round-trip iz");
	}

	// Round-trip exhaustif sur tout l'espace + indices uniques et bornés.
	std::vector<uint8_t> seen(vol.ProbeCount(), 0u);
	for (uint32_t iz = 0; iz < cfg.counts[2]; ++iz)
		for (uint32_t iy = 0; iy < cfg.counts[1]; ++iy)
			for (uint32_t ix = 0; ix < cfg.counts[0]; ++ix)
			{
				const uint32_t idx = vol.ProbeIndex(ix, iy, iz);
				Assert(idx < vol.ProbeCount(), "index dans les bornes");
				Assert(seen[idx] == 0u, "index unique");
				seen[idx] = 1u;
				uint32_t rx = 0u, ry = 0u, rz = 0u;
				vol.GridCoord(idx, rx, ry, rz);
				Assert(rx == ix && ry == iy && rz == iz, "round-trip exhaustif");
			}
	return s_failCount == 0;
}

static bool TestWorldPos()
{
	DdgiVolume vol;
	DdgiGridConfig cfg;
	cfg.origin[0] = 10.0f; cfg.origin[1] = -5.0f; cfg.origin[2] = 2.0f;
	cfg.spacing[0] = 2.0f; cfg.spacing[1] = 3.0f; cfg.spacing[2] = 4.0f;
	cfg.counts[0] = 8u; cfg.counts[1] = 8u; cfg.counts[2] = 4u;
	vol.SetConfig(cfg);

	float x = 0.f, y = 0.f, z = 0.f;
	// Coin origine.
	vol.ProbeWorldPos(0u, 0u, 0u, x, y, z);
	Assert(x == 10.0f && y == -5.0f && z == 2.0f, "ProbeWorldPos origine");

	// Coin opposé : origin + (counts-1) * spacing.
	vol.ProbeWorldPos(cfg.counts[0] - 1u, cfg.counts[1] - 1u, cfg.counts[2] - 1u, x, y, z);
	const float ex = 10.0f + 7.0f * 2.0f;   // 24
	const float ey = -5.0f + 7.0f * 3.0f;   // 16
	const float ez = 2.0f + 3.0f * 4.0f;    // 14
	Assert(x == ex && y == ey && z == ez, "ProbeWorldPos coin oppose");
	return s_failCount == 0;
}

static bool TestAtlasDimensions()
{
	DdgiVolume vol; // 8x8x4, irr=8, vis=16
	AssertEq(vol.IrradianceTileSize(), 8u + 2u, "IrradianceTileSize = texels+2");
	AssertEq(vol.VisibilityTileSize(), 16u + 2u, "VisibilityTileSize = texels+2");
	AssertEq(vol.AtlasCols(), 8u * 4u, "AtlasCols = counts[0]*counts[2]");
	AssertEq(vol.AtlasRows(), 8u, "AtlasRows = counts[1]");

	// width = AtlasCols * tileSize ; height = AtlasRows * tileSize.
	AssertEq(vol.IrradianceAtlasWidth(), vol.AtlasCols() * vol.IrradianceTileSize(), "IrradianceAtlasWidth");
	AssertEq(vol.IrradianceAtlasHeight(), vol.AtlasRows() * vol.IrradianceTileSize(), "IrradianceAtlasHeight");
	AssertEq(vol.VisibilityAtlasWidth(), vol.AtlasCols() * vol.VisibilityTileSize(), "VisibilityAtlasWidth");
	AssertEq(vol.VisibilityAtlasHeight(), vol.AtlasRows() * vol.VisibilityTileSize(), "VisibilityAtlasHeight");
	return s_failCount == 0;
}

static bool TestAtlasTileOrigins()
{
	DdgiVolume vol;
	DdgiGridConfig cfg; // garde irr/vis par défaut, change la grille
	cfg.counts[0] = 6u; cfg.counts[1] = 5u; cfg.counts[2] = 3u;
	cfg.irradianceTexels = 8u;
	cfg.visibilityTexels = 16u;
	vol.SetConfig(cfg);

	// Pour chaque sonde : tuile dans les bornes + unicité (irradiance et visibilité).
	const uint32_t irrTile = vol.IrradianceTileSize();
	const uint32_t irrW = vol.IrradianceAtlasWidth();
	const uint32_t irrH = vol.IrradianceAtlasHeight();
	const uint32_t visTile = vol.VisibilityTileSize();
	const uint32_t visW = vol.VisibilityAtlasWidth();
	const uint32_t visH = vol.VisibilityAtlasHeight();

	std::vector<uint64_t> irrOrigins;
	std::vector<uint64_t> visOrigins;
	irrOrigins.reserve(vol.ProbeCount());
	visOrigins.reserve(vol.ProbeCount());

	for (uint32_t p = 0; p < vol.ProbeCount(); ++p)
	{
		uint32_t px = 0u, py = 0u;
		vol.ProbeAtlasTileOrigin(p, irrTile, px, py);
		Assert(px + irrTile <= irrW, "tuile irradiance dans largeur");
		Assert(py + irrTile <= irrH, "tuile irradiance dans hauteur");
		Assert((px % irrTile) == 0u && (py % irrTile) == 0u, "origine irradiance alignee tuile");
		irrOrigins.push_back((static_cast<uint64_t>(px) << 32) | py);

		uint32_t vx = 0u, vy = 0u;
		vol.ProbeAtlasTileOrigin(p, visTile, vx, vy);
		Assert(vx + visTile <= visW, "tuile visibilite dans largeur");
		Assert(vy + visTile <= visH, "tuile visibilite dans hauteur");
		visOrigins.push_back((static_cast<uint64_t>(vx) << 32) | vy);
	}

	// Unicité des origines (chaque sonde occupe une tuile distincte).
	for (size_t i = 0; i < irrOrigins.size(); ++i)
		for (size_t j = i + 1; j < irrOrigins.size(); ++j)
		{
			Assert(irrOrigins[i] != irrOrigins[j], "origines irradiance uniques");
			Assert(visOrigins[i] != visOrigins[j], "origines visibilite uniques");
		}
	return s_failCount == 0;
}

int main()
{
	TestProbeCount();
	TestIndexRoundTrip();
	TestWorldPos();
	TestAtlasDimensions();
	TestAtlasTileOrigins();
	if (s_failCount != 0)
	{
		std::cerr << "Total echecs : " << s_failCount << std::endl;
		return 1;
	}
	std::cout << "DdgiVolume tests : tout est passe." << std::endl;
	return 0;
}
