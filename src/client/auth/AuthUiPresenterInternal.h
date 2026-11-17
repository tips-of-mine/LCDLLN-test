#pragma once

// AuthUiPresenter — en-tête INTERNE partagé par AuthUiPresenterCore.cpp,
// AuthUiPresenterWorkers.cpp et AuthUiPresenterUpdate.cpp : inclusions
// communes et utilitaires (JSON des réglages, sondage d'état HTTP, dates,
// pays…) anciennement dans l'espace de noms anonyme d'AuthUiPresenterCore.cpp,
// passés `inline` dans engine::client::auth_detail (comportement inchangé).
// Ne pas inclure ailleurs.

// WinHTTP requiert que les includes Winsock soient en place avant `windows.h`.
// On les met tout en haut du fichier pour éviter les conflits de types (HINTERNET, INTERNET_PORT, etc.).
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#endif

#include "src/client/auth/AuthUi.h"
#include "src/client/settings/ViewDistancePreset.h"

// Sous-projet C MVP (Task 12) — Engine.h fournit la declaration complete
// de \c engine::Engine pour le delegate \c GetRaceMeshForId. On l'inclut
// uniquement dans le .cpp (AuthUi.h reste un forward declare pour
// eviter la dependance circulaire Engine.h <-> AuthUi.h).
#include "src/client/app/Engine.h"

#include "src/client/render/AuthUiRenderer.h"

#include "src/client/localization/CountryLanguageMap.h"
#include "src/client/localization/IpApiGeoProvider.h"

#include "src/shared/core/Log.h"
#include "src/shared/network/NetClient.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/platform/Window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <format>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#	include "src/shared/auth/Argon2Hash.h"
#	include "src/shared/network/AuthRegisterPayloads.h"
#	include "src/shared/network/ErrorPacket.h"
#	include "src/shared/network/CharacterPayloads.h"
#	include "src/shared/network/MasterShardClientFlow.h"
#	include "src/shared/network/ProtocolV1Constants.h"
#	include "src/shared/network/RequestResponseDispatcher.h"
#	include "src/shared/network/TermsPayloads.h"
#endif

namespace engine::client::auth_detail
{
		constexpr std::string_view kUserSettingsPath = "user_settings.json";
		constexpr std::string_view kRememberedLoginPath = "remembered_login.json";
		constexpr std::string_view kLoginBackgroundPath = "ui/loading/background.png";
		constexpr std::string_view kLoginLogoPath = "";
		constexpr std::string_view kRegisterBackgroundPath = "ui/loading/background.png";
		// Résolu par Window::ResolveUiImagePath : aussi sous game/data/ (voir FileSystem::ResolveContentPath).
		constexpr std::string_view kRegisterInfoPath = "ui/register/info.png";

		inline std::string JsonBool(bool value)
		{
			return value ? "true" : "false";
		}

		inline std::string EscapeJsonString(std::string_view value)
		{
			std::string out;
			out.reserve(value.size() + 8u);
			for (char c : value)
			{
				switch (c)
				{
				case '\\': out += "\\\\"; break;
				case '"': out += "\\\""; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default: out.push_back(c); break;
				}
			}
			return out;
		}

		inline void PersistRememberedLoginSidecar(std::string_view login)
		{
			const std::filesystem::path path{ kRememberedLoginPath };
			const std::string json = std::string("{\n  \"login\": \"") + EscapeJsonString(std::string(login)) + "\"\n}\n";
			if (!engine::platform::FileSystem::WriteAllText(path, json))
			{
				LOG_WARN(Core, "[AuthUiPresenter] Impossible d'écrire {}", path.string());
			}
		}

		inline void LoadRememberedLoginSidecar(std::string& inOutLogin)
		{
			engine::core::Config side;
			if (!side.LoadFromFile(std::string(kRememberedLoginPath)))
			{
				return;
			}
			const std::string fromFile = side.GetString("login", {});
			if (!fromFile.empty())
			{
				inOutLogin = fromFile;
			}
		}

		inline void ClearRememberedLoginSidecar()
		{
			std::error_code ec;
			std::filesystem::remove(std::filesystem::path{ kRememberedLoginPath }, ec);
		}

