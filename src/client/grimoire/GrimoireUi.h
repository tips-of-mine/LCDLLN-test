#pragma once
// Grimoire / Carnet de techniques — presenter client du livre de sorts +
// assignation des 10 slots de barre d'action. Pas de rendu ImGui (cf.
// GrimoireImGuiRenderer). Lit le kit via SpellKitCatalog + le layout autoritaire
// du UIModel ; émet SetActionBarLayout via un callback (mis à jour optimiste).

#include "src/client/gameplay/SpellKitCatalog.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::client
{
	/// État snapshot exposé au renderer.
	struct GrimoireState
	{
		std::string profileId;                 ///< profil courant ("" = pas de barre).
		bool isCaster = false;                 ///< lanceur/healer/sacre → thème Grimoire.
		std::vector<SpellDisplay> spells;      ///< sorts connus (copie du kit).
		std::array<std::string, 10> slots{};   ///< layout résolu (slot i → spellId).
		std::string searchFilter;              ///< filtre de recherche courant (minuscule).
	};

	/// Presenter du Grimoire. Init() avant usage. Thread : main.
	class GrimoireUiPresenter final
	{
	public:
		GrimoireUiPresenter() = default;
		GrimoireUiPresenter(const GrimoireUiPresenter&) = delete;
		GrimoireUiPresenter& operator=(const GrimoireUiPresenter&) = delete;

		bool Init(const SpellKitCatalog* catalog);
		void Shutdown();
		bool IsInitialized() const { return m_initialized; }

		/// Callback d'envoi : (clientId, 10 slots) → true si émis.
		using SendCallback = std::function<bool(const std::array<std::string, 10>&)>;
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Recalcule l'état depuis le profil + le layout autoritaire du serveur.
		/// À appeler chaque frame (ou sur changement de stats). \p layout = layout
		/// reçu (UIModel) ; "" partout = défaut (ordre du kit).
		void Sync(const std::string& profileId, const std::array<std::string, 10>& serverLayout);

		/// SP-C — surcharge avec kit explicite (compétences de classe connues).
		/// Si \p explicitKit est non vide, l'utilise comme source de sorts au lieu
		/// de FindKit(profileId) ; sinon comportement identique à la surcharge sans kit.
		/// Permet à la barre d'action et au Grimoire d'afficher les compétences de
		/// classe connues avec fallback kit profil transparent.
		void Sync(const std::string& profileId, const std::array<std::string, 10>& serverLayout,
			const std::vector<SpellDisplay>& explicitKit);

		/// Assigne \p spellId au \p slot (0-9) — mise à jour optimiste + envoi.
		/// Retire \p spellId d'un autre slot (unicité). spellId "" = vider le slot.
		void AssignSlot(uint32_t slot, const std::string& spellId);

		/// Met à jour le filtre de recherche (comparaison insensible à la casse).
		void SetSearchFilter(const std::string& filter);

		const GrimoireState& GetState() const { return m_state; }

	private:
		void RebuildSpells();

		bool m_initialized = false;
		const SpellKitCatalog* m_catalog = nullptr;
		GrimoireState m_state{};
		SendCallback m_send;
		/// Dernier layout serveur vu : Sync ne re-résout les slots que lorsqu'il
		/// change (sinon l'assignation optimiste serait écrasée chaque frame).
		std::array<std::string, 10> m_lastServerLayout{};
		bool m_syncedOnce = false;
	};

	/// true si le profil est un profil de caster (lanceur/healer/sacre).
	bool IsCasterProfile(const std::string& profileId);
}
