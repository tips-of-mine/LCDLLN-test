/**
 * CMANGOS.18 (Phase 3.18 step 3) — Round-trip tests for MAIL_*_REQUEST /
 * MAIL_*_RESPONSE payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 *
 * Test policy : symétrique aux ChatPayloadsTests.cpp — pas de framework de
 * test externe, juste un Assert local + main() qui appelle chaque suite.
 */

#include "src/shared/network/MailPayloads.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;
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

// -----------------------------------------------------------------------------
// MAIL_SEND
// -----------------------------------------------------------------------------

static void TestSendRequestRoundTrip()
{
	auto buf = BuildMailSendRequestPayload(42ull, "Hello", "Test body content", 1000ull, 250ull);
	Assert(!buf.empty(), "Send request build not empty");
	auto parsed = ParseMailSendRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Send request parses OK");
	if (parsed)
	{
		Assert(parsed->recipientAccountId == 42ull, "recipient round-trips");
		Assert(parsed->subject == "Hello", "subject round-trips");
		Assert(parsed->body == "Test body content", "body round-trips");
		Assert(parsed->copperGold == 1000ull, "gold round-trips");
		Assert(parsed->copperCod == 250ull, "cod round-trips");
	}
}

static void TestSendRequestEmptyStrings()
{
	auto buf = BuildMailSendRequestPayload(0ull, "", "", 0ull, 0ull);
	auto parsed = ParseMailSendRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Send request with empty strings parses");
	if (parsed)
	{
		Assert(parsed->subject.empty() && parsed->body.empty(), "Empty subject/body round-trip");
		Assert(parsed->recipientAccountId == 0ull && parsed->copperGold == 0ull && parsed->copperCod == 0ull,
		       "Zero numerics round-trip");
	}
}

static void TestSendRequestRejectsShort()
{
	auto parsed = ParseMailSendRequestPayload(nullptr, 28u);
	Assert(!parsed.has_value(), "Send request rejects nullptr");
	uint8_t shortBuf[8]{};
	auto parsed2 = ParseMailSendRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Send request rejects truncated buf");
}

static void TestSendResponseRoundTrip()
{
	auto buf = BuildMailSendResponsePayload(0u, 0xDEADBEEFull);
	auto parsed = ParseMailSendResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Send response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "OK error code");
		Assert(parsed->mailId == 0xDEADBEEFull, "mailId round-trips");
	}

	auto bufErr = BuildMailSendResponsePayload(static_cast<uint8_t>(MailSendErrorCode::RecipientNotFound), 0ull);
	auto parsedErr = ParseMailSendResponsePayload(bufErr.data(), bufErr.size());
	Assert(parsedErr.has_value() && parsedErr->error == 1u, "Error code round-trips");
	Assert(parsedErr && parsedErr->mailId == 0ull, "mailId is 0 on error");
}

static void TestSendResponsePacket()
{
	// Le packet builder produit un paquet complet (header + payload). On le
	// décode en deux temps : PacketView pour le header, puis ParseMailSendResponse
	// pour le payload.
	auto pkt = BuildMailSendResponsePacket(0u, 99ull, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "Send response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeMailSendResponse, "Packet opcode is MailSendResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseMailSendResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->mailId == 99ull, "Payload decodes from packet");
}

// -----------------------------------------------------------------------------
// MAIL_LIST_INBOX
// -----------------------------------------------------------------------------

static void TestListInboxRequest()
{
	auto buf = BuildMailListInboxRequestPayload();
	Assert(buf.empty(), "ListInbox request payload is empty");
	auto parsed = ParseMailListInboxRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "ListInbox request accepts empty payload");
}

static void TestListInboxResponseEmpty()
{
	auto buf = BuildMailListInboxResponsePayload(0u, {});
	auto parsed = ParseMailListInboxResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ListInbox response (empty) parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Empty inbox is OK");
		Assert(parsed->mails.empty(), "Empty inbox has no mails");
	}
}

static void TestListInboxResponseUnauthorized()
{
	// Quand error != 0, on ne sérialise pas le count (économie de bande passante
	// sur le cas d'erreur). Le parser doit retourner error=Unauthorized sans crasher.
	auto buf = BuildMailListInboxResponsePayload(static_cast<uint8_t>(MailSendErrorCode::Unauthorized), {});
	auto parsed = ParseMailListInboxResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "Unauthorized error decodes");
	Assert(parsed && parsed->mails.empty(), "No entries decoded on error");
}