		inline std::string BuildUserSettingsJson(bool rememberLogin, std::string_view locale, bool fullscreen, bool vsync)
		{
			return std::string("{\n  \"client\": {\n    \"locale\": \"")
				+ EscapeJsonString(locale)
				+ "\",\n    \"auth_ui\": {\n      \"remember_login\": "
				+ JsonBool(rememberLogin)
				+ ",\n      \"timeout_ms\": 5000\n    },\n    \"allow_insecure_dev\": true,\n    \"gameplay_udp\": {\n      \"enabled\": false\n    }\n  },\n  \"render\": {\n    \"fullscreen\": "
				+ JsonBool(fullscreen)
				+ ",\n    \"vsync\": "
				+ JsonBool(vsync)
				+ "\n  },\n  \"audio\": {\n    \"master_volume\": 1.0,\n    \"music_volume\": 1.0,\n    \"sfx_volume\": 1.0,\n    \"ui_volume\": 1.0\n  },\n  \"camera\": {\n    \"mouse_sensitivity\": 0.002\n  },\n  \"controls\": {\n    \"invert_y\": false,\n    \"movement_layout\": \"wasd\"\n  }\n}\n";
		}

		inline void ReplaceAudioSettings(std::string& json, float masterVolume, float musicVolume, float sfxVolume, float uiVolume)
		{
			auto replaceValue = [&json](std::string_view key, float value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": ";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && (std::isdigit(static_cast<unsigned char>(json[valueEnd])) != 0 || json[valueEnd] == '.'))
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, std::to_string(value));
			};

			replaceValue("master_volume", masterVolume);
			replaceValue("music_volume", musicVolume);
			replaceValue("sfx_volume", sfxVolume);
			replaceValue("ui_volume", uiVolume);
		}

		inline float ClampOptionStep(float value)
		{
			const float clamped = std::clamp(value, 0.0f, 1.0f);
			const int scaled = static_cast<int>(clamped * 10.0f + 0.5f);
			return static_cast<float>(scaled) / 10.0f;
		}

		inline void ReplaceControlSettings(std::string& json, float mouseSensitivity, bool invertY, bool useZqsd)
		{
			auto replaceBool = [&json](std::string_view key, bool value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": ";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && std::isalpha(static_cast<unsigned char>(json[valueEnd])) != 0)
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, value ? "true" : "false");
			};
			auto replaceString = [&json](std::string_view key, std::string_view value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": \"";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				const size_t valueEnd = json.find('"', valueStart);
				if (valueEnd == std::string::npos)
					return;
				json.replace(valueStart, valueEnd - valueStart, value);
			};
			auto replaceNumber = [&json](std::string_view key, double value)
			{
				const std::string needle = std::string("\"") + std::string(key) + "\": ";
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && (std::isdigit(static_cast<unsigned char>(json[valueEnd])) != 0 || json[valueEnd] == '.'))
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, std::to_string(value));
			};

			replaceNumber("mouse_sensitivity", mouseSensitivity);
			replaceBool("invert_y", invertY);
			replaceString("movement_layout", useZqsd ? "zqsd" : "wasd");
		}

		inline void ReplaceGameSettings(std::string& json, bool gameplayUdpEnabled, bool allowInsecureDev, uint32_t authTimeoutMs)
		{
			auto replaceBoolByNeedle = [&json](std::string_view needle, bool value)
			{
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && std::isalpha(static_cast<unsigned char>(json[valueEnd])) != 0)
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, value ? "true" : "false");
			};
			auto replaceNumberByNeedle = [&json](std::string_view needle, uint32_t value)
			{
				const size_t start = json.find(needle);
				if (start == std::string::npos)
					return;
				const size_t valueStart = start + needle.size();
				size_t valueEnd = valueStart;
				while (valueEnd < json.size() && std::isdigit(static_cast<unsigned char>(json[valueEnd])) != 0)
					++valueEnd;
				json.replace(valueStart, valueEnd - valueStart, std::to_string(value));
			};

			replaceBoolByNeedle("\"allow_insecure_dev\": ", allowInsecureDev);
			replaceNumberByNeedle("\"timeout_ms\": ", authTimeoutMs);
			replaceBoolByNeedle("\"gameplay_udp\": {\n      \"enabled\": ", gameplayUdpEnabled);
		}

		/// Remplace la valeur de "locale" dans un JSON déjà sérialisé (même format que BuildUserSettingsJson).
		/// Retourne true si la clé a été trouvée et remplacée, false si absente (le JSON reste inchangé).
		inline bool ReplaceLocaleInJson(std::string& json, std::string_view locale)
		{
			// Cherche : "locale": "
			constexpr std::string_view kNeedle = "\"locale\": \"";
			const size_t start = json.find(kNeedle);
			if (start == std::string::npos)
				return false;
			const size_t valueStart = start + kNeedle.size();
			const size_t valueEnd = json.find('"', valueStart);
			if (valueEnd == std::string::npos)
				return false;
			json.replace(valueStart, valueEnd - valueStart, locale);
			return true;
		}

		/// Force l'écriture de la locale dans user_settings.json :
		/// - Si le fichier n'existe pas : le crée avec les valeurs par défaut.
		/// - Si le fichier existe   : charge-le, remplace uniquement la clé locale, réécrit.
		/// Utilisé au premier démarrage après que l'utilisateur a confirmé sa langue.
		inline bool PatchLocaleInSettingsFile(std::string_view locale)
		{
			const std::filesystem::path settingsPath{ kUserSettingsPath };
			std::string json;
			if (engine::platform::FileSystem::Exists(settingsPath))
			{
				json = engine::platform::FileSystem::ReadAllText(std::filesystem::path{ kUserSettingsPath });
				if (!ReplaceLocaleInJson(json, locale))
				{
					// La clé n'existe pas dans ce fichier (format inattendu) — on l'ajoute pas,
					// on se contente de logger un avertissement sans écraser le fichier.
					LOG_WARN(Core, "[AuthUiPresenter] PatchLocaleInSettingsFile: clé 'locale' absente dans user_settings.json existant — locale non persistée");
					return false;
				}
			}
			else
			{
				json = BuildUserSettingsJson(false, locale, false, true);
			}
			if (!engine::platform::FileSystem::WriteAllText(settingsPath, json))
			{
				LOG_WARN(Core, "[AuthUiPresenter] PatchLocaleInSettingsFile: échec écriture user_settings.json");
				return false;
			}
			LOG_INFO(Core, "[AuthUiPresenter] PatchLocaleInSettingsFile: locale '{}' persistée dans user_settings.json", locale);
			return true;
		}

		/// Met à jour uniquement `client.auth_ui.remember_login` dans un user_settings.json existant.
		inline bool ReplaceRememberLoginInJson(std::string& json, bool rememberLogin)
		{
			constexpr std::string_view key = "\"remember_login\"";
			size_t pos = json.find(key);
			if (pos == std::string::npos)
				return false;
			pos = json.find(':', pos + key.size());
			if (pos == std::string::npos || pos + 1u >= json.size())
				return false;
			size_t valueStart = pos + 1u;
			while (valueStart < json.size() && std::isspace(static_cast<unsigned char>(json[valueStart])) != 0)
				++valueStart;
			size_t valueEnd = valueStart;
			while (valueEnd < json.size() && std::isalpha(static_cast<unsigned char>(json[valueEnd])) != 0)
				++valueEnd;
			if (valueEnd <= valueStart)
				return false;
			json.replace(valueStart, valueEnd - valueStart, rememberLogin ? "true" : "false");
			return true;
		}

		inline bool InsertRememberLoginInAuthUiJson(std::string& json, bool rememberLogin)
		{
			constexpr std::string_view kAuthUiKey = "\"auth_ui\"";
			size_t pos = json.find(kAuthUiKey);
			if (pos == std::string::npos)
				return false;
			pos = json.find('{', pos + kAuthUiKey.size());
			if (pos == std::string::npos || pos + 1u >= json.size())
				return false;
			const size_t insertAt = pos + 1u;
			const std::string ins = std::string("\n      \"remember_login\": ") + (rememberLogin ? "true" : "false") + ",";
			json.insert(insertAt, ins);
			return true;
		}

		inline bool PatchRememberLoginInSettingsFile(bool rememberLogin)
		{
			const std::filesystem::path settingsPath{ kUserSettingsPath };
			if (!engine::platform::FileSystem::Exists(settingsPath))
				return false;
			std::string json = engine::platform::FileSystem::ReadAllText(std::filesystem::path{ kUserSettingsPath });
			if (!ReplaceRememberLoginInJson(json, rememberLogin))
			{
				if (!InsertRememberLoginInAuthUiJson(json, rememberLogin))
				{
					LOG_WARN(Core, "[AuthUiPresenter] PatchRememberLoginInSettingsFile: clé remember_login absente ou illisible — fichier non modifié");
					return false;
				}
			}
			if (!engine::platform::FileSystem::WriteAllText(settingsPath, json))
			{
				LOG_WARN(Core, "[AuthUiPresenter] PatchRememberLoginInSettingsFile: échec écriture user_settings.json");
				return false;
			}
			LOG_INFO(Core, "[AuthUiPresenter] remember_login={} persisté dans user_settings.json (patch)", rememberLogin);
			return true;
		}

		inline std::string ResolvePasswordRecoveryUrl(const engine::core::Config& cfg)
		{
			// Clé garantie présente via la table des endpoints (config/server.ini).
			return cfg.GetString("client.web_portal_reset_url", "");
		}

		inline std::string ResolveStatusApiUrl(const engine::core::Config& cfg)
		{
			const std::string authUiUrl = cfg.GetString("client.auth_ui.master_availability_url", "");
			if (!authUiUrl.empty())
			{
				return authUiUrl;
			}
			// Clé garantie présente via la table des endpoints (config/server.ini).
			return cfg.GetString("client.status_api_url", "");
		}

		struct StatusHttpResponse
		{
			uint32_t statusCode = 0;
			std::string body;
			std::string transportError;
		};

		inline void LogStatusProbeBodyPreview(std::string_view phase, std::string_view body)
		{
			constexpr size_t kMaxPreview = 512u;
			std::string preview;
			preview.reserve(std::min(body.size(), kMaxPreview) + 8u);
			for (size_t i = 0; i < body.size() && preview.size() < kMaxPreview; ++i)
			{
				const char c = body[i];
				if (c == '\n' || c == '\r' || c == '\t')
					preview.push_back(' ');
				else
					preview.push_back(c);
			}
			if (body.size() > kMaxPreview)
				preview += "…";
			LOG_INFO(Core, "[StatusProbe] {} — corps: {} octet(s), aperçu: \"{}\"", phase, body.size(), preview);
		}

		/// Parse le JSON de /status (clés plates via Config) et remplit \p cache. \return false si rien d'exploitable.
		inline bool ParseStatusProbeJsonBody(const std::string& body, AuthUiPresenter::StatusCache& cache, std::string& detailOut)
		{
			LOG_INFO(Core, "[StatusProbe] parse: début (entrée {} octet(s))", body.size());
			if (body.empty())
			{
				detailOut = "corps HTTP vide";
				LOG_WARN(Core, "[StatusProbe] parse: échec — {}", detailOut);
				return false;
			}
			LogStatusProbeBodyPreview("parse", body);

			try
			{
				const std::filesystem::path tmpPath =
					std::filesystem::temp_directory_path() / "lcdlln_status_probe_tmp.json";
				if (!engine::platform::FileSystem::WriteAllText(tmpPath, body))
				{
					detailOut = "écriture fichier temporaire JSON impossible";
					LOG_WARN(Core, "[StatusProbe] parse: {}", detailOut);
					return false;
				}
				LOG_INFO(Core, "[StatusProbe] parse: JSON écrit vers {}", tmpPath.string());

				engine::core::Config statusCfg;
				if (!statusCfg.LoadFromFile(tmpPath.string()))
				{
					detailOut = "LoadFromFile JSON a échoué (JSON invalide ?)";
					LOG_WARN(Core, "[StatusProbe] parse: {}", detailOut);
					return false;
				}
				LOG_INFO(Core, "[StatusProbe] parse: fichier chargé dans Config (clés aplaties) OK");

				auto getBoolFirst = [&](std::initializer_list<std::string_view> keys, bool fallback) -> bool {
					for (const auto& k : keys)
					{
						if (statusCfg.Has(k))
						{
							const bool v = statusCfg.GetBool(k, fallback);
							LOG_INFO(Core, "[StatusProbe] parse: bool '{}' = {} (présent dans JSON)", k, v ? "true" : "false");
							return v;
						}
					}
					LOG_INFO(Core, "[StatusProbe] parse: aucune des clés bool demandées — fallback {}", fallback ? "true" : "false");
					return fallback;
				};

				cache.authOk = getBoolFirst({ "auth.ok", "auth_ok", "authentication.ok", "authentication_ok" }, true);
				cache.masterOk = getBoolFirst({ "master.ok", "master_ok", "game.ok", "game_ok" }, true);

				auto pickServerPrefix = [&](std::initializer_list<std::string_view> prefixes) -> std::string {
					for (const auto& p : prefixes)
					{
						const std::string idx0 = std::string(p) + "[0].players";
						if (statusCfg.Has(idx0))
						{
							LOG_INFO(Core, "[StatusProbe] parse: préfixe serveurs choisi '{}' (indice players[0])", p);
							return std::string(p);
						}
						const std::string idx0Ok = std::string(p) + "[0].ok";
						if (statusCfg.Has(idx0Ok))
						{
							LOG_INFO(Core, "[StatusProbe] parse: préfixe serveurs choisi '{}' (indice ok[0])", p);
							return std::string(p);
						}
						const std::string idx0Name = std::string(p) + "[0].name";
						if (statusCfg.Has(idx0Name))
						{
							LOG_INFO(Core, "[StatusProbe] parse: préfixe serveurs choisi '{}' (indice name[0])", p);
							return std::string(p);
						}
					}
					LOG_INFO(Core, "[StatusProbe] parse: aucun préfixe serveur reconnu (game_servers / servers / gameServers)");
					return {};
				};

				const std::string serverPrefix = pickServerPrefix({ "game_servers", "servers", "gameServers" });
				cache.servers.clear();
				cache.totalPlayers = 0;

				if (!serverPrefix.empty())
				{
					for (uint32_t i = 0; i < 8u; ++i)
					{
						const std::string base = serverPrefix + "[" + std::to_string(i) + "].";
						const std::string keyPlayers = base + "players";
						const std::string keyOk = base + "ok";
						const std::string keyName = base + "name";

						if (!statusCfg.Has(keyPlayers) && !statusCfg.Has(keyOk) && !statusCfg.Has(keyName))
							break;

						AuthUiPresenter::GameServerStatus s;
						s.name = statusCfg.GetString(keyName, std::string("server") + std::to_string(i));
						s.ok = statusCfg.GetBool(keyOk, true);
						s.players = static_cast<uint32_t>(std::max<int64_t>(0, statusCfg.GetInt(keyPlayers, 0)));
						s.endpoint = statusCfg.GetString(base + "endpoint", "");
						s.display_name = statusCfg.GetString(base + "display_name", s.name);
						s.game_mode = engine::network::ParseGameMode(statusCfg.GetString(base + "game_mode", "pve"));
						s.ruleset = engine::network::ParseRuleset(statusCfg.GetString(base + "ruleset", "cooperative"));
						LOG_INFO(Core, "[StatusProbe] parse: shard [{}] name='{}' display='{}' ok={} players={} endpoint='{}'", i, s.name,
							s.display_name, s.ok ? "true" : "false", s.players, s.endpoint);
						cache.servers.push_back(std::move(s));
					}
				}

				for (const auto& s : cache.servers)
					cache.totalPlayers += s.players;

				const bool hasFlags = statusCfg.Has("auth.ok") || statusCfg.Has("auth_ok") || statusCfg.Has("authentication.ok")
					|| statusCfg.Has("authentication_ok") || statusCfg.Has("master.ok") || statusCfg.Has("master_ok")
					|| statusCfg.Has("game.ok") || statusCfg.Has("game_ok");
				const bool hasServers = !cache.servers.empty();

				if (!hasFlags && !hasServers)
				{
					detailOut = "JSON sans indicateurs auth/master ni liste de serveurs reconnue";
					LOG_WARN(Core, "[StatusProbe] parse: {}", detailOut);
					return false;
				}

				LOG_INFO(Core, "[StatusProbe] parse: synthèse — authOk={} masterOk={} shards={} totalJoueurs={} (flags_explicites={})",
					cache.authOk ? "true" : "false", cache.masterOk ? "true" : "false", cache.servers.size(), cache.totalPlayers,
					hasFlags ? "oui" : "non");
				detailOut = std::string("Status OK — ") + std::to_string(cache.servers.size()) + " shard(s), totalPlayers="
					+ std::to_string(cache.totalPlayers);
				return true;
			}
			catch (const std::exception& ex)
			{
				detailOut = std::string("exception parse: ") + ex.what();
				LOG_WARN(Core, "[StatusProbe] parse: {}", detailOut);
				return false;
			}
			catch (...)
			{
				detailOut = "exception parse inconnue";
				LOG_WARN(Core, "[StatusProbe] parse: {}", detailOut);
				return false;
			}
		}

