#pragma once
// CMANGOS.42 (Phase 4.42 step 3+4) — Presenter client de la fenetre Weather.
// Maintient un cache local des zones meteo + subscriptions actives + zone
// active (pour le HUD top-right) + dernier state recu (kind + intensity).
//
// Pas de rendu ImGui : le panneau et le HUD sont drawes par
// WeatherImGuiRenderer qui lit l'etat via GetState() et propage les inputs
// UI (RequestList / Subscribe / Unsubscribe / SetActiveZone) via les
// methodes du presenter.
//
// Send : fire-and-forget via un callback (cf. m_send dans OutdoorPvpUiPresenter).
// Receive : Engine::SetMasterPushHandler dispatche les opcodes
// 151/153/155/156 vers les OnXxx du presenter.

#include "src/shared/network/WeatherPayloads.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::client
{
	/// Resume d'une zone meteo exposable au layer UI. Mirror direct
	/// de engine::network::WeatherZoneSummary.
	struct WeatherZoneSummary
	{
		uint32_t    zoneId    = 0;
		std::string name;
		uint8_t     kind      = 0;     ///< 0=Clear, 1=Rain, 2=Snow, 3=Storm, 4=Sandstorm, 5=Fog.
		float       intensity = 0.0f;
	};

	/// Etat snapshot expose au renderer ImGui. Le panneau lit ces champs en
	/// lecture seule et appelle les methodes du presenter pour les muter.
	struct WeatherUiState
	{
		std::vector<WeatherZoneSummary>   zones;
		bool                              zonesLoaded = false;

		/// Subscriptions actives cote client (pour synchroniser les boutons
		/// Subscribe/Unsubscribe). Mis a jour aux OnSubscribeResponse Ok /
		/// OnUnsubscribeResponse Ok.
		std::unordered_set<uint32_t>      subscribedZones;

		/// Zone actuellement affichee dans le HUD top-right. Si non-set,
		/// le HUD n'est pas rendu (panneau peut rester ouvert sans HUD).
		std::optional<uint32_t>           activeZoneId;
		uint8_t                           activeKind      = 0;     ///< Clear par defaut.
		float                             activeIntensity = 0.0f;

		/// Vide si pas d'erreur transitoire. Sinon affiche en rouge.
		std::string lastErrorText;
		/// Texte d'info transitoire (e.g. "Abonne a Stormwind."). Vide par defaut.
		std::string lastInfoText;
	};

	/// Static helper : retourne le libelle ASCII en anglais pour un kind donne.
	/// Inputs invalides (kind > 5) renvoient "?".
	const char* WeatherKindName(uint8_t kind);

	/// Presenter pour la fenetre Weather cote client. Doit etre Init()
	/// avant tout usage du callback. Thread : main (comme les autres presenters UI).
	class WeatherUiPresenter final
	{
	public:
		WeatherUiPresenter() = default;

		WeatherUiPresenter(const WeatherUiPresenter&)            = delete;
		WeatherUiPresenter& operator=(const WeatherUiPresenter&) = delete;

		~WeatherUiPresenter();

		/// Initialise le presenter. Idempotent (LOG_WARN si appele 2x).
		bool Init();

		/// Libere le state. Apres Shutdown, IsInitialized() == false.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ---------------------------------------------------------------------
		// Network wiring (CMANGOS.42 step 3+4)
		// ---------------------------------------------------------------------

		/// Callback fire-and-forget : (opcode, payload) sur la connexion master.
		/// Cable via \ref SetSendCallback.
		using SendCallback = std::function<bool(uint16_t opcode, const std::vector<uint8_t>& payload)>;

		/// Cable le callback pour fire-and-forget des requetes au master.
		/// Doit etre appele avant tout RequestList / Subscribe / Unsubscribe.
		void SetSendCallback(SendCallback cb) { m_send = std::move(cb); }

		/// Envoie WEATHER_LIST_REQUEST. Reponse via OnListResponse.
		void RequestList();

		/// Envoie WEATHER_SUBSCRIBE_REQUEST avec \p zoneId. Reponse via
		/// OnSubscribeResponse. Le state est mis a jour cote client a
		/// reception de la reponse Ok.
		void Subscribe(uint32_t zoneId);

		/// Envoie WEATHER_UNSUBSCRIBE_REQUEST avec \p zoneId. Reponse via
		/// OnUnsubscribeResponse.
		void Unsubscribe(uint32_t zoneId);

		/// Selectionne la zone active pour le HUD. Pas d'envoi reseau (purement
		/// cote client). Le HUD lit activeZoneId + activeKind + activeIntensity ;
		/// les valeurs activeKind/activeIntensity sont initialisees depuis
		/// le cache zones[zoneId] et mises a jour a chaque OnUpdateNotification
		/// pour cette zone. Si la zone n'est pas dans le cache, activeZoneId
		/// est quand meme set ; le HUD affichera "?".
		void SetActiveZone(uint32_t zoneId);

		/// Reset l'activeZoneId. Le HUD ne sera plus rendu.
		void ClearActiveZone();

		// ---------------------------------------------------------------------
		// Master responses / push
		// ---------------------------------------------------------------------

		/// Recoit WEATHER_LIST_RESPONSE. Remplace le cache local.
		void OnListResponse(const engine::network::WeatherListResponsePayload& resp);

		/// Recoit WEATHER_SUBSCRIBE_RESPONSE. Si Ok, ajoute zoneId a la
		/// liste des subscriptions locale et met a jour le cache zones (kind
		/// + intensity du state retourne par le master).
		void OnSubscribeResponse(const engine::network::WeatherSubscribeResponsePayload& resp);

		/// Recoit WEATHER_UNSUBSCRIBE_RESPONSE. Si Ok, retire zoneId.
		void OnUnsubscribeResponse(const engine::network::WeatherUnsubscribeResponsePayload& resp);

		/// Recoit un push WEATHER_UPDATE_NOTIFICATION : update kind +
		/// intensity de la zone dans le cache local. Si activeZoneId == zoneId,
		/// met aussi a jour le HUD (activeKind + activeIntensity).
		void OnUpdateNotification(const engine::network::WeatherUpdateNotificationPayload& note);

		// ---------------------------------------------------------------------
		// State access
		// ---------------------------------------------------------------------

		/// Snapshot lecture seule de l'etat courant pour le renderer.
		const WeatherUiState& GetState() const { return m_state; }

	private:
		/// Memorise le zoneId du Subscribe/Unsubscribe pendant pour pouvoir
		/// updater le state (subscribedZones + cache kind/intensity) au
		/// retour Ok.
		uint32_t m_pendingSubscribeZoneId   = 0;
		uint32_t m_pendingUnsubscribeZoneId = 0;

		bool             m_initialized = false;
		WeatherUiState   m_state{};
		SendCallback     m_send;
	};
}
