#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace lcdlln::manifest {

using SigningKeys = std::map<std::string, std::array<std::uint8_t, 32>>;

/// Valide keys.json (signature racine + chaîne delegations) avec K_embedded.
bool VerifyKeysJson(std::string_view keys_json_utf8, const std::array<std::uint8_t, 32>& k_embedded, SigningKeys& out_known,
                    std::string& err);

}  // namespace lcdlln::manifest
