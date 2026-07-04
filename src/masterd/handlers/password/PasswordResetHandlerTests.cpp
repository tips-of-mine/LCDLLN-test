/**
 * Audit F8 : tests du verrou anti-brute-force sur PasswordResetHandler::HandleVerifyEmail.
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 *
 * HandleVerifyEmail() est privée : on passe par HandlePacket() (point d'entrée public),
 * comme le ferait le NetServer réel. Le NetServer utilisé ici est construit mais jamais
 * Init() — Send() devient alors un no-op sûr (m_impl == nullptr), ce qui permet de tester
 * la logique métier sans socket réelle.
 */

#include "src/masterd/handlers/password/PasswordResetHandler.h"
#include "src/masterd/handlers/password/PasswordResetStore.h"
#include "src/masterd/account/InMemoryAccountStore.h"
#include "src/shared/security/RateLimitAndBan.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/AuthRegisterPayloads.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/core/Log.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using namespace engine::server;
using namespace engine::network;

// Audit F8 : sans le verrou anti-brute-force, un attaquant peut essayer les 10^6
// combinaisons du code à 6 chiffres sans limite. Ce test prouve qu'après
// max_failures_before_ban (5) tentatives avec un mauvais code, le compte est
// verrouillé et un 6e appel avec le BON code est quand même refusé.
static void TestVerifyEmailLockedAfterFiveFailures()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	engine::core::Log::Init(logSettings);

	InMemoryAccountStore accountStore;
	PasswordResetStore resetStore;
	RateLimitAndBan rateLimit;
	RateLimitAndBanConfig rlCfg; // valeurs par défaut : max_failures_before_ban=5, ban_duration_sec=3600
	rateLimit.SetConfig(rlCfg);
	NetServer netServer; // jamais Init() : Send() est un no-op sûr (m_impl == nullptr)

	PasswordResetHandler handler;
	handler.SetServer(&netServer);
	handler.SetAccountStore(&accountStore);
	handler.SetPasswordResetStore(&resetStore);
	handler.SetRateLimitAndBan(&rateLimit);

	std::string tagIdOut;
	uint64_t account_id = accountStore.CreateAccount(
		"testuser_f8", "testuser_f8@example.com", "clienthash",
		"Test", "User", "1990-01-01", "FR", tagIdOut);
	Assert(account_id != 0, "CreateAccount succeeds");

	const std::string goodCode = resetStore.CreateVerificationCode(account_id);
	Assert(!goodCode.empty(), "CreateVerificationCode returns a non-empty code");
	Assert(goodCode != "000000", "sanity: generated code differs from the bad code used below");

	const uint32_t connId = 1;
	const uint32_t requestId = 42;
	const uint64_t sessionIdHeader = 0;

	// 5 tentatives avec un mauvais code : chacune doit enregistrer un échec.
	for (int i = 0; i < 5; ++i)
	{
		std::vector<uint8_t> badPayload = BuildVerifyEmailRequestPayload(account_id, "000000");
		handler.HandlePacket(connId, kOpcodeVerifyEmailRequest, requestId, sessionIdHeader,
			badPayload.data(), badPayload.size());
	}

	Assert(rateLimit.IsBanned("vemail:" + std::to_string(account_id)),
		"account rate-limit key banned after 5 failures");

	// 6e tentative avec le BON code : doit être refusée par le verrou (IsBanned),
	// donc l'e-mail ne doit PAS être marqué vérifié malgré le code correct.
	std::vector<uint8_t> goodPayload = BuildVerifyEmailRequestPayload(account_id, goodCode);
	handler.HandlePacket(connId, kOpcodeVerifyEmailRequest, requestId, sessionIdHeader,
		goodPayload.data(), goodPayload.size());

	auto account = accountStore.FindByAccountId(account_id);
	Assert(account.has_value(), "account still exists");
	Assert(account && !account->email_verified,
		"locked after 5 failures: correct code on 6th attempt is still refused");

	engine::core::Log::Shutdown();
}

// Contrôle : sans avoir épuisé les tentatives, le bon code doit être accepté normalement.
static void TestVerifyEmailAcceptedWithoutPriorFailures()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	engine::core::Log::Init(logSettings);

	InMemoryAccountStore accountStore;
	PasswordResetStore resetStore;
	RateLimitAndBan rateLimit;
	RateLimitAndBanConfig rlCfg;
	rateLimit.SetConfig(rlCfg);
	NetServer netServer;

	PasswordResetHandler handler;
	handler.SetServer(&netServer);
	handler.SetAccountStore(&accountStore);
	handler.SetPasswordResetStore(&resetStore);
	handler.SetRateLimitAndBan(&rateLimit);

	std::string tagIdOut;
	uint64_t account_id = accountStore.CreateAccount(
		"testuser_f8b", "testuser_f8b@example.com", "clienthash",
		"Test", "User", "1990-01-01", "FR", tagIdOut);
	Assert(account_id != 0, "CreateAccount succeeds (control case)");

	const std::string goodCode = resetStore.CreateVerificationCode(account_id);

	std::vector<uint8_t> goodPayload = BuildVerifyEmailRequestPayload(account_id, goodCode);
	handler.HandlePacket(1, kOpcodeVerifyEmailRequest, 42, 0, goodPayload.data(), goodPayload.size());

	auto account = accountStore.FindByAccountId(account_id);
	Assert(account && account->email_verified,
		"control: correct code accepted immediately when no prior failures");

	engine::core::Log::Shutdown();
}

int main()
{
	TestVerifyEmailLockedAfterFiveFailures();
	TestVerifyEmailAcceptedWithoutPriorFailures();

	if (s_failCount != 0)
		return s_failCount;
	return 0;
}
