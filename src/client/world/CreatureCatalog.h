#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::client
{
	/// Combat SP1 — apparence client d'un archétype de créature, résolue depuis le
	/// même `game/data/creatures/archetypes.json` que le serveur. Le client ne
	/// consomme que name/level/model (les stats de combat restent l'autorité du
	/// serveur, jamais relues ici).
	struct CreatureAppearance
	{
		std::string name;
		uint32_t level = 1;
		/// Clé d'un set de mesh de race existant ("orcs", "humains", …) réutilisé
		/// pour le rendu du mob en attendant des assets créatures dédiés.
		std::string meshKey;
		float scale = 1.0f;
	};

	/// Combat SP1 — catalogue client des archétypes de créatures (lecture seule,
	/// chargé une fois au boot). Politique TOLÉRANTE, contrairement au serveur :
	/// fichier absent/invalide = catalogue vide + LOG_WARN ; les mobs sont alors
	/// rendus avec le fallback (mesh humains, nom générique "Creature <id>").
	class CreatureCatalog final
	{
	public:
		/// Charge `creatures/archetypes.json` depuis `paths.content`. Retourne
		/// false (catalogue laissé vide) si le fichier est absent ou invalide —
		/// non bloquant côté client. Main thread uniquement (pas de verrou).
		bool Init(const engine::core::Config& config);

		/// Retourne l'apparence de l'archétype, ou nullptr s'il est inconnu.
		const CreatureAppearance* Find(uint32_t archetypeId) const;

		/// Nombre d'archétypes chargés (0 si le catalogue n'a pas pu être lu).
		size_t Count() const { return m_appearances.size(); }

		/// Variante testable sans I/O : parse et valide un texte JSON en mémoire.
		/// Remplace le contenu courant en cas de succès ; vide + outError sinon.
		bool LoadFromText(std::string_view jsonText, std::string& outError);

	private:
		std::unordered_map<uint32_t, CreatureAppearance> m_appearances;
	};
}
