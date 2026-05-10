#pragma once
// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - Presenter client de la fenetre Loot
// Roll. Maintient la liste des rolls actives en attente de choix joueur +
// dernier resultat recu (pour toast 5s).
//
// Pas de rendu ImGui : le panneau est drawe par LootRollImGuiRenderer qui lit
// l'etat via GetState() et propage les inputs UI (Choose / SimulateRoll) via
// les methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans GuildUi).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes
// 182/184/185/187 vers les OnXxx du presenter.

#include "src/shared/network/LootPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Roll en attente de choix joueur expose au layer UI.
	/// Mirror direct de engine::network::LootRollNotificationPayload + un
	/// horodatage local d'expiration et le choix utilisateur s'il a deja
	/// clique.
	struct PendingRoll
	{
		uint64_t              rollId         = 0;
		uint32_t              itemTemplateId = 0;
		std::string           itemName;
		uint32_t              count          = 0;
		/// Steady_clock now en ms a la reception + durationSec*1000.
		/// Permet d'afficher "Time left: 12s" en decomptant.
		uint64_t              expiresAtMs    = 0;
		/// Set apres un click utilisateur (Need/Greed/Pass). Le presenter
		/// laisse la pending visible jusqu'au resultat (lastResult*).
		std::optional<uint8_t> myChoice;
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct LootRollState
	{
		/// Liste des rolls en attente de choix. Trie par ordre d'arrivee.
		std::vector<PendingRoll> pendingRolls;

		/// Toast sur le dernier resultat recu : steady_clock now en ms a la
		/// reception. Le toast est rendu pendant 5s puis disparait. Un seul
		/// toast (le dernier ecrase le precedent V1).
		std::optional<uint64_t> lastResultTimeMs;
		uint64_t                lastResultRollId       = 0;
		std::string             lastResultWinnerName;
		uint8_t                 lastResultWinnerChoice = 0;
		uint8_t                 lastResultWinnerRoll   = 0;
		std::string             lastResultItemName;
		uint32_t                lastResultCount        = 0;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire. Vide par defaut.
		std::string lastInfoText;
	};

	/// Static helper : retourne le libelle ASCII pour un choice (0=Pass,
	/// 1=Greed, 2=Need). Pour un choice hors plage, retourne "?".
	const char* LootChoiceName(uint8_t choice);

	/// Presenter pour la fenetre Loot Roll cote client. Doit etre Init() avant
	/// tout usage du callback. Thread : main (comme les autres presenters UI).
	class LootRollUiPresenter final
	{
	public:
		LootRollUiPresenter() = default;

		LootRollUiPresenter(const LootRollUiPresenter&)            = delete;
		LootRollUiPresenter& operator=(const LootRollUiPresenter&) = delete;

		~LootRollUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.17 step 3+4 Loot)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout Choose / SimulateRoll.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie LOOT_ROLL_CHOICE_REQUEST (opcode 183). Marque la pending
		/// roll \p rollId comme "myChoice set" pour griser les boutons cote
		/// renderer. Reponse via OnChoiceResponse.
		///
		/// \param rollId  identifiant de la roll cible.
		/// \param choice  0=Pass, 1=Greed, 2=Need.
		void Choose(uint64_t rollId, uint8_t choice);

		/// Envoie LOOT_SIMULATE_ROLL_REQUEST (opcode 186). Outil dev V1 : le
		/// master cree une nouvelle roll avec le creator comme seul eligible.
		/// Reponse via OnSimulateRollResponse, et push RollNotification recue
		/// via OnRollNotification.
		void SimulateRoll();

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit LOOT_ROLL_CHOICE_RESPONSE (opcode 184). Met a jour
		/// lastErrorText si !=Ok. Sinon ne fait rien (la roll sera retiree
		/// du cache au moment du RollResultNotification).
		void OnChoiceResponse(const engine::network::LootRollChoiceResponsePayload& resp);

		/// Recoit LOOT_SIMULATE_ROLL_RESPONSE (opcode 187). Met a jour
		/// lastErrorText si !=Ok, ou lastInfoText "Roll simulated" si Ok.
		void OnSimulateRollResponse(const engine::network::LootSimulateRollResponsePayload& resp);

		/// Recoit un push LOOT_ROLL_NOTIFICATION (opcode 182). Ajoute une
		/// nouvelle PendingRoll au cache (calcule expiresAtMs = now + duration).
		void OnRollNotification(const engine::network::LootRollNotificationPayload& note);

		/// Recoit un push LOOT_ROLL_RESULT_NOTIFICATION (opcode 185). Retire
		/// la PendingRoll du cache (par rollId) + met a jour lastResult* pour
		/// le toast UI.
		void OnRollResultNotification(const engine::network::LootRollResultNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const LootRollState& GetState() const { return m_state; }

	private:
		bool             m_initialized = false;
		LootRollState    m_state{};
		SendCallback     m_send;
	};
}
