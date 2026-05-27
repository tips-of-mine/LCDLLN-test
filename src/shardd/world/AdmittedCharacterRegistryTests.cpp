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
	std::puts("All AdmittedCharacterRegistry tests passed");
	return 0;
}
