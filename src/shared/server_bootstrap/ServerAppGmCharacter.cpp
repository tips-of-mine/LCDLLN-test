// ADM.3 — Commandes de personnage des maîtres de jeu (shard).
//
// /level, /xp, /heal, /revive, /kill, /hp, /mana, /invincible, /invisible,
// /cooldown, /nocost, /learn, /unlearn, /resetskills, /profession.
//
// Synchronisation du client :
//   - vie / état mort : CombatEvent « résurrection » (kCombatEventFlagResurrection,
//     sans ligne de journal de combat), seul canal qui rafraîchit les PV du joueur
//     local (les snapshots excluent sa propre entité) ;
//   - ressource : ResourceUpdate forcé ; fiche et vitesses : PlayerStats ; niveau :
//     PlayerXpUpdate ; compétences : ClassProgressionUpdate ; métiers :
//     ProfessionUpdate ; pouvoirs : GmStateUpdate (ADM.1).
// Chaque changement durable (niveau, XP, compétences, métiers) est sauvegardé.

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		/// Bornes d'un niveau de métier (CraftingSystem : 1 à 300).
		constexpr uint32_t kMaxProfessionLevel = 300u;
	}

	void GmCommandExecutor::SyncHealth(ConnectedClient& client)
	{
		CombatEventMessage event{};
		event.attackerEntityId = 0;
		event.targetEntityId = client.entityId;
		event.damage = 0;
		event.targetCurrentHealth = client.stats.currentHealth;
		event.targetMaxHealth = client.stats.maxHealth;
		event.targetStateFlags = client.stateFlags;
		event.flags = kCombatEventFlagResurrection;
		app.BroadcastCombatEvent(event);
	}

	ConnectedClient* GmCommandExecutor::ParseAmountAndTarget(int64_t& outAmount, std::string_view syntax, bool allowNegative)
	{
		// Formes acceptées : « <n> [joueur] » et « <joueur> <n> » (ancienne /setlevel).
		if (args.empty())
		{
			Usage(syntax);
			return nullptr;
		}
		std::optional<int64_t> n = engine::admin::ParseInt(args[0]);
		size_t targetIndex = 1;
		if (!n && args.size() >= 2u)
		{
			n = engine::admin::ParseInt(args[1]);
			if (n)
			{
				std::swap(args[0], args[1]);
				targetIndex = 1;
			}
		}
		if (!n || (!allowNegative && *n < 0))
		{
			Usage(syntax);
			return nullptr;
		}
		outAmount = *n;
		return TargetOrSelf(targetIndex);
	}

	void GmCommandExecutor::SetLevel(ConnectedClient& target, uint32_t level)
	{
		const uint32_t levelMax = app.m_statsTables ? app.m_statsTables->levelMax : 100u;
		target.level = std::clamp<uint32_t>(level, 1u, levelMax);
		target.experiencePoints = 0;
		if (app.m_statsTables)
		{
			const engine::server::gameplay::Sex sex = (target.gender == "female")
				? engine::server::gameplay::Sex::Female : engine::server::gameplay::Sex::Male;
			if (const auto d = engine::server::gameplay::ComputeStats(*app.m_statsTables, target.factionId, target.classId, sex, target.level))
			{
				target.stats.maxHealth = d->hp;
				target.stats.currentHealth = d->hp; // soin complet, comme un passage de niveau
			}
		}
		app.RefreshLiveDerivedStats(target);
		app.RefreshMaxMoveSpeed(target);
		(void)app.SendPlayerStats(target);
		(void)app.SendPlayerXpUpdate(target);
		(void)app.SendClassProgression(target);
		SyncHealth(target);
		app.SaveConnectedClient(target, "gm_level");
	}

	void GmCommandExecutor::CmdLevel()
	{
		int64_t level = 0;
		ConnectedClient* target = ParseAmountAndTarget(level, "/level <niveau> [joueur]", false);
		if (target == nullptr)
			return;
		if (level < 1)
			return Usage("/level <niveau> [joueur] (niveau ≥ 1)");
		SetLevel(*target, static_cast<uint32_t>((std::min<int64_t>)(level, 100000)));
		Say("Niveau de " + NameOf(*target) + " fixé à " + std::to_string(target->level) + " (vie "
			+ std::to_string(target->stats.currentHealth) + "/" + std::to_string(target->stats.maxHealth) + ").");
		NotifyTarget(*target, "Un maître de jeu a fixé votre niveau à " + std::to_string(target->level) + ".");
	}

	void GmCommandExecutor::CmdXp()
	{
		int64_t amount = 0;
		ConnectedClient* target = ParseAmountAndTarget(amount, "/xp <points> [joueur]", false);
		if (target == nullptr)
			return;
		if (amount <= 0 || amount > 100000000)
			return Usage("/xp <points> [joueur] (1 à 100 000 000)");
		if (!app.m_statsTables)
			return Fail(GmCommandStatus::Unavailable, "Tables de progression absentes sur ce shard.");
		const uint32_t before = target->level;
		app.ApplyLevelUpsAfterXp(*target, static_cast<uint32_t>(amount));
		app.SaveConnectedClient(*target, "gm_xp");
		Say(NameOf(*target) + " gagne " + std::to_string(amount) + " XP — niveau " + std::to_string(target->level)
			+ (target->level > before ? " (+" + std::to_string(target->level - before) + ")" : std::string()) + ".");
		NotifyTarget(*target, "Un maître de jeu vous a accordé " + std::to_string(amount) + " points d'expérience.");
	}

	void GmCommandExecutor::CmdHeal()
	{
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		if ((target->stateFlags & kEntityStateDead) != 0u)
			return Fail(GmCommandStatus::Usage, NameOf(*target) + " est mort : utilisez /revive.");
		target->stats.currentHealth = target->stats.maxHealth;
		target->currentResource = target->maxResource;
		app.SendResourceUpdate(*target, true);
		SyncHealth(*target);
		Say(NameOf(*target) + " soigné (vie " + std::to_string(target->stats.maxHealth) + ", ressource "
			+ std::to_string(target->maxResource) + ").");
		NotifyTarget(*target, "Un maître de jeu vous a soigné.");
	}

	void GmCommandExecutor::CmdRevive()
	{
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		if ((target->stateFlags & kEntityStateDead) == 0u)
			return Fail(GmCommandStatus::Usage, NameOf(*target) + " n'est pas mort.");
		target->stateFlags &= ~kEntityStateDead;
		target->stats.currentHealth = target->stats.maxHealth;
		target->currentResource = target->maxResource;
		app.PurgeThreatForEntity(target->entityId);
		// Sur place : position imposée « réapparition » (ferme l'écran de mort même si
		// l'événement de résurrection se perd).
		app.m_antiCheat.Reset(target->persistenceCharacterKey);
		app.SendForcePosition(*target, kForcePositionReasonRespawn);
		app.SendResourceUpdate(*target, true);
		SyncHealth(*target);
		app.UpdateClientInterest(*target);
		app.SaveConnectedClient(*target, "gm_revive");
		Say(NameOf(*target) + " ressuscité sur place.");
		NotifyTarget(*target, "Un maître de jeu vous a ressuscité.");
	}

	void GmCommandExecutor::CmdKill()
	{
		// Sans argument : la dernière cible (mob ou joueur) de l'appelant.
		if (args.empty())
		{
			if (!RequireActor())
				return;
			if (MobEntity* mob = app.FindMobByEntityId(actor->lastTargetEntityId))
			{
				if ((mob->stateFlags & kEntityStateDead) != 0u || mob->stats.currentHealth == 0u)
					return Fail(GmCommandStatus::Usage, "Votre cible est déjà morte.");
				if (!mob->attackable)
					return Fail(GmCommandStatus::Denied, "Cette cible ne peut pas être tuée (PNJ d'ambiance).");
				const std::string label = MobLabel(*mob);
				app.ApplyPlayerDamageToMob(*actor, *mob, mob->stats.currentHealth, 0u);
				return Say(label + " tué (butin et expérience pour vous).");
			}
			if (ConnectedClient* p = app.FindClientByEntityId(actor->lastTargetEntityId))
				args = { NameOf(*p) };
			else
				return Usage("/kill [joueur] (sans argument : votre cible actuelle)");
		}
		ConnectedClient* target = FindPlayer(args[0]);
		if (target == nullptr)
			return Fail(GmCommandStatus::NotFound, "Joueur introuvable sur ce shard : " + args[0]);
		if ((target->stateFlags & kEntityStateDead) != 0u)
			return Fail(GmCommandStatus::Usage, NameOf(*target) + " est déjà mort.");
		target->stats.currentHealth = 0;
		target->stateFlags |= kEntityStateDead;
		app.PurgeThreatForEntity(target->entityId);
		if (!target->auras.empty())
		{
			target->auras.clear();
			app.BroadcastAuraUpdate(target->entityId, target->auras);
		}
		CombatEventMessage event{};
		event.attackerEntityId = actor != nullptr ? actor->entityId : 0;
		event.targetEntityId = target->entityId;
		event.damage = target->stats.maxHealth;
		event.targetCurrentHealth = 0;
		event.targetMaxHealth = target->stats.maxHealth;
		event.targetStateFlags = target->stateFlags;
		app.BroadcastCombatEvent(event);
		app.SaveConnectedClient(*target, "gm_kill");
		Say(NameOf(*target) + " tué.");
		NotifyTarget(*target, "Vous avez été tué par un maître de jeu.");
	}

	void GmCommandExecutor::CmdHp()
	{
		int64_t value = 0;
		ConnectedClient* target = ParseAmountAndTarget(value, "/hp <points de vie> [joueur]", false);
		if (target == nullptr)
			return;
		if ((target->stateFlags & kEntityStateDead) != 0u)
			return Fail(GmCommandStatus::Usage, NameOf(*target) + " est mort : utilisez /revive.");
		if (value < 1)
			return Usage("/hp <points de vie> [joueur] (au moins 1 ; /kill pour tuer)");
		target->stats.currentHealth = static_cast<uint32_t>((std::min<int64_t>)(value, target->stats.maxHealth));
		SyncHealth(*target);
		Say("Vie de " + NameOf(*target) + " : " + std::to_string(target->stats.currentHealth) + "/"
			+ std::to_string(target->stats.maxHealth) + ".");
	}

	void GmCommandExecutor::CmdMana()
	{
		int64_t value = 0;
		ConnectedClient* target = ParseAmountAndTarget(value, "/mana <points> [joueur]", false);
		if (target == nullptr)
			return;
		target->currentResource = static_cast<uint32_t>((std::min<int64_t>)(value, target->maxResource));
		app.SendResourceUpdate(*target, true);
		Say("Ressource de " + NameOf(*target) + " : " + std::to_string(target->currentResource) + "/"
			+ std::to_string(target->maxResource) + ".");
	}

	void GmCommandExecutor::TogglePower(bool gm::GmPlayerState::* field, const char* label, const char* syntax)
	{
		(void)syntax;
		// « [on|off] [joueur] » : un nom seul bascule ce joueur.
		std::optional<bool> wanted;
		size_t targetIndex = 0;
		if (!args.empty())
		{
			wanted = engine::admin::ParseOnOff(args[0]);
			if (wanted)
				targetIndex = 1;
		}
		ConnectedClient* target = TargetOrSelf(targetIndex);
		if (target == nullptr)
			return;
		const bool on = wanted.value_or(!(target->gm.*field));
		target->gm.*field = on;
		ApplyGmState(*target);
		// Devenir invisible ou invincible : les mobs qui le visaient l'oublient.
		if (on)
			app.PurgeThreatForEntity(target->entityId);
		Say(std::string(label) + (on ? " activé" : " désactivé") + " pour " + NameOf(*target) + ".");
		NotifyTarget(*target, std::string("Un maître de jeu a ") + (on ? "activé" : "désactivé") + " : " + label + ".");
	}

	void GmCommandExecutor::CmdInvincible() { TogglePower(&gm::GmPlayerState::invincible, "Invincibilité", "/invincible [on|off] [joueur]"); }
	void GmCommandExecutor::CmdInvisible() { TogglePower(&gm::GmPlayerState::invisible, "Invisibilité", "/invisible [on|off] [joueur]"); }
	void GmCommandExecutor::CmdNoCost() { TogglePower(&gm::GmPlayerState::noCost, "Sorts sans coût ni recharge", "/nocost [on|off] [joueur]"); }

	void GmCommandExecutor::CmdCooldown()
	{
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		const size_t n = target->spellCooldownUntilMs.size();
		target->spellCooldownUntilMs.clear();
		Say(std::to_string(n) + " temps de recharge remis à zéro pour " + NameOf(*target) + ".");
	}

	void GmCommandExecutor::CmdLearn()
	{
		if (args.empty())
			return Usage("/learn <compétence> [joueur] | /learn all [joueur]");
		ConnectedClient* target = TargetOrSelf(1);
		if (target == nullptr)
			return;
		const std::vector<ClassSkillDef>* skills = app.m_classSkills.GetClassSkills(target->classId);
		if (skills == nullptr || skills->empty())
			return Fail(GmCommandStatus::Unavailable, "Aucune compétence connue pour la classe « " + target->classId + " ».");
		auto tierTaken = [&](uint32_t tier) {
			for (const std::string& id : target->knownSkillIds)
			{
				const ClassSkillDef* k = app.m_classSkills.FindSkill(target->classId, id);
				if (k != nullptr && k->tier == tier)
					return true;
			}
			return false;
		};
		if (engine::admin::EqualsIgnoreCase(args[0], "all"))
		{
			// Un choix par palier débloqué (niveau ≥ palier) encore vide : le premier de la liste.
			size_t added = 0;
			for (const ClassSkillDef& sk : *skills)
			{
				if (sk.tier > target->level || tierTaken(sk.tier))
					continue;
				target->knownSkillIds.push_back(sk.skillId);
				++added;
			}
			(void)app.SendClassProgression(*target);
			app.SaveConnectedClient(*target, "gm_learn");
			return Say(std::to_string(added) + " compétence(s) ajoutée(s) à " + NameOf(*target)
				+ " (un choix par palier débloqué).");
		}
		const ClassSkillDef* sk = app.m_classSkills.FindSkill(target->classId, args[0]);
		if (sk == nullptr)
			return Fail(GmCommandStatus::NotFound, "Compétence inconnue pour la classe « " + target->classId + " » : " + args[0]);
		if (std::find(target->knownSkillIds.begin(), target->knownSkillIds.end(), sk->skillId) != target->knownSkillIds.end())
			return Fail(GmCommandStatus::Usage, NameOf(*target) + " connaît déjà " + sk->name + ".");
		// Un seul choix par palier : celui du même palier est remplacé.
		target->knownSkillIds.erase(std::remove_if(target->knownSkillIds.begin(), target->knownSkillIds.end(),
			[&](const std::string& id) {
				const ClassSkillDef* k = app.m_classSkills.FindSkill(target->classId, id);
				return k != nullptr && k->tier == sk->tier;
			}), target->knownSkillIds.end());
		target->knownSkillIds.push_back(sk->skillId);
		(void)app.SendClassProgression(*target);
		app.SaveConnectedClient(*target, "gm_learn");
		Say(NameOf(*target) + " apprend " + sk->name + " (palier " + std::to_string(sk->tier) + ").");
	}

	void GmCommandExecutor::CmdUnlearn()
	{
		if (args.empty())
			return Usage("/unlearn <compétence> [joueur]");
		ConnectedClient* target = TargetOrSelf(1);
		if (target == nullptr)
			return;
		const auto before = target->knownSkillIds.size();
		target->knownSkillIds.erase(std::remove_if(target->knownSkillIds.begin(), target->knownSkillIds.end(),
			[&](const std::string& id) { return engine::admin::EqualsIgnoreCase(id, args[0]); }), target->knownSkillIds.end());
		if (target->knownSkillIds.size() == before)
			return Fail(GmCommandStatus::NotFound, NameOf(*target) + " ne connaît pas « " + args[0] + " ».");
		(void)app.SendClassProgression(*target);
		app.SaveConnectedClient(*target, "gm_unlearn");
		Say("Compétence « " + args[0] + " » retirée à " + NameOf(*target) + ".");
	}

	void GmCommandExecutor::CmdResetSkills()
	{
		ConnectedClient* target = TargetOrSelf(0);
		if (target == nullptr)
			return;
		const size_t n = target->knownSkillIds.size();
		target->knownSkillIds.clear();
		(void)app.SendClassProgression(*target);
		app.SaveConnectedClient(*target, "gm_reset_skills");
		Say(std::to_string(n) + " compétence(s) retirée(s) à " + NameOf(*target) + " : choix à refaire.");
		NotifyTarget(*target, "Un maître de jeu a réinitialisé vos compétences : vous pouvez les choisir de nouveau.");
	}

	void GmCommandExecutor::CmdProfession()
	{
		if (args.size() < 2u)
			return Usage("/profession <métier> <niveau 1-300> [joueur]");
		const auto level = engine::admin::ParseInt(args[1]);
		if (!level || *level < 1 || *level > static_cast<int64_t>(kMaxProfessionLevel))
			return Usage("/profession <métier> <niveau 1-300> [joueur]");
		ConnectedClient* target = TargetOrSelf(2);
		if (target == nullptr)
			return;
		const std::string key = engine::admin::ToLowerAscii(args[0]);
		auto it = std::find_if(target->professions.begin(), target->professions.end(),
			[&](const ProfessionEntry& p) { return engine::admin::EqualsIgnoreCase(p.professionKey, key); });
		const bool added = it == target->professions.end();
		if (added)
		{
			ProfessionEntry entry;
			entry.professionKey = key;
			entry.isPrimary = target->professions.empty();
			target->professions.push_back(entry);
			it = target->professions.end() - 1;
		}
		it->skillLevel = static_cast<uint32_t>(*level);
		(void)app.SendProfessionUpdate(*target);
		app.SaveConnectedClient(*target, "gm_profession");
		Say("Métier « " + key + " » de " + NameOf(*target) + " : niveau " + std::to_string(it->skillLevel)
			+ (added ? " (appris)." : "."));
	}
}
