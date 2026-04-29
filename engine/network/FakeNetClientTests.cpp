/**
 * Phase 3.10.1 — Smoke tests for FakeNetClient (paired in-memory client/server).
 * Returns 0 on success, non-zero on first failure.
 */

#include "engine/network/FakeNetClient.h"
#include "engine/network/CharacterPayloads.h"
#include "engine/network/PacketView.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Log.h"

#include <cstdlib>
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

using namespace engine::network;
using engine::network::test::FakeNetClient;

static void TestPairedConnect()
{
	FakeNetClient client;
	FakeNetClient server;
	client.SetPeer(&server);
	server.SetPeer(&client);

	Assert(client.GetState() == NetClientState::Disconnected, "client initially disconnected");
	Assert(server.GetState() == NetClientState::Disconnected, "server initially disconnected");

	client.Connect("fake-host", 12345u);

	Assert(client.GetState() == NetClientState::Connected, "client connected after Connect()");
	Assert(server.GetState() == NetClientState::Connected, "server flipped to Connected (symmetric link)");

	auto clientEvents = client.PollEvents();
	Assert(clientEvents.size() == 1u, "client gets 1 event");
	Assert(clientEvents.size() == 1u && clientEvents[0].type == NetClientEventType::Connected,
		"client event is Connected");

	auto serverEvents = server.PollEvents();
	Assert(serverEvents.size() == 1u, "server gets 1 event");
	Assert(serverEvents.size() == 1u && serverEvents[0].type == NetClientEventType::Connected,
		"server event is Connected");
}

static void TestPacketRoundTrip()
{
	FakeNetClient client;
	FakeNetClient server;
	client.SetPeer(&server);
	server.SetPeer(&client);
	client.Connect("h", 1u);
	(void)client.PollEvents();
	(void)server.PollEvents();

	// Build a real CHARACTER_DELETE_RESPONSE packet (8-byte payload via PacketBuilder)
	// and verify it round-trips through the fake link unmodified.
	auto serverPkt = BuildCharacterDeleteResponsePacket(1u, 99u, 0xCAFEBABEull);
	Assert(!serverPkt.empty(), "Build response packet");
	const bool sent = server.Send(std::span<const uint8_t>(serverPkt.data(), serverPkt.size()));
	Assert(sent, "server.Send returns true while connected");

	auto clientEvents = client.PollEvents();
	Assert(clientEvents.size() == 1u, "client gets exactly 1 event after server.Send");
	bool packetOk = false;
	if (!clientEvents.empty()
		&& clientEvents[0].type == NetClientEventType::PacketReceived
		&& clientEvents[0].packet == serverPkt)
	{
		packetOk = true;
	}
	Assert(packetOk, "client received the exact bytes sent by server");

	// Parse it through PacketView to confirm the payload is sane post-transport.
	if (packetOk)
	{
		PacketView view;
		Assert(PacketView::Parse(clientEvents[0].packet.data(), clientEvents[0].packet.size(), view) == PacketParseResult::Ok,
			"Parse client-side packet");
		Assert(view.Opcode() == kOpcodeCharacterDeleteResponse, "Opcode preserved through fake link");
		auto parsed = ParseCharacterDeleteResponsePayload(view.Payload(), view.PayloadSize());
		Assert(parsed.has_value() && parsed->success == 1u, "Response success preserved");
	}
}

static void TestSendDisconnectedFails()
{
	FakeNetClient client;
	FakeNetClient server;
	client.SetPeer(&server);
	server.SetPeer(&client);
	// Not connecting on purpose.
	const uint8_t bytes[18]{};
	const bool sent = client.Send(std::span<const uint8_t>(bytes, sizeof(bytes)));
	Assert(!sent, "Send fails on Disconnected state");
}

static void TestSendNoPeerFails()
{
	FakeNetClient lonely;
	lonely.Connect("h", 1u);
	(void)lonely.PollEvents();
	const uint8_t bytes[18]{};
	const bool sent = lonely.Send(std::span<const uint8_t>(bytes, sizeof(bytes)));
	Assert(!sent, "Send fails when no peer is wired");
}

static void TestDisconnectMirrorsToPeer()
{
	FakeNetClient client;
	FakeNetClient server;
	client.SetPeer(&server);
	server.SetPeer(&client);
	client.Connect("h", 1u);
	(void)client.PollEvents();
	(void)server.PollEvents();

	client.Disconnect("user request");
	Assert(client.GetState() == NetClientState::Disconnected, "client state Disconnected");
	Assert(server.GetState() == NetClientState::Disconnected, "server state Disconnected (mirrored)");

	auto serverEvents = server.PollEvents();
	bool foundDisconnect = false;
	for (const auto& ev : serverEvents)
	{
		if (ev.type == NetClientEventType::Disconnected)
		{
			foundDisconnect = true;
			break;
		}
	}
	Assert(foundDisconnect, "server received Disconnected event when client disconnected");
}

static void TestInjectReceivedPacket()
{
	FakeNetClient client;
	const uint8_t fakeBytes[18] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 };
	client.InjectReceivedPacket(std::vector<uint8_t>(std::begin(fakeBytes), std::end(fakeBytes)));
	auto events = client.PollEvents();
	Assert(events.size() == 1u, "Inject queues exactly 1 event");
	bool ok = false;
	if (!events.empty()
		&& events[0].type == NetClientEventType::PacketReceived
		&& events[0].packet.size() == sizeof(fakeBytes))
	{
		ok = (events[0].packet[0] == 1 && events[0].packet[17] == 18);
	}
	Assert(ok, "Injected packet bytes preserved");
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	TestPairedConnect();
	TestPacketRoundTrip();
	TestSendDisconnectedFails();
	TestSendNoPeerFails();
	TestDisconnectMirrorsToPeer();
	TestInjectReceivedPacket();

	engine::core::Log::Shutdown();
	return s_failCount == 0 ? 0 : 1;
}