#if defined(_WIN32)
		inline const char* WinHttpErrorHint(DWORD err)
		{
			switch (err)
			{
			case 12002:
				return "délai dépassé";
			case 12005:
				return "connexion refusée (port fermé ou service absent)";
			case 12029:
				return "connexion impossible — vérifier IP/port (ex. :3842), pare-feu Windows, route réseau, et que lcdlln_server écoute";
			case 12152:
				return "réponse HTTP invalide ou tronquée";
			default:
				return "voir codes d’erreur WinHTTP";
			}
		}

		inline StatusHttpResponse HttpGetStatusUrlWin32(std::string_view url, uint32_t timeoutMs)
		{
			StatusHttpResponse resp{};
			LOG_INFO(Core, "[StatusProbe] WinHTTP: requête GET, timeout_ms={}", timeoutMs);

			auto utf8ToWide = [](std::string_view s) -> std::wstring {
				if (s.empty())
					return {};
				const int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
				if (needed <= 0)
					return {};
				std::wstring out(static_cast<size_t>(needed), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
				return out;
			};

			const std::wstring urlW = utf8ToWide(url);
			if (urlW.empty())
			{
				resp.transportError = "encodage URL UTF-8 → wide invalide";
				LOG_WARN(Core, "[StatusProbe] WinHTTP: {}", resp.transportError);
				return resp;
			}

			URL_COMPONENTS uc{};
			uc.dwStructSize = sizeof(uc);
			wchar_t hostBuf[512]{};
			wchar_t pathBuf[1024]{};
			wchar_t extraBuf[1024]{};
			uc.lpszHostName = hostBuf;
			uc.dwHostNameLength = static_cast<DWORD>(sizeof(hostBuf) / sizeof(hostBuf[0]));
			uc.lpszUrlPath = pathBuf;
			uc.dwUrlPathLength = static_cast<DWORD>(sizeof(pathBuf) / sizeof(pathBuf[0]));
			uc.lpszExtraInfo = extraBuf;
			uc.dwExtraInfoLength = static_cast<DWORD>(sizeof(extraBuf) / sizeof(extraBuf[0]));

			if (!WinHttpCrackUrl(urlW.c_str(), 0, 0, &uc))
			{
				const DWORD gle = GetLastError();
				resp.transportError = std::format("WinHttpCrackUrl a échoué — {} ({})", gle, WinHttpErrorHint(gle));
				LOG_WARN(Core, "[StatusProbe] WinHTTP: {}", resp.transportError);
				return resp;
			}

			const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
			const std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
			const std::wstring extra(uc.lpszExtraInfo, uc.dwExtraInfoLength);
			const std::wstring fullPath = path + extra;
			const bool isHttps = uc.nScheme == INTERNET_SCHEME_HTTPS;
			LOG_INFO(Core, "[StatusProbe] WinHTTP: port={} https={} (URL déjà validée par WinHttpCrackUrl)", static_cast<unsigned>(uc.nPort),
				isHttps ? "oui" : "non");

			HINTERNET hSession = WinHttpOpen(L"LCDLLN", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if (!hSession)
			{
				const DWORD gle = GetLastError();
				resp.transportError = std::format("WinHttpOpen a échoué — {} ({})", gle, WinHttpErrorHint(gle));
				LOG_WARN(Core, "[StatusProbe] WinHTTP: {}", resp.transportError);
				return resp;
			}

			HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), uc.nPort, 0);
			if (!hConnect)
			{
				const DWORD gle = GetLastError();
				resp.transportError = std::format("WinHttpConnect a échoué — {} ({})", gle, WinHttpErrorHint(gle));
				LOG_WARN(Core, "[StatusProbe] WinHTTP: {}", resp.transportError);
				WinHttpCloseHandle(hSession);
				return resp;
			}

			HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, isHttps ? WINHTTP_FLAG_SECURE : 0);
			if (!hRequest)
			{
				const DWORD gle = GetLastError();
				resp.transportError = std::format("WinHttpOpenRequest a échoué — {} ({})", gle, WinHttpErrorHint(gle));
				LOG_WARN(Core, "[StatusProbe] WinHTTP: {}", resp.transportError);
				WinHttpCloseHandle(hConnect);
				WinHttpCloseHandle(hSession);
				return resp;
			}

			// Propager le timeout configure aux 4 phases (resolve, connect, send, receive) :
			// sans cela, resolve/connect restent a 10 s chacun et WinHTTP retente plusieurs
			// fois => le worker bloque ~30 s quand le master est down, alors qu'on attend
			// un echec rapide pour basculer l'UI sur "serveur indisponible".
			WinHttpSetTimeouts(hRequest, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
			LOG_INFO(Core, "[StatusProbe] WinHTTP: envoi de la requête…");

			if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
			{
				const DWORD gle = GetLastError();
				resp.transportError = std::format("WinHttpSendRequest a échoué — {} ({})", gle, WinHttpErrorHint(gle));
				LOG_WARN(Core, "[StatusProbe] WinHTTP: {}", resp.transportError);
				WinHttpCloseHandle(hRequest);
				WinHttpCloseHandle(hConnect);
				WinHttpCloseHandle(hSession);
				return resp;
			}

			if (!WinHttpReceiveResponse(hRequest, nullptr))
			{
				const DWORD gle = GetLastError();
				resp.transportError = std::format("WinHttpReceiveResponse a échoué — {} ({})", gle, WinHttpErrorHint(gle));
				LOG_WARN(Core, "[StatusProbe] WinHTTP: {}", resp.transportError);
				WinHttpCloseHandle(hRequest);
				WinHttpCloseHandle(hConnect);
				WinHttpCloseHandle(hSession);
				return resp;
			}

			DWORD statusCode = 0;
			DWORD statusCodeLen = sizeof(statusCode);
			WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &statusCode, &statusCodeLen,
				nullptr);
			resp.statusCode = statusCode;
			LOG_INFO(Core, "[StatusProbe] WinHTTP: en-têtes reçus — code HTTP {}", resp.statusCode);

			std::string body;
			for (;;)
			{
				DWORD available = 0;
				if (!WinHttpQueryDataAvailable(hRequest, &available))
				{
					LOG_WARN(Core, "[StatusProbe] WinHTTP: WinHttpQueryDataAvailable a échoué (GetLastError={})", GetLastError());
					break;
				}
				if (available == 0)
					break;
				std::vector<char> buf(available);
				DWORD read = 0;
				if (!WinHttpReadData(hRequest, buf.data(), available, &read))
				{
					LOG_WARN(Core, "[StatusProbe] WinHTTP: WinHttpReadData a échoué (GetLastError={})", GetLastError());
					break;
				}
				body.append(buf.data(), static_cast<size_t>(read));
			}
			resp.body = std::move(body);
			LOG_INFO(Core, "[StatusProbe] WinHTTP: lecture corps terminée — {} octet(s)", resp.body.size());

			WinHttpCloseHandle(hRequest);
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return resp;
		}
