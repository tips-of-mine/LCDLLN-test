// ETAPE5.1 — tests de la répartition des canaux de chat entre master et shard.
// Pattern repo : int main() + vérifications + code retour, sans framework.

#include "src/masterd/chat/ChatChannelRouting.h"

#include <cstdio>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}
}

int main()
{
	using engine::net::ChatChannel;
	using engine::server::chat::ChatChannelRoute;
	using engine::server::chat::RouteClientChatChannel;

	const auto route = [](ChatChannel c) { return RouteClientChatChannel(static_cast<uint8_t>(c)); };

	Check(route(ChatChannel::Say) == ChatChannelRoute::Shard, "Dire -> shard");
	Check(route(ChatChannel::Yell) == ChatChannelRoute::Shard, "Crier -> shard");
	Check(route(ChatChannel::Server) == ChatChannelRoute::Forbidden, "Server interdit aux clients");
	Check(route(ChatChannel::Whisper) == ChatChannelRoute::Master, "chuchotement -> master");
	Check(route(ChatChannel::Party) == ChatChannelRoute::Master, "groupe -> master");
	Check(route(ChatChannel::Guild) == ChatChannelRoute::Master, "guilde -> master");
	Check(route(ChatChannel::Zone) == ChatChannelRoute::Master, "zone -> master");
	Check(route(ChatChannel::Global) == ChatChannelRoute::Master, "global -> master");
	Check(route(ChatChannel::Raid) == ChatChannelRoute::Master, "raid -> master");
	Check(route(ChatChannel::Friends) == ChatChannelRoute::Master, "amis -> master");

	if (s_failCount == 0)
		std::puts("[OK] chat_channel_routing_tests");
	return s_failCount == 0 ? 0 : 1;
}
