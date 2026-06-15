#include "src/client/skills/ClassSkillTreeUi.h"
#include "src/shared/core/Log.h"

#include <algorithm>

namespace engine::client
{
	bool ClassSkillTreeUiPresenter::Init(const ClassSkillCatalog* catalog)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[ClassSkillTreeUiPresenter] Init ignored: already initialized");
			return true;
		}
		if (catalog == nullptr)
		{
			LOG_WARN(Core, "[ClassSkillTreeUiPresenter] Init failed: catalog is nullptr");
			return false;
		}
		m_catalog = catalog;
		m_initialized = true;
		m_state = {};
		LOG_INFO(Core, "[ClassSkillTreeUiPresenter] Init OK");
		return true;
	}

	void ClassSkillTreeUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[ClassSkillTreeUiPresenter] Destroyed");
	}

	bool ClassSkillTreeUiPresenter::HasKnownSkillAtLevel(
	    const std::vector<std::string>& knownSkillIds,
	    const std::vector<ClassSkillDisplay>& allSkills,
	    uint32_t tier) const
	{
		for (const std::string& sid : knownSkillIds)
		{
			// Cherche ce skillId dans le vecteur complet de la classe.
			for (const ClassSkillDisplay& s : allSkills)
			{
				if (s.skillId == sid && s.level == tier)
				{
					return true;
				}
			}
		}
		return false;
	}

	void ClassSkillTreeUiPresenter::Sync(const std::string& classId,
	                                      const std::vector<std::string>& knownSkillIds,
	                                      uint32_t playerLevel)
	{
		if (!m_initialized)
		{
			return;
		}

		m_state.classId     = classId;
		m_state.playerLevel = playerLevel;
		m_state.branches.clear();

		// Rien à faire si la classe est vide ou inconnue du catalogue.
		if (classId.empty())
		{
			return;
		}

		const std::vector<ClassSkillDisplay>* allSkills = m_catalog->GetClassSkills(classId);
		if (allSkills == nullptr || allSkills->empty())
		{
			return;
		}

		// Construit les 3 branches dans l'ordre canonique.
		static const char* kBranchOrder[] = { "single", "aoe", "def" };
		for (const char* branchId : kBranchOrder)
		{
			SkillTreeBranch branch;
			branch.id = branchId;

			// Collecte les skills de cette branche, triés par tier.
			std::vector<const ClassSkillDisplay*> branchSkills;
			for (const ClassSkillDisplay& skill : *allSkills)
			{
				if (skill.branch == branchId)
				{
					branchSkills.push_back(&skill);
				}
			}
			std::sort(branchSkills.begin(), branchSkills.end(),
			    [](const ClassSkillDisplay* a, const ClassSkillDisplay* b) {
				    return a->tier < b->tier;
			    });

			// Construit les cellules avec leur statut.
			for (const ClassSkillDisplay* skillPtr : branchSkills)
			{
				SkillTreeCell cell;
				cell.skill = *skillPtr;

				// Statut : Chosen si le skillId est dans knownSkillIds.
				const bool isChosen = std::find(knownSkillIds.begin(), knownSkillIds.end(),
				                                skillPtr->skillId) != knownSkillIds.end();
				if (isChosen)
				{
					cell.status = SkillCellStatus::Chosen;
				}
				else if (skillPtr->tier <= playerLevel
				         && !HasKnownSkillAtLevel(knownSkillIds, *allSkills, skillPtr->tier))
				{
					// Tier accessible ET aucun skill connu pour ce niveau → choisissable.
					cell.status = SkillCellStatus::Available;
				}
				else
				{
					// Tier > playerLevel ou ce niveau est déjà pourvu par un autre skill.
					cell.status = SkillCellStatus::Locked;
				}

				branch.tiers.push_back(std::move(cell));
			}

			m_state.branches.push_back(std::move(branch));
		}
	}

	void ClassSkillTreeUiPresenter::ChooseSkill(uint32_t level, const std::string& skillId)
	{
		if (!m_initialized)
		{
			return;
		}
		if (m_choose)
		{
			(void)m_choose(level, skillId);
		}
	}
}
