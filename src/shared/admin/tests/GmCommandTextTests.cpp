// ADM.0 — Tests des outils texte des commandes de jeu (GmCommandText.h).

#include "src/shared/admin/GmCommandText.h"

#include <iostream>
#include <string>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

int main()
{
	using namespace engine::admin;

	// Découpe : espaces multiples, guillemets, guillemet non fermé.
	auto a = SplitArgs("  tp   12  \"lac de Marn\"  x");
	Check(a.size() == 4 && a[0] == "tp" && a[1] == "12" && a[2] == "lac de Marn" && a[3] == "x", "SplitArgs guillemets");
	a = SplitArgs("\"ouvert jusqu'au bout");
	Check(a.size() == 1 && a[0] == "ouvert jusqu'au bout", "SplitArgs guillemet non fermé");
	Check(SplitArgs("").empty() && SplitArgs("   ").empty(), "SplitArgs vide");
	Check(JoinArgs({ "a", "b", "c" }, 1) == "b c", "JoinArgs");

	// Nombres.
	Check(ParseDouble("12") == 12.0 && ParseDouble("-3.5") == -3.5 && ParseDouble("1,25") == 1.25, "ParseDouble formes");
	Check(ParseDouble("+4") == 4.0, "ParseDouble +");
	Check(!ParseDouble("abc") && !ParseDouble("") && !ParseDouble("1.2.3x") && !ParseDouble("nan"), "ParseDouble refus");
	Check(ParseInt("42") == 42 && ParseInt("-7") == -7 && ParseInt("+3") == 3, "ParseInt");
	Check(!ParseInt("4.2") && !ParseInt("x") && !ParseInt(""), "ParseInt refus");

	// Interrupteurs.
	Check(ParseOnOff("ON") == true && ParseOnOff("oui") == true && ParseOnOff("1") == true, "ParseOnOff vrai");
	Check(ParseOnOff("off") == false && ParseOnOff("Non") == false && ParseOnOff("0") == false, "ParseOnOff faux");
	Check(!ParseOnOff("peut-être"), "ParseOnOff refus");

	// Durées.
	Check(ParseDurationSeconds("30s") == 30u && ParseDurationSeconds("10m") == 600u, "durée s/m");
	Check(ParseDurationSeconds("2h") == 7200u && ParseDurationSeconds("3d") == 259200u && ParseDurationSeconds("1j") == 86400u, "durée h/d/j");
	Check(ParseDurationSeconds("15") == 900u, "durée nombre seul = minutes");
	Check(!ParseDurationSeconds("0") && !ParseDurationSeconds("-5m") && !ParseDurationSeconds("abc"), "durée refus");
	Check(FormatDurationFr(45) == "45 s" && FormatDurationFr(3725) == "1 h 02 min", "FormatDurationFr");

	// Noms, casse, recherche.
	Check(NormalizeCommandName("/TP 10 20") == "tp" && NormalizeCommandName("help") == "help", "NormalizeCommandName");
	Check(EqualsIgnoreCase("Hubert", "hUBERT") && !EqualsIgnoreCase("a", "ab"), "EqualsIgnoreCase");
	Check(ContainsIgnoreCase("Téléportation vers un JOUEUR", "joueur") && !ContainsIgnoreCase("abc", "x"), "ContainsIgnoreCase");

	// Lignes de chat : jamais plus de N octets, UTF-8 intact, coupe sur un espace.
	const std::string longText = "Déplace votre personnage vers les coordonnées indiquées en mètres ; la hauteur est recalée au sol par le client si elle est sous le terrain.";
	const auto lines = WrapUtf8(longText, 40);
	bool allShort = !lines.empty();
	for (const std::string& l : lines)
		allShort = allShort && l.size() <= 40u;
	Check(allShort && lines.size() >= 4u, "WrapUtf8 longueur");
	Check(lines.size() > 1u && lines[1].rfind("  ", 0) == 0, "WrapUtf8 continuation indentée");
	Check(WrapUtf8("court", 40).size() == 1u && WrapUtf8("", 40).size() == 1u, "WrapUtf8 court/vide");
	const std::string accents = "éééééééééééééééééééééééééééééé"; // 60 octets
	for (const std::string& l : WrapUtf8(accents, 21))
		Check(l.size() <= 21u && (l.size() % 2u == 0u || l.rfind("  ", 0) == 0), "WrapUtf8 sans couper un caractère");
	Check(TruncateUtf8("éa", 1).empty() && TruncateUtf8("éa", 2) == "é", "TruncateUtf8");

	if (s_failCount == 0)
		std::cout << "gm_command_text_tests : tout est vert" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
