// ServerApp — sorts : barres d'action, ceinture et consommables, compétences de classe, incantations, auras, attaques des monstres, buffs de gâteau.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	void ServerApp::HandleCastRequest(const Endpoint& endpoint, const CastRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored: client_id mismatch (expected={}, got={})",
				client->clientId, message.clientId);
			return;
		}

		if ((client->stateFlags & kEntityStateDead) != 0u)
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored: caster is dead (client_id={})", client->clientId);
			return;
		}

		// ── Activation d'un OBJET (jeton "item:<id>") — AVANT les chemins de
		// sorts (pas un spellId, aucun profil de classe requis). Gâteaux
		// (anniversaires SP3) et consommables de ceinture (Roadmap-3).
		{
			uint32_t tokenItemId = 0u;
			if (engine::anniversary::ParseItemToken(message.spellId, tokenItemId))
			{
				if (engine::anniversary::IsCakeItemId(tokenItemId))
					HandleCakeActivation(*client, tokenItemId);
				else
					HandleConsumableUse(*client, tokenItemId);
				return;
			}
		}

		if (client->profileId.empty())
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored: no class profile (client_id={})", client->clientId);
			return;
		}

		// ── SP-C : cast par compétence de classe (chemin additif, AVANT le fallback kit profil) ──
		// Conditions : le joueur a une classe résolue, le skillId est dans ses skills connus,
		// et la bibliothèque reconnaît ce skill pour sa classe.
		if (!client->classId.empty())
		{
			const bool skillKnown = std::find(
				client->knownSkillIds.begin(),
				client->knownSkillIds.end(),
				message.spellId) != client->knownSkillIds.end();
			const ClassSkillDef* cs = skillKnown
				? m_classSkills.FindSkill(client->classId, message.spellId)
				: nullptr;
			if (cs != nullptr)
			{
				// Garde : pas de cast en cours (partagée avec le chemin kit profil).
				if (!client->activeCastSpellId.empty())
				{
					LOG_WARN(Net, "[ServerApp] SP-C CastRequest ignored: cast already in progress (client_id={})",
						client->clientId);
					return;
				}

				const uint64_t nowMs = NowMonotonicMs();

				// Garde cooldown : même map spellCooldownUntilMs que le kit profil.
				const auto cooldownIt = client->spellCooldownUntilMs.find(cs->skillId);
				if (cooldownIt != client->spellCooldownUntilMs.end() && nowMs < cooldownIt->second)
				{
					LOG_WARN(Net, "[ServerApp] SP-C CastRequest ignored: skill on cooldown (client_id={}, skill={})",
						client->clientId, cs->skillId);
					return;
				}

				// Garde ressource.
				// ADM.3 — /nocost : sorts gratuits.
				const uint32_t cost = client->gm.noCost ? 0u : cs->resourceCostPercent * client->maxResource / 100u;
				if (client->currentResource < cost)
				{
					LOG_WARN(Net,
						"[ServerApp] SP-C CastRequest ignored: not enough resource (client_id={}, skill={}, cost={}, current={})",
						client->clientId, cs->skillId, cost, client->currentResource);
					return;
				}

				// Résolution de cible selon ClassSkillTarget.
				const float csRange = (cs->rangeMeters > 0.0f) ? cs->rangeMeters : client->combat.attackRangeMeters;
				EntityId csResolvedTarget = 0;
				if (cs->target == ClassSkillTarget::SingleEnemy)
				{
					MobEntity* target = FindMobByEntityId(message.targetEntityId);
					if (target == nullptr || !engine::world::cells::SameMap(target->zoneId, client->zoneId)
						|| (target->stateFlags & kEntityStateDead) != 0u || target->stats.currentHealth == 0)
					{
						LOG_WARN(Net,
							"[ServerApp] SP-C CastRequest ignored: invalid enemy target (client_id={}, target_entity_id={})",
							client->clientId, message.targetEntityId);
						return;
					}
					const float distSq = DistanceSquaredXZ(
						client->positionMetersX, client->positionMetersZ,
						target->positionMetersX, target->positionMetersZ);
					if (distSq > csRange * csRange)
					{
						LOG_WARN(Net, "[ServerApp] SP-C CastRequest ignored: target out of range (client_id={}, skill={})",
							client->clientId, cs->skillId);
						return;
					}
					csResolvedTarget = target->entityId;
				}
				else if (cs->target == ClassSkillTarget::SingleAlly)
				{
					// Défaut : soi ; un allié vivant du même groupe à portée est accepté.
					csResolvedTarget = client->entityId;
					if (message.targetEntityId != 0 && message.targetEntityId != client->entityId)
					{
						if (ConnectedClient* ally = FindClientByEntityId(message.targetEntityId))
						{
							const Party* casterParty = m_partySystem.FindPartyByMember(client->clientId);
							const bool sameParty = casterParty != nullptr
								&& m_partySystem.FindPartyByMember(ally->clientId) == casterParty;
							const float allyDistSq = DistanceSquaredXZ(
								client->positionMetersX, client->positionMetersZ,
								ally->positionMetersX, ally->positionMetersZ);
							// Seconde carte : jamais un allié d'une autre carte (même (x, z) possible).
							if (sameParty && (ally->stateFlags & kEntityStateDead) == 0u
								&& engine::world::cells::SameMap(ally->zoneId, client->zoneId)
								&& allyDistSq <= csRange * csRange)
							{
								csResolvedTarget = ally->entityId;
							}
						}
					}
				}
				// AreaAroundSelf : pas de cible explicite (rayon résolu à l'effet).

				// V1 : compétences de classe résolues instantanément (castTimeMs ignoré ;
				// barre de cast par-classe = amélioration future, cf. TickActiveCasts).
				// TickActiveCasts relit les sorts via m_spellKits.FindSpell, qui ne couvre
				// pas ClassSkillLibrary ; armer un cast à incantation SP-C produirait un
				// cast orphelin sans résolution. La barre de cast par-classe sera introduite
				// quand TickActiveCasts supportera nativement les ClassSkillDef.
				ResolveClassSkillCast(*client, *cs, csResolvedTarget);
				return;
			}
		}
		// ── Fin SP-C ── Fallback : kit profil SP3 (chemin inchangé) ──

		const SpellDef* spell = m_spellKits.FindSpell(client->profileId, message.spellId);
		if (spell == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored: spell '{}' not in kit '{}' (client_id={})",
				message.spellId, client->profileId, client->clientId);
			return;
		}

		if (!client->activeCastSpellId.empty())
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored: cast already in progress (client_id={})", client->clientId);
			return;
		}

		const uint64_t nowMs = NowMonotonicMs();
		const auto cooldownIt = client->spellCooldownUntilMs.find(spell->spellId);
		if (cooldownIt != client->spellCooldownUntilMs.end() && nowMs < cooldownIt->second)
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored: spell on cooldown (client_id={}, spell={})",
				client->clientId, spell->spellId);
			return;
		}

		// ADM.3 — /nocost : sorts gratuits.
		const uint32_t cost = client->gm.noCost ? 0u : spell->resourceCostPercent * client->maxResource / 100u;
		if (client->currentResource < cost)
		{
			LOG_WARN(Net, "[ServerApp] CastRequest ignored: not enough resource (client_id={}, spell={}, cost={}, current={})",
				client->clientId, spell->spellId, cost, client->currentResource);
			return;
		}

		// Résolution et validation de la cible selon le type du sort.
		// rangeMeters 0 = mêlée → portée d'auto-attaque du joueur.
		const float range = (spell->rangeMeters > 0.0f) ? spell->rangeMeters : client->combat.attackRangeMeters;
		EntityId resolvedTarget = 0;
		if (spell->targetType == SpellTargetKind::SingleEnemy)
		{
			MobEntity* target = FindMobByEntityId(message.targetEntityId);
			if (target == nullptr || !engine::world::cells::SameMap(target->zoneId, client->zoneId)
				|| (target->stateFlags & kEntityStateDead) != 0u || target->stats.currentHealth == 0)
			{
				LOG_WARN(Net, "[ServerApp] CastRequest ignored: invalid enemy target (client_id={}, target_entity_id={})",
					client->clientId, message.targetEntityId);
				return;
			}
			const float distanceSquared = DistanceSquaredXZ(
				client->positionMetersX, client->positionMetersZ,
				target->positionMetersX, target->positionMetersZ);
			if (distanceSquared > range * range)
			{
				LOG_WARN(Net, "[ServerApp] CastRequest ignored: target out of range (client_id={}, spell={})",
					client->clientId, spell->spellId);
				return;
			}
			resolvedTarget = target->entityId;
		}
		else if (spell->targetType == SpellTargetKind::SingleAlly)
		{
			// Défaut : soi. Un membre du même groupe vivant et à portée est accepté.
			resolvedTarget = client->entityId;
			if (message.targetEntityId != 0 && message.targetEntityId != client->entityId)
			{
				if (ConnectedClient* ally = FindClientByEntityId(message.targetEntityId))
				{
					const Party* casterParty = m_partySystem.FindPartyByMember(client->clientId);
					const bool sameParty = casterParty != nullptr
						&& m_partySystem.FindPartyByMember(ally->clientId) == casterParty;
					const float allyDistanceSquared = DistanceSquaredXZ(
						client->positionMetersX, client->positionMetersZ,
						ally->positionMetersX, ally->positionMetersZ);
					// Seconde carte : jamais un allié d'une autre carte (même (x, z) possible).
					if (sameParty && (ally->stateFlags & kEntityStateDead) == 0u
						&& engine::world::cells::SameMap(ally->zoneId, client->zoneId)
						&& allyDistanceSquared <= range * range)
					{
						resolvedTarget = ally->entityId;
					}
				}
			}
		}
		// SelfOnly / AreaAroundSelf : pas de cible (résolus autour du casteur).
		// ETAPE3.2 (chantier 4.5) — cible gardée dans les snapshots malgré le plafond.
		if (resolvedTarget != 0u)
			client->lastTargetEntityId = resolvedTarget;

		if (spell->castTimeMs == 0)
		{
			ResolveSpellCast(*client, *spell, resolvedTarget);
			return;
		}

		// Cast à incantation : armé, résolu/annulé par TickActiveCasts.
		client->activeCastSpellId = spell->spellId;
		client->castFinishAtMs = nowMs + spell->castTimeMs;
		client->castStartPosX = client->positionMetersX;
		client->castStartPosZ = client->positionMetersZ;
		client->castTargetEntityId = resolvedTarget;

		CastBarUpdateMessage castBar{};
		castBar.clientId = client->clientId;
		castBar.status = kCastBarStatusStart;
		castBar.durationMs = spell->castTimeMs;
		castBar.spellId = spell->spellId;
		(void)SendDatagram(client->endpoint, EncodeCastBarUpdate(castBar));
		LOG_INFO(Net, "[ServerApp] Cast started (client_id={}, spell={}, duration_ms={})",
			client->clientId, spell->spellId, spell->castTimeMs);
	}

	bool ServerApp::SendActionBarLayout(const ConnectedClient& client)
	{
		ActionBarLayoutUpdateMessage msg{};
		msg.clientId = client.clientId;
		msg.slots = client.actionBarLayout;
		const std::vector<std::byte> packet = EncodeActionBarLayoutUpdate(msg);
		if (!SendDatagram(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendActionBarLayout failed (client_id={})", client.clientId);
			return false;
		}
		return true;
	}

	void ServerApp::HandleSetActionBarLayout(const Endpoint& endpoint, const SetActionBarLayoutMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] SetActionBarLayout ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net, "[ServerApp] SetActionBarLayout ignored: client_id mismatch (expected={}, got={})",
				client->clientId, message.clientId);
			return;
		}

		// Validation autoritaire : chaque slot non vide ∈ kit du profil + unicité.
		std::array<std::string, 10> validated{};
		for (size_t slotIndex = 0; slotIndex < message.slots.size(); ++slotIndex)
		{
			const std::string& spellId = message.slots[slotIndex];
			if (spellId.empty())
			{
				continue; // slot vide autorisé
			}
			// Anniversaires SP3 (2026-07-18) — jeton "item:<id>" : un gâteau
			// possédé peut être slotté (unicité vérifiée comme les sorts).
			{
				uint32_t cakeItemId = 0u;
				if (engine::anniversary::ParseCakeToken(spellId, cakeItemId))
				{
					bool owned = false;
					for (const ItemStack& s : client->inventory)
					{
						if (s.itemId == cakeItemId && s.quantity > 0u) { owned = true; break; }
					}
					if (!owned)
					{
						LOG_WARN(Net, "[ServerApp] SetActionBarLayout reject: gateau {} non possede (client_id={})",
							cakeItemId, client->clientId);
						(void)SendActionBarLayout(*client);
						return;
					}
					bool duplicateToken = false;
					for (size_t prior = 0; prior < slotIndex; ++prior)
					{
						if (validated[prior] == spellId) { duplicateToken = true; break; }
					}
					if (duplicateToken)
					{
						(void)SendActionBarLayout(*client);
						return;
					}
					validated[slotIndex] = spellId;
					continue;
				}
			}
			if (client->profileId.empty()
				|| m_spellKits.FindSpell(client->profileId, spellId) == nullptr)
			{
				LOG_WARN(Net, "[ServerApp] SetActionBarLayout reject: spell '{}' not in kit '{}' (client_id={})",
					spellId, client->profileId, client->clientId);
				(void)SendActionBarLayout(*client); // renvoie l'état inchangé (réconciliation client)
				return;
			}
			// Unicité : un sort ne peut occuper deux slots.
			for (size_t prior = 0; prior < slotIndex; ++prior)
			{
				if (validated[prior] == spellId)
				{
					LOG_WARN(Net, "[ServerApp] SetActionBarLayout reject: duplicate spell '{}' (client_id={})",
						spellId, client->clientId);
					(void)SendActionBarLayout(*client);
					return;
				}
			}
			validated[slotIndex] = spellId;
		}

		client->actionBarLayout = validated;
		SaveConnectedClient(*client, "actionbar_change");
		(void)SendActionBarLayout(*client); // ACK autoritaire (layout validé)
		LOG_INFO(Net, "[ServerApp] SetActionBarLayout applied (client_id={})", client->clientId);
	}

	void ServerApp::HandleSetBeltLayout(const Endpoint& endpoint, const SetBeltLayoutMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] SetBeltLayout ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net, "[ServerApp] SetBeltLayout ignored: client_id mismatch (expected={}, got={})",
				client->clientId, message.clientId);
			return;
		}

		// Ceinture v2 — le count client ne peut pas dépasser la capacité
		// ACTIVE (ceinture équipée en Waist, autoritaire côté serveur).
		const size_t capacity = BeltCapacity(*client);
		if (message.slots.size() > capacity)
		{
			LOG_WARN(Net, "[ServerApp] SetBeltLayout reject: count {} > capacite {} (client_id={})",
				message.slots.size(), capacity, client->clientId);
			(void)SendBeltLayout(*client);
			return;
		}

		// Validation autoritaire : chaque slot non vide = jeton "item:<id>"
		// d'un objet POSSÉDÉ et ACTIVABLE (gâteau ou consommable connu),
		// unicité par slot. Rejet = renvoi de l'état inchangé (kind 100).
		std::vector<std::string> validated(message.slots.size());
		for (size_t slotIndex = 0; slotIndex < message.slots.size(); ++slotIndex)
		{
			const std::string& token = message.slots[slotIndex];
			if (token.empty())
			{
				continue;
			}
			uint32_t itemId = 0u;
			if (!engine::anniversary::ParseItemToken(token, itemId)
				|| (!engine::anniversary::IsCakeItemId(itemId)
					&& FindConsumableEffect(itemId) == nullptr))
			{
				LOG_WARN(Net, "[ServerApp] SetBeltLayout reject: jeton '{}' non activable (client_id={})",
					token, client->clientId);
				(void)SendBeltLayout(*client);
				return;
			}
			bool owned = false;
			for (const ItemStack& s : client->inventory)
			{
				if (s.itemId == itemId && s.quantity > 0u) { owned = true; break; }
			}
			if (!owned)
			{
				LOG_WARN(Net, "[ServerApp] SetBeltLayout reject: objet {} non possede (client_id={})",
					itemId, client->clientId);
				(void)SendBeltLayout(*client);
				return;
			}
			for (size_t prior = 0; prior < slotIndex; ++prior)
			{
				if (validated[prior] == token)
				{
					(void)SendBeltLayout(*client);
					return;
				}
			}
			validated[slotIndex] = token;
		}

		client->beltLayout = validated;
		SaveConnectedClient(*client, "belt_change");
		(void)SendBeltLayout(*client); // ACK autoritaire
		LOG_INFO(Net, "[ServerApp] SetBeltLayout applied (client_id={})", client->clientId);
	}

	bool ServerApp::SendBeltLayout(const ConnectedClient& client)
	{
		BeltLayoutUpdateMessage msg{};
		msg.clientId = client.clientId;
		// Ceinture v2 — exactement la capacité ACTIVE : les slots au-delà
		// (contenu d'une grande ceinture déséquipée) restent stockés côté
		// serveur mais ne sont ni envoyés ni activables.
		msg.slots = client.beltLayout;
		msg.slots.resize(BeltCapacity(client));
		return SendDatagram(client.endpoint, EncodeBeltLayoutUpdate(msg));
	}

	uint8_t ServerApp::BeltCapacity(const ConnectedClient& client) const
	{
		using engine::items::EquipmentSlot;
		const uint32_t waistItemId =
			client.equipment[static_cast<size_t>(EquipmentSlot::Waist)];
		if (waistItemId != 0u && m_itemCatalogLoaded)
		{
			const engine::items::ItemDefinition* def = m_itemCatalog.Find(waistItemId);
			if (def != nullptr && def->beltSlots > 0u)
			{
				return std::clamp<uint8_t>(def->beltSlots,
					engine::items::kBeltSlotsDefault, engine::items::kBeltSlotsMax);
			}
		}
		// Retour joueur 2026-07-21 — la capacité vient EXCLUSIVEMENT de la
		// ceinture équipée : sans ceinture, AUCUNE case (avant : 4 par défaut).
		// La « ceinture de base 4 slots » du démarrage est désormais un VRAI
		// objet équipé d'office (EnsureStarterBelt), que le joueur peut retirer.
		return 0u;
	}

	void ServerApp::EnsureStarterBelt(ConnectedClient& client)
	{
		using engine::items::EquipmentSlot;
		const size_t waistIdx = static_cast<size_t>(EquipmentSlot::Waist);
		if (client.equipment[waistIdx] != 0u)
			return; // une ceinture est déjà portée.
		// Le joueur possède-t-il une ceinture dans le sac (retirée volontairement,
		// ou trouvée sans l'équiper) ? Dans ce cas on respecte son choix.
		if (m_itemCatalogLoaded)
		{
			for (const ItemStack& stack : client.inventory)
			{
				const engine::items::ItemDefinition* def = m_itemCatalog.Find(stack.itemId);
				if (def != nullptr && def->beltSlots > 0u)
					return;
			}
		}
		client.equipment[waistIdx] = engine::items::kStarterBeltItemId;
		LOG_INFO(Net,
			"[ServerApp] Ceinture de depart equipee d'office (client_id={}, item_id={})",
			client.clientId, engine::items::kStarterBeltItemId);
	}

	void ServerApp::HandleConsumableUse(ConnectedClient& client, uint32_t itemId)
	{
		const ConsumableEffectDef* def = FindConsumableEffect(itemId);
		if (def == nullptr) return;
		// Slotté en ceinture (les consommables ne s'activent QUE depuis la
		// ceinture — geste volontaire, cf. règle gâteau). Ceinture v2 : seuls
		// les slots ACTIFS (index < capacité) comptent.
		const std::string token = engine::anniversary::MakeItemToken(itemId);
		const size_t activeSlots =
			std::min<size_t>(client.beltLayout.size(), BeltCapacity(client));
		bool slotted = false;
		for (size_t i = 0; i < activeSlots; ++i)
			if (client.beltLayout[i] == token) { slotted = true; break; }
		if (!slotted)
		{
			SendChatSystemNotice(client, "Placez d'abord cet objet dans votre ceinture.");
			return;
		}
		std::string invErr;
		if (!RemoveStackFromInventory(client, itemId, 1u, invErr))
		{
			SendChatSystemNotice(client, "Vous ne possédez plus cet objet.");
			return;
		}

		// Effet : soin instantané (% PV max) et/ou aura de buff.
		if (def->healPercentMaxHp > 0.0f)
		{
			const uint32_t healAmount = static_cast<uint32_t>(std::llround(
				static_cast<double>(client.stats.maxHealth)
				* static_cast<double>(def->healPercentMaxHp) / 100.0));
			client.stats.currentHealth = std::min(
				client.stats.maxHealth, client.stats.currentHealth + healAmount);
		}
		if (def->auraDurationMs > 0u)
		{
			ActiveAura aura;
			aura.spellId = def->auraSpellId;
			aura.type = def->auraType;
			aura.percent = def->auraPercent;
			aura.expiresAtMs = NowMonotonicMs() + def->auraDurationMs;
			aura.casterEntityId = client.entityId;
			if (UpsertAura(client.auras, std::move(aura)))
			{
				RefreshLiveDerivedStats(client);
				(void)SendPlayerStats(client);
			}
			BroadcastAuraUpdate(client.entityId, client.auras);
		}

		// Pile épuisée : vider les slots de ceinture portant ce jeton.
		bool stillOwned = false;
		for (const ItemStack& s : client.inventory)
			if (s.itemId == itemId && s.quantity > 0u) { stillOwned = true; break; }
		bool beltChanged = false;
		if (!stillOwned)
		{
			for (std::string& slot : client.beltLayout)
				if (slot == token) { slot.clear(); beltChanged = true; }
		}

		SaveConnectedClient(client, "consumable_use");
		(void)SendInventoryDelta(client,
			std::span<const ItemStack>(client.inventory.data(), client.inventory.size()));
		if (beltChanged)
			(void)SendBeltLayout(client);
		if (def->noticeFr[0] != '\0')
			SendChatSystemNotice(client, def->noticeFr);
		LOG_INFO(Net, "[ServerApp] Consommable utilise (client_id={}, item_id={})",
			client.clientId, itemId);
	}

	bool ServerApp::SendClassProgression(const ConnectedClient& client)
	{
		ClassProgressionUpdateMessage msg{};
		msg.clientId = client.clientId;
		msg.classId = client.classId;
		msg.knownSkillIds = client.knownSkillIds;
		const std::vector<std::byte> packet = EncodeClassProgressionUpdate(msg);
		if (!SendDatagram(client.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendClassProgression failed (client_id={})", client.clientId);
			return false;
		}
		return true;
	}

	void ServerApp::HandleChooseClassSkill(const Endpoint& endpoint, const ChooseClassSkillRequestMessage& message)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] ChooseClassSkill ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}
		if (client->clientId != message.clientId)
		{
			LOG_WARN(Net, "[ServerApp] ChooseClassSkill ignored: client_id mismatch (expected={}, got={})",
				client->clientId, message.clientId);
			return;
		}

		// Joueur sans classe résolu (char legacy / no-DB) : rejeter silencieusement.
		if (client->classId.empty())
		{
			LOG_WARN(Net, "[ServerApp] ChooseClassSkill ignored: no class resolved (client_id={})", client->clientId);
			return;
		}

		// Vérifier l'existence du skill dans la bibliothèque.
		const ClassSkillDef* sk = m_classSkills.FindSkill(client->classId, message.skillId);
		if (sk == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] ChooseClassSkill reject: skill '{}' unknown for class '{}' (client_id={})",
				message.skillId, client->classId, client->clientId);
			(void)SendClassProgression(*client);
			return;
		}

		// Vérifier que le tier du skill correspond au niveau demandé et que le niveau est valide.
		if (message.level == 0 || message.level > client->level || sk->tier != message.level)
		{
			LOG_WARN(Net, "[ServerApp] ChooseClassSkill reject: tier/level mismatch (skill='{}', tier={}, level_req={}, client_level={}, client_id={})",
				message.skillId, sk->tier, message.level, client->level, client->clientId);
			(void)SendClassProgression(*client);
			return;
		}

		// Unicité par niveau : vérifier qu'aucun skill du même tier n'est déjà choisi.
		for (const std::string& knownId : client->knownSkillIds)
		{
			const ClassSkillDef* knownSk = m_classSkills.FindSkill(client->classId, knownId);
			if (knownSk != nullptr && knownSk->tier == message.level)
			{
				LOG_WARN(Net, "[ServerApp] ChooseClassSkill reject: skill already chosen for tier {} (existing='{}', client_id={})",
					message.level, knownId, client->clientId);
				(void)SendClassProgression(*client);
				return;
			}
		}

		// Choix validé : enregistrer et persister.
		client->knownSkillIds.push_back(message.skillId);
		SaveConnectedClient(*client, "skill_choice");
		(void)SendClassProgression(*client);
		LOG_INFO(Net, "[ServerApp] ChooseClassSkill applied (client_id={}, class='{}', skill='{}', tier={})",
			client->clientId, client->classId, message.skillId, sk->tier);
	}

	void ServerApp::TickActiveCasts()
	{
		const uint64_t nowMs = NowMonotonicMs();
		for (ConnectedClient& client : m_clients)
		{
			if (client.activeCastSpellId.empty())
			{
				continue;
			}

			const bool casterDead = (client.stateFlags & kEntityStateDead) != 0u;
			const float dx = client.positionMetersX - client.castStartPosX;
			const float dz = client.positionMetersZ - client.castStartPosZ;
			// Annulation au déplacement > 0,5 m (décision plan SP3) ou à la mort.
			const bool moved = (dx * dx + dz * dz) > 0.25f;
			if (casterDead || moved)
			{
				CastBarUpdateMessage cancel{};
				cancel.clientId = client.clientId;
				cancel.status = kCastBarStatusCancel;
				cancel.spellId = client.activeCastSpellId;
				(void)SendDatagram(client.endpoint, EncodeCastBarUpdate(cancel));
				LOG_INFO(Net, "[ServerApp] Cast cancelled (client_id={}, spell={}, reason={})",
					client.clientId, client.activeCastSpellId, casterDead ? "dead" : "moved");
				client.activeCastSpellId.clear();
				client.castFinishAtMs = 0;
				client.castTargetEntityId = 0;
				continue;
			}

			if (nowMs < client.castFinishAtMs)
			{
				continue;
			}

			const SpellDef* spell = m_spellKits.FindSpell(client.profileId, client.activeCastSpellId);
			const EntityId target = client.castTargetEntityId;
			const std::string finishedSpellId = client.activeCastSpellId;
			client.activeCastSpellId.clear();
			client.castFinishAtMs = 0;
			client.castTargetEntityId = 0;
			if (spell == nullptr)
			{
				continue;
			}

			CastBarUpdateMessage complete{};
			complete.clientId = client.clientId;
			complete.status = kCastBarStatusComplete;
			complete.spellId = finishedSpellId;
			(void)SendDatagram(client.endpoint, EncodeCastBarUpdate(complete));
			ResolveSpellCast(client, *spell, target);
		}
	}

	void ServerApp::ResolveSpellCast(ConnectedClient& client, const SpellDef& spell, EntityId targetEntityId)
	{
		const uint64_t nowMs = NowMonotonicMs();
		// Débit (clamp défensif : la ressource a pu varier pendant l'incantation)
		// + cooldown. Validations de cible faites à la demande ; une cible morte
		// entre-temps rend les effets ciblés no-op (le coût reste dû).
		// ADM.3 — /nocost : ni coût ni temps de recharge.
		const uint32_t cost = client.gm.noCost ? 0u : spell.resourceCostPercent * client.maxResource / 100u;
		client.currentResource = (client.currentResource >= cost) ? (client.currentResource - cost) : 0u;
		SendResourceUpdate(client, true);
		if (spell.cooldownMs > 0 && !client.gm.noCost)
		{
			client.spellCooldownUntilMs[spell.spellId] = nowMs + spell.cooldownMs;
		}

		bool casterAurasChanged = false;
		for (const SpellEffectDef& effect : spell.effects)
		{
			switch (effect.type)
			{
			case SpellEffectType::DirectDamage:
			{
				const uint32_t baseDamage = static_cast<uint32_t>(
					std::llround(static_cast<double>(effect.mult) * static_cast<double>(client.combat.damagePerHit)));
				if (spell.targetType == SpellTargetKind::AreaAroundSelf)
				{
					const float radiusSquared = spell.areaRadiusMeters * spell.areaRadiusMeters;
					for (MobEntity& mob : m_mobs)
					{
						if (!engine::world::cells::SameMap(mob.zoneId, client.zoneId) || mob.pendingDespawn
							|| (mob.stateFlags & kEntityStateDead) != 0u || mob.stats.currentHealth == 0)
						{
							continue;
						}
						const float distanceSquared = DistanceSquaredXZ(
							client.positionMetersX, client.positionMetersZ,
							mob.positionMetersX, mob.positionMetersZ);
						if (distanceSquared > radiusSquared)
						{
							continue;
						}
						const combat::AttackRollResult roll = combat::ResolveAttackRoll(
							baseDamage, client.combat.accuracy, client.combat.critRate,
							client.combat.critMult, NextCombatRoll01(), NextCombatRoll01());
						if (roll.miss)
						{
							continue; // V1 : pas d'event raté par cible AoE (anti-spam log).
						}
						uint32_t eventFlags = roll.crit ? kCombatEventFlagCrit : 0u;
						ApplyPlayerDamageToMob(client, mob,
							ApplyAuraDamageModifiers(roll.damage, client.auras, mob.auras), eventFlags);
					}
				}
				else if (MobEntity* target = FindMobByEntityId(targetEntityId))
				{
					if ((target->stateFlags & kEntityStateDead) == 0u && target->stats.currentHealth > 0)
					{
						const combat::AttackRollResult roll = combat::ResolveAttackRoll(
							baseDamage, client.combat.accuracy, client.combat.critRate,
							client.combat.critMult, NextCombatRoll01(), NextCombatRoll01());
						uint32_t eventFlags = 0u;
						if (roll.miss) eventFlags |= kCombatEventFlagMiss;
						if (roll.crit) eventFlags |= kCombatEventFlagCrit;
						if (roll.miss)
						{
							CombatEventMessage missEvent{};
							missEvent.attackerEntityId = client.entityId;
							missEvent.targetEntityId = target->entityId;
							missEvent.targetCurrentHealth = target->stats.currentHealth;
							missEvent.targetMaxHealth = target->stats.maxHealth;
							missEvent.targetStateFlags = target->stateFlags;
							missEvent.flags = eventFlags;
							BroadcastCombatEvent(missEvent);
						}
						else
						{
							ApplyPlayerDamageToMob(client, *target,
								ApplyAuraDamageModifiers(roll.damage, client.auras, target->auras), eventFlags);
						}
					}
				}
				break;
			}
			case SpellEffectType::DamageOverTime:
			case SpellEffectType::SlowMobPercent:
			case SpellEffectType::DebuffDamageTakenPercent:
			{
				MobEntity* target = FindMobByEntityId(targetEntityId);
				if (target == nullptr || (target->stateFlags & kEntityStateDead) != 0u)
				{
					break;
				}
				ActiveAura aura{};
				aura.spellId = spell.spellId;
				aura.type = effect.type;
				aura.percent = effect.percent;
				aura.tickPeriodMs = effect.tickPeriodMs;
				aura.expiresAtMs = nowMs + effect.durationMs;
				aura.nextTickAtMs = nowMs + effect.tickPeriodMs;
				aura.casterEntityId = client.entityId;
				if (effect.type == SpellEffectType::DamageOverTime)
				{
					aura.tickAmount = static_cast<uint32_t>(std::llround(
						static_cast<double>(effect.mult) * static_cast<double>(client.combat.damagePerHit)));
				}
				UpsertAura(target->auras, std::move(aura));
				BroadcastAuraUpdate(target->entityId, target->auras);
				break;
			}
			case SpellEffectType::TauntThreatMult:
			{
				MobEntity* target = FindMobByEntityId(targetEntityId);
				if (target == nullptr || (target->stateFlags & kEntityStateDead) != 0u)
				{
					break;
				}
				// Taunt : menace du casteur = mult × menace max courante (plancher
				// 100 pour engager un mob encore vierge), agro forcée immédiate.
				uint32_t maxThreat = 0;
				for (const ThreatEntry& entry : target->threatTable)
				{
					maxThreat = std::max(maxThreat, entry.threat);
				}
				const uint32_t tauntThreat = std::max(100u, static_cast<uint32_t>(
					std::llround(static_cast<double>(maxThreat) * static_cast<double>(effect.mult))));
				bool found = false;
				for (ThreatEntry& entry : target->threatTable)
				{
					if (entry.entityId == client.entityId)
					{
						entry.threat = std::max(entry.threat, tauntThreat);
						found = true;
						break;
					}
				}
				if (!found)
				{
					target->threatTable.push_back(ThreatEntry{ client.entityId, tauntThreat });
				}
				target->aggroTargetEntityId = client.entityId;
				LOG_INFO(Net, "[ServerApp] Taunt applied (client_id={}, mob_entity_id={}, threat={})",
					client.clientId, target->entityId, tauntThreat);
				break;
			}
			case SpellEffectType::ThreatReducePercent:
			{
				// Dérobade : réduit la menace du casteur sur TOUS les mobs engagés.
				const double keepFactor = 1.0 - static_cast<double>(effect.percent) / 100.0;
				for (MobEntity& mob : m_mobs)
				{
					for (ThreatEntry& entry : mob.threatTable)
					{
						if (entry.entityId == client.entityId)
						{
							entry.threat = static_cast<uint32_t>(std::llround(
								static_cast<double>(entry.threat) * keepFactor));
						}
					}
				}
				LOG_INFO(Net, "[ServerApp] Threat reduced (client_id={}, percent={})",
					client.clientId, effect.percent);
				break;
			}
			case SpellEffectType::DirectHeal:
			case SpellEffectType::HealOverTime:
			case SpellEffectType::BuffDamagePercent:
			{
				// Cible alliée : soi par défaut, ou l'allié validé à la demande.
				ConnectedClient* ally = &client;
				if (targetEntityId != 0 && targetEntityId != client.entityId)
				{
					// Seconde carte : allié passé sur une autre carte pendant l'incantation → soi.
					if (ConnectedClient* other = FindClientByEntityId(targetEntityId);
						other != nullptr && engine::world::cells::SameMap(other->zoneId, client.zoneId))
					{
						ally = other;
					}
				}
				if ((ally->stateFlags & kEntityStateDead) != 0u)
				{
					break; // pas de soin/buff sur un mort (résurrection = hors V1).
				}
				if (effect.type == SpellEffectType::DirectHeal)
				{
					const uint32_t heal = static_cast<uint32_t>(std::llround(
						static_cast<double>(effect.mult) * static_cast<double>(client.combat.damagePerHit)));
					ally->stats.currentHealth = std::min(ally->stats.maxHealth, ally->stats.currentHealth + heal);
					LOG_INFO(Net, "[ServerApp] Heal applied (caster={}, target_entity_id={}, heal={}, hp={}/{})",
						client.clientId, ally->entityId, heal, ally->stats.currentHealth, ally->stats.maxHealth);
				}
				else
				{
					ActiveAura aura{};
					aura.spellId = spell.spellId;
					aura.type = effect.type;
					aura.percent = effect.percent;
					aura.tickPeriodMs = effect.tickPeriodMs;
					aura.expiresAtMs = nowMs + effect.durationMs;
					aura.nextTickAtMs = nowMs + effect.tickPeriodMs;
					aura.casterEntityId = client.entityId;
					if (effect.type == SpellEffectType::HealOverTime)
					{
						// % PV max de la CIBLE prioritaire (« Second souffle »),
						// sinon mult × damage du casteur.
						aura.tickAmount = (effect.percentMaxHpPerTick > 0.0f)
							? static_cast<uint32_t>(std::llround(
								static_cast<double>(ally->stats.maxHealth)
								* static_cast<double>(effect.percentMaxHpPerTick) / 100.0))
							: static_cast<uint32_t>(std::llround(
								static_cast<double>(effect.mult) * static_cast<double>(client.combat.damagePerHit)));
					}
					// Roadmap-2 (2026-07-19) — une NOUVELLE aura peut modifier
					// les stats réelles (%PV max, %vitesse) : recalcul + push 79.
					if (UpsertAura(ally->auras, std::move(aura)))
					{
						RefreshLiveDerivedStats(*ally);
						(void)SendPlayerStats(*ally);
					}
					if (ally == &client)
					{
						casterAurasChanged = true;
					}
					else
					{
						BroadcastAuraUpdate(ally->entityId, ally->auras);
					}
				}
				break;
			}
			}
		}

		if (casterAurasChanged)
		{
			BroadcastAuraUpdate(client.entityId, client.auras);
		}
		LOG_INFO(Net, "[ServerApp] Spell cast resolved (client_id={}, spell={}, cost={}, resource={}/{})",
			client.clientId, spell.spellId, cost, client.currentResource, client.maxResource);
	}

	// SP-C — résolution d'un cast par compétence de classe (chemin additif).
	// Pré-conditions : client non mort, ressource suffisante, cooldown écoulé (vérifiés
	// par HandleCastRequest). La fonction débite la ressource et arme le cooldown elle-même.
	void ServerApp::ResolveClassSkillCast(
		ConnectedClient& client,
		const ClassSkillDef& cs,
		EntityId targetEntityId)
	{
		const uint64_t nowMs = NowMonotonicMs();

		// ── Débit ressource + cooldown (pattern identique à ResolveSpellCast) ──
		// ADM.3 — /nocost : ni coût ni temps de recharge.
		const uint32_t cost = client.gm.noCost ? 0u : cs.resourceCostPercent * client.maxResource / 100u;
		client.currentResource = (client.currentResource >= cost) ? (client.currentResource - cost) : 0u;
		SendResourceUpdate(client, true);
		if (cs.cooldownMs > 0 && !client.gm.noCost)
		{
			client.spellCooldownUntilMs[cs.skillId] = nowMs + cs.cooldownMs;
		}

		// ── Résolution de l'effet selon effectKind ──
		switch (cs.effectKind)
		{
		case ClassSkillEffectKind::Damage:
		{
			// Dégâts directs : base = powerValue × damagePerHit du casteur.
			const uint32_t baseDamage = static_cast<uint32_t>(
				std::llround(static_cast<double>(cs.powerValue)
					* static_cast<double>(client.combat.damagePerHit)));

			if (cs.target == ClassSkillTarget::AreaAroundSelf)
			{
				// AoE — même boucle que DirectDamage/AreaAroundSelf de ResolveSpellCast.
				const float radiusSquared = cs.areaRadiusMeters * cs.areaRadiusMeters;
				for (MobEntity& mob : m_mobs)
				{
					if (!engine::world::cells::SameMap(mob.zoneId, client.zoneId) || mob.pendingDespawn
						|| (mob.stateFlags & kEntityStateDead) != 0u || mob.stats.currentHealth == 0)
					{
						continue;
					}
					const float distSq = DistanceSquaredXZ(
						client.positionMetersX, client.positionMetersZ,
						mob.positionMetersX, mob.positionMetersZ);
					if (distSq > radiusSquared)
					{
						continue;
					}
					const combat::AttackRollResult roll = combat::ResolveAttackRoll(
						baseDamage, client.combat.accuracy, client.combat.critRate,
						client.combat.critMult, NextCombatRoll01(), NextCombatRoll01());
					if (roll.miss)
					{
						continue; // pas d'event raté sur AoE (anti-spam, même règle que SP3).
					}
					const uint32_t eventFlags = roll.crit ? kCombatEventFlagCrit : 0u;
					ApplyPlayerDamageToMob(client, mob,
						ApplyAuraDamageModifiers(roll.damage, client.auras, mob.auras), eventFlags);
				}
			}
			else
			{
				// SingleEnemy (seul cas restant validé par HandleCastRequest).
				if (MobEntity* target = FindMobByEntityId(targetEntityId))
				{
					if ((target->stateFlags & kEntityStateDead) == 0u && target->stats.currentHealth > 0)
					{
						const combat::AttackRollResult roll = combat::ResolveAttackRoll(
							baseDamage, client.combat.accuracy, client.combat.critRate,
							client.combat.critMult, NextCombatRoll01(), NextCombatRoll01());
						uint32_t eventFlags = 0u;
						if (roll.miss) eventFlags |= kCombatEventFlagMiss;
						if (roll.crit) eventFlags |= kCombatEventFlagCrit;
						if (roll.miss)
						{
							CombatEventMessage missEvent{};
							missEvent.attackerEntityId = client.entityId;
							missEvent.targetEntityId   = target->entityId;
							missEvent.targetCurrentHealth = target->stats.currentHealth;
							missEvent.targetMaxHealth     = target->stats.maxHealth;
							missEvent.targetStateFlags    = target->stateFlags;
							missEvent.flags = eventFlags;
							BroadcastCombatEvent(missEvent);
						}
						else
						{
							ApplyPlayerDamageToMob(client, *target,
								ApplyAuraDamageModifiers(roll.damage, client.auras, target->auras),
								eventFlags);
						}
					}
				}
			}
			break;
		}
		case ClassSkillEffectKind::Heal:
		{
			// Soin direct — chemin calqué sur DirectHeal de ResolveSpellCast.
			// Cible : soi par défaut, ou allié résolu par HandleCastRequest (targetEntityId).
			ConnectedClient* ally = &client;
			if (targetEntityId != 0 && targetEntityId != client.entityId)
			{
				// Seconde carte : allié passé sur une autre carte → soi.
				if (ConnectedClient* other = FindClientByEntityId(targetEntityId);
					other != nullptr && engine::world::cells::SameMap(other->zoneId, client.zoneId))
				{
					ally = other;
				}
			}
			if ((ally->stateFlags & kEntityStateDead) == 0u)
			{
				const uint32_t heal = static_cast<uint32_t>(
					std::llround(static_cast<double>(cs.powerValue)
						* static_cast<double>(client.combat.damagePerHit)));
				ally->stats.currentHealth = std::min(ally->stats.maxHealth, ally->stats.currentHealth + heal);
				// Cohérence V1 avec DirectHeal SP3 : pas de CombatEvent de soin (non défini
				// dans le protocole wire v12). L'UI se met à jour via le snapshot HP suivant.
				// TODO SP-C : définir kCombatEventFlagHeal dans ServerProtocol.h pour un vrai
				// feedback visuel du soin sur le client (HUD floating number vert).
				LOG_INFO(Net,
					"[ServerApp] SP-C Heal applied (caster={}, target_entity_id={}, heal={}, hp={}/{})",
					client.clientId, ally->entityId, heal,
					ally->stats.currentHealth, ally->stats.maxHealth);
			}
			break;
		}
		case ClassSkillEffectKind::Defense:
		{
			// Défense — applique une aura DamageReductionPercent sur le casteur.
			// percent = clamp(round(powerValue × 11), 5, 50), durée fixe 8 000 ms.
			// L'aura utilise le même mécanisme UpsertAura que les buffs SP3.
			const float rawPercent = std::round(cs.powerValue * 11.0f);
			const float clampedPercent = std::min(50.0f, std::max(5.0f, rawPercent));
			ActiveAura defAura{};
			defAura.spellId         = cs.skillId;
			defAura.type            = SpellEffectType::DamageReductionPercent;
			defAura.percent         = clampedPercent;
			defAura.tickPeriodMs    = 0u;       // pas de tick : aura passive de durée.
			defAura.expiresAtMs     = nowMs + 8000u;
			defAura.nextTickAtMs    = 0u;
			defAura.casterEntityId  = client.entityId;
			if (UpsertAura(client.auras, std::move(defAura)))
			{
				// Roadmap-2 — recalcul des stats réelles à l'ajout d'aura.
				RefreshLiveDerivedStats(client);
				(void)SendPlayerStats(client);
			}
			BroadcastAuraUpdate(client.entityId, client.auras);
			LOG_INFO(Net,
				"[ServerApp] SP-C Defense aura applied (client_id={}, skill={}, percent={:.1f}, duration_ms=8000)",
				client.clientId, cs.skillId, clampedPercent);
			break;
		}
		}

		LOG_INFO(Net,
			"[ServerApp] SP-C skill cast resolved (client_id={}, skill={}, kind={}, cost={}, resource={}/{})",
			client.clientId, cs.skillId,
			static_cast<int>(cs.effectKind),
			cost, client.currentResource, client.maxResource);
	}

	void ServerApp::TickAuras()
	{
		const uint64_t nowMs = NowMonotonicMs();

		// Joueurs : HoT/buffs (aucune source de DoT sur joueur en V1).
		for (ConnectedClient& client : m_clients)
		{
			if (client.auras.empty())
			{
				continue;
			}
			const bool clientDead = (client.stateFlags & kEntityStateDead) != 0u;
			for (ActiveAura& aura : client.auras)
			{
				if (aura.tickPeriodMs == 0 || nowMs < aura.nextTickAtMs || nowMs >= aura.expiresAtMs)
				{
					continue;
				}
				aura.nextTickAtMs += aura.tickPeriodMs;
				if (aura.type == SpellEffectType::HealOverTime && !clientDead)
				{
					client.stats.currentHealth = std::min(
						client.stats.maxHealth, client.stats.currentHealth + aura.tickAmount);
				}
			}
			const size_t beforeCount = client.auras.size();
			client.auras.erase(
				std::remove_if(client.auras.begin(), client.auras.end(),
					[nowMs](const ActiveAura& aura) { return nowMs >= aura.expiresAtMs; }),
				client.auras.end());
			if (client.auras.size() != beforeCount)
			{
				BroadcastAuraUpdate(client.entityId, client.auras);
				// Roadmap-2 (2026-07-19) — l'expiration d'une aura peut rendre
				// des stats réelles (%PV max, %vitesse) : recalcul + push 79.
				// Les PV courants sont re-clampés sous le nouveau max.
				RefreshLiveDerivedStats(client);
				(void)SendPlayerStats(client);
			}
		}

		// Mobs : DoT (via le chemin commun dégâts/mort/loot/XP si le casteur est
		// encore connecté) + expirations (slow/debuff).
		for (MobEntity& mob : m_mobs)
		{
			if (mob.auras.empty())
			{
				continue;
			}
			if (mob.pendingDespawn || (mob.stateFlags & kEntityStateDead) != 0u)
			{
				mob.auras.clear();
				continue;
			}
			for (ActiveAura& aura : mob.auras)
			{
				if (aura.type != SpellEffectType::DamageOverTime
					|| aura.tickPeriodMs == 0 || nowMs < aura.nextTickAtMs || nowMs >= aura.expiresAtMs)
				{
					continue;
				}
				aura.nextTickAtMs += aura.tickPeriodMs;
				if (ConnectedClient* caster = FindClientByEntityId(aura.casterEntityId))
				{
					// Le tick profite des debuffs « dégâts subis » de la cible mais
					// pas des buffs du casteur (montant figé à l'application).
					ApplyPlayerDamageToMob(*caster, mob,
						ApplyAuraDamageModifiers(aura.tickAmount, {}, mob.auras), 0u);
				}
				else
				{
					// Casteur déconnecté : dégâts secs, sans loot/XP ni événement.
					const uint32_t applied = std::min(aura.tickAmount, mob.stats.currentHealth);
					mob.stats.currentHealth -= applied;
					if (mob.stats.currentHealth == 0)
					{
						mob.stateFlags |= kEntityStateDead;
						mob.pendingDespawn = true;
						mob.aggroTargetEntityId = 0;
						mob.threatTable.clear();
						LOG_INFO(Net, "[ServerApp] Mob died from orphan DoT (entity_id={})", mob.entityId);
					}
				}
				if ((mob.stateFlags & kEntityStateDead) != 0u)
				{
					break; // mort pendant le tick : les auras seront purgées ci-dessous.
				}
			}
			if (mob.pendingDespawn || (mob.stateFlags & kEntityStateDead) != 0u)
			{
				mob.auras.clear();
				continue;
			}
			const size_t beforeCount = mob.auras.size();
			mob.auras.erase(
				std::remove_if(mob.auras.begin(), mob.auras.end(),
					[nowMs](const ActiveAura& aura) { return nowMs >= aura.expiresAtMs; }),
				mob.auras.end());
			if (mob.auras.size() != beforeCount)
			{
				BroadcastAuraUpdate(mob.entityId, mob.auras);
			}
		}
	}

	void ServerApp::PushThreatUpdates()
	{
		const uint64_t nowMs = NowMonotonicMs();
		for (MobEntity& mob : m_mobs)
		{
			if (mob.pendingDespawn || (mob.stateFlags & kEntityStateDead) != 0u)
			{
				continue; // l'effacement a été poussé au moment de la mort/evade.
			}
			if (mob.threatTable.empty() && mob.lastThreatHash == 0u)
			{
				continue; // jamais engagé : rien à pousser ni à effacer.
			}
			if ((nowMs - mob.lastThreatPushMs) < 1000u)
			{
				continue;
			}
			// Hash d'état (FNV-ish) : push uniquement si la table a changé.
			uint64_t hash = 1469598103934665603ull;
			for (const ThreatEntry& entry : mob.threatTable)
			{
				hash = (hash ^ entry.entityId) * 1099511628211ull;
				hash = (hash ^ static_cast<uint64_t>(entry.threat)) * 1099511628211ull;
			}
			if (mob.threatTable.empty())
			{
				hash = 0u;
			}
			if (hash == mob.lastThreatHash)
			{
				continue;
			}
			BroadcastThreatUpdate(mob);
		}
	}

	void ServerApp::BroadcastThreatUpdate(MobEntity& mob)
	{
		ThreatUpdateMessage message{};
		message.mobEntityId = mob.entityId;
		for (const ThreatEntry& entry : mob.threatTable)
		{
			message.entries.push_back(ThreatWireEntry{ entry.entityId, entry.threat });
		}

		const std::vector<std::byte> packet = EncodeThreatUpdate(message);
		for (const ConnectedClient& receiver : m_clients)
		{
			if (receiver.entityId != mob.entityId
				&& !ContainsEntityId(receiver.interestEntityIds, mob.entityId))
			{
				continue;
			}
			(void)SendDatagram(receiver.endpoint, packet);
		}

		const uint64_t nowMs = NowMonotonicMs();
		mob.lastThreatPushMs = nowMs;
		uint64_t hash = 1469598103934665603ull;
		for (const ThreatEntry& entry : mob.threatTable)
		{
			hash = (hash ^ entry.entityId) * 1099511628211ull;
			hash = (hash ^ static_cast<uint64_t>(entry.threat)) * 1099511628211ull;
		}
		mob.lastThreatHash = mob.threatTable.empty() ? 0u : hash;
	}

	void ServerApp::BroadcastAuraUpdate(EntityId entityId, const std::vector<ActiveAura>& auras)
	{
		AuraUpdateMessage message{};
		message.targetEntityId = entityId;
		const uint64_t nowMs = NowMonotonicMs();
		for (const ActiveAura& aura : auras)
		{
			AuraWireEntry entry{};
			entry.spellId = aura.spellId;
			entry.effectType = static_cast<uint8_t>(aura.type);
			entry.remainingMs = (aura.expiresAtMs > nowMs)
				? static_cast<uint32_t>(aura.expiresAtMs - nowMs)
				: 0u;
			entry.stacks = aura.stacks;
			message.auras.push_back(std::move(entry));
		}

		const std::vector<std::byte> packet = EncodeAuraUpdate(message);
		for (const ConnectedClient& receiver : m_clients)
		{
			if (receiver.entityId != entityId && !ContainsEntityId(receiver.interestEntityIds, entityId))
			{
				continue;
			}
			(void)SendDatagram(receiver.endpoint, packet);
		}
	}

	bool ServerApp::TryMobAttackPlayer(MobEntity& mob, ConnectedClient& target)
	{
		if (m_currentTick < mob.combat.nextAttackTick)
		{
			return false;
		}

		const float distanceSquared = DistanceSquaredXZ(
			mob.positionMetersX,
			mob.positionMetersZ,
			target.positionMetersX,
			target.positionMetersZ);
		const float attackRangeSquared = mob.combat.attackRangeMeters * mob.combat.attackRangeMeters;
		if (distanceSquared > attackRangeSquared)
		{
			return false;
		}

		// Combat SP2 — le mob rate/critique aussi (stats d'archétype) ; cooldown
		// consommé même sur un raté.
		mob.combat.nextAttackTick = m_currentTick + mob.combat.cooldownTicks;
		const combat::AttackRollResult roll = combat::ResolveAttackRoll(
			mob.combat.damagePerHit,
			mob.combat.accuracy,
			mob.combat.critRate,
			mob.combat.critMult,
			NextCombatRoll01(),
			NextCombatRoll01());
		uint32_t eventFlags = 0u;
		if (roll.miss) eventFlags |= kCombatEventFlagMiss;
		if (roll.crit) eventFlags |= kCombatEventFlagCrit;

		// Combat SP3 — modificateurs d'auras (debuff « dégâts subis » de la cible ;
		// les mobs n'ont pas de buffs de dégâts en V1 mais le chemin est commun).
		const uint32_t modifiedDamage = roll.miss
			? 0u
			: ApplyAuraDamageModifiers(roll.damage, mob.auras, target.auras);
		// ADM.3 — maître de jeu invincible (/invincible) : aucun dégât.
		const uint32_t appliedDamage = (roll.miss || target.gm.invincible) ? 0u : std::min(modifiedDamage, target.stats.currentHealth);
		if (!roll.miss && appliedDamage == 0)
		{
			return false;
		}

		if (appliedDamage > 0)
		{
			target.stats.currentHealth -= appliedDamage;
		}
		if (target.stats.currentHealth == 0)
		{
			target.stateFlags |= kEntityStateDead;
			LOG_INFO(Net, "[ServerApp] Player died (entity_id={}, attacker_entity_id={})",
				target.entityId,
				mob.entityId);
			// Combat SP2 — un joueur mort sort de toutes les tables de menace ;
			// les mobs repassent en patrouille au prochain tick d'IA.
			PurgeThreatForEntity(target.entityId);
			// Combat SP3 — les auras du mort tombent (le cast en cours est annulé
			// par TickActiveCasts via le flag dead).
			if (!target.auras.empty())
			{
				target.auras.clear();
				BroadcastAuraUpdate(target.entityId, target.auras);
			}
			SaveConnectedClient(target, "player_death");
		}

		CombatEventMessage combatEvent{};
		combatEvent.attackerEntityId = mob.entityId;
		combatEvent.targetEntityId = target.entityId;
		combatEvent.damage = appliedDamage;
		combatEvent.targetCurrentHealth = target.stats.currentHealth;
		combatEvent.targetMaxHealth = target.stats.maxHealth;
		combatEvent.targetStateFlags = target.stateFlags;
		combatEvent.flags = eventFlags;
		LOG_INFO(Net,
			"[ServerApp] Mob attack applied (attacker_entity_id={}, target_entity_id={}, damage={}, hp={}/{}, next_attack_tick={})",
			combatEvent.attackerEntityId,
			combatEvent.targetEntityId,
			combatEvent.damage,
			combatEvent.targetCurrentHealth,
			combatEvent.targetMaxHealth,
			mob.combat.nextAttackTick);
		BroadcastCombatEvent(combatEvent);
		return true;
	}

	void ServerApp::HandleCakeActivation(ConnectedClient& client, uint32_t cakeItemId)
	{
		const CakeBuffDef* def = FindCakeBuff(cakeItemId);
		if (def == nullptr) return;
		const std::string token = engine::anniversary::MakeCakeToken(cakeItemId);
		// Roadmap-3 (2026-07-19) — le gâteau s'active depuis la barre d'action
		// OU la ceinture (nouvelle barre d'objets actifs).
		bool slotted = false;
		for (const std::string& slot : client.actionBarLayout)
			if (slot == token) { slotted = true; break; }
		// Ceinture v2 — seuls les slots ACTIFS (index < capacité) comptent.
		const size_t activeBeltSlots =
			std::min<size_t>(client.beltLayout.size(), BeltCapacity(client));
		for (size_t i = 0; i < activeBeltSlots; ++i)
			if (client.beltLayout[i] == token) { slotted = true; break; }
		if (!slotted)
		{
			SendChatSystemNotice(client, "Placez d'abord le gâteau dans votre ceinture (ou barre d'action).");
			return;
		}
		bool owned = false;
		for (const ItemStack& s : client.inventory)
			if (s.itemId == cakeItemId && s.quantity > 0u) { owned = true; break; }
		if (!owned)
		{
			SendChatSystemNotice(client, "Vous ne possédez plus ce gâteau.");
			return;
		}
		const auto expiryIt = client.cakeExpiresAtMsUtcByItemId.find(cakeItemId);
		if (expiryIt != client.cakeExpiresAtMsUtcByItemId.end()
			&& NowUnixEpochMsUtc() >= expiryIt->second)
		{
			SendChatSystemNotice(client, "Ce gâteau n'est plus frais — la fête est finie.");
			return;
		}
		client.activeCakeItemId = cakeItemId;
		SendChatSystemNotice(client, std::string("Le gâteau est servi ! ") + def->noticeFr
			+ " pour votre groupe à proximité, tant qu'il reste dans la barre d'action.");
		LOG_INFO(Net, "[ServerApp] Gateau active (client_id={}, item_id={})", client.clientId, cakeItemId);
	}

	void ServerApp::TickCakeBuffs(uint64_t nowMonotonicMs)
	{
		// 1 Hz : suffisant (TTL d'aura 3,5 s re-rafraîchi) et évite le spam
		// AuraUpdate (kind 84). m_tickHz garanti > 0 par l'init du scheduler.
		if (m_tickHz != 0u && (m_currentTick % m_tickHz) != 0u)
			return;

		/// Rayon de partage du buff (m) — « rayon raisonnable » du spec.
		constexpr float kCakeBuffRadiusMeters = 30.0f;
		/// TTL de l'aura : > période d'entretien (1 s) pour rester continue,
		/// assez court pour tomber vite quand le gâteau quitte la barre.
		constexpr uint64_t kCakeAuraTtlMs = 3500ull;
		const uint64_t nowUtc = NowUnixEpochMsUtc();

		for (ConnectedClient& client : m_clients)
		{
			// ── Purge de minuit UTC : le gâteau est « mangé ». ──
			bool inventoryChanged = false;
			for (auto it = client.cakeExpiresAtMsUtcByItemId.begin();
				it != client.cakeExpiresAtMsUtcByItemId.end();)
			{
				const uint32_t cakeItemId = it->first;
				if (nowUtc < it->second) { ++it; continue; }
				for (size_t i = 0; i < client.inventory.size(); ++i)
				{
					if (client.inventory[i].itemId == cakeItemId)
					{
						client.inventory.erase(client.inventory.begin() + static_cast<ptrdiff_t>(i));
						inventoryChanged = true;
						break;
					}
				}
				const std::string token = engine::anniversary::MakeCakeToken(cakeItemId);
				bool layoutChanged = false;
				for (std::string& slot : client.actionBarLayout)
				{
					if (slot == token) { slot.clear(); layoutChanged = true; }
				}
				// Roadmap-3 — vide aussi les slots de ceinture du gâteau mangé.
				bool beltChanged = false;
				for (std::string& slot : client.beltLayout)
				{
					if (slot == token) { slot.clear(); beltChanged = true; }
				}
				if (client.activeCakeItemId == cakeItemId)
					client.activeCakeItemId = 0u;
				if (layoutChanged)
					(void)SendActionBarLayout(client);
				if (beltChanged)
					(void)SendBeltLayout(client);
				SendChatSystemNotice(client,
					"Le gâteau d'anniversaire a été mangé — la fête est finie pour cette année !");
				it = client.cakeExpiresAtMsUtcByItemId.erase(it);
			}
			if (inventoryChanged)
			{
				(void)SendInventoryDelta(client,
					std::span<const ItemStack>(client.inventory.data(), client.inventory.size()));
				SaveConnectedClient(client, "cake_expired");
			}

			// ── Entretien du buff : actif + slotté + possédé, sinon extinction. ──
			if (client.activeCakeItemId == 0u)
				continue;
			const CakeBuffDef* def = FindCakeBuff(client.activeCakeItemId);
			const std::string activeToken = engine::anniversary::MakeCakeToken(client.activeCakeItemId);
			// Roadmap-3 — slotté = barre d'action OU ceinture (Ceinture v2 :
			// seuls les slots ACTIFS, index < capacité, comptent).
			bool slotted = false;
			for (const std::string& slot : client.actionBarLayout)
				if (slot == activeToken) { slotted = true; break; }
			const size_t activeBeltSlots =
				std::min<size_t>(client.beltLayout.size(), BeltCapacity(client));
			for (size_t i = 0; i < activeBeltSlots; ++i)
				if (client.beltLayout[i] == activeToken) { slotted = true; break; }
			bool owned = false;
			for (const ItemStack& s : client.inventory)
				if (s.itemId == client.activeCakeItemId && s.quantity > 0u) { owned = true; break; }
			if (def == nullptr || !slotted || !owned)
			{
				// Plus de rafraîchissement : l'aura TTL court expire d'elle-même
				// en ≤ 3,5 s via TickAuras (« retiré du slot → le buff s'arrête »).
				client.activeCakeItemId = 0u;
				continue;
			}

			// Destinataires : le porteur + membres du groupe à portée.
			std::vector<ConnectedClient*> recipients;
			recipients.push_back(&client);
			if (m_partySystem.IsInitialized())
			{
				if (const Party* party = m_partySystem.FindPartyByMember(client.clientId))
				{
					const std::vector<uint32_t> inRange = m_partySystem.GetMembersInRange(
						party->partyId, client.clientId,
						client.positionMetersX, client.positionMetersZ,
						kCakeBuffRadiusMeters,
						[this, &client](uint32_t memberClientId, float& outX, float& outZ) -> bool
						{
							for (const ConnectedClient& c : m_clients)
							{
								// Seconde carte : un membre d'une autre carte n'est jamais « à portée ».
								if (c.clientId == memberClientId && engine::world::cells::SameMap(c.zoneId, client.zoneId))
								{
									outX = c.positionMetersX;
									outZ = c.positionMetersZ;
									return true;
								}
							}
							return false;
						});
					for (uint32_t memberId : inRange)
					{
						if (memberId == client.clientId) continue;
						for (ConnectedClient& other : m_clients)
						{
							if (other.clientId == memberId) { recipients.push_back(&other); break; }
						}
					}
				}
			}
			// Roadmap-7 (2026-07-19) — partage « guilde » (solde la DETTE #991) :
			// la guilde du compte est désormais synchronisée master→shard via le
			// push d'admission (ConnectedClient.guildId, snapshot enter-world).
			// Mêmes règles que le groupe : co-membres CONNECTÉS et À PORTÉE
			// (kCakeBuffRadiusMeters), sans doublon avec les membres de groupe
			// déjà retenus. guildId == 0 (sans guilde / master legacy) → no-op.
			if (client.guildId != 0u)
			{
				for (ConnectedClient& other : m_clients)
				{
					if (other.clientId == client.clientId) continue;
					if (other.guildId != client.guildId) continue;
					if (!engine::world::cells::SameMap(other.zoneId, client.zoneId)) continue; // autre carte
					const float dx = other.positionMetersX - client.positionMetersX;
					const float dz = other.positionMetersZ - client.positionMetersZ;
					if (dx * dx + dz * dz > kCakeBuffRadiusMeters * kCakeBuffRadiusMeters) continue;
					bool already = false;
					for (const ConnectedClient* r : recipients)
						if (r->clientId == other.clientId) { already = true; break; }
					if (!already) recipients.push_back(&other);
				}
			}

			for (ConnectedClient* target : recipients)
			{
				ActiveAura aura;
				aura.spellId = def->buffSpellId;
				aura.type = def->type;
				aura.percent = def->percent;
				aura.expiresAtMs = nowMonotonicMs + kCakeAuraTtlMs;
				aura.casterEntityId = client.entityId;
				const bool newlyAdded = UpsertAura(target->auras, std::move(aura));
				// Roadmap-2 — recalcul UNIQUEMENT au vrai ajout (l'entretien
				// 1 Hz ne fait que rafraîchir la durée : pas de spam 79).
				if (newlyAdded)
				{
					RefreshLiveDerivedStats(*target);
					(void)SendPlayerStats(*target);
				}
				BroadcastAuraUpdate(target->entityId, target->auras);
			}
		}
	}
}
