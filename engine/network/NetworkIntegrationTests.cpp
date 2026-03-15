/// M19.10 — Network integration tests: TLS, protocol framing, abuse cases.
/// Headless test runner: starts server on ephemeral port, connects with TLS, runs scenarios.
/// Linux only (server is epoll/Linux). Deterministic; no render dependency.

#if defined(__linux__)

#include "engine/network/AuthRegisterPayloads.h"
#include "engine/network/ByteWriter.h"
#include "engine/network/ErrorPacket.h"
#include "engine/network/PacketBuilder.h"
#include "engine/network/PacketView.h"
#include "engine/network/ProtocolV1Constants.h"

#include "engine/core/Log.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/sha.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace engine::network;
	using namespace engine::core;

	constexpr uint16_t kHeartbeatOpcode = 7u;
	constexpr int kConnectTimeoutSec = 5;
	constexpr int kServerStartWaitSec = 3;

	/// Returns a free port by binding to 0. Returns 0 on failure.
	uint16_t GetEphemeralPort()
	{
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			return 0;
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
		{
			close(fd);
			return 0;
		}
		socklen_t len = sizeof(addr);
		if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
		{
			close(fd);
			return 0;
		}
		uint16_t port = ntohs(addr.sin_port);
		close(fd);
		return port;
	}

	/// Generate self-signed PEM cert and key into the given paths. Returns true on success.
	bool GenerateSelfSignedCert(const std::string& certPath, const std::string& keyPath)
	{
		EVP_PKEY* pkey = EVP_PKEY_new();
		if (!pkey)
			return false;
		EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
		if (!ctx)
		{
			EVP_PKEY_free(pkey);
			return false;
		}
		if (EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) <= 0 || EVP_PKEY_keygen(ctx, &pkey) <= 0)
		{
			EVP_PKEY_CTX_free(ctx);
			EVP_PKEY_free(pkey);
			return false;
		}
		EVP_PKEY_CTX_free(ctx);

		X509* cert = X509_new();
		if (!cert)
		{
			EVP_PKEY_free(pkey);
			return false;
		}
		X509_set_version(cert, 2);
		ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
		X509_gmtime_adj(X509_get_notBefore(cert), 0);
		X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * 365);
		X509_set_pubkey(cert, pkey);
		X509_NAME* name = X509_get_subject_name(cert);
		X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0);
		X509_set_issuer_name(cert, name);
		if (X509_sign(cert, pkey, EVP_sha256()) <= 0)
		{
			X509_free(cert);
			EVP_PKEY_free(pkey);
			return false;
		}

		FILE* fp = fopen(keyPath.c_str(), "w");
		if (!fp)
		{
			X509_free(cert);
			EVP_PKEY_free(pkey);
			return false;
		}
		PEM_write_PrivateKey(fp, pkey, nullptr, nullptr, 0, nullptr, nullptr);
		fclose(fp);

		fp = fopen(certPath.c_str(), "w");
		if (!fp)
		{
			X509_free(cert);
			EVP_PKEY_free(pkey);
			return false;
		}
		PEM_write_X509(fp, cert);
		fclose(fp);

		X509_free(cert);
		EVP_PKEY_free(pkey);
		return true;
	}

	/// SHA-256 of X509 DER as lowercase hex (64 chars).
	std::string X509FingerprintSha256Hex(X509* cert)
	{
		if (!cert)
			return {};
		unsigned char* der = nullptr;
		int len = i2d_X509(cert, &der);
		if (len <= 0 || !der)
			return {};
		unsigned char hash[SHA256_DIGEST_LENGTH];
		SHA256(der, static_cast<size_t>(len), hash);
		OPENSSL_free(der);
		std::ostringstream os;
		for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
			os << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(hash[i]);
		return os.str();
	}

	/// Minimal TLS client for tests. Connects, optionally verifies fingerprint, sends/receives.
	class TlsTestClient
	{
	public:
		TlsTestClient() = default;
		~TlsTestClient() { Close(); }

		/// Connect to host:port with TLS. If expectedFingerprintHex is non-empty, verify server cert fingerprint; refuse (return false) on mismatch.
		bool Connect(const std::string& host, uint16_t port, const std::string& expectedFingerprintHex = "")
		{
			int fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd < 0)
				return false;
			sockaddr_in addr{};
			addr.sin_family = AF_INET;
			addr.sin_port = htons(port);
			if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
			{
				close(fd);
				return false;
			}
			if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
			{
				close(fd);
				return false;
			}

			SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
			if (!ctx)
			{
				close(fd);
				return false;
			}
			if (!expectedFingerprintHex.empty())
				SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
			else
				SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

			SSL* ssl = SSL_new(ctx);
			if (!ssl)
			{
				SSL_CTX_free(ctx);
				close(fd);
				return false;
			}
			SSL_set_fd(ssl, fd);
			int ret = SSL_connect(ssl);
			if (ret != 1)
			{
				SSL_free(ssl);
				SSL_CTX_free(ctx);
				close(fd);
				return false;
			}

			if (!expectedFingerprintHex.empty())
			{
				X509* peer = SSL_get_peer_certificate(ssl);
				if (!peer)
				{
					SSL_shutdown(ssl);
					SSL_free(ssl);
					SSL_CTX_free(ctx);
					close(fd);
					return false;
				}
				std::string actual = X509FingerprintSha256Hex(peer);
				X509_free(peer);
				std::string expected = expectedFingerprintHex;
				std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
				if (actual != expected)
				{
					SSL_shutdown(ssl);
					SSL_free(ssl);
					SSL_CTX_free(ctx);
					close(fd);
					return false;
				}
			}

			m_ssl = ssl;
			m_ctx = ctx;
			m_fd = fd;
			return true;
		}

		bool Send(std::span<const uint8_t> data)
		{
			if (!m_ssl)
				return false;
			size_t off = 0;
			while (off < data.size())
			{
				int n = SSL_write(m_ssl, data.data() + off, static_cast<int>(data.size() - off));
				if (n <= 0)
					return false;
				off += static_cast<size_t>(n);
			}
			return true;
		}

		/// Receive exactly size bytes (blocking). Returns false on error or timeout.
		bool RecvExact(uint8_t* out, size_t size, int timeoutSec = 2)
		{
			if (!m_ssl)
				return false;
			if (timeoutSec > 0)
			{
				struct timeval tv{ timeoutSec, 0 };
				setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
			}
			size_t off = 0;
			while (off < size)
			{
				int n = SSL_read(m_ssl, out + off, static_cast<int>(size - off));
				if (n <= 0)
					return false;
				off += static_cast<size_t>(n);
			}
			return true;
		}

		void Close()
		{
			if (m_ssl)
			{
				SSL_shutdown(m_ssl);
				SSL_free(m_ssl);
				m_ssl = nullptr;
			}
			if (m_ctx)
			{
				SSL_CTX_free(m_ctx);
				m_ctx = nullptr;
			}
			if (m_fd >= 0)
			{
				close(m_fd);
				m_fd = -1;
			}
		}

		bool IsConnected() const { return m_ssl != nullptr; }

	private:
		SSL* m_ssl = nullptr;
		SSL_CTX* m_ctx = nullptr;
		int m_fd = -1;
	};

	/// Build a protocol v1 packet (header + optional payload).
	std::vector<uint8_t> BuildPacket(uint16_t opcode, uint32_t requestId, std::span<const uint8_t> payload = {})
	{
		PacketBuilder builder;
		ByteWriter w = builder.PayloadWriter();
		if (payload.size() > 0)
		{
			if (w.Remaining() < payload.size() || !w.WriteBytes(payload.data(), payload.size()))
				return {};
		}
		if (!builder.Finalize(opcode, 0, requestId, 0, payload.size()))
			return {};
		return builder.Data();
	}

	/// Build a packet with invalid size field (oversize) for abuse test.
	std::vector<uint8_t> BuildOversizePacket()
	{
		std::vector<uint8_t> buf(kProtocolV1HeaderSize + 4u, 0);
		uint16_t size = static_cast<uint16_t>(kProtocolV1MaxPacketSize + 1u);
		buf[0] = static_cast<uint8_t>(size & 0xff);
		buf[1] = static_cast<uint8_t>((size >> 8) & 0xff);
		buf[2] = 7; // opcode HEARTBEAT
		buf[3] = 0;
		return buf;
	}

	int g_failed = 0;

	void Fail(const char* scenario, const char* msg)
	{
		LOG_ERROR(Net, "[NetworkIntegrationTests] FAIL {}: {}", scenario, msg);
		g_failed++;
	}

	void Ok(const char* scenario)
	{
		LOG_INFO(Net, "[NetworkIntegrationTests] OK {}", scenario);
	}
}

