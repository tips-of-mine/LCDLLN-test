// CMANGOS.18 (Phase 3.18a) — Tests MailManager workflow (Send/Take/Delete/COD/expire).

#include "engine/server/mail/InMemoryMailStore.h"
#include "engine/server/mail/MailManager.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::mail::InMemoryMailStore;
	using engine::server::mail::Mail;
	using engine::server::mail::MailItemAttachment;
	using engine::server::mail::MailManager;
	using engine::server::mail::MailManagerConfig;
	using engine::server::mail::MailOpResult;
	using engine::server::mail::MailState;

	bool TestSendBasic()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		const uint64_t id = mgr.Send(1, 2, "Hi", "Body", {}, 0, 0, 1000);
		if (id == 0) return false;
		if (store.Size() != 1) return false;

		auto inbox = mgr.Inbox(2);
		if (inbox.size() != 1) return false;
		if (inbox[0].subject != "Hi") return false;
		if (inbox[0].state != MailState::Unread) return false;
		// Expiration auto = 30 jours.
		if (inbox[0].expiresTsMs == 0) return false;
		LOG_INFO(Core, "[MailManagerTests] Send basic OK");
		return true;
	}

	bool TestSendValidation()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		MailOpResult err = MailOpResult::OK;

		// Subject trop long.
		std::string longSubject(300, 'A');
		mgr.Send(1, 2, longSubject, "body", {}, 0, 0, 1000, 0, &err);
		if (err != MailOpResult::SubjectTooLong) return false;

		// Body trop long.
		std::string longBody(20000, 'A');
		mgr.Send(1, 2, "ok", longBody, {}, 0, 0, 1000, 0, &err);
		if (err != MailOpResult::BodyTooLong) return false;

		// Trop d'attachments.
		std::vector<MailItemAttachment> tooMany(20);
		mgr.Send(1, 2, "ok", "ok", tooMany, 0, 0, 1000, 0, &err);
		if (err != MailOpResult::AttachmentsTooMany) return false;

		LOG_INFO(Core, "[MailManagerTests] validation OK");
		return true;
	}

	bool TestMarkRead()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		const uint64_t id = mgr.Send(1, 2, "Hi", "B", {}, 0, 0, 100);

		// Bon receiver.
		if (mgr.MarkRead(id, 2) != MailOpResult::OK) return false;
		auto inbox = mgr.Inbox(2);
		if (inbox[0].state != MailState::Read) return false;

		// Mauvais receiver.
		if (mgr.MarkRead(id, 999) != MailOpResult::WrongReceiver) return false;

		// Mail inexistant.
		if (mgr.MarkRead(99999, 2) != MailOpResult::MailNotFound) return false;

		LOG_INFO(Core, "[MailManagerTests] MarkRead OK");
		return true;
	}

	bool TestTakeItemsBasic()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		std::vector<MailItemAttachment> items{
			{42, 5}, {99, 1}
		};
		const uint64_t id = mgr.Send(1, 2, "loot", "enjoy", items, 0, 0, 100);

		std::vector<MailItemAttachment> out;
		if (mgr.TakeItems(id, 2, 0, out) != MailOpResult::OK) return false;
		if (out.size() != 2) return false;
		if (out[0].itemTemplateId != 42 || out[0].count != 5) return false;

		// Re-Take → AlreadyTaken (vide).
		if (mgr.TakeItems(id, 2, 0, out) != MailOpResult::AlreadyTaken) return false;

		// Mail marque Read.
		auto inbox = mgr.Inbox(2);
		if (inbox[0].state != MailState::Read) return false;
		LOG_INFO(Core, "[MailManagerTests] TakeItems basic OK");
		return true;
	}

	bool TestTakeItemsCodFlow()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		std::vector<MailItemAttachment> items{ {7, 1} };
		// COD = 100 copper required.
		const uint64_t id = mgr.Send(1, 2, "for sale", "100c", items, 0, 100, 1000);

		std::vector<MailItemAttachment> out;
		// Pas paye → CodNotPaid.
		if (mgr.TakeItems(id, 2, 0, out) != MailOpResult::CodNotPaid) return false;
		if (mgr.TakeItems(id, 2, 50, out) != MailOpResult::CodNotPaid) return false;

		// Paye exact → OK.
		if (mgr.TakeItems(id, 2, 100, out) != MailOpResult::OK) return false;
		if (out.size() != 1) return false;

		// Re-Take après paiement → AlreadyTaken (items vidés, pas de double-COD).
		if (mgr.TakeItems(id, 2, 100, out) != MailOpResult::AlreadyTaken) return false;
		LOG_INFO(Core, "[MailManagerTests] COD flow OK");
		return true;
	}

	bool TestTakeGold()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		const uint64_t id = mgr.Send(1, 2, "gift", "", {}, 5000, 0, 100);
		uint64_t copper = 0;
		if (mgr.TakeGold(id, 2, copper) != MailOpResult::OK) return false;
		if (copper != 5000) return false;
		// Re-take → AlreadyTaken.
		if (mgr.TakeGold(id, 2, copper) != MailOpResult::AlreadyTaken) return false;
		LOG_INFO(Core, "[MailManagerTests] TakeGold OK");
		return true;
	}

	bool TestDeleteRequiresEmpty()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		const uint64_t id = mgr.Send(1, 2, "x", "", {}, 100, 0, 100);
		// Gold pas encore retire → Delete refuse.
		if (mgr.Delete(id, 2) != MailOpResult::AlreadyTaken) return false;

		uint64_t copper = 0;
		mgr.TakeGold(id, 2, copper);
		// Maintenant on peut Delete.
		if (mgr.Delete(id, 2) != MailOpResult::OK) return false;
		if (store.Size() != 0) return false;
		LOG_INFO(Core, "[MailManagerTests] Delete requires empty OK");
		return true;
	}

	bool TestExpireDetection()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		mgr.Send(1, 2, "old", "", {}, 0, 0, 100, /*expiresTsMs=*/200);
		mgr.Send(1, 2, "new", "", {}, 0, 0, 1000, /*expiresTsMs=*/2000);

		// A nowMs=300 : seul le premier est expire.
		auto exp = mgr.ResolveExpired(300);
		if (exp.size() != 1) return false;

		// A nowMs=2500 : les deux sont expires.
		exp = mgr.ResolveExpired(2500);
		if (exp.size() != 2) return false;
		LOG_INFO(Core, "[MailManagerTests] Expire detection OK");
		return true;
	}

	bool TestInboxSorting()
	{
		InMemoryMailStore store;
		MailManager mgr(&store);
		mgr.Send(1, 2, "old", "", {}, 0, 0, /*nowMs=*/100);
		mgr.Send(1, 2, "mid", "", {}, 0, 0, /*nowMs=*/500);
		mgr.Send(1, 2, "new", "", {}, 0, 0, /*nowMs=*/900);

		auto inbox = mgr.Inbox(2);
		if (inbox.size() != 3) return false;
		if (inbox[0].subject != "new") return false;
		if (inbox[1].subject != "mid") return false;
		if (inbox[2].subject != "old") return false;
		LOG_INFO(Core, "[MailManagerTests] inbox sorted desc by sentTs OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestSendBasic()
		&& TestSendValidation()
		&& TestMarkRead()
		&& TestTakeItemsBasic()
		&& TestTakeItemsCodFlow()
		&& TestTakeGold()
		&& TestDeleteRequiresEmpty()
		&& TestExpireDetection()
		&& TestInboxSorting();

	if (ok)
		LOG_INFO(Core, "[MailManagerTests] ALL OK");
	else
		LOG_ERROR(Core, "[MailManagerTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
