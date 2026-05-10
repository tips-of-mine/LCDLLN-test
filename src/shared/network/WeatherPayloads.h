#pragma once
// CMANGOS.42 (Phase 4.42 step 3+4) — Wire payloads pour les opcodes Weather
// (150-156). 3 paires Request/Response + 1 push notification :
//   - List                              (150/151)
//   - Subscribe                         (152/153)
//   - Unsubscribe                       (154/155)
//   - UpdateNotification                (156 push) : push Master to Client
//     changement meteo (zoneId, kind, intensity).
//
// Le master tient en memoire un WeatherManager (V1 : 3 zones hardcodees).
// Au reboot, subscriptions et states sont reinitialises. Acceptable V1.
//
// Format wire : ByteReader/ByteWriter little-endian. Toutes les strings
// passent par WriteString/ReadString (uint16 length + UTF-8 bytes), et
// toutes les arrays par WriteArrayCount/ReadArrayCount (uint16 count).
// Les float32 (intensity 0..1) sont serialises bit-a-bit en little-endian
// via WriteBytes / ReadBytes sur 4 octets (cf. CharacterPayloads.cpp pour
// le pattern existant ; pas de helper WriteFloat dans ByteWriter).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::network
{
	// =========================================================================
	// Codes d'erreur — wire-level pour Weather.
	// =========================================================================

	/// Code d'erreur generique pour les opcodes Weather. 0 = OK.
	enum class WeatherErrorCode : uint8_t
	{
		Ok            = 0,
		UnknownZone   = 1, ///< zoneId pas dans la liste hardcodee V1.
		NotSubscribed = 2, ///< Tentative d'unsubscribe sans subscription prealable.
		Unauthorized  = 3, ///< Pas de session valide cote master.
	};

	// =========================================================================
	// Sous-struct partage — Zone summary.
	// =========================================================================

	/// Resume d'une zone meteo pour le wire.
	/// Wire format :
	///   uint32 zoneId
	///   string name           (uint16 length + UTF-8)
	///   uint8  kind           (Clear=0, Rain=1, Snow=2, Storm=3, Sandstorm=4, Fog=5)
	///   float  intensity      (32 bits LE, 0..1)
	struct WeatherZoneSummary
	{
		uint32_t    zoneId    = 0;
		std::string name;
		uint8_t     kind      = 0;     ///< WeatherKind enum value (0..5).
		float       intensity = 0.0f;  ///< [0..1].
	};

	// =========================================================================
	// WEATHER_LIST — Client to Master : liste des zones meteo.
	// =========================================================================

	/// Wire format : (vide). L'account est derive de la session cote master.
	struct WeatherListRequestPayload
	{
		// (vide)
	};

	/// Wire format :
	///   uint8  error                     (cf. WeatherErrorCode)
	///   uint16 zoneCount                 (si error == 0)
	///   <count> WeatherZoneSummary
	struct WeatherListResponsePayload
	{
		uint8_t                          error = 0;
		std::vector<WeatherZoneSummary>  zones;
	};

	// =========================================================================
	// WEATHER_SUBSCRIBE — Client to Master : s'abonne aux push d'une zone.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	struct WeatherSubscribeRequestPayload
	{
		uint32_t zoneId = 0;
	};

	/// Wire format :
	///   uint8 error
	///   uint8 currentKind        (si error == 0 ; sinon non lu)
	///   float currentIntensity   (si error == 0 ; sinon non lu)
	struct WeatherSubscribeResponsePayload
	{
		uint8_t error            = 0;
		uint8_t currentKind      = 0;
		float   currentIntensity = 0.0f;
	};

	// =========================================================================
	// WEATHER_UNSUBSCRIBE — Client to Master : se desabonne.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	struct WeatherUnsubscribeRequestPayload
	{
		uint32_t zoneId = 0;
	};

	/// Wire format :
	///   uint8 error
	struct WeatherUnsubscribeResponsePayload
	{
		uint8_t error = 0;
	};

	// =========================================================================
	// WEATHER_UPDATE_NOTIFICATION — Master to Client (push, request_id=0).
	// Changement de meteo dans une zone.
	// =========================================================================

	/// Wire format :
	///   uint32 zoneId
	///   uint8  kind
	///   float  intensity      (32 bits LE, 0..1)
	struct WeatherUpdateNotificationPayload
	{
		uint32_t zoneId    = 0;
		uint8_t  kind      = 0;
		float    intensity = 0.0f;
	};

	// -------------------------------------------------------------------------
	// Parse / Build — Requests
	// -------------------------------------------------------------------------

	std::optional<WeatherListRequestPayload>        ParseWeatherListRequestPayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<WeatherSubscribeRequestPayload>   ParseWeatherSubscribeRequestPayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<WeatherUnsubscribeRequestPayload> ParseWeatherUnsubscribeRequestPayload(const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildWeatherListRequestPayload();
	std::vector<uint8_t> BuildWeatherSubscribeRequestPayload   (uint32_t zoneId);
	std::vector<uint8_t> BuildWeatherUnsubscribeRequestPayload (uint32_t zoneId);

	// -------------------------------------------------------------------------
	// Parse / Build — Responses & Notifications (payload-only)
	// -------------------------------------------------------------------------

	std::optional<WeatherListResponsePayload>        ParseWeatherListResponsePayload       (const uint8_t* payload, size_t payloadSize);
	std::optional<WeatherSubscribeResponsePayload>   ParseWeatherSubscribeResponsePayload  (const uint8_t* payload, size_t payloadSize);
	std::optional<WeatherUnsubscribeResponsePayload> ParseWeatherUnsubscribeResponsePayload(const uint8_t* payload, size_t payloadSize);
	std::optional<WeatherUpdateNotificationPayload>  ParseWeatherUpdateNotificationPayload (const uint8_t* payload, size_t payloadSize);

	std::vector<uint8_t> BuildWeatherListResponsePayload       (uint8_t error, const std::vector<WeatherZoneSummary>& zones);
	std::vector<uint8_t> BuildWeatherSubscribeResponsePayload  (uint8_t error, uint8_t currentKind, float currentIntensity);
	std::vector<uint8_t> BuildWeatherUnsubscribeResponsePayload(uint8_t error);
	std::vector<uint8_t> BuildWeatherUpdateNotificationPayload (uint32_t zoneId, uint8_t kind, float intensity);

	// -------------------------------------------------------------------------
	// Build full packets (header + payload). Utilise cote handler serveur.
	// -------------------------------------------------------------------------

	std::vector<uint8_t> BuildWeatherListResponsePacket        (uint8_t error, const std::vector<WeatherZoneSummary>& zones,
	                                                            uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildWeatherSubscribeResponsePacket   (uint8_t error, uint8_t currentKind, float currentIntensity,
	                                                            uint32_t requestId, uint64_t sessionIdHeader);
	std::vector<uint8_t> BuildWeatherUnsubscribeResponsePacket (uint8_t error, uint32_t requestId, uint64_t sessionIdHeader);

	/// Push asynchrone (request_id=0). Aucun client request en correspondance.
	std::vector<uint8_t> BuildWeatherUpdateNotificationPacket  (uint32_t zoneId, uint8_t kind, float intensity,
	                                                            uint64_t sessionIdHeader);
}
