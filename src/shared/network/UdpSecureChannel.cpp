// ETAPE3.1 (chantier 4.3) — enveloppe ChaCha20-Poly1305 des datagrammes UDP de jeu.
// Voir UdpSecureChannel.h pour le principe et les formats.

#include "src/shared/network/UdpSecureChannel.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstring>
#include <limits>
#include <string_view>

namespace engine::network
{
	namespace
	{
		/// Sens client -> shard ("C2S\0" en mémoire).
		constexpr uint32_t kDirectionClientToServer = 0x00533243u;
		/// Sens shard -> client ("S2C\0" en mémoire).
		constexpr uint32_t kDirectionServerToClient = 0x00433253u;
		/// Libellé HKDF (sépare cet usage de toute autre dérivation de la même clé).
		constexpr std::string_view kHkdfInfo = "LCDLLN UDP v1";

		void PutU32(uint8_t* out, uint32_t value)
		{
			for (int i = 0; i < 4; ++i)
				out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
		}

		void PutU64(uint8_t* out, uint64_t value)
		{
			for (int i = 0; i < 8; ++i)
				out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
		}

		uint32_t GetU32(const std::byte* in)
		{
			uint32_t value = 0;
			for (int i = 0; i < 4; ++i)
				value |= static_cast<uint32_t>(static_cast<uint8_t>(in[i])) << (8 * i);
			return value;
		}

		uint64_t GetU64(const std::byte* in)
		{
			uint64_t value = 0;
			for (int i = 0; i < 8; ++i)
				value |= static_cast<uint64_t>(static_cast<uint8_t>(in[i])) << (8 * i);
			return value;
		}

		/// Nonce ChaCha20-Poly1305 (12 octets) = sens (4) + compteur (8).
		std::array<uint8_t, 12> MakeNonce(uint32_t direction, uint64_t counter)
		{
			std::array<uint8_t, 12> nonce{};
			PutU32(nonce.data(), direction);
			PutU64(nonce.data() + 4, counter);
			return nonce;
		}

		/// HKDF-SHA256 (RFC 5869) pour une sortie de 32 octets (un seul bloc).
		bool HkdfSha256(const uint8_t* ikm, size_t ikmSize, const uint8_t* salt, size_t saltSize,
			std::string_view info, std::array<uint8_t, 32>& out)
		{
			// Extraction : PRK = HMAC(sel, clé d'entrée).
			std::array<uint8_t, 32> prk{};
			unsigned int prkSize = 0;
			if (HMAC(EVP_sha256(), salt, static_cast<int>(saltSize), ikm, ikmSize, prk.data(), &prkSize) == nullptr
				|| prkSize != prk.size())
			{
				return false;
			}
			// Expansion : T(1) = HMAC(PRK, info || 0x01).
			std::vector<uint8_t> block(info.begin(), info.end());
			block.push_back(0x01u);
			unsigned int outSize = 0;
			const bool ok = HMAC(EVP_sha256(), prk.data(), static_cast<int>(prk.size()),
				block.data(), block.size(), out.data(), &outSize) != nullptr && outSize == out.size();
			OPENSSL_cleanse(prk.data(), prk.size());
			return ok;
		}
	}

	bool FillRandomBytes(std::span<uint8_t> out)
	{
		if (out.empty())
			return true;
		if (out.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
			return false;
		return RAND_bytes(out.data(), static_cast<int>(out.size())) == 1;
	}

	bool GenerateUdpSessionKey(UdpSessionKey& outKey)
	{
		return FillRandomBytes(std::span<uint8_t>(outKey.data(), outKey.size()));
	}

	bool IsSecureDatagram(std::span<const std::byte> datagram)
	{
		return datagram.size() >= 5u && GetU32(datagram.data()) == kUdpSecureMagic;
	}

	bool PeekUdpHandshake(std::span<const std::byte> datagram, UdpHandshakeHeader& outHeader)
	{
		if (datagram.size() < kUdpHandshakeOverhead || !IsSecureDatagram(datagram)
			|| static_cast<uint8_t>(datagram[4]) != kUdpSecureTypeHandshake)
		{
			return false;
		}
		outHeader.characterId = GetU64(datagram.data() + 5);
		for (size_t i = 0; i < kUdpSessionSaltSize; ++i)
			outHeader.salt[i] = static_cast<uint8_t>(datagram[13 + i]);
		return outHeader.characterId != 0u;
	}

	// --- UdpReplayWindow ----------------------------------------------------

	bool UdpReplayWindow::CanAccept(uint64_t counter) const
	{
		if (counter == 0u)
			return false; // réservé à la poignée de main
		if (counter > m_highest)
			return true;
		const uint64_t age = m_highest - counter;
		if (age >= kWindowSize)
			return false; // trop ancien : on ne sait plus s'il a déjà été vu
		return (m_bitmap & (uint64_t{ 1 } << age)) == 0u;
	}

	void UdpReplayWindow::Mark(uint64_t counter)
	{
		if (counter > m_highest)
		{
			const uint64_t shift = counter - m_highest;
			m_bitmap = (shift >= kWindowSize) ? 0u : (m_bitmap << shift);
			m_bitmap |= 1u; // bit 0 = le nouveau plus grand compteur
			m_highest = counter;
			return;
		}
		const uint64_t age = m_highest - counter;
		if (age < kWindowSize)
			m_bitmap |= (uint64_t{ 1 } << age);
	}

	// --- UdpSecureSession ---------------------------------------------------

	UdpSecureSession::~UdpSecureSession()
	{
		Reset();
	}

	UdpSecureSession::UdpSecureSession(UdpSecureSession&& other) noexcept
	{
		*this = std::move(other);
	}

	UdpSecureSession& UdpSecureSession::operator=(UdpSecureSession&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_workKey = other.m_workKey;
			m_salt = other.m_salt;
			m_role = other.m_role;
			m_ready = other.m_ready;
			m_handshakeOpened = other.m_handshakeOpened;
			m_handshakeSealed = other.m_handshakeSealed;
			m_sendCounter = other.m_sendCounter;
			m_receiveWindow = other.m_receiveWindow;
			m_rejected = other.m_rejected;
			m_cipherCtx = other.m_cipherCtx;
			other.m_cipherCtx = nullptr;
			OPENSSL_cleanse(other.m_workKey.data(), other.m_workKey.size());
			other.m_ready = false;
		}
		return *this;
	}

