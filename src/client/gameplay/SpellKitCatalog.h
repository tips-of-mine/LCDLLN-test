#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// Combat SP3 — métadonnées d'affichage d'un sort (barre d'action / cast bar).
	/// Le client ne lit QUE l'affichage : la résolution des effets reste serveur.
	struct SpellDisplay
	{
		std::string spellId;
		std::string name;
		/// Slot de la barre d'action [1,4].
		uint32_t slot = 1;
		uint32_t castTimeMs = 0;
		uint32_t cooldownMs = 0;
		uint32_t resourceCostPercent = 0;
		/// true si le sort exige une cible ennemie (targetType SingleEnemy).
		bool needsEnemyTarget = false;
	};

	/// Combat SP3 — catalogue client des kits de sorts (même
	/// `game/data/gameplay/spells/*.json` que le serveur). Politique TOLÉRANTE :
	/// fichier absent/invalide = catalogue vide + LOG_WARN, la barre d'action est
	/// simplement masquée (le serveur reste l'autorité de toute façon).
	class SpellKitCatalog final
	{
	public:
		/// Charge tous les `gameplay/spells/*.json` depuis `paths.content`.
		/// Retourne false (catalogue vide) si rien n'est lisible — non bloquant.
		bool Init(const engine::core::Config& config);

		/// Retourne le kit d'un profil (sorts triés par slot), ou nullptr.
		const std::vector<SpellDisplay>* FindKit(std::string_view profile) const;

		/// Retourne le nom d'affichage d'un sort (tous kits confondus), ou
		/// l'identifiant brut si inconnu (catalogue désynchronisé).
		std::string ResolveSpellName(std::string_view spellId) const;

		/// Nombre de profils chargés (0 si le catalogue n'a pas pu être lu).
		size_t KitCount() const { return m_kits.size(); }

		/// Variante testable sans I/O : parse le JSON d'UN kit.
		bool LoadKitFromText(std::string_view jsonText, std::string& outError);

	private:
		std::unordered_map<std::string, std::vector<SpellDisplay>> m_kits;
	};
}