#else
		/// GET HTTPS via sous-processus \c curl (doit être dans le \c PATH sur Linux/macOS CI).
		inline StatusHttpResponse HttpGetStatusUrlCurl(std::string_view url, uint32_t timeoutMs)
		{
			StatusHttpResponse resp{};
			const unsigned sec = std::max(1u, (timeoutMs + 999u) / 1000u);
			const auto bodyPath = std::filesystem::temp_directory_path() / "lcdlln_status_probe_curl_body.json";
			std::error_code ec;
			std::filesystem::remove(bodyPath, ec);

			std::string cmd = "curl -sS --max-time " + std::to_string(sec) + " -o \"" + bodyPath.string() + "\" -w \"%{http_code}\" '"
				+ std::string(url) + "'";
			LOG_INFO(Core, "[StatusProbe] curl: commande (timeout_s={}) — {}", sec, cmd);

			FILE* pipe = popen(cmd.c_str(), "r");
			if (!pipe)
			{
				resp.transportError = "popen(curl) a échoué — curl est-il installé ?";
				LOG_WARN(Core, "[StatusProbe] {}", resp.transportError);
				return resp;
			}
			std::string codeDigits;
			std::array<char, 256> buf{};
			while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
			{
				codeDigits += buf.data();
			}
			const int pcloseRc = pclose(pipe);
			while (!codeDigits.empty() && (codeDigits.back() == '\n' || codeDigits.back() == '\r'))
				codeDigits.pop_back();

			LOG_INFO(Core, "[StatusProbe] curl: pclose rc={} brut stdout='{}'", pcloseRc, codeDigits);
			if (!codeDigits.empty())
				resp.statusCode = static_cast<uint32_t>(std::strtoul(codeDigits.c_str(), nullptr, 10));

			if (!std::filesystem::exists(bodyPath))
			{
				resp.transportError = "fichier corps curl absent";
				LOG_WARN(Core, "[StatusProbe] curl: {}", resp.transportError);
				return resp;
			}
			resp.body = engine::platform::FileSystem::ReadAllText(bodyPath);
			LOG_INFO(Core, "[StatusProbe] curl: corps lu — {} octet(s), code HTTP {}", resp.body.size(), resp.statusCode);
			return resp;
		}
