// ServerApp — équipement : bonus d'équipement, statistiques dérivées, équiper / déséquiper.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	// ------------------------------------------------------------------------
	// Chantier 2 SP-A — équipement d'objets (serveur autoritaire)
	// ------------------------------------------------------------------------

	engine::items::StatBonus ServerApp::SumEquipmentBonus(const ConnectedClient& client) const
	{
		engine::items::StatBonus total{};
		if (!m_itemCatalogLoaded)
		{
			return total;
		}
		for (std::size_t slot = 1; slot < client.equipment.size(); ++slot)
		{
			const uint32_t itemId = client.equipment[slot];
			if (itemId == 0u)
			{
				continue;
			}
			if (const engine::items::ItemDefinition* def = m_itemCatalog.Find(itemId))
			{
				total += def->bonus;
			}
		}
		return total;
	}

	void ServerApp::ApplyEquipmentBonus(const ConnectedClient& client, engine::server::gameplay::DerivedStats& d) const
	{
		const engine::items::StatBonus b = SumEquipmentBonus(client);
		// Champs uint32 : additionne en int64 puis clamp ≥ 0 (bonus négatif possible).
		const auto addU = [](uint32_t base, int32_t delta) -> uint32_t {
			const int64_t v = static_cast<int64_t>(base) + static_cast<int64_t>(delta);
			return v < 0 ? 0u : static_cast<uint32_t>(v);
		};
		d.hp          = addU(d.hp, b.hp);
		d.resource    = addU(d.resource, b.resource);
		d.damage      = addU(d.damage, b.damage);
		d.stamina     = addU(d.stamina, b.stamina);
		d.accuracy    = std::max(0.0f, d.accuracy + static_cast<float>(b.accuracy));
		d.range       = std::max(0.0f, d.range + b.range);
		d.critRate    = std::max(0.0f, d.critRate + b.critRate);
		d.critMult    = std::max(0.0f, d.critMult + b.critMult);
		d.speedWalk   = std::max(0.0f, d.speedWalk + b.speedWalk);
		d.speedRun    = std::max(0.0f, d.speedRun + b.speedRun);
		d.speedSprint = std::max(0.0f, d.speedSprint + b.speedSprint);
		d.perception  = std::max(0.0f, d.perception + static_cast<float>(b.perception));
		d.stealth     = std::max(0.0f, d.stealth + static_cast<float>(b.stealth));
	}

	void ServerApp::RefreshLiveDerivedStats(ConnectedClient& client)
	{
		if (!m_statsTables) return;
		if (client.factionId.empty() || client.classId.empty()) return;
		const engine::server::gameplay::Sex sex =
			(client.gender == "female") ? engine::server::gameplay::Sex::Female
			                            : engine::server::gameplay::Sex::Male;
		auto d = engine::server::gameplay::ComputeStats(
			*m_statsTables, client.factionId, client.classId, sex, client.level);
		if (!d) return;
		ApplyEquipmentBonus(client, *d);
		// Roadmap-2 (2026-07-19) — les auras de stats (%PV max, %vitesse)
		// s'appliquent APRÈS l'équipement (mêmes règles que SendPlayerStats).
		ApplyAuraStatModifiers(client.auras, *d);

		ApplyDerivedCombatStats(client, *d);
		// ETAPE3.3 — vitesse maximale autoritaire du mouvement (allure la plus rapide).
		{
			const float fastest = (std::max)({ d->speedWalk, d->speedRun, d->speedSprint });
			// ADM.1 — multiplicateur de MJ (/speed, monture, gel) conservé à chaque
			// recalcul (équipement, auras, niveau).
			if (std::isfinite(fastest) && fastest > 0.1f)
				client.maxMoveSpeed = fastest * client.gm.EffectiveSpeedMultiplier();
		}
		client.maxResource = d->resource;
		if (client.currentResource > client.maxResource)
		{
			client.currentResource = client.maxResource;
		}
		// PV max = hp dérivé (bonus inclus) ; on préserve les PV courants (pas de
		// soin en équipant), seulement clampés sur le nouveau max.
		client.stats.maxHealth = d->hp;
		if (client.stats.currentHealth > client.stats.maxHealth)
		{
			client.stats.currentHealth = client.stats.maxHealth;
		}
		// Roadmap-2 (2026-07-19) — déclare à l'anti-cheat le multiplicateur de
		// vitesse LÉGITIME (buffs %MoveSpeed) pour éviter les faux SpeedHack.
		if (client.persistenceCharacterKey != 0u)
		{
			const float spdPct = SumAuraPercent(client.auras, SpellEffectType::MoveSpeedPercent);
			// ADM.1 — le multiplicateur de MJ (/speed, monture) s'y ajoute.
			m_antiCheat.SetPlayerSpeedMultiplier(client.persistenceCharacterKey,
				std::max(0.1f, (1.0f + spdPct / 100.0f) * (std::max)(client.gm.EffectiveSpeedMultiplier(), 0.1f)));
		}
	}

	bool ServerApp::SendEquipmentUpdate(const ConnectedClient& receiver)
	{
		EquipmentUpdateMessage message{};
		message.clientId = receiver.clientId;
		std::vector<EquipmentEntry> entries;
		for (std::size_t slot = 1; slot < receiver.equipment.size(); ++slot)
		{
			if (receiver.equipment[slot] != 0u)
			{
				entries.push_back(EquipmentEntry{static_cast<uint8_t>(slot), receiver.equipment[slot]});
			}
		}
		const std::vector<std::byte> packet = EncodeEquipmentUpdate(message, entries);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] EquipmentUpdate send failed (client_id={})", receiver.clientId);
			return false;
		}
		LOG_INFO(Net, "[ServerApp] EquipmentUpdate sent (client_id={}, worn={})",
			receiver.clientId, entries.size());
		return true;
	}

	void ServerApp::HandleEquipRequest(const Endpoint& endpoint, uint32_t clientId, uint32_t itemId)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] EquipRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] EquipRequest ignored: client_id mismatch (expected={}, got={})",
				client->clientId, clientId);
			return;
		}
		if (!m_itemCatalogLoaded)
		{
			LOG_WARN(Net, "[ServerApp] EquipRequest rejeté: catalogue d'objets non chargé (client_id={})", clientId);
			return;
		}

		// Slot ET bonus proviennent du catalogue serveur (jamais du client) : anti-triche.
		const engine::items::ItemDefinition* def = m_itemCatalog.Find(itemId);
		if (def == nullptr || !def->IsEquippable())
		{
			LOG_WARN(Net, "[ServerApp] EquipRequest rejeté: objet inconnu/non équipable (client_id={}, item_id={})",
				clientId, itemId);
			return;
		}
		const std::size_t slot = static_cast<std::size_t>(def->slot);
		if (slot == 0u || slot >= client->equipment.size())
		{
			LOG_WARN(Net, "[ServerApp] EquipRequest rejeté: slot hors borne (client_id={}, item_id={}, slot={})",
				clientId, itemId, slot);
			return;
		}

		// L'objet doit être présent dans le sac (anti-triche : on ne peut équiper
		// que ce qu'on possède).
		bool owned = false;
		for (const ItemStack& stack : client->inventory)
		{
			if (stack.itemId == itemId && stack.quantity >= 1u)
			{
				owned = true;
				break;
			}
		}
		if (!owned)
		{
			LOG_WARN(Net, "[ServerApp] EquipRequest rejeté: objet absent du sac (client_id={}, item_id={})",
				clientId, itemId);
			return;
		}

		std::string error;
		if (!RemoveStackFromInventory(*client, itemId, 1u, error))
		{
			LOG_WARN(Net, "[ServerApp] EquipRequest échec retrait sac (client_id={}, item_id={}, err={})",
				clientId, itemId, error);
			return;
		}

		// Renvoie au sac l'objet déjà porté sur ce slot (échange).
		const uint32_t previous = client->equipment[slot];
		if (previous != 0u)
		{
			AddItemToInventory(*client, ItemStack{previous, 1u});
		}
		client->equipment[slot] = itemId;

		LOG_INFO(Net, "[ServerApp] Équipement (client_id={}, item_id={}, slot={}, remplacé={})",
			clientId, itemId, slot, previous);

		SaveConnectedClient(*client, "equip");
		RefreshLiveDerivedStats(*client); // combat/PV/ressource refletent le nouvel equipement
		(void)SendInventoryDelta(*client, client->inventory); // snapshot complet
		(void)SendEquipmentUpdate(*client);
		(void)SendPlayerStats(*client);
		// Ceinture v2 — équiper une ceinture (slot Waist) change la capacité
		// active : repousser le layout (le client redimensionne sa barre).
		if (slot == static_cast<uint8_t>(engine::items::EquipmentSlot::Waist))
			(void)SendBeltLayout(*client);
	}

	void ServerApp::HandleUnequipRequest(const Endpoint& endpoint, uint32_t clientId, uint8_t slot)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] UnequipRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] UnequipRequest ignored: client_id mismatch (expected={}, got={})",
				client->clientId, clientId);
			return;
		}
		const std::size_t slotIndex = static_cast<std::size_t>(slot);
		if (slotIndex == 0u || slotIndex >= client->equipment.size())
		{
			LOG_WARN(Net, "[ServerApp] UnequipRequest rejeté: slot hors borne (client_id={}, slot={})",
				clientId, slotIndex);
			return;
		}
		const uint32_t itemId = client->equipment[slotIndex];
		if (itemId == 0u)
		{
			LOG_WARN(Net, "[ServerApp] UnequipRequest ignoré: slot déjà vide (client_id={}, slot={})",
				clientId, slotIndex);
			return;
		}

		// Retour joueur 2026-07-21 — refus si le sac est PLEIN et que l'objet
		// retiré exigerait une nouvelle pile (la grille client est bornée à
		// 8×8 = 64 piles, cf. kInventoryStacksMax : la 65e serait invisible).
		// Le client fait la même pré-vérification et affiche le motif à
		// l'écran ; cette garde serveur est la défense d'autorité.
		bool hasExistingStack = false;
		for (const ItemStack& stack : client->inventory)
			if (stack.itemId == itemId) { hasExistingStack = true; break; }
		if (!hasExistingStack && client->inventory.size() >= kInventoryStacksMax)
		{
			LOG_WARN(Net,
				"[ServerApp] UnequipRequest rejeté: sac plein (client_id={}, item_id={}, piles={})",
				clientId, itemId, client->inventory.size());
			return;
		}

		AddItemToInventory(*client, ItemStack{itemId, 1u});
		client->equipment[slotIndex] = 0u;
		// Retour joueur 2026-07-21 — retirer la CEINTURE vide sa barre : les
		// cases ne sont que des RÉFÉRENCES vers les consommables du sac (les
		// objets y sont donc déjà — « ils retournent à l'inventaire » sans
		// mouvement physique). Plus de contenu « inactif conservé » : rééquiper
		// une ceinture donne des cases vides à réorganiser.
		if (slotIndex == static_cast<size_t>(engine::items::EquipmentSlot::Waist))
			client->beltLayout.clear();

		LOG_INFO(Net, "[ServerApp] Déséquipement (client_id={}, item_id={}, slot={})",
			clientId, itemId, slotIndex);

		SaveConnectedClient(*client, "unequip");
		RefreshLiveDerivedStats(*client); // combat/PV/ressource refletent le nouvel equipement
		(void)SendInventoryDelta(*client, client->inventory); // snapshot complet
		(void)SendEquipmentUpdate(*client);
		(void)SendPlayerStats(*client);
		// Ceinture — le retrait vide la barre (capacité 0 sans ceinture) :
		// pousser le layout autoritaire (kind 100, count 0).
		if (slotIndex == static_cast<size_t>(engine::items::EquipmentSlot::Waist))
			(void)SendBeltLayout(*client);
	}
}
