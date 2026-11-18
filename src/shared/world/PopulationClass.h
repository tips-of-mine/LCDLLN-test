#pragma once
// POP.1 (axe 1 de la réflexion « plus de PNJ et de mobs sans surcoût CPU/GPU ») —
// classes de population.
//
// Chaque PNJ ou mob du jeu appartient à UNE classe, qui fixe ce qu'il coûte :
//
//   A  Interactive  PNJ interactif (service, quête, dialogue, vendeur).
//                   Toujours affiché en détail, jamais délesté.
//   B  Combat       Mob de combat : ciblable, attaquable, aggro. IA du shard
//                   complète (MobActivity : combat / calme / sommeil).
//   C  Extra        Figurant du shard (garde de ronde, porteur, pêcheur…) :
//                   visible par tous au même endroit, NON ciblable, sans effet
//                   de jeu. Routine ou agenda d'ambiance.
//   D  Crowd        Foule client (passants, badauds) : n'existe QUE sur le
//                   client, tirée d'une graine (cellule + heure). Aucun coût
//                   serveur ni réseau.
//   E  Fauna        Faune client (oiseaux, rats, poissons…) : comme D.
//
// RÈGLE D'OR : tout ce qui peut être ciblé, attaqué ou qui a un effet de jeu
// (quête, loot, commerce) reste en A ou en B. C, D et E sont du décor vivant.
//
// Ordre de PRIORITÉ (budget de rendu, délestage) : A > B > C > D > E. Sous
// contrainte, on dégrade d'abord E, puis D, puis C ; A et B ne disparaissent
// jamais.
//
// Module PUR (aucune dépendance) partagé client / shard / outils : testé seul
// (population_class_tests). Ce lot n'introduit que la CLASSIFICATION (données,
// journaux, métriques) : aucun comportement de jeu ne dépend encore de la classe.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace engine::world
{
	/// Classe de population d'un PNJ ou d'un mob (voir l'en-tête du fichier).
	/// Les valeurs numériques sont figées : elles pourront être écrites dans des
	/// formats binaires ou des payloads réseau (ajout uniquement en fin de liste).
	enum class PopulationClass : uint8_t
	{
		Interactive = 0, ///< A — PNJ interactif
		Combat = 1,      ///< B — mob de combat
		Extra = 2,       ///< C — figurant du shard
		Crowd = 3,       ///< D — foule client
		Fauna = 4,       ///< E — faune client
	};

	/// Nombre de classes (taille des tableaux indexés par classe).
	inline constexpr std::size_t kPopulationClassCount = 5;

	/// Indice 0..4 d'une classe (pour indexer un tableau de compteurs).
	constexpr std::size_t PopulationClassIndex(PopulationClass c)
	{
		return static_cast<std::size_t>(c);
	}

	/// Lettre de la classe ('A' à 'E') : forme courte des journaux et des métriques.
	constexpr char PopulationClassLetter(PopulationClass c)
	{
		return static_cast<char>('A' + static_cast<int>(c));
	}

	/// Nom en clair de la classe (minuscules, anglais) : forme longue des données JSON.
	constexpr std::string_view PopulationClassName(PopulationClass c)
	{
		switch (c)
		{
		case PopulationClass::Interactive: return "interactive";
		case PopulationClass::Combat: return "combat";
		case PopulationClass::Extra: return "extra";
		case PopulationClass::Crowd: return "crowd";
		case PopulationClass::Fauna: return "fauna";
		}
		return "combat";
	}

	/// Lit une classe depuis les données : la lettre ("A".."E", casse indifférente)
	/// ou le nom ("interactive", "combat", "extra", "crowd", "fauna").
	/// \return std::nullopt si la valeur est inconnue (l'appelant décide de la politique).
	constexpr std::optional<PopulationClass> ParsePopulationClass(std::string_view text)
	{
		if (text.size() == 1)
		{
			const char ch = (text[0] >= 'a' && text[0] <= 'z') ? static_cast<char>(text[0] - 'a' + 'A') : text[0];
			if (ch >= 'A' && ch <= 'E')
				return static_cast<PopulationClass>(ch - 'A');
			return std::nullopt;
		}
		for (std::size_t i = 0; i < kPopulationClassCount; ++i)
		{
			const PopulationClass c = static_cast<PopulationClass>(i);
			if (text == PopulationClassName(c))
				return c;
		}
		return std::nullopt;
	}

	/// Vrai si la classe peut être ciblée par un joueur (sélection, sort, attaque,
	/// dialogue). Seules A et B le sont (règle d'or).
	constexpr bool IsPopulationTargetable(PopulationClass c)
	{
		return c == PopulationClass::Interactive || c == PopulationClass::Combat;
	}

	/// Vrai si la classe existe sur le shard (simulée et répliquée) : A, B et C.
	/// D et E n'existent que sur le client.
	constexpr bool IsPopulationServerSide(PopulationClass c)
	{
		return c == PopulationClass::Interactive || c == PopulationClass::Combat || c == PopulationClass::Extra;
	}

	/// Vrai si la classe peut être retirée de l'écran sous contrainte de budget
	/// (C, D, E). A et B restent toujours affichés.
	constexpr bool IsPopulationSheddable(PopulationClass c)
	{
		return !IsPopulationTargetable(c);
	}

	/// Rang de priorité : 0 = le plus prioritaire (A) … 4 = le premier sacrifié (E).
	constexpr uint8_t PopulationPriorityRank(PopulationClass c)
	{
		return static_cast<uint8_t>(c);
	}

	/// Classe déduite d'un mob du shard quand ses données n'en déclarent pas :
	/// un PNJ d'ambiance (archétype behavior.kind = ambient) que les joueurs ne
	/// peuvent ni attaquer ni provoquer est un figurant (C) ; tout le reste est un
	/// mob de combat (B).
	/// \param ambient     archétype d'ambiance (CreatureArchetype::ambient)
	/// \param attackable  attaquable par les joueurs (CreatureArchetype::attackable)
	/// \param raceHostile hostile à certaines races (gardes de la Légion…)
	constexpr PopulationClass InferShardPopulationClass(bool ambient, bool attackable, bool raceHostile)
	{
		return (ambient && !attackable && !raceHostile) ? PopulationClass::Extra : PopulationClass::Combat;
	}

	/// Classe effective d'un mob du shard : la classe déclarée si elle respecte la
	/// règle d'or, sinon la classe déduite.
	///
	/// - Déclaration absente → classe déduite.
	/// - D ou E déclarée → refusée (n'existe pas sur le shard) → classe déduite.
	/// - C déclarée pour un mob attaquable ou hostile → refusée (un figurant n'a
	///   aucun effet de jeu) → B.
	/// - A ou B déclarée → acceptée (un PNJ d'ambiance peut être promu interactif).
	///
	/// \param declared   classe lue dans les données (spawner), si présente
	/// \param inferred   résultat d'InferShardPopulationClass pour ce mob
	/// \param attackable / raceHostile : propriétés de l'archétype
	/// \param[out] rejected vrai si la déclaration a été écartée (à journaliser)
	constexpr PopulationClass ResolveShardPopulationClass(std::optional<PopulationClass> declared,
		PopulationClass inferred, bool attackable, bool raceHostile, bool& rejected)
	{
		rejected = false;
		if (!declared.has_value())
			return inferred;
		const PopulationClass d = *declared;
		if (!IsPopulationServerSide(d))
		{
			rejected = true;
			return inferred;
		}
		if (d == PopulationClass::Extra && (attackable || raceHostile))
		{
			rejected = true;
			return PopulationClass::Combat;
		}
		return d;
	}

	/// Compteurs par classe (journaux, métriques). Indexé par PopulationClassIndex.
	using PopulationCounts = std::array<uint32_t, kPopulationClassCount>;
}
