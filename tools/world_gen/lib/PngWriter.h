#pragma once

#include <cstdint>
#include <vector>

namespace tools::world_gen
{
	/// Encode une image RGB 8 bits (rangées de haut en bas, `width*3` octets
	/// par rangée) en fichier PNG complet. Déterministe, sans dépendance.
	std::vector<uint8_t> EncodePngRgb(const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height);
}
