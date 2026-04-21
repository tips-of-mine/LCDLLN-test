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

#include "engine/client/AuthUi.h"

#include "engine/render/AuthUiRenderer.h"

#include "engine/core/DefaultClientEndpoints.h"
#include "engine/core/Log.h"
#include "engine/network/NetClient.h"
#include "engine/platform/FileSystem.h"
#include "engine/platform/Window.h"

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
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#	include "engine/auth/Argon2Hash.h"
#	include "engine/network/AuthRegisterPayloads.h"
#	include "engine/network/ErrorPacket.h"
#	include "engine/network/CharacterPayloads.h"
#	include "engine/network/MasterShardClientFlow.h"
#	include "engine/network/ProtocolV1Constants.h"
#	include "engine/network/RequestResponseDispatcher.h"
#	include "engine/network/TermsPayloads.h"
#endif

namespace engine::client
{
	namespace
	{
		constexpr std::string_view kUserSettingsPath = "user_settings.json";
		constexpr std::string_view kRememberedLoginPath = "remembered_login.json";
		constexpr std::string_view kLoginBackgroundPath = "ui/loading/background.png";
		constexpr std::string_view kLoginLogoPath = "";
		constexpr std::string_view kRegisterBackgroundPath = "ui/loading/background.png";
		// Résolu par Window::ResolveUiImagePath : aussi sous game/data/ (voir FileSystem::ResolveContentPath).
		constexpr std::string_view kRegisterInfoPath = "ui/register/info.png";

		// URL portail reset (identique à external/external_links.json) ; la sonde statut utilise defaults::kStatusApiUrl.
		constexpr std::string_view kProductionWebPortalResetUrl =
			"https://lcdlln-portal.tips-of-mine.com/password-recovery";

		std::string JsonBool(bool value)
		{
			return value ? "true" : "false";
		}

		std::string EscapeJsonString(std::string_view value)
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

		void PersistRememberedLoginSidecar(std::string_view login)
		{
			const std::filesystem::path path{ kRememberedLoginPath };
			const std::string json = std::string("{\n  \"login\": \"") + EscapeJsonString(std::string(login)) + "\"\n}\n";
			if (!engine::platform::FileSystem::WriteAllText(path, json))
			{
				LOG_WARN(Core, "[AuthUiPresenter] Impossible d'écrire {}", path.string());
			}
		}

		void LoadRememberedLoginSidecar(std::string& inOutLogin)
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

		void ClearRememberedLoginSidecar()
		{
			std::error_code ec;
			std::filesystem::remove(std::filesystem::path{ kRememberedLoginPath }, ec);
		}

		std::string BuildUserSettingsJson(bool rememberLogin, std::string_view locale, bool fullscreen, bool vsync)
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

		void ReplaceAudioSettings(std::string& json, float masterVolume, float musicVolume, float sfxVolume, float uiVolume)
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

		float ClampOptionStep(float value)
		{
			const float clamped = std::clamp(value, 0.0f, 1.0f);
			const int scaled = static_cast<int>(clamped * 10.0f + 0.5f);
			return static_cast<float>(scaled) / 10.0f;
		}

		void ReplaceControlSettings(std::string& json, float mouseSensitivity, bool invertY, bool useZqsd)
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

		void ReplaceGameSettings(std::string& json, bool gameplayUdpEnabled, bool allowInsecureDev, uint32_t authTimeoutMs)
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
		bool ReplaceLocaleInJson(std::string& json, std::string_view locale)
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
		bool PatchLocaleInSettingsFile(std::string_view locale)
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
		bool ReplaceRememberLoginInJson(std::string& json, bool rememberLogin)
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

		bool InsertRememberLoginInAuthUiJson(std::string& json, bool rememberLogin)
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

		bool PatchRememberLoginInSettingsFile(bool rememberLogin)
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

		std::string ResolvePasswordRecoveryUrl(const engine::core::Config& cfg)
		{
			const std::string fromCfg = cfg.GetString("client.web_portal_reset_url", "");
			if (!fromCfg.empty())
			{
				return fromCfg;
			}
			return std::string(kProductionWebPortalResetUrl);
		}

		std::string ResolveStatusApiUrl(const engine::core::Config& cfg)
		{
			const std::string authUiUrl = cfg.GetString("client.auth_ui.master_availability_url", "");
			if (!authUiUrl.empty())
			{
				return authUiUrl;
			}
			const std::string fromLinks = cfg.GetString("client.status_api_url", "");
			if (!fromLinks.empty())
			{
				return fromLinks;
			}
			return std::string(engine::core::defaults::kStatusApiUrl);
		}

		struct StatusHttpResponse
		{
			uint32_t statusCode = 0;
			std::string body;
			std::string transportError;
		};

		static void LogStatusProbeBodyPreview(std::string_view phase, std::string_view body)
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
		static bool ParseStatusProbeJsonBody(const std::string& body, AuthUiPresenter::StatusCache& cache, std::string& detailOut)
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
						LOG_INFO(Core, "[StatusProbe] parse: shard [{}] name='{}' ok={} players={}", i, s.name, s.ok ? "true" : "false",
							s.players);
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
		static const char* WinHttpErrorHint(DWORD err)
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

		static StatusHttpResponse HttpGetStatusUrlWin32(std::string_view url, uint32_t timeoutMs)
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

			WinHttpSetTimeouts(hRequest, 10000u, 10000u, timeoutMs, timeoutMs);
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
		static StatusHttpResponse HttpGetStatusUrlCurl(std::string_view url, uint32_t timeoutMs)
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

		bool IsAsciiDigits(std::string_view text)
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

		int DaysInMonth(int year, int month)
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

		[[maybe_unused]] bool IsValidBirthDateFields(std::string_view day, std::string_view month, std::string_view year)
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

		std::string BirthCycleDisplay(std::string_view raw, int defaultV, int minV, int maxV)
		{
			int v = defaultV;
			if (!raw.empty() && IsAsciiDigits(raw))
			{
				v = std::stoi(std::string(raw));
				v = std::clamp(v, minV, maxV);
			}
			return std::string("< ") + std::to_string(v) + " >";
		}

		void AdjustBirthCycle(std::string& s, int delta, int minV, int maxV)
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

		int CountryIndexOf(std::string_view code)
		{
			for (int i = 0; i < static_cast<int>(kCountryCodes.size()); ++i)
			{
				if (kCountryCodes[static_cast<size_t>(i)] == code)
					return i;
			}
			return 0;
		}

		std::string_view CountryCodeAt(int idx)
		{
			const int n = static_cast<int>(kCountryCodes.size());
			if (n == 0) return "FR";
			return kCountryCodes[static_cast<size_t>(((idx % n) + n) % n)];
		}

		void AdjustCountryCycle(std::string& code, int delta)
		{
			int idx = CountryIndexOf(code);
			const int n = static_cast<int>(kCountryCodes.size());
			idx = (((idx + delta) % n) + n) % n;
			code = std::string(CountryCodeAt(idx));
		}

		std::string Pad2(int v)
		{
			v = std::clamp(v, 0, 99);
			char b[8]{};
			std::snprintf(b, sizeof(b), "%02d", v);
			return std::string(b);
		}

		std::string Pad4Year(int y)
		{
			y = std::clamp(y, 1900, 2100);
			char b[8]{};
			std::snprintf(b, sizeof(b), "%04d", y);
			return std::string(b);
		}

		[[maybe_unused]] bool IsValidVerificationCode(std::string_view code)
		{
			return code.size() == 6u && IsAsciiDigits(code);
		}

		[[maybe_unused]] bool IsValidCharacterNameLocal(std::string_view name)
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
		bool WaitConnected(engine::network::NetClient* c, uint32_t timeoutMs)
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
		void ApplyMasterTlsConfig(engine::network::NetClient& client, const std::string& fingerprintHex, bool allowInsecure)
		{
			if (!fingerprintHex.empty())
				client.SetExpectedServerFingerprint(fingerprintHex);
			client.SetAllowInsecureDev(allowInsecure);
		}

		const char* NetErrorLabel(engine::network::NetErrorCode c)
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

	const char* AuthUiPresenter::PhaseLogName(Phase phase)
	{
		switch (phase)
		{
		case Phase::Login: return "Login";
		case Phase::Register: return "Register";
		case Phase::Submitting: return "Submitting";
		case Phase::ForgotPassword: return "ForgotPassword";
		case Phase::VerifyEmail: return "VerifyEmail";
		case Phase::EmailConfirmationPending: return "EmailConfirmationPending";
		case Phase::LanguageSelectionFirstRun: return "LanguageSelectionFirstRun";
		case Phase::LanguageOptions: return "LanguageOptions";
		case Phase::Terms: return "Terms";
		case Phase::CharacterCreate: return "CharacterCreate";
		case Phase::ShardPick: return "ShardPick";
		case Phase::Error: return "Error";
		default: return "Unknown";
		}
	}

	AuthUiPresenter::~AuthUiPresenter()
	{
		Shutdown();
	}

#if !defined(_WIN32)

	bool AuthUiPresenter::Init(const engine::core::Config&)
	{
		m_initialized = true;
		m_flowComplete = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (auth UI skipped on non-Windows build)");
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		m_usingNativeAuthScreen = false;
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	bool AuthUiPresenter::BlocksWorldInput() const
	{
		return false;
	}

	bool AuthUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		m_viewportW = width;
		m_viewportH = height;
		return width > 0 && height > 0;
	}

	void AuthUiPresenter::SaveRememberPreference()
	{
	}

	void AuthUiPresenter::ApplyLocaleSelection(bool)
	{
	}

	bool AuthUiPresenter::PatchPersistedLocaleKey(std::string_view)
	{
		return false;
	}

	void AuthUiPresenter::OpenLanguageOptions()
	{
	}

	std::string AuthUiPresenter::Tr(std::string_view key, const LocalizationService::Params&) const
	{
		return std::string(key);
	}

	std::string AuthUiPresenter::CurrentLocale() const
	{
		return "en";
	}

	std::string AuthUiPresenter::LocalizedLanguageName(std::string_view localeTag) const
	{
		return std::string(localeTag);
	}

	void AuthUiPresenter::SubmitCurrentPhase(const engine::core::Config&)
	{
	}

	void AuthUiPresenter::AppendPasswordStars(std::string&, size_t) const {}
	void AuthUiPresenter::EnsurePasswordSalt(const engine::core::Config&) {}
	std::string AuthUiPresenter::ComputeClientHash(const engine::core::Config&) const { return {}; }
	void AuthUiPresenter::StartRegisterWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartLoginWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartVerifyEmailWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartForgotPasswordWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartUsernameCheckWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartTermsStatusWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartTermsAcceptWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartCharacterCreateWorker(const engine::core::Config&) {}
	void AuthUiPresenter::StartStatusProbeWorker(const engine::core::Config&) {}
	void AuthUiPresenter::ResetMasterSession() {}
	void AuthUiPresenter::StartMasterFlowWorker(const engine::core::Config&) {}
	void AuthUiPresenter::PollAsyncResult(const engine::core::Config&) {}
	void AuthUiPresenter::UpdateWindowTitle(engine::platform::Window&) const {}
	void AuthUiPresenter::JoinWorker() {}

	uint32_t AuthUiPresenter::OptionsSubmenuLineCount(OptionsSubMenu sub)
	{
		(void)sub;
		return 0;
	}

	void AuthUiPresenter::EnterOptionsSubmenuFromRoot(uint32_t /*categoryIndex*/)
	{
	}

	void AuthUiPresenter::ImGuiSubmitLogin(const engine::core::Config&, const char*, const char*, bool) {}
	void AuthUiPresenter::ImGuiNavigateToRegisterFromLogin() {}
	void AuthUiPresenter::ImGuiBackFromRegisterToLogin() {}
	void AuthUiPresenter::ImGuiOpenForgotPasswordPortal(const engine::core::Config&, engine::platform::Window&) {}
	void AuthUiPresenter::ImGuiRequestClose(engine::platform::Window& window)
	{
		window.RequestClose();
	}

	AuthUiPresenter::LanguageOptionsImGuiMirror AuthUiPresenter::BuildLanguageOptionsImGuiMirror() const
	{
		return {};
	}

	void AuthUiPresenter::ImGuiApplyLanguageOptionsMenu(const engine::core::Config&, const LanguageOptionsImGuiMirror&) {}
	void AuthUiPresenter::ImGuiCloseLanguageOptionsWithoutApply() {}

	AuthUiPresenter::RegisterFieldsMirrorForImGui AuthUiPresenter::BuildRegisterFieldsMirrorForImGui() const
	{
		return {};
	}

	void AuthUiPresenter::ImGuiSubmitRegister(const engine::core::Config&, const RegisterImGuiSubmit&) {}
	void AuthUiPresenter::ImGuiNavigateToForgotFromLogin() {}
	void AuthUiPresenter::ImGuiSetShardPickChoiceShardId(uint32_t) {}
	void AuthUiPresenter::ImGuiSubmitShardPick(const engine::core::Config&) {}
	void AuthUiPresenter::ImGuiBackFromShardPickToLogin() {}

	void AuthUiPresenter::CommitLanguageOptionsMenuApply(const engine::core::Config&)
	{
	}

#else

	bool AuthUiPresenter::Init(const engine::core::Config& cfg)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[AuthUiPresenter] Init ignored: already initialized");
			return true;
		}

		// Mutex sur le tas (STAB.11/13) : évite un std::mutex membre d’un gros objet Engine.
		// STAB.14 — CRITICAL_SECTION wrapper instead of std::mutex (SRWLOCK crashes).
		m_asyncMutex = std::make_unique<AuthMutex>();

		m_authEnabled = cfg.GetBool("client.auth_ui.enabled", true);
		if (!m_authEnabled)
		{
			m_flowComplete = true;
			m_initialized = true;
			LOG_INFO(Core, "[AuthUiPresenter] Init OK (disabled via client.auth_ui.enabled=false)");
			return true;
		}

