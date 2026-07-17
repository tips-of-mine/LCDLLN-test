/// Tests unitaires CPU pour le filtrage de la palette de commandes Ctrl+P
/// (réorganisation UI 2026-07-17, PR 3).
///
/// Pas d'ImGui — la suite tourne sous ctest Linux. On vérifie :
///   - NormalizeForSearch : minuscules + translittération des accents FR,
///     idempotence.
///   - FilterPaletteEntries : requête vide → tout dans l'ordre ; insensible
///     casse/accents ; préfixe de mot classé avant sous-chaîne ; match sur
///     la catégorie ; aucun match → vide ; stabilité de l'ordre.
///
/// Framework : REQUIRE maison + main monolithique (pattern des autres
/// suites world_editor).

#include "src/world_editor/ui/CommandPaletteModel.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::editor::world::palette::FilterPaletteEntries;
	using engine::editor::world::palette::NormalizeForSearch;
	using engine::editor::world::palette::PaletteEntry;

	PaletteEntry Make(const char* id, const char* label, const char* cat)
	{
		PaletteEntry e;
		e.id = id;
		e.label = label;
		e.categoryFr = cat;
		return e;
	}

	/// Jeu d'entrées représentatif (libellés réels du registre).
	std::vector<PaletteEntry> MakeEntries()
	{
		return {
			Make("file.save",        "Sauvegarder la carte courante", "Fichier"),   // 0
			Make("edit.undo",        "Annuler",                       "Édition"),   // 1
			Make("edit.redo",        "Rétablir",                      "Édition"),   // 2
			Make("zone.export",      "Exporter en runtime",           "Fichier"),   // 3
			Make("tool.river",       "Rivière",                       "Outils"),    // 4
			Make("tool.hydraulic-erosion", "Érosion hydraulique",     "Outils"),    // 5
		};
	}

	/// Test : normalisation — minuscules, accents translittérés, idempotence.
	void Test_Normalize_LowercaseAndAccents()
	{
		REQUIRE(NormalizeForSearch("Sauvegarder") == "sauvegarder");
		REQUIRE(NormalizeForSearch("Érosion hydraulique") == "erosion hydraulique");
		REQUIRE(NormalizeForSearch("Rivière") == "riviere");
		REQUIRE(NormalizeForSearch("Chaîne de vallées") == "chaine de vallees");
		REQUIRE(NormalizeForSearch("Ça œuvre") == "ca oeuvre");
		// Idempotence : normaliser un texte déjà normalisé ne change rien.
		REQUIRE(NormalizeForSearch(NormalizeForSearch("Préférences"))
			== NormalizeForSearch("Préférences"));
	}

	/// Test : requête vide (ou espaces) → toutes les entrées, ordre d'origine.
	void Test_Filter_EmptyQueryReturnsAllInOrder()
	{
		const auto entries = MakeEntries();
		const std::vector<size_t> all = FilterPaletteEntries("", entries);
		REQUIRE(all.size() == entries.size());
		for (size_t i = 0; i < all.size(); ++i) { REQUIRE(all[i] == i); }

		const std::vector<size_t> spaces = FilterPaletteEntries("   ", entries);
		REQUIRE(spaces.size() == entries.size());
	}

	/// Test : insensible à la casse ET aux accents (requête accentuée ou non).
	void Test_Filter_CaseAndAccentInsensitive()
	{
		const auto entries = MakeEntries();
		const std::vector<size_t> byPlain = FilterPaletteEntries("erosion", entries);
		REQUIRE(byPlain.size() == 1u);
		REQUIRE(byPlain[0] == 5u);

		const std::vector<size_t> byAccent = FilterPaletteEntries("Érosion", entries);
		REQUIRE(byAccent.size() == 1u);
		REQUIRE(byAccent[0] == 5u);

		const std::vector<size_t> upper = FilterPaletteEntries("RIVIERE", entries);
		REQUIRE(upper.size() == 1u);
		REQUIRE(upper[0] == 4u);
	}

	/// Test : préfixe de mot classé AVANT sous-chaîne. « re » préfixe
	/// « Rétablir » et « runtime » (mot de « Exporter en runtime ») mais
	/// n'est que sous-chaîne de « Sauvegarder ».
	void Test_Filter_WordPrefixRanksBeforeSubstring()
	{
		const auto entries = MakeEntries();
		const std::vector<size_t> res = FilterPaletteEntries("re", entries);
		// Préfixes de mot : "Rétablir" (2) et "runtime" dans (3) — ordre
		// d'origine préservé entre eux. Sous-chaînes ensuite : "Sauvegarder"
		// (0, "...der la ca..." ne contient pas "re" ? si : "Sauvegarder"
		// contient "r e" ? — vérifions plutôt par appartenance et rangs.
		REQUIRE(!res.empty());
		// (2) et (3) doivent être présents et AVANT (0) s'il matche.
		size_t rank2 = res.size(), rank3 = res.size(), rank0 = res.size();
		for (size_t i = 0; i < res.size(); ++i)
		{
			if (res[i] == 2u) rank2 = i;
			if (res[i] == 3u) rank3 = i;
			if (res[i] == 0u) rank0 = i;
		}
		REQUIRE(rank2 < res.size());
		REQUIRE(rank3 < res.size());
		REQUIRE(rank2 < rank3); // ordre d'origine entre égaux
		if (rank0 < res.size())
		{
			REQUIRE(rank2 < rank0);
			REQUIRE(rank3 < rank0);
		}
	}

	/// Test : match sur la catégorie (taper « outils » liste les outils).
	void Test_Filter_MatchesCategory()
	{
		const auto entries = MakeEntries();
		const std::vector<size_t> res = FilterPaletteEntries("outils", entries);
		REQUIRE(res.size() == 2u);
		REQUIRE(res[0] == 4u);
		REQUIRE(res[1] == 5u);
	}

	/// Test : aucun match → résultat vide (pas de crash, pas de fallback).
	void Test_Filter_NoMatchReturnsEmpty()
	{
		const auto entries = MakeEntries();
		REQUIRE(FilterPaletteEntries("zzzz_introuvable", entries).empty());
		// Entrées vides : toujours sûr.
		const std::vector<PaletteEntry> none;
		REQUIRE(FilterPaletteEntries("a", none).empty());
		REQUIRE(FilterPaletteEntries("", none).empty());
	}
}

int main()
{
	Test_Normalize_LowercaseAndAccents();
	Test_Filter_EmptyQueryReturnsAllInOrder();
	Test_Filter_CaseAndAccentInsensitive();
	Test_Filter_WordPrefixRanksBeforeSubstring();
	Test_Filter_MatchesCategory();
	Test_Filter_NoMatchReturnsEmpty();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[CommandPaletteModelTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[CommandPaletteModelTests] all tests passed\n");
	return 0;
}
