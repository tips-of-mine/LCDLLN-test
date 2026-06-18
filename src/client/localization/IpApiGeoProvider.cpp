#include "src/client/localization/IpApiGeoProvider.h"

#include "src/shared/core/Log.h"

#include <string>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <winhttp.h>
#endif

namespace engine::client
{
	namespace
	{
		/// Extrait la valeur de "countryCode" d'un corps JSON ip-api, sans parseur
		/// complet : cherche la clé, le ':' puis la chaîne entre guillemets.
		/// Renvoie "" si absente. Robuste aux espaces.
		std::string ExtractCountryCode(const std::string& body)
		{
			const std::string key = "\"countryCode\"";
			size_t pos = body.find(key);
			if (pos == std::string::npos)
				return {};
			pos += key.size();
			while (pos < body.size() && body[pos] != ':')
				++pos;
			if (pos >= body.size())
				return {};
			++pos; // saute ':'
			while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t'))
				++pos;
			if (pos >= body.size() || body[pos] != '"')
				return {};
			++pos;
			std::string out;
			while (pos < body.size() && body[pos] != '"')
				out.push_back(body[pos++]);
			return out;
		}
	}

#if defined(_WIN32)
	std::string IpApiGeoProvider::FetchCountryCode()
	{
		std::string result;
		// DEFAULT_PROXY (et non AUTOMATIC_PROXY/WPAD) : aligné sur l'usage WinHTTP
		// existant du client (AuthUiPresenterCore) et plus robuste pour un appel
		// best-effort court — WPAD peut ajouter de la latence ou échouer sans wpad.
		HINTERNET session = WinHttpOpen(L"LCDLLN-GeoIP/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
		{
			LOG_WARN(Core, "[IpApiGeoProvider] WinHttpOpen échoué -> géoloc ignorée");
			return result;
		}
		WinHttpSetTimeouts(session, static_cast<int>(m_timeoutMs), static_cast<int>(m_timeoutMs),
			static_cast<int>(m_timeoutMs), static_cast<int>(m_timeoutMs));

		HINTERNET connect = WinHttpConnect(session, L"ip-api.com", INTERNET_DEFAULT_HTTP_PORT, 0);
		HINTERNET request = nullptr;
		if (connect)
		{
			request = WinHttpOpenRequest(connect, L"GET", L"/json/",
				nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		}

		bool ok = false;
		if (request)
		{
			ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
			if (ok)
				ok = WinHttpReceiveResponse(request, nullptr) != FALSE;
		}

		if (ok)
		{
			std::string body;
			DWORD available = 0;
			while (WinHttpQueryDataAvailable(request, &available) && available > 0)
			{
				std::string chunk(available, '\0');
				DWORD read = 0;
				if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
					break;
				chunk.resize(read);
				body += chunk;
			}
			result = ExtractCountryCode(body);
			LOG_INFO(Core, "[IpApiGeoProvider] countryCode='{}' (body {} octets)", result, body.size());
		}
		else
		{
			LOG_WARN(Core, "[IpApiGeoProvider] requête échouée -> géoloc ignorée");
		}

		if (request) WinHttpCloseHandle(request);
		if (connect) WinHttpCloseHandle(connect);
		if (session) WinHttpCloseHandle(session);
		return result;
	}
#else
	std::string IpApiGeoProvider::FetchCountryCode()
	{
		// Pas de client UI ni de géoloc hors Windows (cohérent avec l'auth UI).
		return {};
	}
#endif
}