	void UdpSecureSession::Reset()
	{
		if (m_cipherCtx != nullptr)
		{
			EVP_CIPHER_CTX_free(static_cast<EVP_CIPHER_CTX*>(m_cipherCtx));
			m_cipherCtx = nullptr;
		}
		OPENSSL_cleanse(m_workKey.data(), m_workKey.size());
		m_ready = false;
		m_handshakeOpened = false;
		m_handshakeSealed = false;
		m_sendCounter = 1u;
		m_receiveWindow = UdpReplayWindow{};
		m_rejected = 0u;
	}

	bool UdpSecureSession::Init(const UdpSessionKey& sessionKey, const UdpSessionSalt& salt, UdpSecureRole role)
	{
		Reset();
		if (!HkdfSha256(sessionKey.data(), sessionKey.size(), salt.data(), salt.size(), kHkdfInfo, m_workKey))
			return false;
		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (ctx == nullptr)
		{
			OPENSSL_cleanse(m_workKey.data(), m_workKey.size());
			return false;
		}
		m_cipherCtx = ctx;
		m_salt = salt;
		m_role = role;
		m_ready = true;
		return true;
	}

	bool UdpSecureSession::SealWith(uint32_t direction, uint64_t counter, std::span<const uint8_t> header,
		std::span<const std::byte> plain, std::vector<std::byte>& out)
	{
		if (plain.size() > static_cast<size_t>((std::numeric_limits<int>::max)() / 2))
			return false;
		auto* ctx = static_cast<EVP_CIPHER_CTX*>(m_cipherCtx);
		const auto nonce = MakeNonce(direction, counter);
		out.resize(header.size() + plain.size() + kUdpAeadTagSize);
		std::memcpy(out.data(), header.data(), header.size());
		int len = 0;
		if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1
			|| EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1
			|| EVP_EncryptInit_ex(ctx, nullptr, nullptr, m_workKey.data(), nonce.data()) != 1
			|| EVP_EncryptUpdate(ctx, nullptr, &len, header.data(), static_cast<int>(header.size())) != 1)
		{
			out.clear();
			return false;
		}
		auto* cipherOut = reinterpret_cast<unsigned char*>(out.data() + header.size());
		int written = 0;
		if (!plain.empty())
		{
			if (EVP_EncryptUpdate(ctx, cipherOut, &len, reinterpret_cast<const unsigned char*>(plain.data()),
				static_cast<int>(plain.size())) != 1)
			{
				out.clear();
				return false;
			}
			written = len;
		}
		if (EVP_EncryptFinal_ex(ctx, cipherOut + written, &len) != 1
			|| static_cast<size_t>(written + len) != plain.size()
			|| EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, static_cast<int>(kUdpAeadTagSize),
				out.data() + header.size() + plain.size()) != 1)
		{
			out.clear();
			return false;
		}
		return true;
	}

	bool UdpSecureSession::OpenWith(uint32_t direction, uint64_t counter, std::span<const uint8_t> header,
		std::span<const std::byte> cipherAndTag, std::vector<std::byte>& outPlain)
	{
		if (cipherAndTag.size() < kUdpAeadTagSize)
			return false;
		const size_t cipherSize = cipherAndTag.size() - kUdpAeadTagSize;
		auto* ctx = static_cast<EVP_CIPHER_CTX*>(m_cipherCtx);
		const auto nonce = MakeNonce(direction, counter);
		std::array<uint8_t, kUdpAeadTagSize> tag{};
		std::memcpy(tag.data(), cipherAndTag.data() + cipherSize, tag.size());
		outPlain.resize(cipherSize);
		int len = 0;
		if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1
			|| EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) != 1
			|| EVP_DecryptInit_ex(ctx, nullptr, nullptr, m_workKey.data(), nonce.data()) != 1
			|| EVP_DecryptUpdate(ctx, nullptr, &len, header.data(), static_cast<int>(header.size())) != 1)
		{
			outPlain.clear();
			return false;
		}
		int written = 0;
		if (cipherSize > 0u)
		{
			if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(outPlain.data()), &len,
				reinterpret_cast<const unsigned char*>(cipherAndTag.data()), static_cast<int>(cipherSize)) != 1)
			{
				outPlain.clear();
				return false;
			}
			written = len;
		}
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, static_cast<int>(tag.size()), tag.data()) != 1
			|| EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(outPlain.data()) + written, &len) != 1
			|| static_cast<size_t>(written + len) != cipherSize)
		{
			// Étiquette fausse : le contenu déchiffré n'est pas fiable, on l'efface.
			OPENSSL_cleanse(outPlain.data(), outPlain.size());
			outPlain.clear();
			return false;
		}
		return true;
	}

	bool UdpSecureSession::SealHandshake(uint64_t characterId, std::span<const std::byte> plain,
		std::vector<std::byte>& outDatagram)
	{
		if (!m_ready || m_role != UdpSecureRole::Client || characterId == 0u || m_handshakeSealed)
			return false;
		std::array<uint8_t, kUdpHandshakeHeaderSize> header{};
		PutU32(header.data(), kUdpSecureMagic);
		header[4] = kUdpSecureTypeHandshake;
		PutU64(header.data() + 5, characterId);
		std::memcpy(header.data() + 13, m_salt.data(), m_salt.size());
		m_handshakeSealed = true; // même en cas d'échec : le compteur 0 n'est jamais retenté
		return SealWith(kDirectionClientToServer, 0u, header, plain, outDatagram);
	}

	bool UdpSecureSession::OpenHandshake(std::span<const std::byte> datagram, std::vector<std::byte>& outPlain)
	{
		UdpHandshakeHeader parsed{};
		if (!m_ready || m_role != UdpSecureRole::Server || m_handshakeOpened
			|| !PeekUdpHandshake(datagram, parsed) || parsed.salt != m_salt)
		{
			++m_rejected;
			return false;
		}
		std::array<uint8_t, kUdpHandshakeHeaderSize> header{};
		std::memcpy(header.data(), datagram.data(), header.size());
		if (!OpenWith(kDirectionClientToServer, 0u, header, datagram.subspan(kUdpHandshakeHeaderSize), outPlain))
		{
			++m_rejected;
			return false;
		}
		m_handshakeOpened = true;
		return true;
	}

	bool UdpSecureSession::Seal(std::span<const std::byte> plain, std::vector<std::byte>& outDatagram)
	{
		if (!m_ready || m_sendCounter == (std::numeric_limits<uint64_t>::max)())
			return false;
		const uint64_t counter = m_sendCounter;
		std::array<uint8_t, kUdpDataHeaderSize> header{};
		PutU32(header.data(), kUdpSecureMagic);
		header[4] = kUdpSecureTypeData;
		PutU64(header.data() + 5, counter);
		const uint32_t direction = (m_role == UdpSecureRole::Client) ? kDirectionClientToServer : kDirectionServerToClient;
		if (!SealWith(direction, counter, header, plain, outDatagram))
			return false;
		++m_sendCounter;
		return true;
	}

	bool UdpSecureSession::Open(std::span<const std::byte> datagram, std::vector<std::byte>& outPlain)
	{
		if (!m_ready || datagram.size() < kUdpDataOverhead || !IsSecureDatagram(datagram)
			|| static_cast<uint8_t>(datagram[4]) != kUdpSecureTypeData)
		{
			++m_rejected;
			return false;
		}
		const uint64_t counter = GetU64(datagram.data() + 5);
		if (!m_receiveWindow.CanAccept(counter))
		{
			++m_rejected;
			return false;
		}
		std::array<uint8_t, kUdpDataHeaderSize> header{};
		std::memcpy(header.data(), datagram.data(), header.size());
		const uint32_t direction = (m_role == UdpSecureRole::Client) ? kDirectionServerToClient : kDirectionClientToServer;
		if (!OpenWith(direction, counter, header, datagram.subspan(kUdpDataHeaderSize), outPlain))
		{
			++m_rejected;
			return false;
		}
		m_receiveWindow.Mark(counter);
		return true;
	}
}
