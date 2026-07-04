#include "src/shared/security/SharedSecretPolicy.h"
#include <iostream>
using engine::security::IsWeakSharedSecret;
int main()
{
	int fails = 0;
	auto A = [&](bool c, const char* m){ if(!c){ ++fails; std::cerr << "[FAIL] " << m << "\n"; } };
	A(IsWeakSharedSecret("") == true, "vide = faible");
	A(IsWeakSharedSecret("dev_secret_change_in_production") == true, "valeur dev connue = faible");
	A(IsWeakSharedSecret("un-vrai-secret-aleatoire-long-9f3a") == false, "secret fort = OK");
	if (fails) return 1;
	std::cout << "OK\n"; return 0;
}
