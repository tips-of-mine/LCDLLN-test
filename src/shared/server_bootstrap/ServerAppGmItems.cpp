// ADM.4 — Commandes d'objets et d'argent des maîtres de jeu (shard).
//
// /additem, /removeitem, /money, /inventory, /equipment, /clearbag, /itemsearch.
//
// Les objets viennent du catalogue autoritaire du shard (m_itemCatalog, mêmes
// identifiants que le client). Un objet se désigne par son numéro ou par son nom
// (sans casse ni accents, début ou partie du nom). Le sac est renvoyé en entier
// au client (InventoryDelta = instantané complet) ; la bourse par WalletUpdate.
// Chaque changement est sauvegardé.

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include "src/shardd/gameplay/economy/PlayerWalletService.h"
#include "src/shardd/gameplay/gm/GmPlaceDirectory.h"

#include <algorithm>

namespace engine::server
{
	namespace
	{
		/// Quantité maximale d'un /additem.
		constexpr int64_t kMaxGrantQuantity = 10000;
		/// Montant maximal d'un /money (en valeur absolue).
		constexpr int64_t kMaxMoneyDelta = 1000000000;
	}

	std::vector<const engine::items::ItemDefinition*> GmCommandExecutor::FindItems(std::string_view query, size_t maxResults) const
	{
		std::vector<const engine::items::ItemDefinition*> out;
		if (const auto id = engine::admin::ParseInt(query); id && *id > 0)
		{
			if (const auto* def = app.m_itemCatalog.Find(static_cast<uint32_t>(*id)))
				out.push_back(def);
			return out;
		}
		const std::string folded = gm::FoldPlaceText(query);
		if (folded.empty())
			return out;
		std::vector<std::pair<int, const engine::items::ItemDefinition*>> scored;
		for (const auto& [id, def] : app.m_itemCatalog.All())
		{
			(void)id;
			const std::string name = gm::FoldPlaceText(def.name);
			int score = -1;
			if (name == folded)
				score = 0;
			else if (name.rfind(folded, 0) == 0)
				score = 1;
			else if (name.find(folded) != std::string::npos)
				score = 2;
			if (score >= 0)
				scored.emplace_back(score, &def);
		}
		std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
			return a.first != b.first ? a.first < b.first : a.second->id < b.second->id;
		});
		for (const auto& [score, def] : scored)
		{
			(void)score;
			if (out.size() >= maxResults)
				break;
			out.push_back(def);
		}
		return out;
	}

	std::string GmCommandExecutor::ItemLabel(uint32_t itemId) const
	{
		const auto* def = app.m_itemCatalog.Find(itemId);
		return (def != nullptr && !def->name.empty() ? def->name : std::string("objet")) + " #" + std::to_string(itemId);
	}

	bool GmCommandExecutor::ParseItemCommand(std::string_view syntax, const engine::items::ItemDefinition*& outItem,
		uint32_t& outQuantity, ConnectedClient*& outTarget)
	{
		// « <objet> [quantité] [joueur] » ; l'objet peut faire plusieurs mots s'il est
		// entre guillemets. Quantité : premier nombre après l'objet.
		if (args.empty())
		{
			Usage(syntax);
			return false;
		}
		const auto items = FindItems(args[0], 6u);
		if (items.empty())
		{
			Fail(GmCommandStatus::NotFound, "Objet inconnu : « " + args[0] + " » (voir /itemsearch).");
			return false;
		}
		if (items.size() > 1u && gm::FoldPlaceText(items[0]->name) != gm::FoldPlaceText(args[0]))
		{
			std::string list = "Plusieurs objets correspondent :";
			for (const auto* def : items)
				list += " " + def->name + " (#" + std::to_string(def->id) + ")";
			Fail(GmCommandStatus::Usage, list + " — précisez le nom ou le numéro.");
			return false;
		}
		outItem = items[0];
		outQuantity = 1u;
		size_t targetIndex = 1;
		if (args.size() >= 2u)
		{
			if (const auto q = engine::admin::ParseInt(args[1]))
			{
				if (*q < 1 || *q > kMaxGrantQuantity)
				{
					Usage(std::string(syntax) + " (quantité 1 à 10000)");
					return false;
				}
				outQuantity = static_cast<uint32_t>(*q);
				targetIndex = 2;
			}
		}
		outTarget = TargetOrSelf(targetIndex);
		return outTarget != nullptr;
	}

	void GmCommandExecutor::CmdAddItem()
	{
		const engine::items::ItemDefinition* def = nullptr;
		uint32_t quantity = 0;
		ConnectedClient* target = nullptr;
		if (!ParseItemCommand("/additem <objet|n°> [quantité] [joueur]", def, quantity, target))
			return;
		const bool newStack = std::none_of(target->inventory.begin(), target->inventory.end(),
			[&](const ItemStack& s) { return s.itemId == def->id; });
		if (newStack && target->inventory.size() >= kInventoryStacksMax)
			return Fail(GmCommandStatus::Denied, "Le sac de " + NameOf(*target) + " est plein (" + std::to_string(kInventoryStacksMax) + " piles).");
		const ItemStack stack{ def->id, quantity };
		app.AddItemToInventory(*target, stack);
		const ItemStack changed[] = { stack };
		(void)app.SendInventoryDelta(*target, changed);
		app.SaveConnectedClient(*target, "gm_additem");
		Say(std::to_string(quantity) + " × " + ItemLabel(def->id) + " ajouté(s) au sac de " + NameOf(*target) + ".");
		NotifyTarget(*target, "Un maître de jeu vous a donné " + std::to_string(quantity) + " × " + def->name + ".");
	}

	void GmCommandExecutor::CmdRemoveItem()
	{
		const engine::items::ItemDefinition* def = nullptr;
		uint32_t quantity = 0;
		ConnectedClient* target = nullptr;
		if (!ParseItemCommand("/removeitem <objet|n°> [quantité] [joueur]", def, quantity, target))
			return;
		uint32_t owned = 0;
		for (const ItemStack& s : target->inventory)
		{
			if (s.itemId == def->id)
				owned += s.quantity;
		}
		if (owned == 0u)
			return Fail(GmCommandStatus::NotFound, NameOf(*target) + " n'a pas de " + def->name + " dans son sac.");
		const uint32_t removed = (std::min)(owned, quantity);
		std::string error;
		if (!app.RemoveStackFromInventory(*target, def->id, removed, error))
			return Fail(GmCommandStatus::Failed, "Retrait impossible : " + error);
		const ItemStack changed[] = { ItemStack{ def->id, removed } };
		(void)app.SendInventoryDelta(*target, changed);
		app.SaveConnectedClient(*target, "gm_removeitem");
		Say(std::to_string(removed) + " × " + ItemLabel(def->id) + " retiré(s) du sac de " + NameOf(*target) + ".");
	}

	void GmCommandExecutor::CmdMoney()
	{
		// « <±montant> [monnaie] [joueur] » ; monnaie : gold (défaut), honor, badges, premium.
		if (args.empty())
			return Usage("/money <+/-montant> [gold|honor|badges|premium] [joueur]");
		const auto amount = engine::admin::ParseInt(args[0]);
		if (!amount || *amount == 0 || *amount > kMaxMoneyDelta || *amount < -kMaxMoneyDelta)
			return Usage("/money <+/-montant> [gold|honor|badges|premium] [joueur] (montant non nul, 1 milliard au plus)");
		uint8_t currency = kCurrencyGold;
		const char* currencyLabel = "or";
		size_t targetIndex = 1;
		if (args.size() >= 2u)
		{
			const std::string c = engine::admin::ToLowerAscii(args[1]);
			if (c == "gold" || c == "or") { currency = kCurrencyGold; currencyLabel = "or"; targetIndex = 2; }
			else if (c == "honor" || c == "honneur") { currency = kCurrencyHonor; currencyLabel = "honneur"; targetIndex = 2; }
			else if (c == "badges" || c == "badge" || c == "insignes") { currency = kCurrencyBadges; currencyLabel = "insignes"; targetIndex = 2; }
			else if (c == "premium") { currency = kCurrencyPremium; currencyLabel = "monnaie premium"; targetIndex = 2; }
		}
		ConnectedClient* target = TargetOrSelf(targetIndex);
		if (target == nullptr)
			return;
		std::string error;
		const bool ok = *amount > 0
			? app.TryAddCurrency(target->clientId, currency, static_cast<uint64_t>(*amount), error)
			: app.TrySubtractCurrency(target->clientId, currency, static_cast<uint64_t>(-*amount), error);
		if (!ok)
			return Fail(GmCommandStatus::Denied, "Opération refusée (" + error + ") : plafond atteint ou solde insuffisant.");
		const uint32_t balance = currency == kCurrencyGold ? target->gold
			: currency == kCurrencyHonor ? target->honor
			: currency == kCurrencyBadges ? target->badges : target->premiumCurrency;
		Say(std::string(*amount > 0 ? "+" : "") + std::to_string(*amount) + " " + currencyLabel + " pour " + NameOf(*target)
			+ " — solde " + std::to_string(balance) + ".");
		NotifyTarget(*target, std::string("Un maître de jeu a modifié votre bourse : ") + (*amount > 0 ? "+" : "")
			+ std::to_string(*amount) + " " + currencyLabel + ".");
	}

	void GmCommandExecutor::CmdInventory()
	{
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		Say("Sac de " + NameOf(*target) + " : " + std::to_string(target->inventory.size()) + "/" + std::to_string(kInventoryStacksMax)
			+ " pile(s) — or " + std::to_string(target->gold) + ", honneur " + std::to_string(target->honor)
			+ ", insignes " + std::to_string(target->badges) + ".");
		for (size_t i = 0; i < target->inventory.size() && i < 40u; ++i)
			Say("  " + std::to_string(target->inventory[i].quantity) + " × " + ItemLabel(target->inventory[i].itemId));
		if (target->inventory.size() > 40u)
			Say("  … et " + std::to_string(target->inventory.size() - 40u) + " autre(s) pile(s).");
	}

	void GmCommandExecutor::CmdEquipment()
	{
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		size_t count = 0;
		for (size_t slot = 1; slot < target->equipment.size(); ++slot)
		{
			if (target->equipment[slot] == 0u)
				continue;
			++count;
			Say(std::string("  ") + engine::items::ToString(static_cast<engine::items::EquipmentSlot>(slot)) + " : "
				+ ItemLabel(target->equipment[slot]));
		}
		Say("Équipement de " + NameOf(*target) + " : " + std::to_string(count) + " objet(s) porté(s).");
	}

	void GmCommandExecutor::CmdClearBag()
	{
		if (args.empty() || !engine::admin::EqualsIgnoreCase(args.back(), "confirm"))
			return Usage("/clearbag [joueur] confirm (vide le sac : l'équipement porté est conservé)");
		args.pop_back();
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		const size_t stacks = target->inventory.size();
		target->inventory.clear();
		(void)app.SendInventoryDelta(*target, std::span<const ItemStack>{});
		app.SaveConnectedClient(*target, "gm_clearbag");
		Say("Sac de " + NameOf(*target) + " vidé (" + std::to_string(stacks) + " pile(s) supprimée(s)).");
		NotifyTarget(*target, "Un maître de jeu a vidé votre sac.");
	}

	void GmCommandExecutor::CmdItemSearch()
	{
		if (args.empty())
			return Usage("/itemsearch <mot|n°>");
		const std::string query = engine::admin::JoinArgs(args, 0);
		const auto items = FindItems(query, 25u);
		if (items.empty())
			return Fail(GmCommandStatus::NotFound, "Aucun objet ne correspond à « " + query + " » ("
				+ std::to_string(app.m_itemCatalog.Count()) + " objets au catalogue).");
		Say(std::to_string(items.size()) + " objet(s) pour « " + query + " »" + (items.size() >= 25u ? " (25 premiers)" : "") + " :");
		for (const auto* def : items)
		{
			Say("  #" + std::to_string(def->id) + " " + def->name
				+ (def->IsEquippable() ? std::string(" — ") + engine::items::ToString(def->slot) : std::string()));
		}
	}
}
