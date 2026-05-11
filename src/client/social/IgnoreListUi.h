#pragma once
// CMANGOS.25 (Phase 3.25 step 3+4) — Presenter client de la liste d'ignore.
// Maintient un cache local des account_id ignores recu via IGNORE_LIST_RESPONSE,
// et expose Ignore/Unignore/RequestIgnoreList qui envoient les requests via un
// callback fire-and-forget (cf. m_send dans QuestUiPresenter).
//
// Le presenter ne fait pas de rendu ImGui directement (le panneau Friends
// pourrait l'afficher dans une PR ulterieure). Sa principale utilite cote
// client est la cache locale (`IsIgnoredLocal`) — utile par exemple pour
// griser un bouton "ignorer" dans un menu contextuel ou afficher visuellement
// les whispers consommes mais bloques.

#include "src/shared/network/IgnoreListPayloads.h"

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

namespace engine::client
{
	/// Etat snapshot exposable au layer UI. La V1 ne fait que lister les
	/// account_id ; un futur enrichissement pourra resoudre name/last_seen.
	struct IgnoreListPanelState
	{
		std::vector<uint64_t> ignoredAccountIds;
		bool                  layoutValid = false;
	};

	/// Presenter pour la liste d'ignore client-side. Doit etre Init() avant tout
	/// usage du callback. Thread : main (comme les autres presenters UI).
	class IgnoreListUiPresenter final
	{
	public:
		IgnoreListUiPresenter() = default;

		IgnoreListUiPresenter(const IgnoreListUiPresenter&)            = delete;
		IgnoreListUiPresenter& operator=(const IgnoreListUiPresenter&) = delete;

		~IgnoreListUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.25 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback. Mirror du pattern QuestUi/MailUi.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant Ignore/Unignore/RequestIgnoreList.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie un IGNORE_ADD_REQUEST au master.
		void IgnoreAccount(uint64_t targetAccountId);

		/// Envoie un IGNORE_REMOVE_REQUEST au master.
		void UnignoreAccount(uint64_t targetAccountId);

		/// Envoie un IGNORE_LIST_REQUEST (a faire au login + manuellement via /ignore list).
		void RequestIgnoreList();

		/// Recoit IGNORE_ADD_RESPONSE : insere dans la cache locale si OK.
		void OnIgnoreAddResponse(const engine::network::IgnoreAddResponsePayload& resp);

		/// Recoit IGNORE_REMOVE_RESPONSE : retire de la cache locale si OK.
		void OnIgnoreRemoveResponse(const engine::network::IgnoreRemoveResponsePayload& resp);

		/// Recoit IGNORE_LIST_RESPONSE : remplace la cache locale.
		void OnIgnoreListResponse(const engine::network::IgnoreListResponsePayload& resp);

		/// Indique si \p accountId est dans la cache locale d'ignore. Utilise pour
		/// filtrer cote client (par ex. griser un bouton "envoyer un whisper").
		/// Le serveur applique aussi le filtre (autoritaire) dans ChatRelayHandler.
		bool IsIgnoredLocal(uint64_t accountId) const;

		/// Acces lecture seule au snapshot pour un layer ImGui de panneau social.
		const IgnoreListPanelState& GetState() const { return m_state; }

		/// Snapshot brut du set (utile pour debug overlay et tests).
		const std::unordered_set<uint64_t>& GetCachedSet() const { return m_ignoredAccountIds; }

	private:
		/// Met a jour \ref m_state.ignoredAccountIds depuis \ref m_ignoredAccountIds.
		void RebuildState();

		bool                          m_initialized = false;
		IgnoreListPanelState          m_state{};
		std::unordered_set<uint64_t>  m_ignoredAccountIds;
		SendCallback                  m_send;
	};
}