#endif

		inline bool IsAsciiDigits(std::string_view text)
		{
			if (text.empty())
				return false;
			for (unsigned char c : text)
			{
				if (c < '0' || c > '9')
					return false;
			}
			return true;
		}

		inline int DaysInMonth(int year, int month)
		{
			static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
			if (month < 1 || month > 12)
			{
				return 31;
			}
			int d = dim[month - 1];
			if (month == 2)
			{
				const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
				if (leap)
				{
					d = 29;
				}
			}
			return d;
		}

		[[maybe_unused]] inline bool IsValidBirthDateFields(std::string_view day, std::string_view month, std::string_view year)
		{
			if (!IsAsciiDigits(day) || !IsAsciiDigits(month) || !IsAsciiDigits(year))
			{
				return false;
			}
			const int d = std::stoi(std::string(day));
			const int m = std::stoi(std::string(month));
			const int y = std::stoi(std::string(year));
			if (y < 1900 || y > 2100 || m < 1 || m > 12 || d < 1)
			{
				return false;
			}
			if (d > DaysInMonth(y, m))
			{
				return false;
			}
			return true;
		}

		inline std::string BirthCycleDisplay(std::string_view raw, int defaultV, int minV, int maxV)
		{
			int v = defaultV;
			if (!raw.empty() && IsAsciiDigits(raw))
			{
				v = std::stoi(std::string(raw));
				v = std::clamp(v, minV, maxV);
			}
			return std::string("< ") + std::to_string(v) + " >";
		}

		inline void AdjustBirthCycle(std::string& s, int delta, int minV, int maxV)
		{
			int v = minV;
			if (!s.empty() && IsAsciiDigits(s))
			{
				v = std::stoi(s);
				v = std::clamp(v, minV, maxV);
			}
			v += delta;
			v = std::clamp(v, minV, maxV);
			s = std::to_string(v);
		}

		/// Liste des codes pays ISO-2 triée alphabétiquement par code.
		static constexpr std::array<std::string_view, 50> kCountryCodes = {
			"AF","AL","AR","AT","AU","BE","BR","CA","CH","CL",
			"CN","CO","CZ","DE","DK","DZ","EG","ES","FI","FR",
			"GB","GR","HR","HU","ID","IE","IL","IN","IT","JP",
			"KR","LU","MA","MX","NL","NO","NZ","PE","PL","PT",
			"RO","RU","SA","SE","TN","TR","UA","US","VE","ZA"
		};

		inline int CountryIndexOf(std::string_view code)
		{
			for (int i = 0; i < static_cast<int>(kCountryCodes.size()); ++i)
			{
				if (kCountryCodes[static_cast<size_t>(i)] == code)
					return i;
			}
			return 0;
		}

		inline std::string_view CountryCodeAt(int idx)
		{
			const int n = static_cast<int>(kCountryCodes.size());
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127) // n derive d une table de taille compile-time (garde defensive)
#endif
			if (n == 0) return "FR";
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
			return kCountryCodes[static_cast<size_t>(((idx % n) + n) % n)];
		}

		inline void AdjustCountryCycle(std::string& code, int delta)
		{
			int idx = CountryIndexOf(code);
			const int n = static_cast<int>(kCountryCodes.size());
			idx = (((idx + delta) % n) + n) % n;
			code = std::string(CountryCodeAt(idx));
		}

		inline std::string Pad2(int v)
		{
			v = std::clamp(v, 0, 99);
			char b[8]{};
			std::snprintf(b, sizeof(b), "%02d", v);
			return std::string(b);
		}

		inline std::string Pad4Year(int y)
		{
			y = std::clamp(y, 1900, 2100);
			char b[8]{};
			std::snprintf(b, sizeof(b), "%04d", y);
			return std::string(b);
		}

		[[maybe_unused]] inline bool IsValidVerificationCode(std::string_view code)
		{
			return code.size() == 6u && IsAsciiDigits(code);
		}

		[[maybe_unused]] inline bool IsValidCharacterNameLocal(std::string_view name)
		{
			if (name.size() < 3u || name.size() > 32u)
				return false;
			for (unsigned char c : name)
			{
				const bool ok = std::isalnum(c) || c == '_';
				if (!ok)
					return false;
			}
			return true;
		}

