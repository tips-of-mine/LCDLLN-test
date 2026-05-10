// Implementation Build/Parse des payloads AdminCommand (opcodes 195/196).
// Format little-endian, length-prefixed strings. Cf. AdminCommandPayloads.h
// pour le layout binaire. Le module produit uniquement le payload nu : le
// caller (handler ou client) ajoute le header protocol_v1 via PacketBuilder.

#include "src/shared/network/AdminCommandPayloads.h"

#include <cstring>

namespace engine::network::admin
{
	namespace
	{
		/// Limite max d'une chaine encodee : 65535 octets (cap u16 prefix).
		/// Limite max d'une liste : 65535 elements.
		constexpr uint16_t kMaxStringLen = 0xFFFFu;
		constexpr uint16_t kMaxListSize  = 0xFFFFu;

		/// Ecrit un uint8 a la fin de \p out.
		void WriteU8(std::vector<uint8_t>& out, uint8_t v)
		{
			out.push_back(v);
		}

		/// Ecrit un uint16 little-endian a la fin de \p out.
		void WriteU16LE(std::vector<uint8_t>& out, uint16_t v)
		{
			out.push_back(static_cast<uint8_t>(v));
			out.push_back(static_cast<uint8_t>(v >> 8));
		}

		/// Ecrit une chaine length-prefixed (u16 LE + octets bruts).
		/// Si la chaine depasse \c kMaxStringLen, elle est tronquee silencieusement.
		void WriteString(std::vector<uint8_t>& out, const std::string& s)
		{
			const size_t n = s.size() > kMaxStringLen ? static_cast<size_t>(kMaxStringLen) : s.size();
			WriteU16LE(out, static_cast<uint16_t>(n));
			out.insert(out.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(n));
		}

		/// Ecrit une liste length-prefixed de chaines (u16 LE count + chaines).
		void WriteStringList(std::vector<uint8_t>& out, const std::vector<std::string>& v)
		{
			const size_t n = v.size() > kMaxListSize ? static_cast<size_t>(kMaxListSize) : v.size();
			WriteU16LE(out, static_cast<uint16_t>(n));
			for (size_t i = 0; i < n; ++i)
				WriteString(out, v[i]);
		}

		/// Lit un uint8 et avance \p pos. Retourne false si insuffisant.
		bool ReadU8(const uint8_t* d, size_t sz, size_t& pos, uint8_t& out)
		{
			if (pos + 1 > sz) return false;
			out = d[pos];
			pos += 1;
			return true;
		}

		/// Lit un uint16 little-endian et avance \p pos. Retourne false si insuffisant.
		bool ReadU16LE(const uint8_t* d, size_t sz, size_t& pos, uint16_t& out)
		{
			if (pos + 2 > sz) return false;
			out = static_cast<uint16_t>(d[pos])
			    | (static_cast<uint16_t>(d[pos + 1]) << 8);
			pos += 2;
			return true;
		}

		/// Lit une chaine length-prefixed et avance \p pos. Retourne false si
		/// le prefixe ou les octets de contenu sont insuffisants.
		bool ReadString(const uint8_t* d, size_t sz, size_t& pos, std::string& out)
		{
			uint16_t len = 0;
			if (!ReadU16LE(d, sz, pos, len)) return false;
			if (pos + len > sz) return false;
			out.assign(reinterpret_cast<const char*>(d + pos), static_cast<size_t>(len));
			pos += len;
			return true;
		}

		/// Lit une liste de chaines (u16 count + N strings) et avance \p pos.
		bool ReadStringList(const uint8_t* d, size_t sz, size_t& pos, std::vector<std::string>& out)
		{
			uint16_t count = 0;
			if (!ReadU16LE(d, sz, pos, count)) return false;
			out.clear();
			out.reserve(count);
			for (uint16_t i = 0; i < count; ++i)
			{
				std::string s;
				if (!ReadString(d, sz, pos, s)) return false;
				out.push_back(std::move(s));
			}
			return true;
		}
	}

	void BuildAdminCommandRequestPayload(const AdminCommandRequest& msg, std::vector<uint8_t>& out)
	{
		out.clear();
		WriteString(out, msg.command);
		WriteStringList(out, msg.args);
	}

	void BuildAdminCommandResponsePayload(const AdminCommandResponse& msg, std::vector<uint8_t>& out)
	{
		out.clear();
		WriteU8(out, static_cast<uint8_t>(msg.status));
		WriteString(out, msg.command);
		WriteStringList(out, msg.result);
		WriteString(out, msg.message);
	}

	bool ParseAdminCommandRequestPayload(const uint8_t* d, size_t sz, AdminCommandRequest& out)
	{
		size_t pos = 0;
		out = AdminCommandRequest{};
		if (!ReadString(d, sz, pos, out.command)) return false;
		if (!ReadStringList(d, sz, pos, out.args)) return false;
		return pos == sz;
	}

	bool ParseAdminCommandResponsePayload(const uint8_t* d, size_t sz, AdminCommandResponse& out)
	{
		size_t pos = 0;
		uint8_t status = 0;
		out = AdminCommandResponse{};
		if (!ReadU8(d, sz, pos, status)) return false;
		out.status = static_cast<AdminCommandStatus>(status);
		if (!ReadString(d, sz, pos, out.command)) return false;
		if (!ReadStringList(d, sz, pos, out.result)) return false;
		if (!ReadString(d, sz, pos, out.message)) return false;
		return pos == sz;
	}
}
