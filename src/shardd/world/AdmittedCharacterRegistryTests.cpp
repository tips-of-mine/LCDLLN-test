// TA.3 — Tests de AdmittedCharacterRegistry (gate de session UDP côté shard).
// Pattern repo : int main() + assert + std::puts, sans framework.
// Cible CTest : admitted_character_registry_tests (branche UNIX de src/CMakeLists.txt).
//
// Le preset CI Linux est Release (-DNDEBUG) → on neutralise NDEBUG pour que les
// assert mordent réellement (cf. UdpTransportTests.cpp).
#ifdef NDEBUG
#	undef NDEBUG
#endif

#include "src/shardd/world/AdmittedCharacterRegistry.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string_view>

using engine::server::AdmittedCharacterRegistry;

namespace
{
	/// Un personnage admis est reconnu ; un autre, non admis, ne l'est pas.
	void TestAdmitThenIsAdmitted()
	{
		AdmittedCharacterRegistry reg;
		reg.Admit(42u, 1001u, 1000u);
		assert(reg.IsAdmitted(42u, 1000u));
		assert(reg.AdmittedAccountId(42u, 1000u) == 1001u);
		assert(!reg.IsAdmitted(43u, 1000u));
		assert(reg.AdmittedAccountId(43u, 1000u) == 0u);
		std::puts("[OK] TestAdmitThenIsAdmitted");
	}

	/// character_id == 0 (sentinelle « pas de personnage », ticket pré-EnterWorld)
	/// n'est jamais admis, même si Admit(0) est appelé.
	void TestZeroCharacterNeverAdmitted()
	{
		AdmittedCharacterRegistry reg;
		reg.Admit(0u, 1001u, 1000u);
		assert(!reg.IsAdmitted(0u, 1000u));
		assert(reg.Count() == 0u); // Admit(0) est un no-op
		std::puts("[OK] TestZeroCharacterNeverAdmitted");
	}

	/// Une admission expire après le TTL.
	void TestAdmissionExpiresAfterTtl()
	{
		AdmittedCharacterRegistry reg;
		reg.SetTtlMs(5000u);
		reg.Admit(7u, 1u, 1000u);
		assert(reg.IsAdmitted(7u, 1000u + 5000u));      // pile au TTL : encore admis
		assert(!reg.IsAdmitted(7u, 1000u + 5001u));     // au-delà du TTL : expiré
		assert(reg.AdmittedAccountId(7u, 1000u + 5001u) == 0u);
		std::puts("[OK] TestAdmissionExpiresAfterTtl");
	}

	/// Revoke retire l'admission immédiatement.
	void TestRevoke()
	{
		AdmittedCharacterRegistry reg;
		reg.Admit(9u, 2u, 100u);
		assert(reg.IsAdmitted(9u, 100u));
		reg.Revoke(9u);
		assert(!reg.IsAdmitted(9u, 100u));
		std::puts("[OK] TestRevoke");
	}

	/// Re-Admit rafraîchit l'horodatage (prolonge la fenêtre TTL).
	void TestReAdmitRefreshesTimestamp()
	{
		AdmittedCharacterRegistry reg;
		reg.SetTtlMs(1000u);
		reg.Admit(5u, 1u, 0u);
		assert(!reg.IsAdmitted(5u, 2000u));    // expiré sans refresh
		reg.Admit(5u, 1u, 2000u);              // refresh
		assert(reg.IsAdmitted(5u, 2500u));     // de nouveau valide
		std::puts("[OK] TestReAdmitRefreshesTimestamp");
	}

	/// TD.5 — Admit avec nom retourne ce nom ; Admit sans nom retourne chaîne vide.
	void TestAdmitWithName()
	{
		AdmittedCharacterRegistry reg;
		reg.SetTtlMs(1000u);
		reg.Admit(11u, 1u, std::string_view{"homme"}, 100u);
		assert(reg.AdmittedCharacterName(11u, 100u) == "homme");
		reg.Admit(12u, 1u, 100u); // surcharge legacy = nom vide
		assert(reg.AdmittedCharacterName(12u, 100u).empty());
		// character_id inconnu ou expiré => chaîne vide
		assert(reg.AdmittedCharacterName(999u, 100u).empty());
		assert(reg.AdmittedCharacterName(11u, 100u + 5000u).empty()); // TTL dépassé
		std::puts("[OK] TestAdmitWithName");
	}