#if defined(_WIN32)
		inline bool WaitConnected(engine::network::NetClient* c, uint32_t timeoutMs)
		{
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
			while (std::chrono::steady_clock::now() < deadline)
			{
				auto events = c->PollEvents();
				for (const auto& ev : events)
				{
					if (ev.type == engine::network::NetClientEventType::Connected)
						return true;
					if (ev.type == engine::network::NetClientEventType::Disconnected)
						return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			return false;
		}

		/// Même clé que le load tester (`client.server_fingerprint`) : si non vide, NetClient négocie le TLS et épingle le certificat.
		inline void ApplyMasterTlsConfig(engine::network::NetClient& client, const std::string& fingerprintHex, bool allowInsecure)
		{
			if (!fingerprintHex.empty())
				client.SetExpectedServerFingerprint(fingerprintHex);
			client.SetAllowInsecureDev(allowInsecure);
		}

		inline const char* NetErrorLabel(engine::network::NetErrorCode c)
		{
			using engine::network::NetErrorCode;
			switch (c)
			{
			case NetErrorCode::OK:
				return "OK";
			case NetErrorCode::BAD_REQUEST:
				return "Bad request";
			case NetErrorCode::INVALID_CREDENTIALS:
				return "Invalid credentials";
			case NetErrorCode::ACCOUNT_NOT_FOUND:
				return "Account not found";
			case NetErrorCode::ACCOUNT_LOCKED:
				return "Account locked";
			case NetErrorCode::ALREADY_LOGGED_IN:
				return "Already logged in";
			case NetErrorCode::LOGIN_ALREADY_TAKEN:
				return "Login already taken";
			case NetErrorCode::INVALID_EMAIL:
				return "Invalid email";
			case NetErrorCode::WEAK_PASSWORD:
				return "Weak password";
			case NetErrorCode::INVALID_LOGIN:
				return "Invalid login";
			case NetErrorCode::EMAIL_VERIFICATION_REQUIRED:
				return "Email verification required";
			case NetErrorCode::EMAIL_ALREADY_VERIFIED:
				return "Email already verified";
			case NetErrorCode::VERIFICATION_CODE_INVALID:
				return "Verification code invalid";
			case NetErrorCode::REGISTRATION_DISABLED:
				return "Registration disabled";
			case NetErrorCode::REGISTRATION_INVALID:
				return "Registration invalid";
			case NetErrorCode::INTERNAL_ERROR:
				return "Server error";
			case NetErrorCode::TIMEOUT:
				return "Timeout";
			default:
				return "Network error";
			}
		}
#endif
}
