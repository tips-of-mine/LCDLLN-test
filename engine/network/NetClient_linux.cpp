#if !defined(_WIN32)

#include "engine/network/NetClient.h"

namespace engine::network
{
	NetClient::NetClient() = default;
	NetClient::~NetClient() = default;

	void NetClient::SetExpectedServerFingerprint(std::string hexFingerprint)
	{
		m_expectedServerFingerprintHex = std::move(hexFingerprint);
	}

	void NetClient::SetAllowInsecureDev(bool allow)
	{
		m_allowInsecureDev = allow;
	}

	void NetClient::Connect(const std::string& host, uint16_t port)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_pendingHost = host;
		m_pendingPort = port;
		m_pendingConnect = true;
		m_state.store(NetClientState::Disconnected);
	}

	void NetClient::Disconnect(std::string reason)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_requestDisconnect = true;
		m_disconnectReason = std::move(reason);
		m_state.store(NetClientState::Disconnected);
	}

	bool NetClient::Send(std::span<const uint8_t>)
	{
		return false;
	}

	std::vector<NetClientEvent> NetClient::PollEvents()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto events = std::move(m_eventQueue);
		m_eventQueue.clear();
		return events;
	}

	NetClientState NetClient::GetState() const
	{
		return m_state.load();
	}

	uint64_t NetClient::GetBytesIn() const
	{
		return m_bytesIn.load();
	}

	uint64_t NetClient::GetBytesOut() const
	{
		return m_bytesOut.load();
	}

	uint64_t NetClient::GetPacketsIn() const
	{
		return m_packetsIn.load();
	}

	uint64_t NetClient::GetPacketsOut() const
	{
		return m_packetsOut.load();
	}

	void NetClient::NetworkThreadRun()
	{
	}

	void NetClient::TlsCleanupAndDisconnect(void*, void*, uintptr_t&, std::string_view)
	{
	}
}

#endif