	/// TD.5 — un re-Admit anonyme après un Admit nommé préserve le nom (le ticket TCP
	/// rafraîchit le timestamp mais ne porte pas le nom — ne doit pas l'effacer).
	void TestReAdmitWithoutNamePreservesName()
	{
		AdmittedCharacterRegistry reg;
		reg.Admit(20u, 7u, std::string_view{"femme"}, 0u);
		assert(reg.AdmittedCharacterName(20u, 0u) == "femme");
		reg.Admit(20u, 7u, 1000u); // legacy : sans nom
		assert(reg.AdmittedCharacterName(20u, 1000u) == "femme");
		std::puts("[OK] TestReAdmitWithoutNamePreservesName");
	}

	/// TD.6 — Admit avec genre retourne ce genre ; surcharges sans genre retournent vide.
	void TestAdmitWithGender()
	{
		AdmittedCharacterRegistry reg;
		reg.SetTtlMs(1000u);
		reg.Admit(30u, 1u, std::string_view{"homme"}, std::string_view{"male"}, 100u);
		assert(reg.AdmittedGender(30u, 100u) == "male");
		reg.Admit(31u, 1u, std::string_view{"femme"}, std::string_view{"female"}, 100u);
		assert(reg.AdmittedGender(31u, 100u) == "female");
		// Surcharge legacy (nom mais sans genre) => genre vide.
		reg.Admit(32u, 1u, std::string_view{"anon"}, 100u);
		assert(reg.AdmittedGender(32u, 100u).empty());
		// character_id inconnu ou expiré => chaîne vide.
		assert(reg.AdmittedGender(999u, 100u).empty());
		assert(reg.AdmittedGender(30u, 100u + 5000u).empty()); // TTL dépassé
		std::puts("[OK] TestAdmitWithGender");
	}

	/// TD.6 — un re-Admit sans genre après un Admit avec genre préserve le genre.
	void TestReAdmitWithoutGenderPreservesGender()
	{
		AdmittedCharacterRegistry reg;
		reg.Admit(40u, 7u, std::string_view{"femme"}, std::string_view{"female"}, 0u);
		assert(reg.AdmittedGender(40u, 0u) == "female");
		reg.Admit(40u, 7u, std::string_view{"femme"}, 1000u); // surcharge sans genre
		assert(reg.AdmittedGender(40u, 1000u) == "female");
		std::puts("[OK] TestReAdmitWithoutGenderPreservesGender");
	}

	/// Roadmap-7 — Admit avec guilde retourne ce guildId ; surcharges legacy
	/// (sans guilde) retournent 0 ; TTL et character_id inconnu → 0.
	void TestAdmitWithGuild()
	{
		AdmittedCharacterRegistry reg;
		reg.SetTtlMs(1000u);
		reg.Admit(50u, 1u, std::string_view{"homme"}, std::string_view{"male"}, 7u, 100u);
		assert(reg.AdmittedGuildId(50u, 100u) == 7u);
		// Surcharge legacy (nom+genre sans guilde) => 0.
		reg.Admit(51u, 1u, std::string_view{"anon"}, std::string_view{"male"}, 100u);
		assert(reg.AdmittedGuildId(51u, 100u) == 0u);
		// character_id inconnu ou expiré => 0.
		assert(reg.AdmittedGuildId(999u, 100u) == 0u);
		assert(reg.AdmittedGuildId(50u, 100u + 5000u) == 0u); // TTL dépassé
		std::puts("[OK] TestAdmitWithGuild");
	}

	/// Roadmap-7 — un re-Admit sans guilde (ticket TCP, master legacy) après un
	/// Admit avec guilde PRÉSERVE la guilde (même règle que nom/genre).
	void TestReAdmitWithoutGuildPreservesGuild()
	{
		AdmittedCharacterRegistry reg;
		reg.Admit(60u, 7u, std::string_view{"femme"}, std::string_view{"female"}, 3u, 0u);
		assert(reg.AdmittedGuildId(60u, 0u) == 3u);
		reg.Admit(60u, 7u, std::string_view{"femme"}, std::string_view{"female"}, 1000u); // sans guilde
		assert(reg.AdmittedGuildId(60u, 1000u) == 3u);
		std::puts("[OK] TestReAdmitWithoutGuildPreservesGuild");
	}
}

int main()
{
	TestAdmitThenIsAdmitted();
	TestZeroCharacterNeverAdmitted();
	TestAdmissionExpiresAfterTtl();
	TestRevoke();
	TestReAdmitRefreshesTimestamp();
	TestAdmitWithName();
	TestReAdmitWithoutNamePreservesName();
	TestAdmitWithGender();
	TestReAdmitWithoutGenderPreservesGender();
	TestAdmitWithGuild();
	TestReAdmitWithoutGuildPreservesGuild();
	std::puts("All AdmittedCharacterRegistry tests passed");
	return 0;
}
