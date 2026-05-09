#include "engine/server/shard/chat/ChatLocalPayload.h"

#include <cstring>

namespace engine::server::shard::chat
{
	namespace
	{
		template<typename T>
		void Append(std::vector<uint8_t>& out, const T& v)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto* p = reinterpret_cast<const uint8_t*>(&v);
			out.insert(out.end(), p, p + sizeof(T));
		}

		template<typename T>
		bool Read(std::span<const uint8_t> in, size_t& off, T& out)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			if (off + sizeof(T) > in.size())
				return false;
			std::memcpy(&out, in.data() + off, sizeof(T));
			off += sizeof(T);
			return true;
		}
	}

	std::vector<uint8_t> EncodeChatLocal(const ChatLocalPayload& msg)
	{
		std::vector<uint8_t> out;
		// Header fixe = 8 + 1 + 12 + 2 + 2 = 25 bytes + 2 strings.
		out.reserve(25 + msg.senderName.size() + msg.text.size());

		Append(out, msg.senderGuid);
		Append(out, msg.channel);
		Append(out, msg.posX);
		Append(out, msg.posY);
		Append(out, msg.posZ);

		const auto sn = static_cast<uint16_t>(
			msg.senderName.size() > kMaxChatLocalSenderNameBytes
				? kMaxChatLocalSenderNameBytes : msg.senderName.size());
		Append(out, sn);
		for (size_t i = 0; i < sn; ++i)
			out.push_back(static_cast<uint8_t>(msg.senderName[i]));

		const auto tl = static_cast<uint16_t>(
			msg.text.size() > kMaxChatLocalTextBytes
				? kMaxChatLocalTextBytes : msg.text.size());
		Append(out, tl);
		for (size_t i = 0; i < tl; ++i)
			out.push_back(static_cast<uint8_t>(msg.text[i]));

		return out;
	}

	ChatLocalDecodeResult DecodeChatLocal(std::span<const uint8_t> in,
		ChatLocalPayload& out)
	{
		size_t off = 0;
		uint64_t guid = 0;
		uint8_t  channel = 0;
		float    px = 0, py = 0, pz = 0;
		uint16_t snLen = 0;

		if (!Read(in, off, guid)
			|| !Read(in, off, channel)
			|| !Read(in, off, px)
			|| !Read(in, off, py)
			|| !Read(in, off, pz)
			|| !Read(in, off, snLen))
			return ChatLocalDecodeResult::BufferTooSmall;

		if (snLen > kMaxChatLocalSenderNameBytes)
			return ChatLocalDecodeResult::SenderNameTooLong;
		if (off + snLen + 2 > in.size())
			return ChatLocalDecodeResult::BufferTooSmall;

		std::string senderName(reinterpret_cast<const char*>(in.data() + off), snLen);
		off += snLen;

		uint16_t txtLen = 0;
		if (!Read(in, off, txtLen))
			return ChatLocalDecodeResult::BufferTooSmall;
		if (txtLen > kMaxChatLocalTextBytes)
			return ChatLocalDecodeResult::TextTooLong;
		if (off + txtLen != in.size())
			return ChatLocalDecodeResult::LengthMismatch;

		std::string text(reinterpret_cast<const char*>(in.data() + off), txtLen);

		out.senderGuid = guid;
		out.channel    = channel;
		out.posX       = px;
		out.posY       = py;
		out.posZ       = pz;
		out.senderName = std::move(senderName);
		out.text       = std::move(text);
		return ChatLocalDecodeResult::OK;
	}
}