		m_flowComplete = false;
		m_phase = Phase::Login;
		m_errorReturnPhase = Phase::Login;
		m_login.clear();
		m_password.clear();
		m_email.clear();
		m_firstName.clear();
		m_lastName.clear();
		m_birthDay.clear();
		m_birthMonth.clear();
		m_birthYear.clear();
		m_country.clear();
		m_verifyCode.clear();
		m_termsTitle.clear();
		m_termsVersionLabel.clear();
		m_termsLocale.clear();
		m_termsContent.clear();
		m_characterName.clear();
		m_activeField = 0;
		m_termsScrollOffset = 0;
		m_termsTotalLength = 0;
		m_userErrorText.clear();
		m_infoBanner.clear();
		m_registeredTagId.clear();
		m_pendingVerifyAccountId = 0;
		m_pendingTermsEditionId = 0;
		m_termsScrolledToBottom = false;
		m_termsAcknowledgeChecked = false;
		m_rememberLogin = false;
		m_savedRememberLogin = false;
		m_hasPersistedLocale = false;
		m_languageSelectionIndex = 0;
		m_optionsSubMenu = OptionsSubMenu::Root;
		m_optionsRootSelection = 0;
		m_optionsSubSelection = 0;
		m_phaseBeforeOptions = Phase::Login;
		m_selectedLocale.clear();
		m_persistedLocale.clear();
		m_videoFullscreen = cfg.GetBool("render.fullscreen", true);
		m_videoVsync = cfg.GetBool("render.vsync", true);
		m_videoResWidth = static_cast<int32_t>(std::max<int64_t>(640, cfg.GetInt("render.resolution_width", 1920)));
		m_videoResHeight = static_cast<int32_t>(std::max<int64_t>(480, cfg.GetInt("render.resolution_height", 1080)));
		m_videoQualityPreset = static_cast<int32_t>(std::clamp<int64_t>(cfg.GetInt("render.quality_preset", 2), 0, 3));
		m_videoFovDegrees = static_cast<float>(std::clamp(cfg.GetDouble("render.fov", 70.0), 60.0, 120.0));
		m_videoFullscreenPending = m_videoFullscreen;
		m_videoVsyncPending = m_videoVsync;
		m_videoResWidthPending = m_videoResWidth;
		m_videoResHeightPending = m_videoResHeight;
		m_videoQualityPresetPending = m_videoQualityPreset;
		m_videoFovDegreesPending = m_videoFovDegrees;
		m_pendingVideoSettings = {};
		m_audioMasterVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.master_volume", 1.0)));
		m_audioMusicVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.music_volume", 1.0)));
		m_audioSfxVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.sfx_volume", 1.0)));
		m_audioUiVolume = ClampOptionStep(static_cast<float>(cfg.GetDouble("audio.ui_volume", 1.0)));
		m_audioMasterVolumePending = m_audioMasterVolume;
		m_audioMusicVolumePending = m_audioMusicVolume;
		m_audioSfxVolumePending = m_audioSfxVolume;
		m_audioUiVolumePending = m_audioUiVolume;
		m_pendingAudioSettings = {};
		m_mouseSensitivity = static_cast<float>(cfg.GetDouble("camera.mouse_sensitivity", 0.002));
		m_mouseSensitivityPending = m_mouseSensitivity;
		m_invertY = cfg.GetBool("controls.invert_y", false);
		m_invertYPending = m_invertY;
		m_useZqsd = cfg.GetString("controls.movement_layout", "wasd") == "zqsd";
		m_useZqsdPending = m_useZqsd;
		m_pendingControlSettings = {};
		m_gameplayUdpEnabled = cfg.GetBool("client.gameplay_udp.enabled", false);
		m_gameplayUdpEnabledPending = m_gameplayUdpEnabled;
		m_allowInsecureDev = cfg.GetBool("client.allow_insecure_dev", true);
		m_allowInsecureDevPending = m_allowInsecureDev;
		m_authTimeoutMs = static_cast<uint32_t>(std::clamp<int64_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000), 1000, 15000));
		m_authTimeoutMsPending = m_authTimeoutMs;
		m_authMinimalChrome = cfg.GetBool("render.auth_ui.minimal_chrome", true);
		m_authLoginArtColumn = cfg.GetBool("render.auth_ui.login_art_column", false);
		m_layoutAuthTitleLine1FromPanelTopPx = static_cast<int32_t>(
			std::clamp<int64_t>(cfg.GetInt("render.auth_ui.layout.title_line1_from_panel_top_px", 4), 0, 120));
		m_layoutAuthGapTitleToSectionPx = static_cast<int32_t>(
			std::clamp<int64_t>(cfg.GetInt("render.auth_ui.layout.gap_title_to_section_px", 8), 0, 80));
		m_layoutAuthTitleCenterViewportWidth = cfg.GetBool("render.auth_ui.layout.title_center_viewport_width", true);
		m_layoutAuthFieldRowExtraPx = static_cast<int32_t>(
			std::clamp<int64_t>(cfg.GetInt("render.auth_ui.layout.field_row_extra_px", 0), 0, 64));
		// URL de sonde "status" (defaults::kStatusApiUrl si non fourni par la config) au début de l'écran de connexion.
		m_masterAvailabilityUrl = ResolveStatusApiUrl(cfg);
		LOG_INFO(Core, "[StatusProbe] Init: URL statut maître = '{}'", m_masterAvailabilityUrl);
		m_statusProbeInitialized = false;
		m_statusProbeCompletedOnce = false;
		m_lastStatusProbeHttpSuccess = false;
		m_statusPollTimer = 0.f;
		m_authLogoSizePx = static_cast<int32_t>(std::clamp<int64_t>(cfg.GetInt("render.auth_ui.logo_size_px", 96), 32, 256));
		m_authAvailabilityChecking = false;
		m_authAvailabilityPollTimer = 0.f;
		m_pendingGameSettings = {};
		m_argonSalt.clear();
		m_asyncResult = {};
		m_pendingAsyncKind = AsyncKind::None;
		m_masterSessionId = 0;
		JoinWorker();
		m_masterClient.reset();
		LoadRememberPreference();
		const std::string configLocale = LocalizationService::NormalizeLocaleTag(cfg.GetString("client.locale", ""));
		const std::string requestedLocale = !m_persistedLocale.empty() ? m_persistedLocale : configLocale;
		if (!m_localization.Init(cfg, requestedLocale.empty() ? LocalizationService::DetectSystemLocaleTag() : requestedLocale))
		{
			LOG_ERROR(Core, "[AuthUiPresenter] Init FAILED: localization init failed");
			return false;
		}
		if (requestedLocale.empty())
		{
			m_selectedLocale = m_localization.GetCurrentLocale();
			const auto& locales = m_localization.GetAvailableLocales();
			auto it = std::find(locales.begin(), locales.end(), m_selectedLocale);
			m_languageSelectionIndex = it != locales.end() ? static_cast<uint32_t>(std::distance(locales.begin(), it)) : 0u;
			SetPhase(Phase::LanguageSelectionFirstRun);
			LOG_INFO(Core, "[AuthUiPresenter] First run locale selection required (detected={})", m_selectedLocale);
		}
		else
		{
			m_selectedLocale = m_localization.GetCurrentLocale();
			LOG_INFO(Core, "[AuthUiPresenter] Initial locale retained ({})", m_selectedLocale);
		}

		m_initialized = true;
		LOG_INFO(Core, "[AuthUiPresenter] Init OK (master host from client.master_host ou client.master_tcp_host / client.master_port, locale={})", m_localization.GetCurrentLocale());
		return true;
	}

	void AuthUiPresenter::Shutdown()
	{
		if (!m_initialized)
			return;
		JoinWorker();
		ResetMasterSession();
		SaveRememberPreference();
		m_password.clear();
		m_localization.Shutdown();
		m_initialized = false;
		LOG_INFO(Core, "[AuthUiPresenter] Destroyed");
	}

	void AuthUiPresenter::LoadRememberPreference()
	{
		engine::core::Config persisted;
		if (persisted.LoadFromFile(kUserSettingsPath))
		{
			m_rememberLogin = persisted.GetBool("client.auth_ui.remember_login", false);
			m_savedRememberLogin = m_rememberLogin;
			if (m_rememberLogin)
			{
				LoadRememberedLoginSidecar(m_login);
			}
			else
			{
				std::string sideLogin;
				LoadRememberedLoginSidecar(sideLogin);
				if (!sideLogin.empty())
				{
					m_rememberLogin = true;
					m_savedRememberLogin = true;
					m_login = std::move(sideLogin);
				}
			}
			m_persistedLocale = LocalizationService::NormalizeLocaleTag(persisted.GetString("client.locale", ""));
			m_hasPersistedLocale = !m_persistedLocale.empty();
			m_videoFullscreen = persisted.GetBool("render.fullscreen", m_videoFullscreen);
			m_videoVsync = persisted.GetBool("render.vsync", m_videoVsync);
			if (persisted.Has("render.resolution_width"))
				m_videoResWidth = static_cast<int32_t>(std::max<int64_t>(640, persisted.GetInt("render.resolution_width", m_videoResWidth)));
			if (persisted.Has("render.resolution_height"))
				m_videoResHeight = static_cast<int32_t>(std::max<int64_t>(480, persisted.GetInt("render.resolution_height", m_videoResHeight)));
			if (persisted.Has("render.quality_preset"))
				m_videoQualityPreset = static_cast<int32_t>(std::clamp<int64_t>(persisted.GetInt("render.quality_preset", m_videoQualityPreset), 0, 3));
			if (persisted.Has("render.fov"))
				m_videoFovDegrees = static_cast<float>(std::clamp(persisted.GetDouble("render.fov", m_videoFovDegrees), 60.0, 120.0));
			m_videoFullscreenPending = m_videoFullscreen;
			m_videoVsyncPending = m_videoVsync;
			m_videoResWidthPending = m_videoResWidth;
			m_videoResHeightPending = m_videoResHeight;
			m_videoQualityPresetPending = m_videoQualityPreset;
			m_videoFovDegreesPending = m_videoFovDegrees;
			m_audioMasterVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.master_volume", m_audioMasterVolume)));
			m_audioMusicVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.music_volume", m_audioMusicVolume)));
			m_audioSfxVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.sfx_volume", m_audioSfxVolume)));
			m_audioUiVolume = ClampOptionStep(static_cast<float>(persisted.GetDouble("audio.ui_volume", m_audioUiVolume)));
			m_audioMasterVolumePending = m_audioMasterVolume;
			m_audioMusicVolumePending = m_audioMusicVolume;
			m_audioSfxVolumePending = m_audioSfxVolume;
			m_audioUiVolumePending = m_audioUiVolume;
			m_mouseSensitivity = static_cast<float>(persisted.GetDouble("camera.mouse_sensitivity", m_mouseSensitivity));
			m_mouseSensitivityPending = m_mouseSensitivity;
			m_invertY = persisted.GetBool("controls.invert_y", m_invertY);
			m_invertYPending = m_invertY;
			m_useZqsd = persisted.GetString("controls.movement_layout", m_useZqsd ? "zqsd" : "wasd") == "zqsd";
			m_useZqsdPending = m_useZqsd;
			m_gameplayUdpEnabled = persisted.GetBool("client.gameplay_udp.enabled", m_gameplayUdpEnabled);
			m_gameplayUdpEnabledPending = m_gameplayUdpEnabled;
			m_allowInsecureDev = persisted.GetBool("client.allow_insecure_dev", m_allowInsecureDev);
			m_allowInsecureDevPending = m_allowInsecureDev;
			m_authTimeoutMs = static_cast<uint32_t>(std::clamp<int64_t>(persisted.GetInt("client.auth_ui.timeout_ms", m_authTimeoutMs), 1000, 15000));
			m_authTimeoutMsPending = m_authTimeoutMs;
			LOG_INFO(Core, "[AuthUiPresenter] Preferences loaded (remember_login={}, locale={}, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
				m_rememberLogin, m_persistedLocale, m_videoFullscreen, m_videoVsync,
				m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
				m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
				m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
			return;
		}
		m_rememberLogin = false;
		m_savedRememberLogin = false;
		{
			std::string sideLogin;
			LoadRememberedLoginSidecar(sideLogin);
			if (!sideLogin.empty())
			{
				m_rememberLogin = true;
				m_savedRememberLogin = true;
				m_login = std::move(sideLogin);
			}
		}
		m_persistedLocale.clear();
		m_hasPersistedLocale = false;
		LOG_INFO(Core, "[AuthUiPresenter] Preferences defaulted (remember_login=false, locale=unset, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_videoFullscreen, m_videoVsync, m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
			m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
			m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
	}

	void AuthUiPresenter::SaveRememberPreference()
	{
		const std::filesystem::path settingsPath{ kUserSettingsPath };
		if (engine::platform::FileSystem::Exists(settingsPath))
		{
			if (PatchRememberLoginInSettingsFile(m_rememberLogin))
				m_savedRememberLogin = m_rememberLogin;
			else
				LOG_WARN(Core, "[AuthUiPresenter] remember_login non persisté dans user_settings.json (patch absent ou échoué)");
			return;
		}

		const std::string locale = CurrentLocale();
		std::string json = BuildUserSettingsJson(m_rememberLogin, locale, m_videoFullscreen, m_videoVsync);
		ReplaceAudioSettings(json, m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume);
		ReplaceControlSettings(json, m_mouseSensitivity, m_invertY, m_useZqsd);
		ReplaceGameSettings(json, m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
		if (!engine::platform::FileSystem::WriteAllText(settingsPath, json))
		{
			LOG_WARN(Core, "[AuthUiPresenter] Failed to persist preferences");
			return;
		}
		m_savedRememberLogin = m_rememberLogin;
		m_persistedLocale = locale;
		m_hasPersistedLocale = !locale.empty();
		LOG_INFO(Core, "[AuthUiPresenter] Preferences persisted (remember_login={}, locale={}, fullscreen={}, vsync={}, master={:.1f}, music={:.1f}, sfx={:.1f}, ui={:.1f}, sens={:.4f}, invert_y={}, layout={}, gameplay_udp={}, allow_insecure_dev={}, timeout_ms={})",
			m_rememberLogin, locale, m_videoFullscreen, m_videoVsync,
			m_audioMasterVolume, m_audioMusicVolume, m_audioSfxVolume, m_audioUiVolume,
			m_mouseSensitivity, m_invertY, m_useZqsd ? "zqsd" : "wasd",
			m_gameplayUdpEnabled, m_allowInsecureDev, m_authTimeoutMs);
	}

	bool AuthUiPresenter::BlocksWorldInput() const
	{
		return m_initialized && !m_flowComplete;
	}

	void AuthUiPresenter::JoinWorker()
	{
		if (m_worker.joinable())
		{
			m_worker.join();
			LOG_INFO(Core, "[AuthUiPresenter] Background worker joined");
		}
	}

	void AuthUiPresenter::ResetMasterSession()
	{
		JoinWorker();
		m_masterSessionId = 0;
		if (m_masterClient)
		{
			m_masterClient->Disconnect("auth_ui_reset");
			m_masterClient.reset();
		}
	}

	void AuthUiPresenter::AppendPasswordStars(std::string& out, size_t len) const
	{
		for (size_t i = 0; i < len; ++i)
			out.push_back('*');
	}

	void AuthUiPresenter::EnsurePasswordSalt(const engine::core::Config& cfg)
	{
		(void)cfg;
		m_argonSalt = engine::auth::DeriveClientPasswordSaltFromLogin(m_login);
		if (m_argonSalt.size() != engine::auth::kArgon2SaltLength)
		{
			m_argonSalt.clear();
			LOG_ERROR(Core, "[AuthUiPresenter] Client-side Argon2 salt derivation FAILED (login trim empty or crypto error)");
			return;
		}
		LOG_INFO(Core, "[AuthUiPresenter] Client-side Argon2 salt derived from login (len={})", m_argonSalt.size());
	}

	std::string AuthUiPresenter::ComputeClientHash(const engine::core::Config& cfg) const
	{
		engine::auth::Argon2Params params;
		params.time_cost = static_cast<uint32_t>(cfg.GetInt("client.argon2.time_cost", static_cast<int64_t>(params.time_cost)));
		params.memory_kib = static_cast<uint32_t>(cfg.GetInt("client.argon2.memory_kib", static_cast<int64_t>(params.memory_kib)));
		params.parallelism = static_cast<uint32_t>(cfg.GetInt("client.argon2.parallelism", static_cast<int64_t>(params.parallelism)));
		params.hash_len = static_cast<uint32_t>(cfg.GetInt("client.argon2.hash_len", static_cast<int64_t>(params.hash_len)));
		return engine::auth::Hash(m_password, m_argonSalt, params);
	}

	void AuthUiPresenter::UpdateWindowTitle(engine::platform::Window& window) const
	{
		std::string t = Tr("app.title");
		t += " | ";
		switch (m_phase)
		{
		case Phase::Login:
			t += Tr("auth.phase.login");
			break;
		case Phase::Register:
			t += Tr("auth.phase.register");
			break;
		case Phase::VerifyEmail:
			t += Tr("auth.phase.verify_email");
			break;
		case Phase::EmailConfirmationPending:
			t += Tr("auth.phase.email_confirmation");
			break;
		case Phase::ForgotPassword:
			t += Tr("auth.phase.forgot_password");
			break;
		case Phase::Terms:
			t += Tr("auth.phase.terms");
			break;
		case Phase::CharacterCreate:
			t += Tr("auth.phase.character_create");
			break;
		case Phase::ShardPick:
			t += Tr("auth.phase.shard_pick");
			break;
		case Phase::LanguageSelectionFirstRun:
			t += Tr("language.first_run.title");
			break;
		case Phase::LanguageOptions:
			t += Tr("language.options.title");
			break;
		case Phase::Submitting:
			t += Tr("auth.phase.submitting");
			break;
		case Phase::Error:
			t += Tr("auth.phase.error");
			break;
		}
		if (!m_userErrorText.empty() && m_phase == Phase::Error)
		{
			t += " — ";
			const size_t kMax = 80;
			if (m_userErrorText.size() > kMax)
				t.append(m_userErrorText.data(), kMax);
			else
				t += m_userErrorText;
		}
		window.SetTitle(t);
	}

	bool AuthUiPresenter::PatchPersistedLocaleKey(std::string_view locale)
	{
		return PatchLocaleInSettingsFile(locale);
	}














	std::string AuthUiPresenter::Tr(std::string_view key, const LocalizationService::Params& params) const
	{
		return m_localization.Translate(key, params);
	}

	std::string AuthUiPresenter::CurrentLocale() const
	{
		return m_localization.GetCurrentLocale();
	}

	std::string AuthUiPresenter::LocalizedLanguageName(std::string_view localeTag) const
	{
		return Tr(std::string("language.name.") + std::string(localeTag));
	}

	void AuthUiPresenter::PollAsyncResult(const engine::core::Config& cfg)
	{
		auto pendingKindTag = [](AsyncKind k) -> const char*
		{
			switch (k)
			{
			case AsyncKind::None:
				return "None";
			case AsyncKind::Register:
				return "Register";
			case AsyncKind::AuthOnly:
				return "AuthOnly";
			case AsyncKind::VerifyEmail:
				return "VerifyEmail";
			case AsyncKind::ResendVerification:
				return "ResendVerification";
			case AsyncKind::ForgotPassword:
				return "ForgotPassword";
			case AsyncKind::TermsStatus:
				return "TermsStatus";
			case AsyncKind::TermsAccept:
				return "TermsAccept";
			case AsyncKind::CharacterCreate:
				return "CharacterCreate";
			case AsyncKind::Login:
				return "Login";
			case AsyncKind::StatusProbe:
				return "StatusProbe";
			case AsyncKind::UsernameCheck:
				return "UsernameCheck";
			default:
				return "?";
			}
		};
		// Écran login initial : pas de worker, rien à consommer — évite de verrouiller sans nécessité.
		LOG_DEBUG(Core, "[DIAG-POLL] enter worker.joinable={} asyncResult.ready={}", m_worker.joinable() ? 1 : 0, m_asyncResult.ready ? 1 : 0);
		const bool workerJoinable = m_worker.joinable();
		if (!workerJoinable && !m_asyncResult.ready)
		{
			LOG_DEBUG(Core, "[DIAG-POLL] early return (no worker, no result)");
			return;
		}

		LOG_DEBUG(Core, "[DIAG-POLL] before mutex lock m_asyncMutex={}", (void*)m_asyncMutex.get());
		AsyncResult copy{};
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			LOG_DEBUG(Core, "[DIAG-POLL] mutex locked, asyncResult.ready={}", m_asyncResult.ready ? 1 : 0);
			if (!m_asyncResult.ready)
			{
				LOG_DEBUG(Core,
					"[DIAG-POLL] return (result not ready) pendingKind={}",
					pendingKindTag(m_pendingAsyncKind));
				return;
			}
			copy = m_asyncResult;
			m_asyncResult = {};
		}
		LOG_DEBUG(Core, "[DIAG-POLL] mutex released, joining worker");
		JoinWorker();

		const AsyncKind kind = m_pendingAsyncKind;
		m_pendingAsyncKind = AsyncKind::None;

		if (kind == AsyncKind::StatusProbe)
		{
			m_statusCache = copy.statusCache;
			m_statusProbeCompletedOnce = true;
			m_lastStatusProbeHttpSuccess = copy.success;
			// Si on a fini la première vérification, on autorise les refresh périodiques.
			m_statusProbeInitialized = true;
			m_authAvailabilityChecking = false;
			m_authAvailabilityPollTimer = 0.f;
			m_authLogoRotationRad = 0.f;
			LOG_INFO(Core,
				"[StatusProbe] thread principal: résultat consommé — httpLayerOk={} authOk={} masterOk={} shards={} totalJoueurs={} "
				"infoMessage='{}' workerMsg='{}' → UI: maintenance seulement si httpLayerOk et JSON indique indisponibilité",
				copy.success ? 1 : 0, m_statusCache.authOk ? 1 : 0, m_statusCache.masterOk ? 1 : 0, m_statusCache.servers.size(),
				m_statusCache.totalPlayers, m_statusCache.infoMessage, copy.message);
			return;
		}

		if (kind == AsyncKind::Register)
		{
			LOG_INFO(Core,
				"[AUTH-REG] PollAsyncResult: consuming register outcome success={} accountId={} msg_len={}",
				copy.success ? 1 : 0,
				copy.accountId,
				copy.message.size());
			if (copy.success)
			{
				m_pendingVerifyAccountId = copy.accountId;
				m_verifyCodeSentAt = std::chrono::steady_clock::now();
				SetPhase(Phase::EmailConfirmationPending);
				// Conserver m_pendingVerifyAccountId = copy.accountId (doit rester en place)
				m_userErrorText.clear();
				m_infoBanner.clear();
				if (!copy.tagId.empty())
				{
					m_registeredTagId = copy.tagId;
				}
				LOG_INFO(Core, "[AuthUiPresenter] Register finished OK: accountId={} tagId={}", copy.accountId, m_registeredTagId);
			}
			else
			{
				EnterAuthErrorPhase(Phase::Register, copy.message);
				LOG_WARN(Core, "[AuthUiPresenter] Register FAILED: {}", copy.message);
			}
			(void)cfg;
			return;
		}

		if (kind == AsyncKind::AuthOnly)
		{
			if (copy.success)
			{
				m_masterSessionId = copy.sessionId;
				if (copy.termsPendingCount > 0)
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					SetPhase(Phase::Terms);
					m_infoBanner = copy.message.empty() ? Tr("auth.info.terms_required") : copy.message;
				}
				else
				{
					ResetMasterSession();
					SetPhase(Phase::Submitting);
					StartMasterFlowWorker(cfg);
				}
			}
			else
			{
				// Masquer la distinction identifiant/mot de passe pour ne pas révéler
				// si c'est le login ou le mot de passe qui est incorrect.
				if (copy.message == "Invalid credentials" || copy.message == "Account not found")
				{
					EnterAuthErrorPhase(Phase::Login, Tr("auth.error.invalid_credentials"));
				}
				else
				{
					EnterAuthErrorPhase(Phase::Login, copy.message);
				}
			}
			return;
		}

		if (kind == AsyncKind::VerifyEmail)
		{
			if (copy.success)
			{
				SetPhase(Phase::Login);
				m_userErrorText.clear();
				m_registeredTagId.clear();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.email_verified") : copy.message;
				LOG_INFO(Core, "[AuthUiPresenter] VerifyEmail OK: {}", m_infoBanner);
			}
			else
			{
				EnterAuthErrorPhase(Phase::VerifyEmail, copy.message);
				LOG_WARN(Core, "[AuthUiPresenter] VerifyEmail FAILED: {}", copy.message);
			}
			return;
		}

		if (kind == AsyncKind::ResendVerification)
		{
			if (copy.success)
			{
				// Réinitialiser le timer : le nouveau code expire dans 15 min.
				m_verifyCodeSentAt = std::chrono::steady_clock::now();
				m_userErrorText.clear();
				m_infoBanner = Tr("auth.info.email_resent");
				LOG_INFO(Core, "[AuthUiPresenter] ResendVerification OK");
			}
			else
			{
				const Phase returnTo = (m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
					? m_phase : Phase::EmailConfirmationPending;
				EnterAuthErrorPhase(returnTo, copy.message);
				LOG_WARN(Core, "[AuthUiPresenter] ResendVerification FAILED: {}", copy.message);
			}
			return;
		}

		if (kind == AsyncKind::ForgotPassword)
		{
			SetPhase(Phase::Login);
			if (copy.success)
			{
				m_userErrorText.clear();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.forgot_password") : copy.message;
			}
			else
			{
				m_infoBanner = copy.message;
			}
			return;
		}

		// Plan C : résultat de la vérification disponibilité username.
		if (kind == AsyncKind::UsernameCheck)
		{
			if (copy.usernameCheckSeq != m_usernameCheckSeq)
			{
				// Réponse obsolète (une nouvelle frappe a incrémenté le seq) : ignorer.
				LOG_DEBUG(Core, "[UsernameCheck] réponse obsolète seq={} attendu={}", copy.usernameCheckSeq, m_usernameCheckSeq);
				return;
			}
			m_usernameCheckState = (copy.usernameAvailable != 0)
				? UsernameCheckState::Available
				: UsernameCheckState::Taken;
			LOG_INFO(Core, "[UsernameCheck] résultat seq={} available={}", copy.usernameCheckSeq, (int)copy.usernameAvailable);
			return;
		}

		if (kind == AsyncKind::TermsStatus)
		{
			if (copy.success)
			{
				if (copy.termsPendingCount == 0)
				{
					m_infoBanner = Tr("auth.info.terms_done");
					SetPhase(Phase::CharacterCreate);
				}
				else
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					SetPhase(Phase::Terms);
					m_infoBanner = copy.message;
				}
			}
			else
			{
				ResetMasterSession();
				EnterAuthErrorPhase(Phase::Terms, copy.message);
			}
			return;
		}

		if (kind == AsyncKind::TermsAccept)
		{
			if (copy.success)
			{
				if (copy.termsPendingCount == 0)
				{
					m_infoBanner = Tr("auth.info.terms_done");
					SetPhase(Phase::CharacterCreate);
				}
				else
				{
					m_pendingTermsEditionId = copy.termsEditionId;
					m_termsTitle = copy.termsTitle;
					m_termsVersionLabel = copy.termsVersionLabel;
					m_termsLocale = copy.termsLocale;
					m_termsContent = copy.termsContent;
					m_termsTotalLength = copy.totalLength;
					m_termsScrollOffset = 0;
					m_termsScrolledToBottom = false;
					m_termsAcknowledgeChecked = false;
					SetPhase(Phase::Terms);
					m_infoBanner = copy.message;
				}
			}
			else
			{
				ResetMasterSession();
				EnterAuthErrorPhase(Phase::Terms, copy.message);
			}
			return;
		}

		if (kind == AsyncKind::CharacterCreate)
		{
			if (copy.success)
			{
				ResetMasterSession();
				m_infoBanner = copy.message.empty() ? Tr("auth.info.character_created") : copy.message;
				SetPhase(Phase::Submitting);
				StartMasterFlowWorker(cfg);
			}
			else
			{
				EnterAuthErrorPhase(Phase::CharacterCreate, copy.message);
			}
			return;
		}

		if (kind == AsyncKind::Login && copy.shardChoiceRequired)
		{
			m_shardPickEntries = std::move(copy.serverListForPick);
			m_shardPickChoiceShardId = 0;
			for (const auto& e : m_shardPickEntries)
			{
				if (e.status == 1u && !e.endpoint.empty())
				{
					m_shardPickChoiceShardId = e.shard_id;
					break;
				}
			}
			SetPhase(Phase::ShardPick);
			m_userErrorText.clear();
			m_infoBanner.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Shard choice UI ({} liste entrées)", m_shardPickEntries.size());
			return;
		}

		if (copy.success)
		{
			m_userErrorText.clear();
			m_infoBanner = copy.message;
			LOG_INFO(Core, "[AuthUiPresenter] Master/shard flow OK: {}", copy.message);
			if (kind == AsyncKind::Login)
			{
				SaveRememberPreference();
				if (m_rememberLogin)
				{
					PersistRememberedLoginSidecar(m_login);
				}
				else
				{
					ClearRememberedLoginSidecar();
				}
			}
			m_flowComplete = true;
			SetPhase(Phase::Login);
			return;
		}
		EnterAuthErrorPhase(Phase::Login, copy.message);
		LOG_WARN(Core, "[AuthUiPresenter] Master/shard flow FAILED: {}", copy.message);
	}

	void AuthUiPresenter::StartLoginWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			EnterAuthErrorPhase(Phase::Login, Tr("auth.error.hash_password_failed"));
			return;
		}

		ResetMasterSession();
		m_masterClient = std::make_unique<engine::network::NetClient>();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const std::string login = m_login;
		const std::string locale = CurrentLocale();

		m_pendingAsyncKind = AsyncKind::AuthOnly;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		engine::network::NetClient* const masterClient = m_masterClient.get();
		m_worker = std::thread([this, masterClient, host, port, timeoutMs, allowInsecure, serverFingerprint, login, hash, locale]() {
			AsyncResult local{};
			if (masterClient == nullptr)
			{
				local.ready = true;
				local.message = "Internal error: master client missing.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			ApplyMasterTlsConfig(*masterClient, serverFingerprint, allowInsecure);
			masterClient->Connect(host, port);
			if (!WaitConnected(masterClient, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			engine::network::RequestResponseDispatcher disp(masterClient);
			bool authDone = false;
			bool authOk = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeAuthRequest, engine::network::BuildAuthRequestPayload(login, hash),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						authDone = true;
						if (timeout)
						{
							errMsg = "AUTH timeout.";
							return;
						}
						auto auth = engine::network::ParseAuthResponsePayload(pl.data(), pl.size());
						if (auth && auth->success != 0)
						{
							local.sessionId = auth->session_id;
							authOk = true;
							return;
						}
						if (auth && auth->success == 0)
						{
							errMsg = std::string(NetErrorLabel(auth->error_code));
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
							errMsg = std::string(NetErrorLabel(er->errorCode)) + (er->message.empty() ? "" : (": " + er->message));
						else
							errMsg = "AUTH failed.";
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send AUTH failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!authDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!authOk)
			{
				masterClient->Disconnect("auth_failed");
				local.ready = true;
				local.message = errMsg.empty() ? "AUTH failed." : errMsg;
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			disp.SetSessionId(local.sessionId);
			bool statusDone = false;
			if (!disp.SendRequest(engine::network::kOpcodeTermsStatusRequest, engine::network::BuildTermsStatusRequestPayload(locale),
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						statusDone = true;
						if (timeout)
						{
							errMsg = "TERMS status timeout.";
							return;
						}
						auto terms = engine::network::ParseTermsStatusResponsePayload(pl.data(), pl.size());
						if (!terms)
						{
							errMsg = "TERMS status parse failed.";
							return;
						}
						local.termsPendingCount = terms->pending_count;
						local.termsEditionId = terms->next_edition_id;
						local.termsTitle = terms->title;
						local.termsVersionLabel = terms->version_label;
						local.termsLocale = terms->resolved_locale;
					},
					timeoutMs))
			{
				masterClient->Disconnect("terms_status_send_failed");
				local.ready = true;
				local.message = "Send TERMS_STATUS failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!statusDone && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			if (!statusDone)
			{
				masterClient->Disconnect("terms_status_timeout");
				local.ready = true;
				local.message = "TERMS status timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}

			if (local.termsPendingCount > 0 && local.termsEditionId != 0)
			{
				bool contentDone = false;
				if (!disp.SendRequest(engine::network::kOpcodeTermsContentRequest,
						engine::network::BuildTermsContentRequestPayload(local.termsEditionId, local.termsLocale, 0u, 8192u),
						[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
							contentDone = true;
							if (timeout)
							{
								errMsg = "TERMS content timeout.";
								return;
							}
							auto content = engine::network::ParseTermsContentResponsePayload(pl.data(), pl.size());
							if (!content)
							{
								errMsg = "TERMS content parse failed.";
								return;
							}
							local.totalLength = content->total_length;
							local.termsContent = content->chunk;
						},
						timeoutMs))
				{
					masterClient->Disconnect("terms_content_send_failed");
					local.ready = true;
					local.message = "Send TERMS_CONTENT failed.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
				while (!contentDone && std::chrono::steady_clock::now() < deadline)
				{
					disp.Pump();
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				}
				if (!contentDone)
				{
					masterClient->Disconnect("terms_content_timeout");
					local.ready = true;
					local.message = "TERMS content timeout.";
					std::lock_guard<AuthMutex> lock(*m_asyncMutex);
					m_asyncResult = local;
					return;
				}
				local.message = "Terms acceptance required before entering the game.";
			}
			else
			{
				local.message = "Authentication successful.";
			}

			local.ready = true;
			local.success = true;
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartMasterFlowWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		EnsurePasswordSalt(cfg);
		const std::string hash = ComputeClientHash(cfg);
		if (hash.empty())
		{
			EnterAuthErrorPhase(Phase::Login, Tr("auth.error.hash_password_failed"));
			LOG_ERROR(Core, "[AuthUiPresenter] Master flow aborted: empty client_hash");
			return;
		}

		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const std::string login = m_login;
		const bool shardPickWhenMultiple = cfg.GetBool("client.auth_ui.shard_pick_when_multiple", true);
		const uint32_t shardOverride = m_shardFlowOverrideId;
		m_shardFlowOverrideId = 0;

		m_pendingAsyncKind = AsyncKind::Login;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, login, hash, allowInsecure, serverFingerprint, shardPickWhenMultiple, shardOverride]() {
			AsyncResult local{};
			engine::network::NetClient masterClient;
			ApplyMasterTlsConfig(masterClient, serverFingerprint, allowInsecure);
			engine::network::MasterShardClientFlow flow;
			flow.SetMasterAddress(host, port);
			flow.SetCredentials(login, hash);
			flow.SetTimeoutMs(timeoutMs);
			flow.SetShardPickWhenMultiple(shardPickWhenMultiple);
			flow.SetShardIdOverride(shardOverride);
			LOG_INFO(Net, "[AuthUiPresenter] MasterShardClientFlow starting (login='{}')", login);
			engine::network::MasterShardFlowResult r = flow.Run(&masterClient);
			local.ready = true;
			if (r.shard_choice_required)
			{
				local.shardChoiceRequired = true;
				local.serverListForPick = std::move(r.server_list_for_pick);
				local.success = false;
				local.message.clear();
			}
			else
			{
				local.success = r.success;
				local.message = r.success ? (std::string("Shard ready (shard_id=") + std::to_string(r.shard_id) + ").") : r.errorMessage;
			}
			{
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
			}
			LOG_INFO(Net, "[AuthUiPresenter] MasterShardClientFlow finished (success={} shard_pick={})", (int)r.success, (int)r.shard_choice_required);
		});
	}

	void AuthUiPresenter::StartVerifyEmailWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const uint64_t accountId = m_pendingVerifyAccountId;
		const std::string code = m_verifyCode;

		m_pendingAsyncKind = AsyncKind::VerifyEmail;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, serverFingerprint, accountId, code]() {
			AsyncResult local{};
			engine::network::NetClient client;
			ApplyMasterTlsConfig(client, serverFingerprint, allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			std::vector<uint8_t> payload = engine::network::BuildVerifyEmailRequestPayload(accountId, code);
			bool done = false;
			bool ok = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeVerifyEmailRequest, payload,
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "VERIFY_EMAIL timeout.";
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
						{
							errMsg = std::string(NetErrorLabel(er->errorCode));
							return;
						}
						ok = true;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send VERIFY_EMAIL failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			local.ready = true;
			local.success = ok;
			local.message = ok ? std::string("Email verified successfully.") : (errMsg.empty() ? "VERIFY_EMAIL failed." : errMsg);
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartResendVerificationWorker(const engine::core::Config& cfg)
	{
		JoinWorker();
		const std::string host = cfg.GetEffectiveMasterHost("localhost");
		const uint16_t port = static_cast<uint16_t>(cfg.GetInt("client.master_port", 3840));
		const uint32_t timeoutMs = static_cast<uint32_t>(cfg.GetInt("client.auth_ui.timeout_ms", 5000));
		const bool allowInsecure = cfg.GetBool("client.allow_insecure_dev", true);
		const std::string serverFingerprint = cfg.GetString("client.server_fingerprint", "");
		const uint64_t accountId = m_pendingVerifyAccountId;

		m_pendingAsyncKind = AsyncKind::ResendVerification;
		{
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = {};
		}

		m_worker = std::thread([this, host, port, timeoutMs, allowInsecure, serverFingerprint, accountId]() {
			AsyncResult local{};
			engine::network::NetClient client;
			ApplyMasterTlsConfig(client, serverFingerprint, allowInsecure);
			client.Connect(host, port);
			if (!WaitConnected(&client, timeoutMs + 2000u))
			{
				local.ready = true;
				local.message = "Master connect failed or timeout.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			engine::network::RequestResponseDispatcher disp(&client);
			std::vector<uint8_t> payload = engine::network::BuildResendVerificationRequestPayload(accountId);
			bool done = false;
			bool ok = false;
			std::string errMsg;
			if (!disp.SendRequest(engine::network::kOpcodeResendVerificationRequest, payload,
					[&](uint32_t, bool timeout, std::vector<uint8_t> pl) {
						done = true;
						if (timeout)
						{
							errMsg = "RESEND_VERIFICATION timeout.";
							return;
						}
						auto er = engine::network::ParseErrorPayload(pl.data(), pl.size());
						if (er)
						{
							errMsg = std::string(NetErrorLabel(er->errorCode));
							return;
						}
						ok = true;
					},
					timeoutMs))
			{
				local.ready = true;
				local.message = "Send RESEND_VERIFICATION failed.";
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				return;
			}
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs + 500);
			while (!done && std::chrono::steady_clock::now() < deadline)
			{
				disp.Pump();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			local.ready = true;
			local.success = ok;
			local.message = ok ? std::string("Verification email resent.") : (errMsg.empty() ? "RESEND_VERIFICATION failed." : errMsg);
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
		});
	}

	void AuthUiPresenter::StartStatusProbeWorker(const engine::core::Config& cfg)
	{
		LOG_INFO(Core, "[StatusProbe] démarrage worker — url='{}'", m_masterAvailabilityUrl);
		JoinWorker();

		const std::string url = m_masterAvailabilityUrl;
		m_pendingAsyncKind = AsyncKind::StatusProbe;
		m_asyncResult = {};
		LOG_INFO(Core, "[StatusProbe] état async réinitialisé, kind=StatusProbe");

		auto fillSimulatedStatus = [&](AsyncResult& out, std::string_view reason)
		{
			out.ready = true;
			out.success = true;
			out.statusCache = {};
			out.statusCache.authOk = true;
			out.statusCache.masterOk = true;
			out.statusCache.totalPlayers = 0;
			out.statusCache.servers.clear();
			out.statusCache.servers.push_back({ "EU-1", true, 128u });
			out.statusCache.servers.push_back({ "US-1", true, 74u });
			out.statusCache.servers.push_back({ "AS-1", true, 19u });
			for (const auto& s : out.statusCache.servers)
				out.statusCache.totalPlayers += s.players;
			out.message = std::string("Status probe simulated: ") + std::string(reason);
		};

		if (url.empty())
		{
			LOG_WARN(Core, "[StatusProbe] URL vide — utilisation du jeu de données simulé (dev)");
			AsyncResult local{};
			fillSimulatedStatus(local, "empty status url");
			std::lock_guard<AuthMutex> lock(*m_asyncMutex);
			m_asyncResult = local;
			return;
		}

		const uint32_t timeoutMs =
			static_cast<uint32_t>(std::clamp<int64_t>(cfg.GetInt("client.auth_ui.status_probe_timeout_ms", 1500), 300, 5000));
		LOG_INFO(Core, "[StatusProbe] timeout HTTP configuré: {} ms", timeoutMs);

		m_worker = std::thread([this, url, timeoutMs]() {
			LOG_INFO(Core, "[StatusProbe] thread réseau: GET en cours vers {}", url);
			AsyncResult local{};
#if defined(_WIN32)
			const StatusHttpResponse resp = HttpGetStatusUrlWin32(url, timeoutMs);
#else
			const StatusHttpResponse resp = HttpGetStatusUrlCurl(url, timeoutMs);
#endif

			auto publishFailure = [&](std::string_view userMessage, std::string_view transportDetail) {
				local.ready = true;
				local.success = false;
				local.statusCache = {};
				local.statusCache.authOk = false;
				local.statusCache.masterOk = false;
				const std::string detail = std::string(transportDetail);
				local.statusCache.infoMessage = detail.empty() ? std::string(userMessage) : detail;
				local.message = std::string(userMessage);
				LOG_WARN(Core, "[StatusProbe] échec — msg='{}' transport='{}'", userMessage, detail);
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
			};

			if (!resp.transportError.empty())
			{
				publishFailure("erreur transport HTTP", resp.transportError);
				return;
			}

			LOG_INFO(Core, "[StatusProbe] réponse brute: code HTTP {} — {} octet(s)", resp.statusCode, resp.body.size());

			if (resp.statusCode < 200u || resp.statusCode >= 300u)
			{
				publishFailure("code HTTP hors plage 2xx", std::to_string(resp.statusCode));
				return;
			}

			if (resp.body.empty())
			{
				publishFailure("corps HTTP vide", {});
				return;
			}

			std::string parseDetail;
			if (!ParseStatusProbeJsonBody(resp.body, local.statusCache, parseDetail))
			{
				publishFailure(parseDetail, {});
				return;
			}

			local.ready = true;
			local.success = true;
			local.message = parseDetail;
			LOG_INFO(Core,
				"[StatusProbe] succès — authOk={} masterOk={} shards={} totalJoueurs={} message='{}'",
				local.statusCache.authOk ? "oui" : "non", local.statusCache.masterOk ? "oui" : "non", local.statusCache.servers.size(),
				local.statusCache.totalPlayers, local.message);

			{
				std::lock_guard<AuthMutex> lock(*m_asyncMutex);
				m_asyncResult = local;
				LOG_INFO(Core, "[StatusProbe] résultat publié (mutex), prêt pour PollAsyncResult");
			}
		});
		LOG_INFO(Core, "[StatusProbe] worker std::thread créé, joinable={}", m_worker.joinable() ? 1 : 0);
	}

	bool AuthUiPresenter::SetViewportSize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
		{
			LOG_WARN(Core, "[AuthUiPresenter] SetViewportSize FAILED: {}x{}", width, height);
			return false;
		}
		m_viewportW = width;
		m_viewportH = height;
		LOG_INFO(Core, "[AuthUiPresenter] Viewport {}x{}", width, height);
		return true;
	}


void AuthUiPresenter::SubmitCurrentPhase(const engine::core::Config& cfg)
{
	if (m_phase == Phase::Error)
	{
		const Phase target = m_errorReturnPhase;
		SetPhase(target);
		m_activeField = 0;
		const char* const phaseLabel = AuthUiPresenter::PhaseLogName(target);
		LOG_INFO(Core, "[AuthUiPresenter] Error acknowledged -> {}", phaseLabel);
		return;
	}
	if (m_phase == Phase::ShardPick)
	{
		if (m_shardPickChoiceShardId == 0)
		{
			EnterAuthErrorPhase(Phase::ShardPick, Tr("auth.error.shard_pick_required"));
			LOG_WARN(Core, "[AuthUiPresenter] ShardPick submit rejected: no shard selected");
			return;
		}
		SetPhase(Phase::Submitting);
		m_shardFlowOverrideId = m_shardPickChoiceShardId;
		StartMasterFlowWorker(cfg);
		return;
	}
	if (m_phase == Phase::Login)
	{
		if (m_login.empty() || m_password.empty())
		{
			EnterAuthErrorPhase(Phase::Login, Tr("auth.error.enter_login_password"));
			LOG_WARN(Core, "[AuthUiPresenter] Submit rejected: empty fields");
			return;
		}
		SetPhase(Phase::Submitting);
		StartLoginWorker(cfg);
		return;
	}
	if (m_phase == Phase::Register)
	{
		if (m_login.empty() || m_password.empty() || m_passwordConfirm.empty() || m_email.empty()
			|| m_firstName.empty() || m_lastName.empty() || m_birthDay.empty()
			|| m_birthMonth.empty() || m_birthYear.empty() || m_country.empty())
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.enter_register_fields"));
			LOG_WARN(Core, "[AuthUiPresenter] Register submit rejected: empty fields");
			return;
		}
		if (m_password != m_passwordConfirm)
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.password_mismatch"));
			LOG_WARN(Core, "[AuthUiPresenter] Register submit rejected: password mismatch");
			return;
		}
		if (!IsValidBirthDateFields(m_birthDay, m_birthMonth, m_birthYear))
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.invalid_birth_date"));
			LOG_INFO(Core,
				"[AUTH-REG] submit rejected: invalid birth date (day_len={} month_len={} year_len={})",
				m_birthDay.size(),
				m_birthMonth.size(),
				m_birthYear.size());
			return;
		}
		if (m_country.size() != 2 || !std::isalpha(static_cast<unsigned char>(m_country[0])) || !std::isalpha(static_cast<unsigned char>(m_country[1])))
		{
			EnterAuthErrorPhase(Phase::Register, Tr("auth.error.enter_country"));
			LOG_INFO(Core, "[AUTH-REG] submit rejected: invalid country ('{}')", m_country);
			return;
		}
		LOG_INFO(Core,
			"[AUTH-REG] submit accepted -> StartRegisterWorker (login_len={} email_len={} first_len={} last_len={} pwd_len={} "
			"birth_d_len={} birth_m_len={} birth_y_len={})",
			m_login.size(),
			m_email.size(),
			m_firstName.size(),
			m_lastName.size(),
			m_password.size(),
			m_birthDay.size(),
			m_birthMonth.size(),
			m_birthYear.size());
		SetPhase(Phase::Submitting);
		StartRegisterWorker(cfg);
		return;
	}
	if (m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
	{
		if (m_pendingVerifyAccountId == 0 || m_verifyCode.empty())
		{
			EnterAuthErrorPhase(Phase::VerifyEmail, Tr("auth.error.enter_verify_code"));
			return;
		}
		if (!IsValidVerificationCode(m_verifyCode))
		{
			EnterAuthErrorPhase(Phase::VerifyEmail, Tr("auth.error.invalid_verify_code"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartVerifyEmailWorker(cfg);
		return;
	}
	if (m_phase == Phase::ForgotPassword)
	{
		if (m_email.empty())
		{
			EnterAuthErrorPhase(Phase::ForgotPassword, Tr("auth.error.enter_recovery_email"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartForgotPasswordWorker(cfg);
		return;
	}
	if (m_phase == Phase::Terms)
	{
		if (!m_termsScrolledToBottom || !m_termsAcknowledgeChecked)
		{
			EnterAuthErrorPhase(Phase::Terms, Tr("auth.error.accept_terms"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartTermsAcceptWorker(cfg);
		return;
	}
	if (m_phase == Phase::CharacterCreate)
	{
		if (m_characterName.empty())
		{
			EnterAuthErrorPhase(Phase::CharacterCreate, Tr("auth.error.enter_character_name"));
			return;
		}
		if (!IsValidCharacterNameLocal(m_characterName))
		{
			EnterAuthErrorPhase(Phase::CharacterCreate, Tr("auth.error.invalid_character_name"));
			return;
		}
		SetPhase(Phase::Submitting);
		StartCharacterCreateWorker(cfg);
	}
}
#endif

	void AuthUiPresenter::Update(engine::platform::Input& input, float deltaSeconds, engine::platform::Window& window,
		const engine::core::Config& cfg)
	{
		LOG_DEBUG(Core, "[DIAG-AUTH] Update enter this={} initialized={} flowComplete={} authEnabled={}",
			(void*)this, m_initialized ? 1 : 0, m_flowComplete ? 1 : 0, m_authEnabled ? 1 : 0);
		m_usingNativeAuthScreen = false;
		LOG_DEBUG(Core, "[DIAG-AUTH] before SetAuthScreenState");
		window.SetAuthScreenState({});
		LOG_DEBUG(Core, "[DIAG-AUTH] after SetAuthScreenState");
		if (!m_initialized || m_flowComplete || !m_authEnabled)
			return;

		LOG_DEBUG(Core, "[DIAG-AUTH] before PollAsyncResult m_asyncMutex={}", (void*)m_asyncMutex.get());
		PollAsyncResult(cfg);
		LOG_DEBUG(Core, "[DIAG-AUTH] after PollAsyncResult");
		if (m_flowComplete)
			return;

		LOG_DEBUG(Core, "[DIAG-AUTH] before statusProbe check phase={}", static_cast<int>(m_phase));
		constexpr float kStatusProbeIntervalSeconds = 120.0f;
		const bool shouldProbeStatus = !m_masterAvailabilityUrl.empty() && !m_flowComplete && m_phase != Phase::Submitting;
		const bool statusProbeInFlight = m_worker.joinable() && m_pendingAsyncKind == AsyncKind::StatusProbe;
		m_authAvailabilityChecking = statusProbeInFlight;
		if (m_authAvailabilityChecking)
			m_authLogoRotationRad += deltaSeconds * 2.8f;

		if (shouldProbeStatus)
		{
			// Démarrage immédiat dès le début de l'écran d'authentification, puis toutes les 2 minutes.
			if (!m_statusProbeInitialized && !m_worker.joinable())
			{
				LOG_INFO(Core, "[StatusProbe] Update: première sonde planifiée (écran auth)");
				StartStatusProbeWorker(cfg);
				m_statusProbeInitialized = true;
				m_statusPollTimer = 0.f;
			}
			else if (m_statusProbeInitialized && !m_worker.joinable())
			{
				m_statusPollTimer += deltaSeconds;
				if (m_statusPollTimer >= kStatusProbeIntervalSeconds)
				{
					LOG_INFO(Core, "[StatusProbe] Update: rafraîchissement périodique (timer ≥ {} s)", kStatusProbeIntervalSeconds);
					StartStatusProbeWorker(cfg);
					m_statusPollTimer = 0.f;
				}
			}
		}
		else if (!statusProbeInFlight)
		{
			m_authAvailabilityPollTimer = 0.f;
			m_authLogoRotationRad = 0.f;
			m_statusProbeInitialized = false;
			m_statusPollTimer = 0.f;
		}

		if (m_phase == Phase::Submitting)
		{
			window.SetAuthScreenState({});
			UpdateWindowTitle(window);
			return;
		}

		// Plan C : tick debounce disponibilité username.
		// Le worker n'est lancé que si aucun worker critique n'est actif (Register/Login/etc.).
		if (m_usernameDebounceTimer > 0.0)
		{
			m_usernameDebounceTimer -= static_cast<double>(deltaSeconds);
			if (m_usernameDebounceTimer <= 0.0)
			{
				m_usernameDebounceTimer = 0.0;
				if (m_phase == Phase::Register && m_login.size() >= 3
					&& !m_worker.joinable() && !m_asyncResult.ready)
				{
					m_usernameCheckState = UsernameCheckState::Pending;
					m_usernameLastChecked = m_login;
					StartUsernameCheckWorker(cfg);
					LOG_DEBUG(Core, "[UsernameCheck] worker lancé pour login='{}' seq={}", m_usernameLastChecked, m_usernameCheckSeq);
				}
			}
		}

		const bool usingNativeAuth = false;
		const bool authUiImguiMode = cfg.GetBool("render.auth_ui.imgui.enabled", false);

		auto currentField = [this]() -> std::string* {
			switch (m_phase)
			{
			case Phase::Login:
				if (m_activeField == 0)
					return &m_login;
				if (m_activeField == 1)
					return &m_password;
				return nullptr;
			case Phase::Register:
				switch (m_activeField)
				{
				case 0: return &m_login;
				case 1: return &m_password;
				case 2: return &m_passwordConfirm;
				case 3: return &m_email;
				case 4: return &m_firstName;
				case 5: return &m_lastName;
				case 6: return &m_birthDay;
				case 7: return &m_birthMonth;
				case 8: return &m_birthYear;
				default: return &m_country;
				}
			case Phase::VerifyEmail:
			case Phase::EmailConfirmationPending:
				return &m_verifyCode;
			case Phase::ForgotPassword:
				return &m_email;
			case Phase::Terms:
				return nullptr;
			case Phase::CharacterCreate:
				return &m_characterName;
			case Phase::LanguageSelectionFirstRun:
			case Phase::LanguageOptions:
			default:
				return nullptr;
			}
		};

		auto applyPrimaryAction = [&]()
		{
			if (m_phase == Phase::LanguageSelectionFirstRun)
			{
				ApplyLocaleSelection(true);
			}
			else if (m_phase == Phase::LanguageOptions)
			{
				CommitLanguageOptionsMenuApply(cfg);
			}
			else if (m_phase == Phase::Terms)
			{
				if (!m_termsScrolledToBottom)
					return;
				if (!m_termsAcknowledgeChecked)
				{
					m_termsAcknowledgeChecked = true;
					return;
				}
				SubmitCurrentPhase(cfg);
			}
			else
			{
				if (m_phase == Phase::Register)
				{
					LOG_INFO(Core, "[AUTH-REG] UI primary action (submit button / Enter) -> SubmitCurrentPhase");
				}
				SubmitCurrentPhase(cfg);
			}
		};

		if (!usingNativeAuth)
		{
			RenderModel model = BuildRenderModel();
			if (model.visible && m_viewportW > 0 && m_viewportH > 0 && !authUiImguiMode)
			{
				const VkExtent2D ext{ m_viewportW, m_viewportH };
				const VisualState vsLayout = GetVisualState();
				const engine::render::AuthUiLayoutMetrics lay = engine::render::BuildAuthUiLayoutMetrics(ext, vsLayout, model);
				const int32_t panelY = lay.panelY;
				const int32_t panelH = lay.panelH;
				const int32_t contentX = lay.contentX;
				const int32_t contentW = lay.contentW;
				const int32_t topOffset = lay.topOffset;
				const int32_t fieldStep = lay.fieldRowStepPx;
				const int32_t bodyScale = engine::render::AuthUiClassicTextScaleFromPanelW(lay.panelW);
				const int32_t bodyLineStep = 7 * bodyScale + 2 * bodyScale;
				const bool centeredLanguageSelection = vsLayout.languageSelection || vsLayout.languageOptions;
				const int32_t bitmapBodyLinePitch = centeredLanguageSelection
					? std::max(36, bodyLineStep + 16)
					: std::max(28, bodyLineStep + 10);
				const int32_t approxTtfLh = static_cast<int32_t>(std::lround(static_cast<float>(bodyLineStep) * 1.28f));
				const int32_t bodyLinePitch = std::max(bitmapBodyLinePitch,
					approxTtfLh + (centeredLanguageSelection ? 14 : 10));
				const int32_t afterFieldsGap = centeredLanguageSelection ? 34 : 18;
				const int32_t fieldCount = static_cast<int32_t>(model.fields.size());
				std::vector<int32_t> fieldLogicalRow(static_cast<size_t>(fieldCount), 0);
				if (fieldCount > 0)
				{
					int32_t row = -1;
					int32_t lastCol = engine::render::kAuthUiGridColumns;
					for (int32_t fi = 0; fi < fieldCount; ++fi)
					{
						const auto& f = model.fields[static_cast<size_t>(fi)];
						if (f.gridColumn < 0 || f.gridColumn <= lastCol)
							++row;
						lastCol = (f.gridColumn < 0) ? engine::render::kAuthUiGridColumns : f.gridColumn;
						fieldLogicalRow[static_cast<size_t>(fi)] = row;
					}
				}
				const int32_t logicalRowCount =
					fieldCount > 0 ? fieldLogicalRow[static_cast<size_t>(fieldCount - 1)] + 1 : 0;
				const int32_t bodyStartY = panelY + topOffset + logicalRowCount * fieldStep + afterFieldsGap;
				const int32_t mx = input.MouseX();
				const int32_t my = input.MouseY();
				m_hoveredFieldIndex = -1;
				m_hoveredFieldInfoIndex = -1;
				m_hoveredBodyLineIndex = -1;
				m_hoveredActionIndex = -1;
				m_hoveredLanguageCardIndex = -1;

				auto contains = [](int32_t px, int32_t py, int32_t x, int32_t y, int32_t rw, int32_t rh)
				{
					return px >= x && py >= y && px < (x + rw) && py < (y + rh);
				};

				// --- Calcul géométrie dropdowns date (phase Inscription) ---
				// Les dropdowns jour/mois/année sont à la ligne logique 3 de la grille d'inscription,
				// colonnes 0, 1, 2. On remplit dd.x/y/w/h ici (même logique que AuthGlyphPass).
				if (m_phase == Phase::Register && model.dropdowns.size() == 3)
				{
					// Les champs birthDay/Month/Year ont les indices 5, 6, 7 dans la liste des champs.
					// Leurs colonnes respectives : 0, 1, 2.
					constexpr int kBirthFieldBase = 5;
					constexpr int kDropdownH = engine::render::kAuthUiFieldBoxHeightPx;
					for (int di = 0; di < 3 && (kBirthFieldBase + di) < fieldCount; ++di)
					{
						const int32_t logRow = fieldLogicalRow[static_cast<size_t>(kBirthFieldBase + di)];
						const int32_t ddY = panelY + topOffset + logRow * fieldStep;
						int32_t ddX = contentX;
						int32_t ddW = contentW;
						engine::render::AuthUiGridFieldGeometry(contentX, contentW, di, 1, ddX, ddW);
						auto& dd = model.dropdowns[static_cast<size_t>(di)];
						dd.x = ddX;
						dd.y = ddY;
						dd.w = ddW;
						dd.h = kDropdownH;
					}
				}

				for (size_t i = 0; i < model.fields.size(); ++i)
				{
					const auto& fldHit = model.fields[i];
					const int32_t rowY = panelY + topOffset + fieldLogicalRow[i] * fieldStep;
					int32_t fx = contentX;
					int32_t fw = contentW;
					if (fldHit.gridColumn >= 0)
					{
						engine::render::AuthUiGridFieldGeometry(
						    contentX, contentW, fldHit.gridColumn, fldHit.gridSpan, fx, fw);
					}
					if (contains(mx, my, fx, rowY, fw, engine::render::kAuthUiFieldBoxHeightPx))
					{
						m_hoveredFieldIndex = static_cast<int32_t>(i);
						break;
					}
				}
				const int32_t smallScaleHit = std::max(2, bodyScale - 1);
				const int32_t labelAboveFieldPxHit = smallScaleHit * 11 + 6;
				for (int32_t fi = 0; fi < static_cast<int32_t>(model.fields.size()); ++fi)
				{
					const RenderField& fld = model.fields[static_cast<size_t>(fi)];
					if (fld.tooltipText.empty())
					{
						continue;
					}
					const int32_t rowY = panelY + topOffset + fieldLogicalRow[static_cast<size_t>(fi)] * fieldStep;
					int32_t fx = contentX;
					int32_t fw = contentW;
					if (fld.gridColumn >= 0)
					{
						engine::render::AuthUiGridFieldGeometry(
						    contentX, contentW, fld.gridColumn, fld.gridSpan, fx, fw);
					}
					const int32_t ix = std::max(fx + 10, fx + fw - 36);
					const int32_t iy = rowY - labelAboveFieldPxHit;
					if (contains(mx, my, ix, iy, 18, 18))
					{
						m_hoveredFieldInfoIndex = fi;
						break;
					}
				}

				if (m_phase == Phase::Register && input.MouseScrollDelta() != 0)
				{
					int32_t f = m_hoveredFieldIndex;
					if ((f < 6 || f > 8) && f != 9)
					{
						f = static_cast<int32_t>(m_activeField);
					}
					if ((f >= 6 && f <= 8) || f == 9)
					{
						const int d = input.MouseScrollDelta() > 0 ? 1 : -1;
						if (f == 6)
						{
							AdjustBirthCycle(m_birthDay, d, 1, 31);
						}
						else if (f == 7)
						{
							AdjustBirthCycle(m_birthMonth, d, 1, 12);
						}
						else if (f == 8)
						{
							AdjustBirthCycle(m_birthYear, d, 1900, 2100);
						}
						else if (f == 9)
							AdjustCountryCycle(m_country, d);
					}
				}

				for (int32_t localIdx = 0; localIdx < model.visibleBodyLineCount; ++localIdx)
				{
					const int32_t bodyIndex = model.visibleBodyLineStart + localIdx;
					const int32_t y = bodyStartY + localIdx * bodyLinePitch - 4;
					bool hit = false;
					if (((m_phase == Phase::Login) || (m_phase == Phase::ShardPick))
						&& static_cast<size_t>(bodyIndex) < model.bodyLines.size())
					{
						const RenderBodyLine& bl = model.bodyLines[static_cast<size_t>(bodyIndex)];
						if (bl.checkbox)
						{
							const int32_t cbX = contentX + 2;
							const int32_t cbY = y - 5;
							const int32_t cbS = engine::render::kAuthUiCheckboxOuterPx;
							if (contains(mx, my, cbX, cbY, cbS, cbS)
								|| contains(mx, my, contentX + engine::render::kAuthUiCheckboxLabelOffsetX - 2, y - 8, contentW - engine::render::kAuthUiCheckboxLabelOffsetX, 22))
							{
								hit = true;
							}
						}
						else if (bl.link)
						{
							const int32_t fw = std::min(contentW - 8, static_cast<int32_t>(static_cast<int>(bl.text.size()) * bodyScale * 6));
							if (contains(mx, my, contentX + 2, y - 8, std::max(64, fw), 22))
							{
								hit = true;
							}
						}
						else
						{
							hit = contains(mx, my, contentX - 4, y - 8, contentW, 22);
						}
					}
					else
					{
						hit = contains(mx, my, contentX - 4, y - 8, contentW, 22);
					}
					if (hit)
					{
						m_hoveredBodyLineIndex = bodyIndex;
						break;
					}
				}
				const int32_t actionCount = std::max<int32_t>(1, static_cast<int32_t>(model.actions.size()));
				const int32_t gap = 10;
				engine::render::AuthLoginTwoRowLayout loginTwoRow{};
				if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout)
				{
					for (int32_t ci = 0; ci < lay.languageCardCount; ++ci)
					{
						if (contains(mx, my, lay.languageCardX[ci], lay.languageCardY[ci], lay.languageCardW[ci], lay.languageCardH[ci]))
						{
							m_hoveredLanguageCardIndex = ci;
							break;
						}
					}
					if (contains(mx, my, lay.languagePanelPrimaryButtonX, lay.languagePanelPrimaryButtonY,
							lay.languagePanelPrimaryButtonW, lay.languagePanelPrimaryButtonH))
					{
						for (int32_t ai = 0; ai < actionCount; ++ai)
						{
							if (model.actions[static_cast<size_t>(ai)].primary)
							{
								m_hoveredActionIndex = ai;
								break;
							}
						}
					}
				}
				else if (m_phase == Phase::Terms)
				{
					const int32_t actionW = std::max(110, (contentW - (actionCount - 1) * gap) / actionCount);
					const int32_t termsBtnY = panelY + panelH - 92 + (58 - engine::render::kAuthUiActionButtonHeightPx) / 2;
					for (int32_t i = 0; i < actionCount; ++i)
					{
						const int32_t x = contentX + i * (actionW + gap);
						if (contains(mx, my, x, termsBtnY, actionW, engine::render::kAuthUiActionButtonHeightPx))
						{
							m_hoveredActionIndex = i;
							break;
						}
					}
				}
				else if (m_phase == Phase::Login && lay.loginMaquetteLayout && actionCount == 4)
				{
					const int32_t yBtn = lay.loginPairRowY;
					if (contains(mx, my, lay.loginRegisterBtnX, yBtn, lay.loginRegisterBtnW, engine::render::kAuthUiActionButtonHeightPx))
					{
						m_hoveredActionIndex = 0;
					}
					else if (contains(mx, my, lay.loginSubmitBtnX, yBtn, lay.loginSubmitBtnW, engine::render::kAuthUiActionButtonHeightPx))
					{
						m_hoveredActionIndex = 2;
					}
					else if (contains(mx, my, lay.loginOutLinkOptsX, lay.loginOutLinksY - 4, lay.loginOutLinkOptsW, 26))
					{
						m_hoveredActionIndex = 1;
					}
					else if (contains(mx, my, lay.loginOutLinkQuitX, lay.loginOutLinksY - 4, lay.loginOutLinkQuitW, 26))
					{
						m_hoveredActionIndex = 3;
					}
				}
				else if (engine::render::TryGetLoginTwoRowLayout(lay, vsLayout, model, loginTwoRow))
				{
					bool foundAction = false;
					for (int32_t row = 0; row < 2 && !foundAction; ++row)
					{
						const int32_t rowY = (row == 0) ? loginTwoRow.secondaryRowY : loginTwoRow.primaryRowY;
						for (int32_t col = 0; col < 2; ++col)
						{
							const int32_t i = row * 2 + col;
							if (i >= actionCount)
							{
								break;
							}
							int32_t btnW = loginTwoRow.buttonHalfWidth;
							int32_t x = contentX + col * (loginTwoRow.buttonHalfWidth + gap);
							if (row == 1)
							{
								btnW = (col == 0) ? loginTwoRow.primarySubmitWidth : loginTwoRow.primaryQuitWidth;
								x = (col == 0) ? contentX : (contentX + loginTwoRow.primarySubmitWidth + gap);
							}
							if (contains(mx, my, x, rowY, btnW, engine::render::kAuthUiActionButtonHeightPx))
							{
								m_hoveredActionIndex = i;
								foundAction = true;
								break;
							}
						}
					}
				}
				else
				{
					const int32_t buttonPadAfterBody = centeredLanguageSelection ? 28 : 20;
					constexpr int32_t kAuthErrorFooterBarH = 58;
					int32_t buttonY = std::min(panelY + panelH - 86,
						bodyStartY + static_cast<int32_t>(model.visibleBodyLineCount) * bodyLinePitch + buttonPadAfterBody);
					if (m_phase == Phase::Error && lay.authErrorFooterTopFromPanelTopPx > 0)
					{
						buttonY = panelY + lay.authErrorFooterTopFromPanelTopPx
							+ (kAuthErrorFooterBarH - engine::render::kAuthUiActionButtonHeightPx) / 2;
					}
					const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
					for (int32_t i = 0; i < actionCount; ++i)
					{
						const int32_t x = contentX + i * (actionW + gap);
						if (contains(mx, my, x, buttonY, actionW, engine::render::kAuthUiActionButtonHeightPx))
						{
							m_hoveredActionIndex = i;
							break;
						}
					}
				}

				const bool leftClickRaw = input.WasMousePressed(engine::platform::MouseButton::Left);
				const bool leftClick = leftClickRaw
					|| (input.IsMouseDown(engine::platform::MouseButton::Left) && !m_authPrevMouseLeftDown);
				const bool rightClick = input.WasMousePressed(engine::platform::MouseButton::Right);
				if (leftClick || rightClick)
				{
					for (size_t i = 0; i < model.fields.size(); ++i)
					{
						const auto& fldClick = model.fields[i];
						const int32_t rowY = panelY + topOffset + fieldLogicalRow[i] * fieldStep;
						int32_t fx = contentX;
						int32_t fw = contentW;
						if (fldClick.gridColumn >= 0)
						{
							engine::render::AuthUiGridFieldGeometry(
							    contentX, contentW, fldClick.gridColumn, fldClick.gridSpan, fx, fw);
						}
						if (contains(mx, my, fx, rowY, fw, engine::render::kAuthUiFieldBoxHeightPx))
						{
							m_activeField = static_cast<uint32_t>(i);
							break;
						}
					}

					if (m_hoveredBodyLineIndex >= 0)
					{
						if (m_phase == Phase::Login && m_hoveredBodyLineIndex == 1 && leftClick)
						{
							const std::string resetUrl = ResolvePasswordRecoveryUrl(cfg);
							if (!window.OpenExternalUrl(resetUrl))
							{
								EnterAuthErrorPhase(Phase::Login, Tr("auth.error.open_recovery_portal"));
							}
						}
						else if (m_phase == Phase::Login && m_hoveredBodyLineIndex == 0)
						{
							m_activeField = 2u;
							m_rememberLogin = !m_rememberLogin;
							SaveRememberPreference();
							if (m_rememberLogin)
							{
								PersistRememberedLoginSidecar(m_login);
							}
							else
							{
								ClearRememberedLoginSidecar();
							}
						}
						else if (m_phase == Phase::ShardPick && m_hoveredBodyLineIndex >= 1 && leftClick)
						{
							int32_t remaining = m_hoveredBodyLineIndex;
							for (const auto& e : m_shardPickEntries)
							{
								if (e.status != 1u || e.endpoint.empty())
								{
									continue;
								}
								if (--remaining == 0)
								{
									m_shardPickChoiceShardId = e.shard_id;
									break;
								}
							}
						}
						else if (m_phase == Phase::LanguageSelectionFirstRun)
						{
							const auto& locales = m_localization.GetAvailableLocales();
							if (m_hoveredBodyLineIndex == 1 && !locales.empty())
							{
								m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % static_cast<uint32_t>(locales.size());
								m_selectedLocale = locales[static_cast<size_t>(m_languageSelectionIndex)];
							}
							else if (rightClick && !locales.empty())
							{
								m_languageSelectionIndex = (m_languageSelectionIndex == 0u)
									? static_cast<uint32_t>(locales.size() - 1u)
									: (m_languageSelectionIndex - 1u);
								m_selectedLocale = locales[m_languageSelectionIndex];
							}
						}
						else if (m_phase == Phase::LanguageOptions)
						{
							const auto& locales = m_localization.GetAvailableLocales();
							if (m_optionsSubMenu == OptionsSubMenu::Root)
							{
								const int32_t catLine = m_hoveredBodyLineIndex - 1;
								if (leftClick && catLine >= 0 && catLine < 5)
								{
									m_optionsRootSelection = static_cast<uint32_t>(catLine);
									EnterOptionsSubmenuFromRoot(m_optionsRootSelection);
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Language && !locales.empty()
								&& (m_hoveredBodyLineIndex == 0 || m_hoveredBodyLineIndex == 1)
								&& (leftClick || rightClick))
							{
								if (rightClick)
								{
									m_languageSelectionIndex = (m_languageSelectionIndex == 0u)
										? static_cast<uint32_t>(locales.size() - 1u)
										: (m_languageSelectionIndex - 1u);
								}
								else
								{
									m_languageSelectionIndex = (m_languageSelectionIndex + 1u) % static_cast<uint32_t>(locales.size());
								}
								m_selectedLocale = locales[static_cast<size_t>(m_languageSelectionIndex)];
								if (m_hoveredBodyLineIndex >= 0)
								{
									m_optionsSubSelection = static_cast<uint32_t>(m_hoveredBodyLineIndex);
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Video)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 1 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									if (row == 0)
									{
										m_videoFullscreenPending = !m_videoFullscreenPending;
									}
									else
									{
										m_videoVsyncPending = !m_videoVsyncPending;
									}
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Audio)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 3 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									const bool dec = rightClick;
									switch (row)
									{
									case 0:
										m_audioMasterVolumePending = ClampOptionStep(m_audioMasterVolumePending + (dec ? -0.1f : 0.1f));
										break;
									case 1:
										m_audioMusicVolumePending = ClampOptionStep(m_audioMusicVolumePending + (dec ? -0.1f : 0.1f));
										break;
									case 2:
										m_audioSfxVolumePending = ClampOptionStep(m_audioSfxVolumePending + (dec ? -0.1f : 0.1f));
										break;
									case 3:
										m_audioUiVolumePending = ClampOptionStep(m_audioUiVolumePending + (dec ? -0.1f : 0.1f));
										break;
									default:
										break;
									}
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Controls)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 2 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									switch (row)
									{
									case 0:
										if (rightClick)
										{
											m_mouseSensitivityPending = std::max(0.001f, m_mouseSensitivityPending - 0.001f);
										}
										else
										{
											m_mouseSensitivityPending = std::min(0.010f, m_mouseSensitivityPending + 0.001f);
										}
										break;
									case 1:
										m_invertYPending = !m_invertYPending;
										break;
									case 2:
										m_useZqsdPending = !m_useZqsdPending;
										break;
									default:
										break;
									}
								}
							}
							else if (m_optionsSubMenu == OptionsSubMenu::Game)
							{
								const int32_t row = m_hoveredBodyLineIndex;
								if (row >= 0 && row <= 2 && (leftClick || rightClick))
								{
									m_optionsSubSelection = static_cast<uint32_t>(row);
									switch (row)
									{
									case 0:
										m_gameplayUdpEnabledPending = !m_gameplayUdpEnabledPending;
										break;
									case 1:
										m_allowInsecureDevPending = !m_allowInsecureDevPending;
										break;
									case 2:
										if (rightClick)
										{
											m_authTimeoutMsPending = (m_authTimeoutMsPending > 1000u) ? (m_authTimeoutMsPending - 1000u) : 1000u;
										}
										else
										{
											m_authTimeoutMsPending = std::min<uint32_t>(15000u, m_authTimeoutMsPending + 1000u);
										}
										break;
									default:
										break;
									}
								}
							}
						}
						else if (m_phase == Phase::Terms && m_hoveredBodyLineIndex >= 0 && rightClick && m_termsScrolledToBottom)
						{
							m_termsAcknowledgeChecked = !m_termsAcknowledgeChecked;
						}
					}

					if (leftClick)
					{
						// --- Hit-test icône "i" (avant dropdowns et boutons) ---
						bool iconHit = false;
						if (model.infoIconVisible)
						{
							if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout)
							{
								const bool hitIcon = contains(mx, my, lay.languageInfoIconX, lay.languageInfoIconY, lay.languageInfoIconW,
									lay.languageInfoIconH);
								const bool hitPlate = lay.languageProgressPlatePresent
									&& contains(mx, my, lay.languageProgressPlateX, lay.languageProgressPlateY, lay.languageProgressPlateW,
										lay.languageProgressPlateH);
								if (hitIcon || hitPlate)
								{
									m_infoPopupVisible = !m_infoPopupVisible;
									if (m_infoPopupVisible)
									{
										m_infoPopupText = Tr("language.first_run.info_popup");
									}
									iconHit = true;
								}
							}
							else
							{
								for (int32_t fi = 0; fi < static_cast<int32_t>(model.fields.size()) && !iconHit; ++fi)
								{
									const RenderField& fld = model.fields[static_cast<size_t>(fi)];
									if (fld.tooltipText.empty())
									{
										continue;
									}
									const int32_t rowY = panelY + topOffset + fieldLogicalRow[static_cast<size_t>(fi)] * fieldStep;
									int32_t fx = contentX;
									int32_t fw = contentW;
									if (fld.gridColumn >= 0)
									{
										engine::render::AuthUiGridFieldGeometry(
										    contentX, contentW, fld.gridColumn, fld.gridSpan, fx, fw);
									}
									const int32_t ix = std::max(fx + 10, fx + fw - 36);
									const int32_t iy = rowY - labelAboveFieldPxHit;
									if (contains(mx, my, ix, iy, 18, 18))
									{
										m_infoPopupVisible = !m_infoPopupVisible;
										m_infoPopupText    = fld.tooltipText;
										iconHit = true;
									}
								}
							}
						}
						// Clic hors icône quand le popup est ouvert → fermer le popup.
						if (!iconHit && m_infoPopupVisible)
						{
							m_infoPopupVisible = false;
						}

						// --- Gestion clics dropdowns date de naissance (phase Inscription) ---
						// Traité avant les boutons d'action pour éviter qu'un clic sur un dropdown
						// déclenche accidentellement un bouton.
						bool dropdownHandled = false;
						if (m_phase == Phase::Register && model.dropdowns.size() == 3)
						{
							constexpr int32_t kOptionH = engine::render::kAuthUiFieldBoxHeightPx;
							for (int i = 0; i < 3 && !dropdownHandled; ++i)
							{
								const auto& dd = model.dropdowns[static_cast<size_t>(i)];
								// Clic sur l'en-tête du dropdown (ouverture/fermeture).
								if (contains(mx, my, dd.x, dd.y, dd.w, dd.h))
								{
									m_openDropdownIndex = (m_openDropdownIndex == i) ? -1 : i;
									dropdownHandled = true;
									break;
								}
								// Si ce dropdown est ouvert, tester les options.
								if (m_openDropdownIndex == i && dd.isOpen)
								{
									const int32_t optCount = static_cast<int32_t>(dd.options.size());
									for (int32_t j = 0; j < optCount && !dropdownHandled; ++j)
									{
										const int32_t optY = dd.y + dd.h + j * kOptionH;
										if (contains(mx, my, dd.x, optY, dd.w, kOptionH))
										{
											if (i == 0) m_birthDayIndex   = j;
											else if (i == 1) m_birthMonthIndex = j;
											else if (i == 2) m_birthYearIndex  = j;
											m_openDropdownIndex = -1; // fermer après sélection
											dropdownHandled = true;
										}
									}
								}
							}
							// Clic en dehors de tout dropdown : fermer le dropdown ouvert.
							if (!dropdownHandled && m_openDropdownIndex >= 0)
							{
								m_openDropdownIndex = -1;
							}
						}

						bool actionHit = false;
						if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout
							&& (leftClick || rightClick))
						{
							for (int32_t ci = 0; ci < lay.languageCardCount; ++ci)
							{
								if (contains(mx, my, lay.languageCardX[ci], lay.languageCardY[ci], lay.languageCardW[ci], lay.languageCardH[ci]))
								{
									actionHit = true;
									const auto& localesPick = m_localization.GetAvailableLocales();
									if (!localesPick.empty() && static_cast<size_t>(ci) < model.languageFirstRunCards.size())
									{
										const std::string& tagPick = model.languageFirstRunCards[static_cast<size_t>(ci)].localeTag;
										const auto itPick = std::find(localesPick.begin(), localesPick.end(), tagPick);
										if (itPick != localesPick.end())
										{
											m_languageSelectionIndex = static_cast<uint32_t>(std::distance(localesPick.begin(), itPick));
											m_selectedLocale = *itPick;
										}
									}
									break;
								}
							}
							if (!actionHit && leftClick && contains(mx, my, lay.languagePanelPrimaryButtonX, lay.languagePanelPrimaryButtonY,
									lay.languagePanelPrimaryButtonW, lay.languagePanelPrimaryButtonH))
							{
								actionHit = true;
								applyPrimaryAction();
							}
						}
						else if (m_phase == Phase::Terms)
						{
							const int32_t termsBtnY = panelY + panelH - 92 + (58 - engine::render::kAuthUiActionButtonHeightPx) / 2;
							const int32_t actionW = std::max(110, (contentW - (actionCount - 1) * gap) / actionCount);
							for (int32_t i = 0; i < actionCount; ++i)
							{
								const int32_t x = contentX + i * (actionW + gap);
								if (!contains(mx, my, x, termsBtnY, actionW, engine::render::kAuthUiActionButtonHeightPx))
								{
									continue;
								}
								actionHit = true;
								if (i == 0)
								{
									applyPrimaryAction();
								}
								break;
							}
						}
						else if (m_phase == Phase::Login && lay.loginMaquetteLayout && actionCount == 4)
						{
							const int32_t yBtn = lay.loginPairRowY;
							if (contains(mx, my, lay.loginRegisterBtnX, yBtn, lay.loginRegisterBtnW, engine::render::kAuthUiActionButtonHeightPx))
							{
								actionHit = true;
								SetPhase(Phase::Register);
								m_activeField = 0;
								m_userErrorText.clear();
								m_passwordConfirm.clear();
								m_usernameCheckState = UsernameCheckState::Idle;
								m_usernameCheckSeq++;
								m_usernameDebounceTimer = 0.0;
								m_usernameLastChecked.clear();
							}
							else if (contains(mx, my, lay.loginSubmitBtnX, yBtn, lay.loginSubmitBtnW, engine::render::kAuthUiActionButtonHeightPx))
							{
								actionHit = true;
								applyPrimaryAction();
							}
							else if (contains(mx, my, lay.loginOutLinkOptsX, lay.loginOutLinksY - 4, lay.loginOutLinkOptsW, 26))
							{
								actionHit = true;
								OpenLanguageOptions();
							}
							else if (contains(mx, my, lay.loginOutLinkQuitX, lay.loginOutLinksY - 4, lay.loginOutLinkQuitW, 26))
							{
								actionHit = true;
								window.RequestClose();
							}
						}
						else if (engine::render::TryGetLoginTwoRowLayout(lay, vsLayout, model, loginTwoRow))
						{
							for (int32_t row = 0; row < 2 && !actionHit; ++row)
							{
								const int32_t rowY = (row == 0) ? loginTwoRow.secondaryRowY : loginTwoRow.primaryRowY;
								for (int32_t col = 0; col < 2; ++col)
								{
									const int32_t i = row * 2 + col;
									if (i >= actionCount)
									{
										break;
									}
									int32_t btnW = loginTwoRow.buttonHalfWidth;
									int32_t ax = contentX + col * (loginTwoRow.buttonHalfWidth + gap);
									if (row == 1)
									{
										btnW = (col == 0) ? loginTwoRow.primarySubmitWidth : loginTwoRow.primaryQuitWidth;
										ax = (col == 0) ? contentX : (contentX + loginTwoRow.primarySubmitWidth + gap);
									}
									if (!contains(mx, my, ax, rowY, btnW, engine::render::kAuthUiActionButtonHeightPx))
									{
										continue;
									}
									actionHit = true;
									switch (m_phase)
									{
									case Phase::Login:
										if (i == 0) {
											SetPhase(Phase::Register);
											m_activeField = 0;
											m_userErrorText.clear();
											m_passwordConfirm.clear();
											// Plan C : réinitialiser état disponibilité username à l'entrée en Register.
											m_usernameCheckState = UsernameCheckState::Idle;
											m_usernameCheckSeq++;
											m_usernameDebounceTimer = 0.0;
											m_usernameLastChecked.clear();
										}
										else if (i == 1) { OpenLanguageOptions(); }
										else if (i == 2) { applyPrimaryAction(); }
										else if (i == 3) { window.RequestClose(); }
										break;
									case Phase::ShardPick:
										if (i == 0)
										{
											applyPrimaryAction();
										}
										else if (i == 1)
										{
											m_shardPickEntries.clear();
											m_shardPickChoiceShardId = 0;
											SetPhase(Phase::Login);
											m_userErrorText.clear();
										}
										break;
									default:
										break;
									}
									break;
								}
							}
						}
						if (!actionHit
							&& !(m_phase == Phase::Login && actionCount == 4
								&& (lay.loginMaquetteLayout
									|| engine::render::TryGetLoginTwoRowLayout(lay, vsLayout, model, loginTwoRow))))
						{
							constexpr int32_t kAuthErrorFooterBarH = 58;
							const int32_t buttonPadAfterBody = centeredLanguageSelection ? 28 : 20;
							int32_t buttonY = std::min(panelY + panelH - 86,
								bodyStartY + static_cast<int32_t>(model.visibleBodyLineCount) * bodyLinePitch + buttonPadAfterBody);
							if (m_phase == Phase::Error && lay.authErrorFooterTopFromPanelTopPx > 0)
							{
								buttonY = panelY + lay.authErrorFooterTopFromPanelTopPx
									+ (kAuthErrorFooterBarH - engine::render::kAuthUiActionButtonHeightPx) / 2;
							}
							const int32_t actionW = std::max(100, (contentW - (actionCount - 1) * gap) / actionCount);
							for (int32_t i = 0; i < actionCount; ++i)
							{
								const int32_t x = contentX + i * (actionW + gap);
								if (!contains(mx, my, x, buttonY, actionW, engine::render::kAuthUiActionButtonHeightPx))
								{
									continue;
								}

								switch (m_phase)
								{
								case Phase::Login:
									if (i == 0) {
										SetPhase(Phase::Register);
										m_activeField = 0;
										m_userErrorText.clear();
										m_passwordConfirm.clear();
										// Plan C : réinitialiser état disponibilité username à l'entrée en Register.
										m_usernameCheckState = UsernameCheckState::Idle;
										m_usernameCheckSeq++;
										m_usernameDebounceTimer = 0.0;
										m_usernameLastChecked.clear();
									}
									else if (i == 1) { OpenLanguageOptions(); }
									else if (i == 2) { applyPrimaryAction(); }
									else if (i == 3) { window.RequestClose(); }
									break;
								case Phase::Register:
								case Phase::VerifyEmail:
								case Phase::EmailConfirmationPending:
								case Phase::ForgotPassword:
									if (i == 0) applyPrimaryAction();
									else if (i == 1)
									{
										// Plan C : réinitialiser état disponibilité username à la sortie de Register.
										if (m_phase == Phase::Register)
										{
											m_usernameCheckState = UsernameCheckState::Idle;
											m_usernameCheckSeq++;
											m_usernameDebounceTimer = 0.0;
											m_usernameLastChecked.clear();
										}
										if (m_phase == Phase::EmailConfirmationPending)
										{
											m_registeredTagId.clear();
											m_userErrorText.clear();
											m_activeField = 0;
											SetPhase(Phase::Login);
										}
										else
										{
											SetPhase(Phase::Login);
											m_activeField = 0;
											m_userErrorText.clear();
										}
									}
									break;
								case Phase::ShardPick:
									if (i == 0)
									{
										applyPrimaryAction();
									}
									else if (i == 1)
									{
										m_shardPickEntries.clear();
										m_shardPickChoiceShardId = 0;
										SetPhase(Phase::Login);
										m_userErrorText.clear();
									}
									break;
								case Phase::LanguageSelectionFirstRun:
									if (i == 0)
									{
										applyPrimaryAction();
									}
									break;
								case Phase::CharacterCreate:
								case Phase::Submitting:
								case Phase::Error:
									if (i == 0) applyPrimaryAction();
									break;
								case Phase::LanguageOptions:
									if (i == 0)
									{
										applyPrimaryAction();
									}
									else if (i == 1)
									{
										if (m_optionsSubMenu != OptionsSubMenu::Root)
										{
											m_optionsSubMenu = OptionsSubMenu::Root;
											LOG_INFO(Core, "[AuthUiPresenter] Language options: back to root menu");
										}
										else
										{
											m_phase = m_phaseBeforeOptions;
											LOG_INFO(Core, "[AuthUiPresenter] Language options closed via 'return'");
										}
									}
									break;
								default:
									break;
								}
								break;
							}
						}
					}
				}
			}
			m_authPrevMouseLeftDown = input.IsMouseDown(engine::platform::MouseButton::Left);
		}

		std::string text;
		if (!usingNativeAuth && !authUiImguiMode)
		{
			input.ConsumePendingTextUtf8(text);
			if (!text.empty())
			{
				if (std::string* field = currentField())
				{
					const bool registerBirthCombo = m_phase == Phase::Register &&
						((m_activeField >= 6u && m_activeField <= 8u) || m_activeField == 9u);
					if (!registerBirthCombo)
					{
						for (unsigned char c : text)
						{
							// Tab / retours chariot : gérés par Key::Tab et ignorés ici (sinon Tab s’insère comme texte).
							if (c < 32)
								continue;
							const bool digitsOnlyField =
								(m_phase == Phase::Register && (m_activeField == 6 || m_activeField == 7 || m_activeField == 8)) ||
								(m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending);
							if (digitsOnlyField && (c < '0' || c > '9'))
								continue;
							const size_t maxLen =
								(m_phase == Phase::Register && (m_activeField == 6 || m_activeField == 7)) ? 2u :
								(m_phase == Phase::Register && m_activeField == 8) ? 4u :
								(m_phase == Phase::Register && m_activeField == 9) ? 2u :
								(m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending) ? 6u :
								(m_phase == Phase::CharacterCreate) ? 32u : 256u;
							if (field->size() >= maxLen)
								continue;
							field->push_back(static_cast<char>(c));
						}
					}
					if (m_phase == Phase::Login && m_rememberLogin && m_activeField == 0 && !text.empty())
					{
						PersistRememberedLoginSidecar(m_login);
					}
				}
			}
		}

	if (!usingNativeAuth && !authUiImguiMode && input.WasPressed(engine::platform::Key::Backspace))
		{
			auto popLast = [](std::string& s) {
				while (!s.empty())
				{
					const unsigned char back = static_cast<unsigned char>(s.back());
					s.pop_back();
					if ((back & 0xC0u) != 0x80u)
						break;
				}
			};
			if (std::string* field = currentField())
			{
				if (m_phase == Phase::Register && (
					(m_activeField >= 6u && m_activeField <= 8u) || m_activeField == 9u))
				{
					if (m_activeField == 6)
					{
						AdjustBirthCycle(m_birthDay, -1, 1, 31);
					}
					else if (m_activeField == 7)
					{
						AdjustBirthCycle(m_birthMonth, -1, 1, 12);
					}
					else if (m_activeField == 8)
					{
						AdjustBirthCycle(m_birthYear, -1, 1900, 2100);
					}
					else if (m_activeField == 9)
						AdjustCountryCycle(m_country, -1);
				}
				else
				{
					popLast(*field);
				}
			}
			if (m_phase == Phase::Login && m_rememberLogin && m_activeField == 0)
			{
				PersistRememberedLoginSidecar(m_login);
			}
		}

		// Plan C : réinitialiser debounce disponibilité username après toute frappe sur le champ login en inscription.
		if (m_phase == Phase::Register && m_activeField == 0)
		{
			m_usernameCheckState = UsernameCheckState::Idle;
			m_usernameCheckSeq++;
			m_usernameDebounceTimer = (m_login.size() >= 3) ? 0.8 : 0.0;
		}

	if (!usingNativeAuth && !authUiImguiMode && input.WasPressed(engine::platform::Key::Tab))
		{
			if (m_phase == Phase::Login)
				m_activeField = (m_activeField + 1u) % 3u;
			else if (m_phase == Phase::Register)
				m_activeField = (m_activeField + 1u) % 10u;
			else if (m_phase == Phase::ForgotPassword)
				m_activeField = 0;
			else
				m_activeField = 0;
			LOG_DEBUG(Core, "[AuthUiPresenter] Focus field={}", m_activeField);
		}

		if (!usingNativeAuth && !authUiImguiMode && m_phase == Phase::Login && input.WasPressed(engine::platform::Key::Space) && m_activeField == 2u)
		{
			m_rememberLogin = !m_rememberLogin;
			SaveRememberPreference();
			if (m_rememberLogin)
			{
				PersistRememberedLoginSidecar(m_login);
			}
			else
			{
				ClearRememberedLoginSidecar();
			}
		}

		Update_Terms(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_CharacterCreate(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_LanguageSelect(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_ShardPick(input, cfg, window, usingNativeAuth, authUiImguiMode);

		Update_LoginShortcuts(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_Register(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_ForgotPassword(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_VerifyEmail(input, cfg, window, usingNativeAuth, authUiImguiMode);
		Update_Options(input, cfg, window, usingNativeAuth, authUiImguiMode);

		if ((!usingNativeAuth && !authUiImguiMode && input.WasPressed(engine::platform::Key::Enter))
			|| (usingNativeAuth && m_phase == Phase::Error))
		{
			if (!usingNativeAuth && !authUiImguiMode && m_phase == Phase::LanguageOptions && m_optionsSubMenu == OptionsSubMenu::Root)
			{
				EnterOptionsSubmenuFromRoot(m_optionsRootSelection);
			}
			else
			{
				applyPrimaryAction();
			}
		}

		UpdateWindowTitle(window);
	}

	void AuthUiPresenter::ResolveActionButtonLabels(RenderModel& model) const
	{
		for (auto& a : model.actions)
		{
			if (a.labelKey.empty())
			{
				continue;
			}
			a.label = Tr(a.labelKey);
			if (a.label.empty() && !a.labelKeyFallback.empty())
			{
				a.label = Tr(a.labelKeyFallback);
			}
			if (a.label.empty())
			{
				a.label = a.labelKey;
			}
		}
	}

	AuthUiPresenter::RenderModel AuthUiPresenter::BuildRenderModel() const
	{
		RenderModel model{};
		model.layoutAuthTitleLine1FromPanelTopPx = m_layoutAuthTitleLine1FromPanelTopPx;
		model.layoutAuthGapTitleToSectionPx = m_layoutAuthGapTitleToSectionPx;
		model.layoutAuthTitleCenterViewportWidth = m_layoutAuthTitleCenterViewportWidth;
		model.layoutAuthFieldRowExtraPx = m_layoutAuthFieldRowExtraPx;
		model.visible = m_initialized && !m_flowComplete && m_authEnabled;
		model.hoveredFieldInfoIndex = m_hoveredFieldInfoIndex;
		model.titleLine1 = Tr("auth.title_line1");
		model.titleLine2 = Tr("auth.title_line2");
		// Phase::Error : on n'affiche pas la bannière (elle serait superposée au cadre d'erreur).
		// SetPhase(Phase::Error) efface m_infoBanner, mais cette garde défensive empêche tout
		// chevauchement résiduel si un chemin de code atypique ne passe pas par SetPhase.
		model.infoBanner = (m_phase == Phase::Error) ? std::string{} : m_infoBanner;
		model.errorText = (m_phase == Phase::Error) ? m_userErrorText : std::string{};
		model.footerHint.clear();

		auto addField = [this, &model](std::string label, std::string value, bool active, bool secret = false, bool cyclePicker = false,
			std::string tooltipKey = {}, std::string tooltipTextDirect = {}, std::string inputPlaceholder = {})
		{
			RenderField field{};
			field.label = std::move(label);
			field.value = std::move(value);
			field.active = active;
			field.hovered = static_cast<int32_t>(model.fields.size()) == m_hoveredFieldIndex;
			field.secret = secret;
			field.cyclePicker = cyclePicker;
			field.tooltipKey = std::move(tooltipKey);
			if (!tooltipTextDirect.empty())
			{
				field.tooltipText = std::move(tooltipTextDirect);
			}
			field.inputPlaceholder = std::move(inputPlaceholder);
			model.fields.push_back(std::move(field));
		};
		auto addActionKeys = [this, &model](std::string_view labelKey, bool primary, bool active = true, bool emphasized = false,
			std::string_view labelKeyFallback = {})
		{
			RenderAction action{};
			action.labelKey = std::string(labelKey);
			if (!labelKeyFallback.empty())
			{
				action.labelKeyFallback = std::string(labelKeyFallback);
			}
			action.primary = primary;
			action.active = active;
			action.emphasized = emphasized;
			action.hovered = static_cast<int32_t>(model.actions.size()) == m_hoveredActionIndex;
			model.actions.push_back(std::move(action));
		};
		auto addBodyLine = [this, &model](std::string text, bool active = false, bool link = false)
		{
			RenderBodyLine line{};
			line.text = std::move(text);
			line.active = active;
			line.link = link;
			line.hovered = static_cast<int32_t>(model.bodyLines.size()) == m_hoveredBodyLineIndex;
			model.bodyLines.push_back(std::move(line));
		};
		auto addBodyLinesFromNewlines = [&addBodyLine](const std::string& text)
		{
			if (text.empty())
			{
				return;
			}
			size_t start = 0;
			for (;;)
			{
				const size_t nl = text.find('\n', start);
				if (nl == std::string::npos)
				{
					const std::string chunk = text.substr(start);
					if (!chunk.empty())
					{
						addBodyLine(chunk);
					}
					break;
				}
				const std::string chunk = text.substr(start, nl - start);
				if (!chunk.empty())
				{
					addBodyLine(chunk);
				}
				start = nl + 1u;
			}
		};
		auto maskedPassword = [this]() -> std::string
		{
			std::string out;
			AppendPasswordStars(out, m_password.size());
			return out;
		};

		switch (m_phase)
		{
		case Phase::Login:
			BuildModel_Login(model);
			break;
		case Phase::Register:
			BuildModel_Register(model);
			break;
		case Phase::VerifyEmail:
			BuildModel_VerifyEmail(model);
			break;
		case Phase::EmailConfirmationPending:
			BuildModel_EmailConfirmationPending(model);
			break;
		case Phase::ForgotPassword:
			BuildModel_ForgotPassword(model);
			break;
		case Phase::Terms:
			BuildModel_Terms(model);
			break;
		case Phase::CharacterCreate:
			BuildModel_CharacterCreate(model);
			break;
		case Phase::ShardPick:
			BuildModel_ShardPick(model);
			break;
		case Phase::LanguageSelectionFirstRun:
		{
			BuildModel_LanguageSelect(model);
			addActionKeys("common.continue", true, true, false);
			break;
		}
		case Phase::LanguageOptions:
			BuildModel_Options(model);
			break;
		case Phase::Submitting:
			// Un seul libellé : sectionTitle (évite doublon avec l’ancienne bodyLine dessinée sur les barres).
			model.sectionTitle = Tr("auth.panel.submitting");
			break;
		case Phase::Error:
			BuildModel_Error(model);
			break;
		}

		for (RenderField& f : model.fields)
		{
			if (!f.tooltipKey.empty())
			{
				f.tooltipText = Tr(f.tooltipKey);
			}
		}

		ResolveActionButtonLabels(model);

		if (m_phase == Phase::Login && model.actions.size() == 4u)
		{
			model.actions[0].actionBadge = Tr("auth.badge.ctrl_r");
			model.actions[2].actionBadge = Tr("auth.badge.submit");
		}
		if (m_phase == Phase::Register && model.actions.size() >= 2u)
		{
			model.actions[0].actionBadge = Tr("auth.badge.submit_enter");
			model.actions[1].actionBadge = Tr("auth.badge.esc_back");
		}
		if ((m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending) && model.actions.size() >= 2u)
		{
			model.actions[0].actionBadge = Tr("auth.badge.submit_enter");
			model.actions[1].actionBadge = Tr("auth.badge.esc_back");
		}

		if (!model.bodyLines.empty())
		{
			const int32_t total = static_cast<int32_t>(model.bodyLines.size());
			const int32_t maxVisible =
				(m_phase == Phase::LanguageOptions || m_phase == Phase::ShardPick) ? 10 : 6;
			int32_t focusIndex = 0;
			for (int32_t i = 0; i < total; ++i)
			{
				if (model.bodyLines[static_cast<size_t>(i)].hovered || model.bodyLines[static_cast<size_t>(i)].active)
				{
					focusIndex = i;
					break;
				}
			}
			int32_t start = std::max(0, focusIndex - (maxVisible / 2));
			if (start + maxVisible > total)
			{
				start = std::max(0, total - maxVisible);
			}
			model.visibleBodyLineStart = start;
			model.visibleBodyLineCount = std::min(maxVisible, total - start);
		}

		model.authLogoSizePx = m_authLogoSizePx;

		// Popup info — propagé depuis l'état.
		model.infoPopupVisible = m_infoPopupVisible;
		if (!m_infoPopupText.empty())
		{
			model.infoPopupText = m_infoPopupText;
		}
		else
		{
			// Fallback : texte générique selon la phase (utilisé quand le popup n'est pas ouvert sur un champ spécifique).
			if (m_phase == Phase::Login || m_phase == Phase::ForgotPassword)
				model.infoPopupText = Tr("auth.info.login_help");
			else if (m_phase == Phase::Register)
				model.infoPopupText = Tr("auth.info.register_help");
			else if (m_phase == Phase::ShardPick)
				model.infoPopupText = Tr("auth.shard_pick.popup_help");
			else if (m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
				model.infoPopupText = Tr("auth.verify.info_popup");
			else if (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout)
				model.infoPopupText = Tr("language.first_run.info_popup");
		}

		// Icône "i" visible sur Login et Register.
		model.infoIconVisible = (m_phase == Phase::Login || m_phase == Phase::Register || m_phase == Phase::ShardPick
			|| m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending
			|| (m_phase == Phase::LanguageSelectionFirstRun && model.languageFirstRunLayout));

		if (model.languageFirstRunLayout && m_viewportW > 0u && m_viewportH > 0u)
		{
			const VkExtent2D extPatch{ m_viewportW, m_viewportH };
			const VisualState vsPatch = GetVisualState();
			const engine::render::AuthUiLayoutMetrics layPatch =
				engine::render::BuildAuthUiLayoutMetrics(extPatch, vsPatch, model);
			model.infoIconX = layPatch.languageInfoIconX;
			model.infoIconY = layPatch.languageInfoIconY;
			model.infoIconW = layPatch.languageInfoIconW;
			model.infoIconH = layPatch.languageInfoIconH;
		}

		return model;
	}

	std::string AuthUiPresenter::BuildPanelText() const
	{
		const RenderModel model = BuildRenderModel();
		if (!model.visible)
		{
			return {};
		}

		std::string s;
		s += model.titleLine1;
		s += "\n";
		s += model.titleLine2;
		s += "\n\n";
		if (!model.infoBanner.empty())
		{
			s += Tr("common.information");
			s += "\n";
			s += model.infoBanner;
			s += "\n\n";
		}
		if (!model.sectionTitle.empty())
		{
			s += model.sectionTitle;
			s += "\n\n";
		}
		for (const RenderField& field : model.fields)
		{
			s += field.label;
			s += "\n";
			s += field.value;
			s += field.active ? "|\n" : "\n";
			s += "\n";
		}
		for (int32_t i = 0; i < model.visibleBodyLineCount; ++i)
		{
			const RenderBodyLine& line = model.bodyLines[static_cast<size_t>(model.visibleBodyLineStart + i)];
			if (line.text.empty() && line.valueText.empty())
				continue;
			s += line.text;
			if (!line.valueText.empty())
			{
				s += "\t";
				s += line.valueText;
			}
			s += "\n";
		}
		if (!model.bodyLines.empty())
		{
			s += "\n";
		}
		if (!model.errorText.empty())
		{
			s += "\n\n";
			s += model.errorText;
			s += "\n";
		}
		for (const RenderAction& action : model.actions)
		{
			s += action.primary ? "[*] " : "[ ] ";
			s += action.label;
			s += "\n";
		}
		s += "\n";
		s += model.footerHint;
		s += "\n";
		return s;
	}

	AuthUiPresenter::VisualState AuthUiPresenter::GetVisualState() const
	{
		VisualState state{};
		state.active = m_initialized && !m_flowComplete && m_authEnabled;
		state.login = m_phase == Phase::Login;
		state.registerMode = m_phase == Phase::Register;
		state.verifyEmail = m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending;
		state.forgotPassword = m_phase == Phase::ForgotPassword;
		state.terms = m_phase == Phase::Terms;
		state.characterCreate = m_phase == Phase::CharacterCreate;
		state.languageSelection = m_phase == Phase::LanguageSelectionFirstRun;
		state.languageOptions = m_phase == Phase::LanguageOptions;
		state.options = m_phase == Phase::LanguageOptions;
		state.shardPick = m_phase == Phase::ShardPick;
		state.emailConfirmationPending = m_phase == Phase::EmailConfirmationPending;
		state.submitting = m_phase == Phase::Submitting;
		state.error = m_phase == Phase::Error;
		state.minimalChrome = m_authMinimalChrome;
		state.loginArtColumn = m_authLoginArtColumn;
		state.authLogoSpin = m_authAvailabilityChecking;
		state.authStatusKnown = m_statusProbeCompletedOnce;
		state.authStatusOk = m_statusCache.authOk;
		return state;
	}


	bool AuthUiPresenter::OnEscape()
	{
		if (m_phase == Phase::Submitting)
			return true;
		if (m_phase == Phase::LanguageSelectionFirstRun)
		{
			return true;
		}
		if (m_phase == Phase::LanguageOptions)
		{
			if (m_optionsSubMenu != OptionsSubMenu::Root)
			{
				m_optionsSubMenu = OptionsSubMenu::Root;
				LOG_INFO(Core, "[AuthUiPresenter] Escape: Options -> root menu");
				return true;
			}
			m_phase = m_phaseBeforeOptions;
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Language options closed");
			return true;
		}
		if (m_phase == Phase::Register || m_phase == Phase::ForgotPassword || m_phase == Phase::VerifyEmail || m_phase == Phase::EmailConfirmationPending)
		{
			if (m_phase == Phase::Register)
			{
				// Plan C : réinitialiser état disponibilité username à la sortie de Register.
				m_usernameCheckState = UsernameCheckState::Idle;
				m_usernameCheckSeq++;
				m_usernameDebounceTimer = 0.0;
				m_usernameLastChecked.clear();
			}
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			m_passwordConfirm.clear();
			m_registeredTagId.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Auth sub-screen -> Login");
			return true;
		}
		if (m_phase == Phase::Terms)
		{
			return true;
		}
		if (m_phase == Phase::CharacterCreate)
		{
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			m_infoBanner = Tr("auth.info.character_cancelled");
			ResetMasterSession();
			return true;
		}
		if (m_phase == Phase::ShardPick)
		{
			m_shardPickEntries.clear();
			m_shardPickChoiceShardId = 0;
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: ShardPick -> Login");
			return true;
		}
		if (m_phase == Phase::Login)
		{
			// Sur la page connexion, Escape ne doit pas fermer le jeu :
			// la sortie se fait uniquement via le bouton "Quitter".
			return true;
		}
		if (m_phase == Phase::Error)
		{
			SetPhase(Phase::Login);
			m_userErrorText.clear();
			LOG_INFO(Core, "[AuthUiPresenter] Escape: Error -> Login");
			return true;
		}
		return false;
	}

	void AuthUiPresenter::BypassAuthGateForWorldEditor()
	{
		if (!m_initialized)
		{
			return;
		}
		JoinWorker();
		m_flowComplete = true;
		m_userErrorText.clear();
		m_infoBanner.clear();
		LOG_INFO(Core, "[AuthUiPresenter] World Editor : flux auth marqué complet (pas d’écran login)");
	}

}
