#include "src/client/grimoire/GrimoireUi.h"
#include "src/client/gameplay/ActionBarLayout.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <cctype>

namespace engine::client
{
	bool IsCasterProfile(const std::string& profileId)
	{
		return profileId == "lanceur" || profileId == "healer" || profileId == "sacre";
	}

	bool GrimoireUiPresenter::Init(const SpellKitCatalog* catalog)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[GrimoireUiPresenter] Init ignored: already initialized");
			return true;
		}
		m_catalog = catalog;
		m_initialized = true;
		m_state = {};
		LOG_INFO(Core, "[GrimoireUiPresenter] Init OK");
		return true;
	}

	void GrimoireUiPresenter::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}
		m_initialized = false;
		m_state = {};
		LOG_INFO(Core, "[GrimoireUiPresenter] Destroyed");
	}

	void GrimoireUiPresenter::RebuildSpells()
	{
		m_state.spells.clear();
		if (m_catalog == nullptr || m_state.profileId.empty())
		{
			return;
		}
		const std::vector<SpellDisplay>* kit = m_catalog->FindKit(m_state.profileId);
		if (kit != nullptr)
		{
			m_state.spells = *kit;
		}
	}

	void GrimoireUiPresenter::Sync(const std::string& profileId, const std::array<std::string, 10>& serverLayout)
	{
		if (!m_initialized)
		{
			return;
		}
		const bool profileChanged = (profileId != m_state.profileId);
		if (profileChanged)
		{
			m_state.profileId = profileId;
			m_state.isCaster = IsCasterProfile(profileId);
			RebuildSpells();
		}
		// Ne re-résout les slots que si le layout serveur a réellement changé
		// (nouvel ACK / enter-world) ou au premier Sync / changement de profil ;
		// sinon on conserve l'assignation optimiste posée par AssignSlot.
		if (!m_syncedOnce || profileChanged || serverLayout != m_lastServerLayout)
		{
			m_lastServerLayout = serverLayout;
			m_syncedOnce = true;
			m_state.slots = ResolveActionBarLayout(serverLayout, m_state.spells);
		}
	}

	void GrimoireUiPresenter::AssignSlot(uint32_t slot, const std::string& spellId)
	{
		if (!m_initialized || slot >= m_state.slots.size())
		{
			return;
		}
		// Unicité : retire le sort d'un autre slot.
		if (!spellId.empty())
		{
			for (std::string& s : m_state.slots)
			{
				if (s == spellId)
				{
					s.clear();
				}
			}
		}
		m_state.slots[slot] = spellId; // mise à jour optimiste
		if (m_send)
		{
			(void)m_send(m_state.slots);
		}
	}

	void GrimoireUiPresenter::SetSearchFilter(const std::string& filter)
	{
		std::string lowered = filter;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		m_state.searchFilter = lowered;
	}
}
