#pragma once

#include <cstdint>
#include <string>

namespace engine::client
{
	/// Décomposition d'un montant en pièces or / argent / bronze.
	/// Retour joueur 2026-07-08 : intégrer l'unité « bronze » comme base, avec
	/// 100 bronze = 1 argent et 100 argent = 1 or.
	struct CoinBreakdown
	{
		uint32_t gold = 0;   ///< 1 or   = 100 argent = 10 000 bronze.
		uint32_t silver = 0; ///< 0..99  argent.
		uint32_t bronze = 0; ///< 0..99  bronze.
	};

	/// Décompose un total exprimé en BRONZE (unité de base) en or/argent/bronze.
	/// La valeur stockée côté modèle (UIWalletState.gold, UIQuestEntry.rewardGold)
	/// est réinterprétée comme un total en bronze — 100% client, aucun changement
	/// de protocole. Ex. 10075 -> { or=1, argent=0, bronze=75 }.
	inline CoinBreakdown SplitCoins(uint32_t totalBronze)
	{
		CoinBreakdown coins{};
		coins.gold = totalBronze / 10000u;
		coins.silver = (totalBronze / 100u) % 100u;
		coins.bronze = totalBronze % 100u;
		return coins;
	}

	/// Texte compact d'un montant en bronze : n'affiche un palier supérieur que
	/// s'il est non nul (ou impliqué par un palier plus haut) ; le bronze est
	/// toujours montré. Ex. 75 -> « 75 br » ; 10075 -> « 1 or 0 arg 75 br ».
	inline std::string FormatCoinsText(uint32_t totalBronze)
	{
		const CoinBreakdown coins = SplitCoins(totalBronze);
		std::string out;
		if (coins.gold > 0u)
			out += std::to_string(coins.gold) + " or ";
		if (coins.gold > 0u || coins.silver > 0u)
			out += std::to_string(coins.silver) + " arg ";
		out += std::to_string(coins.bronze) + " br";
		return out;
	}
}
