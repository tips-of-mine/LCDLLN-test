#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lcdlln::manifest {

/// GET HTTPS ou lecture `file://` (tests / hors ligne). Retourne le corps brut.
bool FetchUrlBytes(std::string_view url, std::vector<std::uint8_t>& out, std::string& err);

}  // namespace lcdlln::manifest