static void TestListInboxResponseMultiple()
{
	std::vector<MailInboxEntry> mails;
	{
		MailInboxEntry e1;
		e1.mailId = 1ull;
		e1.senderAccountId = 100ull;
		e1.subject = "Subject A";
		e1.sentTsMs = 1700000000000ull;
		e1.expiresTsMs = 1700001000000ull;
		e1.state = 0u;
		e1.copperGold = 50ull;
		e1.copperCod = 0ull;
		mails.push_back(e1);
	}
	{
		MailInboxEntry e2;
		e2.mailId = 2ull;
		e2.senderAccountId = 200ull;
		e2.subject = "";
		e2.sentTsMs = 1700000050000ull;
		e2.expiresTsMs = 0ull;
		e2.state = 1u;
		e2.copperGold = 0ull;
		e2.copperCod = 999ull;
		mails.push_back(e2);
	}

	auto buf = BuildMailListInboxResponsePayload(0u, mails);
	auto parsed = ParseMailListInboxResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ListInbox response (2 entries) parses OK");
	if (parsed && parsed->mails.size() == 2u)
	{
		Assert(parsed->mails[0].mailId == 1ull, "entry[0] id");
		Assert(parsed->mails[0].subject == "Subject A", "entry[0] subject");
		Assert(parsed->mails[0].copperGold == 50ull, "entry[0] gold");
		Assert(parsed->mails[1].mailId == 2ull, "entry[1] id");
		Assert(parsed->mails[1].subject.empty(), "entry[1] empty subject");
		Assert(parsed->mails[1].state == 1u, "entry[1] state Read");
		Assert(parsed->mails[1].copperCod == 999ull, "entry[1] cod");
	}
	else
	{
		Assert(false, "ListInbox response should have 2 entries");
	}
}

static void TestListInboxResponseRejectsShort()
{
	auto parsed = ParseMailListInboxResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "ListInbox response rejects nullptr");
	// 1-octet buffer with size 0 is rejected as well.
	uint8_t one[1]{0};
	auto parsed2 = ParseMailListInboxResponsePayload(one, 0u);
	Assert(!parsed2.has_value(), "ListInbox response rejects size 0");
}

// -----------------------------------------------------------------------------
// MAIL_READ
// -----------------------------------------------------------------------------

static void TestReadRequestRoundTrip()
{
	auto buf = BuildMailReadRequestPayload(123456789ull);
	Assert(buf.size() == 8u, "Read request is 8 bytes");
	auto parsed = ParseMailReadRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->mailId == 123456789ull, "Read request round-trips");
}

static void TestReadRequestRejectsShort()
{
	uint8_t shortBuf[7]{};
	auto parsed = ParseMailReadRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Read request rejects 7 bytes");
}

static void TestReadResponseRoundTrip()
{
	auto buf = BuildMailReadResponsePayload(0u, 42ull, "The full body of the mail.\nWith newlines.");
	auto parsed = ParseMailReadResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Read response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Read response error 0");
		Assert(parsed->mailId == 42ull, "Read response mailId round-trips");
		Assert(parsed->body == "The full body of the mail.\nWith newlines.", "Read response body round-trips");
	}
}

static void TestReadResponseError()
{
	auto buf = BuildMailReadResponsePayload(static_cast<uint8_t>(MailReadErrorCode::NotFound), 0ull, "");
	auto parsed = ParseMailReadResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "Read response error decodes");
	Assert(parsed && parsed->body.empty(), "Read response body empty on error");
}

// -----------------------------------------------------------------------------
// MAIL_TAKE_ATTACHMENTS
// -----------------------------------------------------------------------------

static void TestTakeRequestRoundTrip()
{
	auto buf = BuildMailTakeAttachmentsRequestPayload(7ull, 500ull);
	Assert(buf.size() == 16u, "Take request is 16 bytes");
	auto parsed = ParseMailTakeAttachmentsRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Take request parses OK");
	if (parsed)
	{
		Assert(parsed->mailId == 7ull, "Take request mailId");
		Assert(parsed->paidCopperCod == 500ull, "Take request paidCod");
	}
}

static void TestTakeRequestRejectsShort()
{
	uint8_t shortBuf[15]{};
	auto parsed = ParseMailTakeAttachmentsRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Take request rejects 15 bytes");
}

static void TestTakeResponseRoundTrip()
{
	auto buf = BuildMailTakeAttachmentsResponsePayload(0u, 7ull, 1234ull);
	Assert(buf.size() == 17u, "Take response is 17 bytes");
	auto parsed = ParseMailTakeAttachmentsResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Take response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Take response OK");
		Assert(parsed->mailId == 7ull, "Take response mailId");
		Assert(parsed->copperGoldTaken == 1234ull, "Take response gold taken");
	}
}

