// CommandPaletteModel — implémentation. Voir CommandPaletteModel.h.

#include "src/world_editor/ui/CommandPaletteModel.h"

#include <algorithm>
#include <cctype>

namespace engine::editor::world::palette
{
	namespace
	{
		/// Table de translittération UTF-8 → ASCII des accents français
		/// courants (les libellés d'actions sont en UTF-8). Chaque entrée
		/// mappe une séquence multi-octets vers son équivalent non accentué.
		struct AccentMap { const char* utf8; const char* ascii; };
		constexpr AccentMap kAccents[] = {
			{ "à", "a" }, { "â", "a" }, { "ä", "a" },
			{ "é", "e" }, { "è", "e" }, { "ê", "e" }, { "ë", "e" },
			{ "î", "i" }, { "ï", "i" },
			{ "ô", "o" }, { "ö", "o" },
			{ "ù", "u" }, { "û", "u" }, { "ü", "u" },
			{ "ç", "c" }, { "œ", "oe" },
			{ "À", "a" }, { "Â", "a" }, { "Ä", "a" },
			{ "É", "e" }, { "È", "e" }, { "Ê", "e" }, { "Ë", "e" },
			{ "Î", "i" }, { "Ï", "i" },
			{ "Ô", "o" }, { "Ö", "o" },
			{ "Ù", "u" }, { "Û", "u" }, { "Ü", "u" },
			{ "Ç", "c" }, { "Œ", "oe" },
		};

		/// Si `text` à la position `pos` commence par une séquence accentuée
		/// connue, ajoute l'équivalent ASCII à `out` et retourne la longueur
		/// consommée ; sinon retourne 0.
		size_t TryTransliterate(std::string_view text, size_t pos, std::string& out)
		{
			for (const AccentMap& m : kAccents)
			{
				const std::string_view seq{ m.utf8 };
				if (text.size() - pos >= seq.size()
					&& text.compare(pos, seq.size(), seq) == 0)
				{
					out += m.ascii;
					return seq.size();
				}
			}
			return 0;
		}

		/// True si `word` commence à un début de mot de `text` (position 0 ou
		/// précédée d'un espace/ponctuation). `text` et `word` déjà normalisés.
		bool MatchesWordPrefix(const std::string& text, const std::string& word)
		{
			size_t pos = text.find(word);
			while (pos != std::string::npos)
			{
				if (pos == 0) return true;
				const char prev = text[pos - 1];
				if (prev == ' ' || prev == '.' || prev == '(' || prev == '-'
					|| prev == '/' || prev == '\'')
				{
					return true;
				}
				pos = text.find(word, pos + 1);
			}
			return false;
		}
	}

	std::string NormalizeForSearch(std::string_view text)
	{
		std::string out;
		out.reserve(text.size());
		size_t i = 0;
		while (i < text.size())
		{
			const size_t consumed = TryTransliterate(text, i, out);
			if (consumed > 0)
			{
				i += consumed;
				continue;
			}
			const unsigned char c = static_cast<unsigned char>(text[i]);
			if (c < 0x80)
			{
				out.push_back(static_cast<char>(std::tolower(c)));
			}
			else
			{
				// Octet UTF-8 hors table : conservé tel quel (comparaison
				// exacte reste possible pour les caractères non mappés).
				out.push_back(text[i]);
			}
			++i;
		}
		return out;
	}

	std::vector<size_t> FilterPaletteEntries(std::string_view query,
		const std::vector<PaletteEntry>& entries)
	{
		const std::string q = NormalizeForSearch(query);
		// Requête vide ou espaces : tout, ordre d'origine.
		const bool emptyQuery =
			q.find_first_not_of(' ') == std::string::npos;

		std::vector<size_t> prefixMatches;
		std::vector<size_t> substringMatches;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (emptyQuery)
			{
				prefixMatches.push_back(i);
				continue;
			}
			const std::string label = NormalizeForSearch(entries[i].label);
			const std::string category = NormalizeForSearch(entries[i].categoryFr);
			if (MatchesWordPrefix(label, q))
			{
				prefixMatches.push_back(i);
			}
			else if (label.find(q) != std::string::npos
				|| category.find(q) != std::string::npos)
			{
				substringMatches.push_back(i);
			}
		}
		prefixMatches.insert(prefixMatches.end(),
			substringMatches.begin(), substringMatches.end());
		return prefixMatches;
	}
}
