#pragma once
// Validation autoritaire (serveur) d'une CharacterCustomization contre le catalogue.
#include "src/shared/Character/CustomizationCatalog.h"
#include "src/shared/network/CharacterPayloads.h"

#include <string>
#include <vector>

namespace engine::character
{
    struct ValidationResult
    {
        bool ok = true;
        std::vector<std::string> errors;
    };

    /// Valide \p c pour la race \p raceId. ok=false si la race est inconnue ou si
    /// un index/clé est hors limites. Les messages d'erreur sont anglais (logs).
    ValidationResult ValidateCustomization(const CustomizationCatalog& catalog,
                                           const std::string& raceId,
                                           const engine::network::CharacterCustomization& c);
}
