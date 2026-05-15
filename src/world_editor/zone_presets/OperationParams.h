#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace engine::editor::world::zone_presets
{
	/// Valeur d'un paramètre d'opération de zone preset (M100.46).
	///
	/// Le format JSON des opérations contient des scalaires (nombre,
	/// booléen, chaîne) et des listes de coordonnées (`polyline`,
	/// `polygon`, `sources` = `[[x,z],…]` ; `worldPosition`, `pillarA` =
	/// `[x,y,z]`). Les listes sont **aplaties** en `vector<double>` — pas
	/// besoin d'un DOM JSON récursif : les params d'opération sont au
	/// plus « tableau de tableaux de nombres ».
	using OperationParamValue =
		std::variant<double, bool, std::string, std::vector<double>>;

	/// Sac de paramètres d'une opération, extrait du `rawJson` d'une
	/// `ZonePresetOperation` (M100.46 incrément 2a). Niveau 1 uniquement :
	/// les clés top-level de l'objet opération. Les clés structurelles
	/// `type` / `preset` / `affectedBy` sont ignorées (déjà parsées en
	/// structuré côté `ZonePresetOperation`).
	class OperationParams
	{
	public:
		/// Parse les paramètres depuis le texte JSON d'un objet opération.
		/// Tolérant : une valeur non reconnue (objet imbriqué, null) est
		/// simplement ignorée.
		static OperationParams Parse(const std::string& rawJson);

		bool Has(const std::string& key) const;
		size_t Size() const { return m_values.size(); }

		/// Lecteurs typés. \return false si la clé est absente ou d'un
		/// autre type — `out` est alors laissé inchangé.
		bool GetNumber(const std::string& key, double& out) const;
		bool GetBool(const std::string& key, bool& out) const;
		bool GetString(const std::string& key, std::string& out) const;
		bool GetNumberList(const std::string& key, std::vector<double>& out) const;

		/// Multiplie la valeur numérique de `key` par `factor`, in-place.
		/// No-op si la clé est absente ou n'est pas un nombre. Utilisé par
		/// `CustomizationApplier`.
		void ScaleNumber(const std::string& key, double factor);

		const std::unordered_map<std::string, OperationParamValue>& Raw() const
		{
			return m_values;
		}

	private:
		std::unordered_map<std::string, OperationParamValue> m_values;
	};
}
