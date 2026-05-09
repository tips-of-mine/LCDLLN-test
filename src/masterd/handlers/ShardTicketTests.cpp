/**
 * M22.4: Unit tests for shard ticket validation (expired/refused, valid accepted).
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "engine/server/ShardTicketValidator.h"
#include "engine/server/ShardTicketCrypto.h"
#include "engine/network/ShardTicketPayloads.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

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

static std::vector<uint8_t> MakeValidTicket(std::string_view secret, uint64_t account_id, uint32_t target_shard_id, uint64_t expires_at)
{
	ShardTicketId ticket_id{};
	for (size_t i = 0; i < ticket_id.size(); ++i)
		ticket_id[i] = static_cast<uint8_t>(i + 1);
	std::array<uint8_t, kShardTicketHmacSize> hmac{};
	if (!ComputeTicketHmac(ticket_id.data(), ticket_id.size(), account_id, target_shard_id, expires_at, secret, hmac.data(), hmac.size()))
		return {};
	return BuildShardTicketPayload(ticket_id, account_id, target_shard_id, expires_at, hmac.data(), hmac.size());
}

static void TestValidTicketAccepted()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	const std::string secret = "test_secret_32_bytes_long_enough!!";
	uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	std::vector<uint8_t> payload = MakeValidTicket(secret, 1001, 2, now + 60);
	Assert(!payload.empty(), "MakeValidTicket returns non-empty");

	ShardTicketValidator validator;
	validator.SetSecret(secret);
	auto accept = validator.VerifyAndConsume(payload.data(), payload.size());
	Assert(accept.has_value(), "Valid ticket accepted");
	Assert(accept->account_id == 1001, "account_id correct");
	Assert(accept->target_shard_id == 2, "target_shard_id correct");

	engine::core::Log::Shutdown();
}

static void TestExpiredTicketRefused()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	const std::string secret = "test_secret";
	uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	std::vector<uint8_t> payload = MakeValidTicket(secret, 1002, 1, now - 10);
	Assert(!payload.empty(), "MakeValidTicket expired");

	ShardTicketValidator validator;
	validator.SetSecret(secret);
	auto accept = validator.VerifyAndConsume(payload.data(), payload.size());
	Assert(!accept.has_value(), "Expired ticket refused");

	engine::core::Log::Shutdown();
}

static void TestInvalidHmacRefused()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	const std::string secret = "test_secret";
	uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	std::vector<uint8_t> payload = MakeValidTicket(secret, 1003, 1, now + 60);
	Assert(!payload.empty(), "MakeValidTicket");
	payload[payload.size() - 1] ^= 0xFF;

	ShardTicketValidator validator;
	validator.SetSecret(secret);
	auto accept = validator.VerifyAndConsume(payload.data(), payload.size());
	Assert(!accept.has_value(), "Invalid HMAC refused");

	engine::core::Log::Shutdown();
}

static void TestAlreadyUsedRefused()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	const std::string secret = "test_secret";
	uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	std::vector<uint8_t> payload = MakeValidTicket(secret, 1004, 1, now + 60);
	Assert(!payload.empty(), "MakeValidTicket");

	ShardTicketValidator validator;
	validator.SetSecret(secret);
	auto accept1 = validator.VerifyAndConsume(payload.data(), payload.size());
	Assert(accept1.has_value(), "First use accepted");
	auto accept2 = validator.VerifyAndConsume(payload.data(), payload.size());
	Assert(!accept2.has_value(), "Second use (already used) refused");

	engine::core::Log::Shutdown();
}

int main()
{
	TestValidTicketAccepted();
	TestExpiredTicketRefused();
	TestInvalidHmacRefused();
	TestAlreadyUsedRefused();
	return s_failCount != 0 ? 1 : 0;
}
