#include "engine/network/FakeNetClient.h"

namespace engine::network::test
{
	void FakeNetClient::SetPeer(FakeNetClient* peer)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_peer = peer;
	}

	void FakeNetClient::Connect(const std::string& host, uint16_t port)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_pendingHost = host;
			m_pendingPort = port;
		}
		m_state.store(NetClientState::Connected);
		NetClientEvent ev;
		ev.type = NetClientEventType::Connected;
		PushEvent(std::move(ev));

		// Mirror : the peer also flips to Connected (symmetric in-memory link).
		FakeNetClient* peer = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			peer = m_peer;
		}
		if (peer && peer->GetState() != NetClientState::Connected)
		{
			peer->m_state.store(NetClientState::Connected);
			NetClientEvent peerEv;
			peerEv.type = NetClientEventType::Connected;
			peer->PushEvent(std::move(peerEv));
		}
	}

	void FakeNetClient::Disconnect(std::string reason)
	{
		FakeNetClient* peer = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			peer = m_peer;
		}
		m_state.store(NetClientState::Disconnected);
		NetClientEvent ev;
		ev.type = NetClientEventType::Disconnected;
		ev.reason = reason;
		PushEvent(std::move(ev));

		if (peer && peer->GetState() != NetClientState::Disconnected)
		{
			peer->m_state.store(NetClientState::Disconnected);
			NetClientEvent peerEv;
			peerEv.type = NetClientEventType::Disconnected;
			peerEv.reason = "peer closed: " + reason;
			peer->PushEvent(std::move(peerEv));
		}
	}

	bool FakeNetClient::Send(std::span<const uint8_t> packet)
	{
		if (m_state.load() != NetClientState::Connected)
			return false;
		FakeNetClient* peer = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			peer = m_peer;
		}
		if (!peer)
			return false;
		std::vector<uint8_t> copy(packet.begin(), packet.end());
		NetClientEvent ev;
		ev.type = NetClientEventType::PacketReceived;
		ev.packet = std::move(copy);
		peer->PushEvent(std::move(ev));
		return true;
	}

	std::vector<NetClientEvent> FakeNetClient::PollEvents()
	{
		std::vector<NetClientEvent> out;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			out.swap(m_events);
		}
		return out;
	}

	void FakeNetClient::InjectReceivedPacket(std::vector<uint8_t> packet)
	{
		NetClientEvent ev;
		ev.type = NetClientEventType::PacketReceived;
		ev.packet = std::move(packet);
		PushEvent(std::move(ev));
	}

	void FakeNetClient::PushEvent(NetClientEvent ev)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_events.push_back(std::move(ev));
	}
}
