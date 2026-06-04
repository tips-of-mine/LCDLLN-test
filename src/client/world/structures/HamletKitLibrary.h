#pragma once

// M100.31 — Bibliothèque de kits d'hameau (par race/biome). Parsing JSON pur.

#include <string>
#include <utility>
#include <vector>

namespace engine::world::structures
{
	struct HamletKit
	{
		std::string race;
		std::string biome;
		std::vector<std::pair<std::string, float>> houses; // mesh, weight
		float minSpacingDefault = 8.0f;
		float footprintRadius = 5.0f;
	};

	/// Parse un kit JSON (`assets/structures/kits/<name>.json`). Tolérant aux
	/// clés inconnues ; renvoie false + `err` en cas d'échec structurel.
	bool ParseHamletKitJson(const std::string& jsonText, HamletKit& out, std::string& err);
}
