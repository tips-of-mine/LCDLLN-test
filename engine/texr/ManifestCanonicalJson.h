#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace lcdlln::manifest {

/// JSON canonical selon manifest_v1.md / keys_v1.md (objets : clés triées UTF-8, pas d'espaces).
[[nodiscard]] std::string CanonicalStringify(const nlohmann::json& j);

}  // namespace lcdlln::manifest
