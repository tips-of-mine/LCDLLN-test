#pragma once
// (Dé)sérialisation JSON de CharacterCustomization pour la colonne
// characters.appearance_json. Forward-compatible : l'ajout de clés en slice 2
// ne casse pas la lecture (clés inconnues ignorées).
#include "src/shared/network/CharacterPayloads.h"

#include <string>

namespace engine::character
{
    /// Sérialise en objet JSON compact (clé "v" = version de schéma = 1).
    std::string CustomizationToJson(const engine::network::CharacterCustomization& c);

    /// Parse un JSON appearance ; valeurs absentes/invalides → défauts. Robuste
    /// à `'{}'` et aux chaînes vides (lignes pré-slice-1).
    engine::network::CharacterCustomization CustomizationFromJson(const std::string& jsonStr);
}
