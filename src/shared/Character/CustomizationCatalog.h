#pragma once
// Catalogue de personnalisation par race, data-driven. Lit game/data/races/races.json
// (ids + tailles de palettes couleurs) et game/data/races/customization/<id>.json
// (modules par frame + features raciales). Read-only après chargement.
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::character
{
    struct FrameModules
    {
        std::vector<std::string> bodyTypes;
        std::vector<std::string> faces;
        std::vector<std::string> hair;
        std::vector<std::string> facialHair;
    };

    struct RaceCustomization
    {
        std::vector<std::string> frames;                         ///< ex. ["masculine","feminine"]
        std::unordered_map<std::string, FrameModules> modules;   ///< frame -> modules
        std::unordered_map<std::string, std::vector<std::string>> racialFeatures; ///< key -> ids
        uint32_t skinColorCount = 0;                             ///< depuis races.json
        uint32_t hairColorCount = 0;
        uint32_t eyeColorCount  = 0;
    };

    class CustomizationCatalog
    {
    public:
        /// Charge depuis le dossier des races (\p racesDir doit contenir races.json
        /// et le sous-dossier customization/). Retourne true si au moins une race chargée.
        bool LoadFromDir(const std::string& racesDir);

        /// Insère/remplace une race (utilisé par le chargeur et par les tests).
        void Set(const std::string& raceId, RaceCustomization rc);

        /// Retourne la config d'une race ou nullptr.
        const RaceCustomization* Find(const std::string& raceId) const;

        /// Nombre de races chargées.
        std::size_t Size() const { return m_races.size(); }

    private:
        std::unordered_map<std::string, RaceCustomization> m_races;
    };
}
