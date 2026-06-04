// M100.50 — Implémentation ConditionEvaluator.

#include "src/world_editor/wizard/ConditionEvaluator.h"

#include <cstdint>

namespace engine::editor::world::wizard
{
	namespace
	{
		void ReplaceAll(std::string& s, const std::string& from, const std::string& to)
		{
			if (from.empty()) return;
			size_t pos = 0;
			while ((pos = s.find(from, pos)) != std::string::npos)
			{
				s.replace(pos, from.size(), to);
				pos += to.size();
			}
		}

		std::string Trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t");
			if (a == std::string::npos) return "";
			size_t b = s.find_last_not_of(" \t");
			return s.substr(a, b - a + 1);
		}

		/// Valeur d'un champ de choix par nom (vide si inconnu).
		std::string FieldValue(const std::string& field, const WizardChoices& c)
		{
			if (field == "climate") return c.climate;
			if (field == "relief")  return c.relief;
			if (field == "coast")   return c.coast;
			if (field == "poi")     return c.poi;
			return "";
		}
	}

	std::string SubstituteVariables(const std::string& text, const WizardChoices& choices)
	{
		std::string out = text;
		ReplaceAll(out, "{{climate}}", choices.climate);
		ReplaceAll(out, "{{relief}}",  choices.relief);
		ReplaceAll(out, "{{coast}}",   choices.coast);
		ReplaceAll(out, "{{poi}}",     choices.poi);
		ReplaceAll(out, "{{seed}}",    std::to_string(choices.seed));
		return out;
	}

	bool EvaluateCondition(const std::string& condition, const WizardChoices& choices)
	{
		const std::string cond = Trim(condition);
		if (cond.empty()) return true;

		// Détecte l'opérateur (== ou !=).
		bool negate = false;
		size_t opPos = cond.find("==");
		if (opPos == std::string::npos)
		{
			opPos = cond.find("!=");
			if (opPos == std::string::npos) return true; // forme non reconnue → gardée.
			negate = true;
		}

		const std::string field = Trim(cond.substr(0, opPos));
		std::string rhs = Trim(cond.substr(opPos + 2));
		// Retire les quotes simples/doubles autour de la valeur.
		if (rhs.size() >= 2 && (rhs.front() == '\'' || rhs.front() == '"') && rhs.back() == rhs.front())
			rhs = rhs.substr(1, rhs.size() - 2);

		const bool equal = (FieldValue(field, choices) == rhs);
		return negate ? !equal : equal;
	}
}
