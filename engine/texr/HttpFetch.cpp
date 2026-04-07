#include "engine/texr/HttpFetch.h"

#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace lcdlln::manifest {
namespace {

bool FetchFileUrl(std::string_view url, std::vector<std::uint8_t>& out, std::string& err)
{
	out.clear();
	err.clear();
	constexpr std::string_view prefix = "file://";
	if (url.size() < prefix.size() || url.compare(0, prefix.size(), prefix) != 0)
	{
		err = "internal: not a file URL";
		return false;
	}
	std::string path_utf8(url.substr(prefix.size()));
	// file:///C:/path → /C:/path → C:/path
	if (path_utf8.size() >= 3 && path_utf8[0] == '/' && path_utf8[2] == ':'
	    && ((path_utf8[1] >= 'A' && path_utf8[1] <= 'Z') || (path_utf8[1] >= 'a' && path_utf8[1] <= 'z')))
	{
		path_utf8.erase(0, 1);
	}
	std::filesystem::path p(path_utf8);
	std::ifstream f(p, std::ios::binary | std::ios::ate);
	if (!f)
	{
		err = "file:// open failed: " + path_utf8;
		return false;
	}
	const auto sz = static_cast<std::size_t>(f.tellg());
	f.seekg(0);
	out.resize(sz);
	if (sz > 0 && !f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz)))
	{
		err = "file:// read failed";
		out.clear();
		return false;
	}
	return true;
}

#if defined(_WIN32)

std::wstring Utf8ToWide(std::string_view utf8)
{
	if (utf8.empty())
	{
		return {};
	}
	const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (n <= 0)
	{
		return {};
	}
	std::wstring w(static_cast<std::size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
	return w;
}

bool FetchHttpsWin32(std::string_view url, std::vector<std::uint8_t>& out, std::string& err)
{
	out.clear();
	err.clear();
	if (url.size() < 8 || url.compare(0, 8, "https://") != 0)
	{
		err = "HTTPS URL must start with https://";
		return false;
	}
	const std::wstring wurl = Utf8ToWide(url);
	if (wurl.empty())
	{
		err = "URL UTF-8 conversion failed";
		return false;
	}

	URL_COMPONENTS uc{};
	uc.dwStructSize = sizeof(uc);
	wchar_t host[256]{};
	wchar_t urlpath[2048]{};
	uc.lpszHostName = host;
	uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
	uc.lpszUrlPath = urlpath;
	uc.dwUrlPathLength = static_cast<DWORD>(std::size(urlpath));
	if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc))
	{
		err = "WinHttpCrackUrl failed";
		return false;
	}
	if (uc.nScheme != INTERNET_SCHEME_HTTPS)
	{
		err = "only https scheme supported";
		return false;
	}

	HINTERNET hSession =
	    WinHttpOpen(L"LCDLLN-manifest/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
	                WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
	{
		err = "WinHttpOpen failed";
		return false;
	}
	HINTERNET hConnect =
	    WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0);
	if (!hConnect)
	{
		WinHttpCloseHandle(hSession);
		err = "WinHttpConnect failed";
		return false;
	}
	DWORD flags = WINHTTP_FLAG_REFRESH | WINHTTP_FLAG_SECURE;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", uc.lpszUrlPath, nullptr, WINHTTP_NO_REFERER,
	                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		err = "WinHttpOpenRequest failed";
		return false;
	}

	const BOOL okSend = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	if (!okSend || !WinHttpReceiveResponse(hRequest, nullptr))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		err = "WinHttpSendRequest/ReceiveResponse failed";
		return false;
	}

	DWORD status = 0;
	DWORD sz = sizeof(status);
	if (!WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
	                         &status, &sz, WINHTTP_NO_HEADER_INDEX)
	    || status != 200)
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		err = "HTTP status != 200";
		return false;
	}

	for (;;)
	{
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail))
		{
			break;
		}
		if (avail == 0)
		{
			break;
		}
		const std::size_t old = out.size();
		out.resize(old + avail);
		DWORD read = 0;
		if (!WinHttpReadData(hRequest, out.data() + old, avail, &read) || read == 0)
		{
			out.resize(old);
			break;
		}
		if (read < avail)
		{
			out.resize(old + read);
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return true;
}

#endif

}  // namespace

bool FetchUrlBytes(std::string_view url, std::vector<std::uint8_t>& out, std::string& err)
{
	if (url.size() >= 7 && url.compare(0, 7, "file://") == 0)
	{
		return FetchFileUrl(url, out, err);
	}
#if defined(_WIN32)
	if (url.size() >= 8 && url.compare(0, 8, "https://") == 0)
	{
		return FetchHttpsWin32(url, out, err);
	}
#endif
	err = "unsupported URL (need https:// or file:// on this platform)";
	return false;
}

}  // namespace lcdlln::manifest
