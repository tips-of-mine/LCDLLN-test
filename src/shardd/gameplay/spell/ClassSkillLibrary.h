#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// SP-A — catégorie d'effet d'une compétence par-classe (auto-contenu ; le
	/// mapping vers le moteur de combat est fait en SP-C, pas ici).
	enum class ClassSkillEffectKind : uint8_t { Damage = 0, Heal = 1, Defense = 2 };

	/// SP-A — type de cible d'une compétence par-classe.
	enum class ClassSkillTarget : uint8_t { SingleEnemy = 0, AreaAroundSelf = 1, SingleAlly = 2 };

	/// SP-A — une compétence par-classe (immuable après chargement).
	struct ClassSkillDef
	{
		std::string skillId;
		std::string name;
		std::string branch;          ///< "single" / "aoe" / "def".
		uint32_t tier = 1;           ///< 1..60.
		uint32_t level = 1;          ///< niveau de déblocage (= tier).
		ClassSkillEffectKind effectKind = ClassSkillEffectKind::Damage;
		ClassSkillTarget target = ClassSkillTarget::SingleEnemy;
		float powerValue = 1.0f;     ///< multiplicateur (≥ 1.0).
		float rangeMeters = 0.0f;
		float areaRadiusMeters = 0.0f;
		uint32_t castTimeMs = 0;
		uint32_t cooldownMs = 0;
		uint32_t resourceCostPercent = 0; ///< [5,60].
		std::string description;
	};

	/// SP-A — bibliothèque serveur des compétences par-classe, résolue depuis
	/// `paths.content` (`gameplay/class_skills/*.json`). Politique STRICTE (pattern
	/// SpellKitLibrary) : fichier illisible/invalide = échec d'Init.
	class ClassSkillLibrary final
	{
	public:
		explicit ClassSkillLibrary(const engine::core::Config& config);

		/// Charge et valide tous les `gameplay/class_skills/*.json`. Idempotent.
		bool Init();

		/// Retourne les skills d'une classe (triés par level), ou nullptr.
		const std::vector<ClassSkillDef>* GetClassSkills(std::string_view classId) const;

		/// Retourne un skill d'une classe, ou nullptr.
		const ClassSkillDef* FindSkill(std::string_view classId, std::string_view skillId) const;

		/// Nombre de classes chargées (0 avant Init).
		size_t ClassCount() const { return m_classes.size(); }

		/// Variante testable sans I/O : parse et valide le JSON d'UNE classe.
		bool LoadClassFromText(std::string_view jsonText, std::string& outError);

	private:
		engine::core::Config m_config;
		std::unordered_map<std::string, std::vector<ClassSkillDef>> m_classes;
		bool m_initialized = false;
	};
}
