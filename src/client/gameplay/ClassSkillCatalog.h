#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// SP-A — métadonnées d'affichage d'une compétence par-classe (client). Le
	/// client ne lit QUE l'affichage ; la résolution des effets reste serveur (SP-C).
	struct ClassSkillDisplay
	{
		std::string skillId;
		std::string name;
		std::string branch;           ///< "single" / "aoe" / "def".
		uint32_t tier = 1;
		uint32_t level = 1;
		std::string effectKind;       ///< "Damage" / "Heal" / "Defense" (brut).
		std::string target;           ///< "SingleEnemy" / "AreaAroundSelf" / "SingleAlly".
		float powerValue = 1.0f;
		float rangeMeters = 0.0f;
		float areaRadiusMeters = 0.0f;
		uint32_t castTimeMs = 0;
		uint32_t cooldownMs = 0;
		uint32_t resourceCostPercent = 0;
		std::string description;
	};

	/// SP-A — catalogue client des compétences par-classe (mêmes fichiers
	/// `gameplay/class_skills/*.json`). Politique TOLÉRANTE : fichier absent/invalide
	/// = catalogue vide + LOG_WARN (le serveur reste autorité).
	class ClassSkillCatalog final
	{
	public:
		/// Charge tous les `gameplay/class_skills/*.json`. Retourne false si rien
		/// n'est lisible (non bloquant).
		bool Init(const engine::core::Config& config);

		/// Retourne les skills d'une classe (triés par level), ou nullptr.
		const std::vector<ClassSkillDisplay>* GetClassSkills(std::string_view classId) const;

		/// Nombre de classes chargées (0 si vide).
		size_t ClassCount() const { return m_classes.size(); }

		/// Variante testable sans I/O : parse le JSON d'UNE classe.
		bool LoadClassFromText(std::string_view jsonText, std::string& outError);

	private:
		std::unordered_map<std::string, std::vector<ClassSkillDisplay>> m_classes;
	};
}
