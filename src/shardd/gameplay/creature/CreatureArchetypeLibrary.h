#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server
{
	/// Combat SP1 — un archétype de créature data-driven chargé depuis
	/// `game/data/creatures/archetypes.json`. Porte les stats de combat du mob
	/// (consommées par ServerApp au spawn), la récompense d'XP au kill, et les
	/// informations d'apparence (mesh/scale/nom/niveau) que le client résout de
	/// son côté via le même fichier (cf. CreatureCatalog côté client).
	/// `accuracy`/`critRate`/`critMult` sont portés par le schéma dès SP1 mais ne
	/// seront consommés qu'à partir de SP2 (jets de précision/critique).
	struct CreatureArchetype
	{
		uint32_t archetypeId = 0;
		std::string name;
		uint32_t level = 1;
		uint32_t hp = 1;
		uint32_t damage = 0;
		float accuracy = 100.0f;
		float rangeMeters = 2.0f;
		float critRate = 0.0f;
		float critMult = 1.5f;
		uint32_t attackPeriodMs = 2000;
		uint32_t xpReward = 0;
		/// Clé d'un set de mesh de race existant ("orcs", "humains", …) réutilisé
		/// pour le rendu du mob en attendant des assets créatures dédiés.
		std::string meshKey;
		float scale = 1.0f;
	};

	/// Combat SP1 — catalogue serveur des archétypes de créatures, résolu depuis
	/// `paths.content` (même politique stricte que SpawnerRuntime : fichier
	/// absent/illisible ou entrée invalide = échec d'Init, le shard ne boote pas
	/// avec un catalogue corrompu).
	class CreatureArchetypeLibrary final
	{
	public:
		/// Capture la config utilisée pour résoudre le JSON du catalogue.
		explicit CreatureArchetypeLibrary(const engine::core::Config& config);

		/// Charge et valide `creatures/archetypes.json` depuis le content root.
		/// Idempotent (warn + true si déjà initialisé). Retourne false si le
		/// fichier manque, ne parse pas, ou si une entrée est invalide (id
		/// dupliqué, hp/attackPeriodMs nuls, champs obligatoires manquants).
		bool Init();

		/// Retourne l'archétype demandé, ou nullptr s'il est inconnu.
		const CreatureArchetype* Find(uint32_t archetypeId) const;

		/// Nombre d'archétypes chargés (0 avant Init).
		size_t Count() const { return m_archetypes.size(); }

		/// Variante testable sans I/O : parse et valide un texte JSON en mémoire.
		/// Utilisée par Init() (qui lit le fichier) et par les tests unitaires.
		/// Remplace intégralement le contenu courant en cas de succès ; laisse le
		/// catalogue vide et renseigne outError en cas d'échec.
		bool LoadFromText(std::string_view jsonText, std::string& outError);

	private:
		engine::core::Config m_config;
		std::unordered_map<uint32_t, CreatureArchetype> m_archetypes;
		bool m_initialized = false;
	};
}
