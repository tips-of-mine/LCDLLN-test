#pragma once
// CMANGOS.39 (Phase 4.39 step 3+4) — Presenter client de la skill book.
// Maintient un cache local des skills (value/cap/bonus) et expose les
// reponses serveur (List/Learn/Use) ainsi qu'un indicateur transitoire
// du dernier resultat de Use (Success/Fail/Crit).
//
// Pas de rendu ImGui : le panneau est drawe par SkillBookImGuiRenderer qui
// lit l'etat via GetState() et propage les inputs UI via les methodes du
// presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans QuestUiPresenter
// et ReputationUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes 114/116/118/119
// vers les OnXxx du presenter.

#include "src/shared/network/SkillPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace engine::client
{
	/// Une entree de la skill book exposable au layer UI.
	/// Mirror direct de SkillBookEntry sur le wire avec un nom resolu cote
	/// client (table statique GetSkillName(skillId)).
	struct SkillBookEntryView
	{
		uint16_t    skillId = 0;
		uint16_t    value   = 0; ///< niveau actuel.
		uint16_t    cap     = 0; ///< cap dur (max attribuable a ce moment).
		uint16_t    bonus   = 0; ///< bonus temporaire (potion, equipement).
		std::string name;        ///< nom localise (FR) ou "Skill #N".
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct SkillBookState
	{
		std::vector<SkillBookEntryView> skills;
		bool                            isLoading = false;
		std::string                     lastErrorText;            ///< Vide si pas d'erreur transitoire.
		/// Indicateur du dernier Use : 0=Success, 1=Fail, 2=Crit. nullopt = pas d'indicateur actif.
		std::optional<uint8_t>          lastUseResult;
		uint16_t                        lastUseDelta       = 0;   ///< delta du dernier Use (0 si pas de gain).
		uint16_t                        lastUseSkillId     = 0;   ///< skillId du dernier Use (pour highlight).
		float                           lastUseExpireAt    = 0.0f;///< game seconds (cf. TickIndicator).
		bool                            layoutValid        = false;
	};

	/// Resout le nom localise d'un skill par son id. V1 : table hardcode des
	/// 5 skills du starter set ; les ids inconnus retournent "Skill #N".
	std::string GetSkillName(uint16_t skillId);

	/// Presenter pour le panneau Skill Book cote client. Doit etre Init()
	/// avant tout usage du callback. Thread : main.
	class SkillBookUiPresenter final
	{
	public:
		SkillBookUiPresenter() = default;

		SkillBookUiPresenter(const SkillBookUiPresenter&)            = delete;
		SkillBookUiPresenter& operator=(const SkillBookUiPresenter&) = delete;

		~SkillBookUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.39 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestList / RequestLearn / RequestUse.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie SKILLS_LIST_REQUEST. Reponse via OnListResponse.
		/// Met aussi m_state.isLoading = true le temps de la reponse.
		void RequestList();

		/// Envoie SKILL_LEARN_REQUEST pour \p skillId. Reponse via OnLearnResponse.
		void RequestLearn(uint16_t skillId);

		/// Envoie SKILL_USE_REQUEST pour \p skillId avec target opaque \p targetEntityId.
		/// V1 : target est log seulement cote serveur, pas de check.
		/// Reponse via OnUseResponse (et eventuellement push UpgradeNotification).
		void RequestUse(uint16_t skillId, uint64_t targetEntityId = 0u);

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit SKILLS_LIST_RESPONSE. Remplace la cache locale.
		void OnListResponse(const engine::network::SkillsListResponsePayload& resp);

		/// Recoit SKILL_LEARN_RESPONSE. Met a jour ou insere un skill avec value=0
		/// si error == Ok ; affiche un message transitoire sinon.
		void OnLearnResponse(const engine::network::SkillLearnResponsePayload& resp);

		/// Recoit SKILL_USE_RESPONSE. Arme l'indicateur transitoire (Success/Fail/Crit).
		void OnUseResponse(const engine::network::SkillUseResponsePayload& resp);

		/// Recoit un push SKILL_UPGRADE_NOTIFICATION : update entry locale +
		/// arme l'indicateur si delta > 0 (gain visible cote UI).
		void OnUpgradeNotification(const engine::network::SkillUpgradeNotificationPayload& note);

		// ---------------------------------------------------------------------
		// Tick / state access
		// ---------------------------------------------------------------------

		/// Avance le compteur de l'indicateur Use et clear l'overlay si expire.
		/// \p deltaSeconds est le dt frame (en secondes). A appeler chaque frame
		/// depuis Engine.
		void TickIndicator(float deltaSeconds);

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const SkillBookState& GetState() const { return m_state; }

	private:
		/// Reconstruit m_state.skills depuis une SkillsListResponsePayload.
		void RebuildSkillsFromResponse(const engine::network::SkillsListResponsePayload& resp);

		/// Met a jour ou insere une entree apres push notification ou Learn ack.
		void UpdateOrInsertEntry(uint16_t skillId, uint16_t newValue, uint16_t newCap, uint16_t bonus = 0u);

		/// Arme l'indicateur Use pour ~2s.
		void ArmUseIndicator(uint16_t skillId, uint8_t result, uint16_t delta);

		bool                  m_initialized       = false;
		SkillBookState        m_state{};
		SendCallback          m_send;
		float                 m_clockSeconds      = 0.0f; ///< Cumul depuis Init() (utilise pour comparer expiry).
	};
}
