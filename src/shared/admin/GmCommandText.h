#pragma once
// ADM.0 — Outils texte des commandes de jeu (« /tp », « /speed », « /help »…).
//
// Module PUR, en-tête seul (aucune dépendance hors bibliothèque standard) :
// partagé par le master (ChatCommandRouter, aide) et par le shard (exécution
// des commandes MJ reçues du master). Inclus tel quel par la cible Windows
// server_app (qui ne linke pas engine_core) : ne jamais y ajouter de .cpp.
//
// Conventions :
//   - les noms de commandes sont en anglais, comparés sans casse (ASCII) ;
//   - les nombres acceptent le point ou la virgule décimale (« 1,5 » = 1.5) ;
//   - un argument entre guillemets peut contenir des espaces (« "lac de Marn" »).
//
// Tests : src/shared/admin/tests/GmCommandTextTests.cpp (gm_command_text_tests).

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::admin
{
	/// Minuscules ASCII (les octets UTF-8 non ASCII sont laissés tels quels).
	inline std::string ToLowerAscii(std::string_view text)
	{
		std::string out(text);
		for (char& c : out)
		{
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		return out;
	}

	/// Égalité sans casse ASCII.
	inline bool EqualsIgnoreCase(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			char x = a[i];
			char y = b[i];
			if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
			if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
			if (x != y)
				return false;
		}
		return true;
	}

	/// \p text contient-il \p needle (sans casse ASCII) ? Une aiguille vide est toujours trouvée.
	inline bool ContainsIgnoreCase(std::string_view text, std::string_view needle)
	{
		if (needle.empty())
			return true;
		if (needle.size() > text.size())
			return false;
		for (size_t i = 0; i + needle.size() <= text.size(); ++i)
		{
			if (EqualsIgnoreCase(text.substr(i, needle.size()), needle))
				return true;
		}
		return false;
	}

	/// Retire les espaces (et tabulations) de début et de fin.
	inline std::string_view Trim(std::string_view text)
	{
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
			text.remove_prefix(1);
		while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
			text.remove_suffix(1);
		return text;
	}

	/// Découpe une ligne d'arguments sur les espaces. Un segment entre guillemets
	/// doubles forme un seul argument (guillemets retirés) ; un guillemet non fermé
	/// court jusqu'à la fin de la ligne. Les arguments vides ("") sont conservés.
	inline std::vector<std::string> SplitArgs(std::string_view text)
	{
		std::vector<std::string> out;
		size_t i = 0;
		while (i < text.size())
		{
			while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
				++i;
			if (i >= text.size())
				break;
			std::string token;
			if (text[i] == '"')
			{
				++i;
				while (i < text.size() && text[i] != '"')
					token.push_back(text[i++]);
				if (i < text.size())
					++i; // guillemet fermant
			}
			else
			{
				while (i < text.size() && text[i] != ' ' && text[i] != '\t')
					token.push_back(text[i++]);
			}
			out.push_back(std::move(token));
		}
		return out;
	}

	/// Recolle les arguments \p from.. séparés par un espace (texte libre : motif,
	/// message d'annonce, nom de lieu en plusieurs mots).
	inline std::string JoinArgs(const std::vector<std::string>& args, size_t from)
	{
		std::string out;
		for (size_t i = from; i < args.size(); ++i)
		{
			if (!out.empty())
				out.push_back(' ');
			out += args[i];
		}
		return out;
	}

	/// Réel fini ; accepte « 12 », « -3.5 », « 1,25 », « +4 ». nullopt sinon.
	inline std::optional<double> ParseDouble(std::string_view text)
	{
		text = Trim(text);
		if (text.empty() || text.size() > 64)
			return std::nullopt;
		std::string buf(text);
		for (char& c : buf)
		{
			if (c == ',')
				c = '.';
		}
		if (buf.front() == '+')
			buf.erase(buf.begin());
		if (buf.empty())
			return std::nullopt;
		// Uniquement chiffres, un point, un signe en tête, un exposant éventuel.
		for (size_t i = 0; i < buf.size(); ++i)
		{
			const char c = buf[i];
			const bool ok = (c >= '0' && c <= '9') || c == '.' || ((c == '-' || c == '+') && (i == 0 || buf[i - 1] == 'e' || buf[i - 1] == 'E'))
				|| c == 'e' || c == 'E';
			if (!ok)
				return std::nullopt;
		}
		char* end = nullptr;
		const double v = std::strtod(buf.c_str(), &end);
		if (end == buf.c_str() || *end != '\0' || !std::isfinite(v))
			return std::nullopt;
		return v;
	}

	/// Entier signé 64 bits en base 10 (« 42 », « -7 », « +3 »). nullopt sinon.
	inline std::optional<int64_t> ParseInt(std::string_view text)
	{
		text = Trim(text);
		if (!text.empty() && text.front() == '+')
			text.remove_prefix(1);
		if (text.empty())
			return std::nullopt;
		int64_t v = 0;
		const auto r = std::from_chars(text.data(), text.data() + text.size(), v, 10);
		if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
			return std::nullopt;
		return v;
	}

	/// Interrupteur : on/off, 1/0, true/false, yes/no, oui/non (sans casse).
	inline std::optional<bool> ParseOnOff(std::string_view text)
	{
		const std::string t = ToLowerAscii(Trim(text));
		if (t == "on" || t == "1" || t == "true" || t == "yes" || t == "oui")
			return true;
		if (t == "off" || t == "0" || t == "false" || t == "no" || t == "non")
			return false;
		return std::nullopt;
	}

	/// Durée en secondes : « 30s », « 10m », « 2h », « 3d » (ou « 3j »), nombre seul
	/// = minutes (convention des sanctions /mute, /ban). Strictement positive, au plus
	/// 10 ans. nullopt sinon.
	inline std::optional<uint64_t> ParseDurationSeconds(std::string_view text)
	{
		text = Trim(text);
		if (text.empty())
			return std::nullopt;
		uint64_t unit = 60u;
		const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
		if (last == 's') { unit = 1u; text.remove_suffix(1); }
		else if (last == 'm') { unit = 60u; text.remove_suffix(1); }
		else if (last == 'h') { unit = 3600u; text.remove_suffix(1); }
		else if (last == 'd' || last == 'j') { unit = 86400u; text.remove_suffix(1); }
		const auto n = ParseInt(text);
		if (!n || *n <= 0)
			return std::nullopt;
		const uint64_t seconds = static_cast<uint64_t>(*n) * unit;
		if (seconds > 10ull * 365ull * 86400ull)
			return std::nullopt;
		return seconds;
	}

	/// Durée lisible en français : « 2 h 05 min », « 45 s », « 3 j 4 h ».
	inline std::string FormatDurationFr(uint64_t seconds)
	{
		const uint64_t d = seconds / 86400u;
		const uint64_t h = (seconds / 3600u) % 24u;
		const uint64_t m = (seconds / 60u) % 60u;
		const uint64_t s = seconds % 60u;
		char buf[64];
		if (d > 0u)
			std::snprintf(buf, sizeof(buf), "%llu j %llu h", static_cast<unsigned long long>(d), static_cast<unsigned long long>(h));
		else if (h > 0u)
			std::snprintf(buf, sizeof(buf), "%llu h %02llu min", static_cast<unsigned long long>(h), static_cast<unsigned long long>(m));
		else if (m > 0u)
			std::snprintf(buf, sizeof(buf), "%llu min %02llu s", static_cast<unsigned long long>(m), static_cast<unsigned long long>(s));
		else
			std::snprintf(buf, sizeof(buf), "%llu s", static_cast<unsigned long long>(s));
		return buf;
	}

	/// Réel formaté avec \p decimals décimales (point décimal), ex. « 1250.4 ».
	inline std::string FormatFixed(double value, int decimals)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.*f", decimals < 0 ? 0 : (decimals > 6 ? 6 : decimals), value);
		return buf;
	}

	/// Nom de commande normalisé : minuscules, sans « / » de tête, coupé au premier
	/// espace (« /TP 10 20 » → « tp »).
	inline std::string NormalizeCommandName(std::string_view text)
	{
		text = Trim(text);
		while (!text.empty() && text.front() == '/')
			text.remove_prefix(1);
		const size_t space = text.find_first_of(" \t");
		if (space != std::string_view::npos)
			text = text.substr(0, space);
		return ToLowerAscii(text);
	}

	/// Découpe \p text en lignes d'au plus \p maxBytes octets (limite d'une ligne de
	/// chat : 256 octets), de préférence sur un espace, sans couper un caractère UTF-8.
	/// Les lignes de continuation commencent par deux espaces. Texte vide : une ligne vide.
	inline std::vector<std::string> WrapUtf8(std::string_view text, size_t maxBytes)
	{
		std::vector<std::string> out;
		if (maxBytes < 16u)
			maxBytes = 16u;
		bool first = true;
		while (true)
		{
			const size_t budget = first ? maxBytes : maxBytes - 2u;
			if (text.size() <= budget)
			{
				out.push_back((first ? std::string() : std::string("  ")) + std::string(text));
				break;
			}
			size_t cut = budget;
			while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u)
				--cut;
			const size_t space = text.substr(0, cut).find_last_of(' ');
			if (space != std::string_view::npos && space > budget / 2u)
				cut = space;
			out.push_back((first ? std::string() : std::string("  ")) + std::string(text.substr(0, cut)));
			text.remove_prefix(cut);
			while (!text.empty() && text.front() == ' ')
				text.remove_prefix(1);
			if (text.empty())
				break;
			first = false;
		}
		return out;
	}

	/// Tronque \p text à \p maxBytes octets sans couper un caractère UTF-8 au milieu.
	inline std::string TruncateUtf8(std::string_view text, size_t maxBytes)
	{
		if (text.size() <= maxBytes)
			return std::string(text);
		size_t cut = maxBytes;
		while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u)
			--cut;
		return std::string(text.substr(0, cut));
	}
}