static void TestTakeResponseCodNotPaid()
{
	auto buf = BuildMailTakeAttachmentsResponsePayload(
		static_cast<uint8_t>(MailTakeErrorCode::CodNotPaid), 7ull, 0ull);
	auto parsed = ParseMailTakeAttachmentsResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 4u, "Take response CodNotPaid decodes");
}

// -----------------------------------------------------------------------------
// MAIL_DELETE
// -----------------------------------------------------------------------------

static void TestDeleteRequestRoundTrip()
{
	auto buf = BuildMailDeleteRequestPayload(99ull);
	Assert(buf.size() == 8u, "Delete request is 8 bytes");
	auto parsed = ParseMailDeleteRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->mailId == 99ull, "Delete request round-trips");
}

static void TestDeleteResponseRoundTrip()
{
	auto buf = BuildMailDeleteResponsePayload(0u, 99ull);
	auto parsed = ParseMailDeleteResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Delete response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Delete response error 0");
		Assert(parsed->mailId == 99ull, "Delete response mailId");
	}
}

static void TestDeleteResponseHasAttachments()
{
	auto buf = BuildMailDeleteResponsePayload(
		static_cast<uint8_t>(MailDeleteErrorCode::HasAttachments), 99ull);
	auto parsed = ParseMailDeleteResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "Delete response HasAttachments decodes");
}

// -----------------------------------------------------------------------------
// UTF-8 / boundary
// -----------------------------------------------------------------------------

static void TestSendRequestUtf8()
{
	// Wire format byte-pour-byte : on encode des octets >= 0x80 directement
	// (\xc3\xa9 = 'e' acute UTF-8). MSVC parse \xHH greedy (lit tous les
	// hex digits adjacents), donc "\xc3\xa9p" devient "\xc3\xa9p" entier
	// puis "\xa9p" out of range. Solution : casser via concatenation de
	// string literals adjacents pour forcer la fin de l'escape.
	const std::string subjectUtf8 = "Re: " "\xc3\xa9" "p" "\xc3\xa9" "e";
	const std::string bodyUtf8    = "L'" "\xc3\xa9" "p" "\xc3\xa9" "e brille " "\xc3\xa0" " la lune.";
	auto buf = BuildMailSendRequestPayload(1ull, subjectUtf8, bodyUtf8, 0ull, 0ull);
	auto parsed = ParseMailSendRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "UTF-8 parses");
	Assert(parsed && parsed->subject == subjectUtf8, "UTF-8 subject byte-perfect");
	Assert(parsed && parsed->body == bodyUtf8, "UTF-8 body byte-perfect");
}

static void TestBoundaryValues()
{
	// 0xFFFFFFFFFFFFFFFFull pour vérifier qu'on n'a pas de signe-extension cachée.
	auto buf = BuildMailSendRequestPayload(0xFFFFFFFFFFFFFFFFull, "", "",
	                                       0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull);
	auto parsed = ParseMailSendRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Boundary uint64 parses");
	if (parsed)
	{
		Assert(parsed->recipientAccountId == 0xFFFFFFFFFFFFFFFFull, "uint64 max recipient");
		Assert(parsed->copperGold == 0xFFFFFFFFFFFFFFFFull, "uint64 max gold");
		Assert(parsed->copperCod == 0xFFFFFFFFFFFFFFFFull, "uint64 max cod");
	}
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	// MAIL_SEND
	TestSendRequestRoundTrip();
	TestSendRequestEmptyStrings();
	TestSendRequestRejectsShort();
	TestSendResponseRoundTrip();
	TestSendResponsePacket();

	// MAIL_LIST_INBOX
	TestListInboxRequest();
	TestListInboxResponseEmpty();
	TestListInboxResponseUnauthorized();
	TestListInboxResponseMultiple();
	TestListInboxResponseRejectsShort();

	// MAIL_READ
	TestReadRequestRoundTrip();
	TestReadRequestRejectsShort();
	TestReadResponseRoundTrip();
	TestReadResponseError();

	// MAIL_TAKE_ATTACHMENTS
	TestTakeRequestRoundTrip();
	TestTakeRequestRejectsShort();
	TestTakeResponseRoundTrip();
	TestTakeResponseCodNotPaid();

	// MAIL_DELETE
	TestDeleteRequestRoundTrip();
	TestDeleteResponseRoundTrip();
	TestDeleteResponseHasAttachments();

	// Edge cases
	TestSendRequestUtf8();
	TestBoundaryValues();

	std::cerr << (s_failCount == 0 ? "[OK] all mail payload tests passed\n" : "[FAIL] some mail tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
