/**
 * Tests unitaires — WrapShardAuth / UnwrapShardAuth (audit F3).
 * Pur (pas de réseau, pas de DB). Retourne 0 si OK, non-zéro sinon.
 *
 * Couvre :
 *  - round-trip nominal : wrap avec un secret non vide produit tag(32)||body,
 *    unwrap avec le même secret retrouve le body identique.
 *  - rejet mauvais secret : unwrap avec un secret différent échoue.
 *  - rejet altération : un octet du payload wrappé modifié fait échouer unwrap.
 *  - secret vide : wrap renvoie {} (pas de tag calculable).
 *  - payload trop court : unwrap rejette (< kShardAuthTagSize octets).
 */

#include "src/shared/network/ShardWireAuth.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using engine::network::WrapShardAuth;
using engine::network::UnwrapShardAuth;
using engine::network::kShardAuthTagSize;

int main()
{
	int fails = 0;
	auto A = [&](bool c, const char* m){ if(!c){ ++fails; std::cerr << "[FAIL] " << m << "\n"; } };
	const std::string secret = "un-secret-fort-de-test";
	std::vector<uint8_t> body = {1,2,3,4,5};

	auto wrapped = WrapShardAuth(secret, body);
	A(wrapped.size() == kShardAuthTagSize + body.size(), "wrap = tag(32) + body");

	auto ok = UnwrapShardAuth(secret, wrapped.data(), wrapped.size());
	A(ok.has_value(), "unwrap avec bon secret réussit");
	A(ok && ok->second == body.size(), "body de bonne taille");
	A(ok && std::equal(body.begin(), body.end(), ok->first), "body identique");

	auto bad = UnwrapShardAuth("mauvais-secret", wrapped.data(), wrapped.size());
	A(!bad.has_value(), "unwrap avec mauvais secret rejeté");

	std::vector<uint8_t> tampered = wrapped; tampered.back() ^= 0xFF;
	A(!UnwrapShardAuth(secret, tampered.data(), tampered.size()).has_value(), "body falsifié rejeté");

	A(WrapShardAuth("", body).empty(), "wrap avec secret vide -> vide");
	A(!UnwrapShardAuth(secret, wrapped.data(), kShardAuthTagSize - 1).has_value(), "trop court rejeté");

	if (fails) { std::cerr << fails << " FAIL\n"; return 1; }
	std::cout << "OK\n"; return 0;
}