int main(int argc, char** argv)
{
	using namespace engine::core;

	LogSettings logSettings;
	logSettings.level = LogLevel::Info;
	logSettings.console = true;
	logSettings.flushAlways = true;
	logSettings.filePath = "network_integration_tests.log";
	Log::Init(logSettings);

	LOG_INFO(Net, "[NetworkIntegrationTests] Starting (harness: start server, connect TLS, run scenarios)");

	std::string serverExe = "pkg/server/lcdlln_server";
	if (argc >= 2)
		serverExe = argv[1];

	std::string workDir = ".";
	if (argc >= 3)
		workDir = argv[2];

	uint16_t port = GetEphemeralPort();
	if (port == 0)
	{
		LOG_ERROR(Net, "[NetworkIntegrationTests] GetEphemeralPort failed");
		Log::Shutdown();
		return 1;
	}

	std::string certPath = workDir + "/test_server_cert.pem";
	std::string keyPath = workDir + "/test_server_key.pem";
	if (!GenerateSelfSignedCert(certPath, keyPath))
	{
		LOG_ERROR(Net, "[NetworkIntegrationTests] GenerateSelfSignedCert failed");
		Log::Shutdown();
		return 1;
	}

	std::ofstream config(workDir + "/config.json");
	if (!config)
	{
		LOG_ERROR(Net, "[NetworkIntegrationTests] Could not write config.json");
		Log::Shutdown();
		return 1;
	}
	config << "{\n"
		<< "  \"log\": { \"file\": \"server.log\", \"level\": \"Info\" },\n"
		<< "  \"server\": {\n"
		<< "    \"tcp\": { \"port\": " << port << ", \"max_connections\": 10, \"worker_threads\": 1,\n"
		<< "      \"packet_rate_per_sec\": 200, \"packet_burst\": 400, \"decode_failure_threshold\": 5,\n"
		<< "      \"handshake_timeout_sec\": 10, \"max_queued_tx_bytes\": 262144 },\n"
		<< "    \"tls\": { \"cert\": \"test_server_cert.pem\", \"key\": \"test_server_key.pem\" }\n"
		<< "  },\n"
		<< "  \"session\": { \"max_age_sec\": 86400, \"heartbeat_timeout_sec\": 5, \"reconnection_window_sec\": 300, \"duplicate_login_policy\": \"kick\" },\n"
		<< "  \"security\": { \"auth_per_minute\": 60, \"register_per_hour\": 10, \"max_failures_before_ban\": 10, \"ban_duration_sec\": 60, \"audit_log_path\": \"security_audit.log\" }\n"
		<< "}\n";
	config.close();

	pid_t pid = fork();
	if (pid < 0)
	{
		LOG_ERROR(Net, "[NetworkIntegrationTests] fork failed");
		Log::Shutdown();
		return 1;
	}
	if (pid == 0)
	{
		if (chdir(workDir.c_str()) != 0)
			_exit(1);
		execl(serverExe.c_str(), serverExe.c_str(), nullptr);
		_exit(1);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	for (int i = 0; i < kServerStartWaitSec * 10; ++i)
	{
		TlsTestClient c;
		if (c.Connect("127.0.0.1", port, ""))
		{
			c.Close();
			break;
		}
		if (i == kServerStartWaitSec * 10 - 1)
		{
			LOG_ERROR(Net, "[NetworkIntegrationTests] Server did not become ready");
			kill(pid, SIGTERM);
			waitpid(pid, nullptr, 0);
			Log::Shutdown();
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	// Get server cert fingerprint for fingerprint test (read from file)
	std::string serverFingerprintHex;
	{
		FILE* fp = fopen(certPath.c_str(), "r");
		if (fp)
		{
			X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
			fclose(fp);
			if (cert)
			{
				serverFingerprintHex = X509FingerprintSha256Hex(cert);
				X509_free(cert);
			}
		}
	}

	// --- Scenario: handshake OK ---
	{
		TlsTestClient c;
		if (!c.Connect("127.0.0.1", port, ""))
			Fail("handshake OK", "TLS connect failed");
		else
			Ok("handshake OK");
		c.Close();
	}

	// --- Scenario: fingerprint mismatch (client refuse) ---
	{
		TlsTestClient c;
		std::string wrongFingerprint = "0000000000000000000000000000000000000000000000000000000000000001";
		if (c.Connect("127.0.0.1", port, wrongFingerprint))
		{
			Fail("fingerprint mismatch", "client should have refused connection");
			c.Close();
		}
		else
			Ok("fingerprint mismatch (client refuse)");
	}

	// --- Scenario: ping/pong framing (HEARTBEAT) ---
	{
		TlsTestClient c;
		if (!c.Connect("127.0.0.1", port, ""))
			Fail("ping/pong framing", "connect failed");
		else
		{
			std::vector<uint8_t> pkt = BuildPacket(kHeartbeatOpcode, 1, {});
			if (pkt.empty() || !c.Send(pkt))
				Fail("ping/pong framing", "build/send HEARTBEAT failed");
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				if (!c.IsConnected())
					Fail("ping/pong framing", "disconnected after valid HEARTBEAT (framing rejected?)");
				else
					Ok("ping/pong framing");
			}
			c.Close();
		}
	}

	// --- Scenario: oversize reject (decode failure threshold → disconnect) ---
	{
		TlsTestClient c;
		if (!c.Connect("127.0.0.1", port, ""))
			Fail("oversize reject", "connect failed");
		else
		{
			std::vector<uint8_t> bad = BuildOversizePacket();
			for (int i = 0; i < 5; ++i)
				c.Send(bad);
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			if (c.IsConnected())
				Fail("oversize reject", "expected server to disconnect after decode failures");
			else
				Ok("oversize reject");
			c.Close();
		}
	}

	// --- Scenario: unknown opcode → ERROR or disconnect ---
	{
		TlsTestClient c;
		if (!c.Connect("127.0.0.1", port, ""))
			Fail("unknown opcode → ERROR", "connect failed");
		else
		{
			const uint16_t unknownOpcode = 255;
			std::vector<uint8_t> pkt = BuildPacket(unknownOpcode, 42, {});
			if (!c.Send(pkt))
				Fail("unknown opcode → ERROR", "send failed");
			else
			{
				uint8_t buf[256];
				if (!c.RecvExact(buf, 18))
				{
					// Disconnect without response is acceptable (server policy)
					if (!c.IsConnected())
						Ok("unknown opcode → ERROR (disconnect)");
					else
						Fail("unknown opcode → ERROR", "no response");
				}
				else
				{
					PacketView view;
					if (PacketView::Parse(buf, 18, view) != PacketParseResult::Ok)
						Fail("unknown opcode → ERROR", "parse header failed");
					else if (view.Opcode() != kOpcodeError)
						Fail("unknown opcode → ERROR", "expected ERROR opcode or disconnect");
					else
					{
						size_t payloadSize = view.PayloadSize();
						if (payloadSize > 0 && payloadSize <= sizeof(buf) - 18u)
							c.RecvExact(buf + 18, payloadSize);
						Ok("unknown opcode → ERROR");
					}
				}
			}
			c.Close();
		}
	}

	// --- Scenario: flood → disconnect offender ---
	{
		TlsTestClient c;
		if (!c.Connect("127.0.0.1", port, ""))
			Fail("flood → disconnect", "connect failed");
		else
		{
			std::vector<uint8_t> pkt = BuildPacket(kHeartbeatOpcode, 1, {});
			for (int i = 0; i < 1000 && c.IsConnected(); ++i)
				c.Send(pkt);
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (c.IsConnected())
				Fail("flood → disconnect", "expected server to disconnect offender (rate limit)");
			else
				Ok("flood → disconnect offender");
			c.Close();
		}
	}

	// --- Scenario: register then auth (M20.5) ---
	{
		TlsTestClient c;
		if (!c.Connect("127.0.0.1", port, ""))
			Fail("register then auth", "connect failed");
		else
		{
			const std::string login = "testuser";
			const std::string email = "test@example.com";
			const std::string client_hash = "test_client_hash_placeholder";
			std::vector<uint8_t> regPayload = BuildRegisterRequestPayload(login, email, client_hash);
			if (regPayload.empty())
				Fail("register then auth", "BuildRegisterRequestPayload failed");
			else
			{
				std::vector<uint8_t> regPkt = BuildPacket(kOpcodeRegisterRequest, 100, regPayload);
				if (regPkt.empty() || !c.Send(regPkt))
					Fail("register then auth", "send REGISTER_REQUEST failed");
				else
				{
					uint8_t hdrBuf[kProtocolV1HeaderSize];
					if (!c.RecvExact(hdrBuf, kProtocolV1HeaderSize))
						Fail("register then auth", "recv REGISTER_RESPONSE header failed");
					else
					{
						PacketView view;
						if (PacketView::Parse(hdrBuf, kProtocolV1HeaderSize, view) != PacketParseResult::Ok)
							Fail("register then auth", "parse REGISTER_RESPONSE header failed");
						else
						{
							size_t payloadSize = view.PayloadSize();
							std::vector<uint8_t> respPayload(payloadSize, 0);
							if (payloadSize > 0 && !c.RecvExact(respPayload.data(), payloadSize))
								Fail("register then auth", "recv REGISTER_RESPONSE payload failed");
							else if (view.Opcode() == kOpcodeError)
								Fail("register then auth", "server sent ERROR instead of REGISTER_RESPONSE");
							else if (view.Opcode() != kOpcodeRegisterResponse)
								Fail("register then auth", "unexpected opcode");
							else
							{
								auto regResp = ParseRegisterResponsePayload(respPayload.data(), respPayload.size());
								if (!regResp || regResp->success == 0)
									Fail("register then auth", "register failed");
								else
								{
									std::vector<uint8_t> authPayload = BuildAuthRequestPayload(login, client_hash);
									if (authPayload.empty())
										Fail("register then auth", "BuildAuthRequestPayload failed");
									else
									{
										std::vector<uint8_t> authPkt = BuildPacket(kOpcodeAuthRequest, 101, authPayload);
										if (authPkt.empty() || !c.Send(authPkt))
											Fail("register then auth", "send AUTH_REQUEST failed");
										else
										{
											if (!c.RecvExact(hdrBuf, kProtocolV1HeaderSize))
												Fail("register then auth", "recv AUTH_RESPONSE header failed");
											else if (PacketView::Parse(hdrBuf, kProtocolV1HeaderSize, view) != PacketParseResult::Ok)
												Fail("register then auth", "parse AUTH_RESPONSE header failed");
											else
											{
												payloadSize = view.PayloadSize();
												respPayload.resize(payloadSize, 0);
												if (payloadSize > 0 && !c.RecvExact(respPayload.data(), payloadSize))
													Fail("register then auth", "recv AUTH_RESPONSE payload failed");
												else if (view.Opcode() == kOpcodeError)
													Fail("register then auth", "server sent ERROR for auth");
												else if (view.Opcode() != kOpcodeAuthResponse)
													Fail("register then auth", "unexpected auth opcode");
												else
												{
													auto authResp = ParseAuthResponsePayload(respPayload.data(), respPayload.size());
													if (!authResp)
														Fail("register then auth", "ParseAuthResponsePayload failed");
													else if (authResp->success == 0)
														Fail("register then auth", "auth failed");
													else if (authResp->session_id == 0)
														Fail("register then auth", "auth success but session_id is 0");
													else
														Ok("register then auth");
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
			c.Close();
		}
	}

	// --- Scenario: client offline → session expired (M20.6 heartbeat timeout) ---
	{
		TlsTestClient c;
		if (!c.Connect("127.0.0.1", port, ""))
			Fail("client offline → session close", "connect failed");
		else
		{
			const std::string login = "hbuser";
			const std::string email = "hb@example.com";
			const std::string client_hash = "hb_client_hash";
			std::vector<uint8_t> regPayload = BuildRegisterRequestPayload(login, email, client_hash);
			std::vector<uint8_t> regPkt = BuildPacket(kOpcodeRegisterRequest, 200, regPayload);
			if (regPayload.empty() || regPkt.empty() || !c.Send(regPkt))
				Fail("client offline → session close", "register send failed");
			else
			{
				uint8_t hdrBuf[kProtocolV1HeaderSize];
				if (!c.RecvExact(hdrBuf, kProtocolV1HeaderSize))
					Fail("client offline → session close", "register response header failed");
				else
				{
					PacketView view;
					if (PacketView::Parse(hdrBuf, kProtocolV1HeaderSize, view) != PacketParseResult::Ok)
						Fail("client offline → session close", "parse register response failed");
					size_t payloadSize = view.PayloadSize();
					std::vector<uint8_t> respPayload(payloadSize, 0);
					if (payloadSize > 0 && !c.RecvExact(respPayload.data(), payloadSize))
						Fail("client offline → session close", "register payload failed");
					auto regResp = ParseRegisterResponsePayload(respPayload.data(), respPayload.size());
					if (!regResp || regResp->success == 0)
						Fail("client offline → session close", "register failed");
					else
					{
						std::vector<uint8_t> authPayload = BuildAuthRequestPayload(login, client_hash);
						std::vector<uint8_t> authPkt = BuildPacket(kOpcodeAuthRequest, 201, authPayload);
						if (authPayload.empty() || authPkt.empty() || !c.Send(authPkt))
							Fail("client offline → session close", "auth send failed");
						else if (!c.RecvExact(hdrBuf, kProtocolV1HeaderSize))
							Fail("client offline → session close", "auth response header failed");
						else if (PacketView::Parse(hdrBuf, kProtocolV1HeaderSize, view) != PacketParseResult::Ok)
							Fail("client offline → session close", "parse auth response failed");
						else
						{
							payloadSize = view.PayloadSize();
							respPayload.resize(payloadSize, 0);
							if (payloadSize > 0 && !c.RecvExact(respPayload.data(), payloadSize))
								Fail("client offline → session close", "auth payload failed");
							else
							{
								auto authResp = ParseAuthResponsePayload(respPayload.data(), respPayload.size());
								if (!authResp || authResp->success == 0 || authResp->session_id == 0)
									Fail("client offline → session close", "auth failed");
								else
								{
									// Do NOT send heartbeat; wait for server to expire session (heartbeat_timeout_sec=5, watchdog every 10s).
									std::this_thread::sleep_for(std::chrono::seconds(15));
									uint8_t buf[1];
									bool readFailed = !c.RecvExact(buf, 1, 1);
									if (readFailed)
										Ok("client offline → session close");
									else
										Fail("client offline → session close", "expected server to close connection after heartbeat timeout");
								}
							}
						}
					}
				}
			}
			c.Close();
		}
	}

	kill(pid, SIGTERM);
	waitpid(pid, nullptr, 0);

	LOG_INFO(Net, "[NetworkIntegrationTests] Finished (failed={})", g_failed);
	Log::Shutdown();
	return g_failed > 0 ? 1 : 0;
}

#else

#include "engine/core/Log.h"

int main(int, char**)
{
	engine::core::LogSettings s;
	s.level = engine::core::LogLevel::Info;
	s.console = true;
	s.filePath = "network_integration_tests.log";
	engine::core::Log::Init(s);
	LOG_INFO(Net, "[NetworkIntegrationTests] Skipped (Linux only)");
	engine::core::Log::Shutdown();
	return 0;
}

#endif
