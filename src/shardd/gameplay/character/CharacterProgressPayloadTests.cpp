// ETAPE2.1 — tests du texte d'état d'un personnage : charge utile « progression »
// stockée en base (character_progress.progress_payload) et texte complet du
// fichier .ini. Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/gameplay/character/CharacterPersistence.h"

#include "src/shared/core/Config.h"

#include <cstdio>
#include <string>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	/// État représentatif : une donnée dans chaque domaine.
	engine::server::PersistedCharacterState MakeState()
	{
		using namespace engine::server;
		PersistedCharacterState state{};
		state.characterKey = 42;
		state.zoneId = 3;
		state.positionMetersX = 10.5f;
		state.experiencePoints = 1234;
		state.level = 7;
		state.gold = 900;
		state.stats.currentHealth = 55;
		state.stats.maxHealth = 120;
		state.inventory.push_back(ItemStack{ 101, 4 });
		state.equipment[2] = 5001;
		QuestState quest{};
		quest.questId = "q_loup";
		quest.status = QuestStatus::Active;
		quest.completedAtEpochMs = 77;
		quest.stepProgressCounts = { 2, 5 };
		state.questStates.push_back(quest);
		state.chatIgnoredDisplayNames = { "Bavard" };
		state.chatModeratorRole = true;
		state.actionBarLayout[0] = "spell_feu";
		state.beltLayout = { "item:12", "", "item:13" };
		state.cakeExpiresAtMsUtcByItemId[5101] = 999;
		state.knownSkillIds = { "skill_a", "skill_b" };
		ProfessionEntry prof{};
		prof.professionKey = "forge";
		prof.skillLevel = 12;
		prof.isPrimary = true;
		state.professions.push_back(prof);
		state.mailboxGold = 30;
		state.mailboxItems.push_back(ItemStack{ 7, 1 });
		return state;
	}
}

int main()
{
	using namespace engine::server;
	const PersistedCharacterState source = MakeState();

	// 1. La charge utile ne contient pas ce qui a ses propres colonnes ou tables.
	const std::string payload = SerializeCharacterProgressPayload(source);
	Check(payload.find("character.gold") == std::string::npos, "payload sans bourse");
	Check(payload.find("inventory.") == std::string::npos, "payload sans sac");
	Check(payload.find("equipment.") == std::string::npos, "payload sans équipement");
	Check(payload.find("mailbox.") == std::string::npos, "payload sans courrier");
	Check(payload.find("character.position") == std::string::npos, "payload sans position");

	// 2. Aller-retour : les champs de progression reviennent, le reste est intact.
	PersistedCharacterState target{};
	target.characterKey = 42;
	target.gold = 1;               // ne doit pas bouger
	target.inventory.push_back(ItemStack{ 9, 9 });
	ApplyCharacterProgressPayload(payload, target);
	Check(target.gold == 1u, "bourse non touchée par la charge utile");
	Check(target.inventory.size() == 1u && target.inventory[0].itemId == 9u, "sac non touché");
	Check(target.questStates.size() == 1u, "quête relue");
	if (!target.questStates.empty())
	{
		const QuestState& q = target.questStates[0];
		Check(q.questId == "q_loup", "id de quête");
		Check(q.status == QuestStatus::Active, "statut de quête");
		Check(q.completedAtEpochMs == 77u, "date de complétion");
		Check(q.stepProgressCounts.size() == 2u && q.stepProgressCounts[1] == 5u, "progression des étapes");
	}
	Check(target.chatIgnoredDisplayNames.size() == 1u && target.chatIgnoredDisplayNames[0] == "Bavard", "ignorés");
	Check(target.actionBarLayout[0] == "spell_feu", "barre d'action");
	Check(target.beltLayout.size() == 3u && target.beltLayout[2] == "item:13", "ceinture");
	Check(target.cakeExpiresAtMsUtcByItemId.count(5101u) == 1u
		&& target.cakeExpiresAtMsUtcByItemId.at(5101u) == 999u, "gâteaux");
	Check(target.knownSkillIds.size() == 2u && target.knownSkillIds[1] == "skill_b", "compétences");
	Check(target.professions.size() == 1u && target.professions[0].professionKey == "forge"
		&& target.professions[0].skillLevel == 12u && target.professions[0].isPrimary, "métiers");

	// 3. Charge utile vide (personnage neuf) : valeurs par défaut, pas de plantage.
	PersistedCharacterState empty{};
	ApplyCharacterProgressPayload("", empty);
	Check(empty.questStates.empty(), "charge utile vide : pas de quête");
	Check(empty.beltLayout.size() == 4u, "charge utile vide : ceinture historique à 4 cases");

	// 4. Texte complet (.ini) : aller-retour fidèle, courrier compris.
	engine::core::Config ini;
	ini.LoadFromIniString(SerializeCharacterState(source));
	PersistedCharacterState fromIni{};
	ReadCharacterStateFromConfig(ini, 42, fromIni);
	Check(fromIni.zoneId == 3u, "ini : zone");
	Check(fromIni.experiencePoints == 1234u, "ini : XP");
	Check(fromIni.level == 7u, "ini : niveau");
	Check(fromIni.gold == 900u, "ini : bourse");
	Check(fromIni.stats.currentHealth == 55u && fromIni.stats.maxHealth == 120u, "ini : PV");
	Check(fromIni.inventory.size() == 1u && fromIni.inventory[0].quantity == 4u, "ini : sac");
	Check(fromIni.equipment[2] == 5001u, "ini : équipement");
	Check(fromIni.chatModeratorRole, "ini : rôle modérateur");
	Check(fromIni.mailboxGold == 30u && fromIni.mailboxItems.size() == 1u, "ini : courrier");
	Check(fromIni.professions.size() == 1u, "ini : métiers");

	if (s_failCount == 0)
		std::puts("[OK] character_progress_payload_tests");
	return s_failCount == 0 ? 0 : 1;
}
