#include "src/masterd/gmtickets/GmTicketSystem.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::gmtickets::GmTicketSystem;
	using engine::server::gmtickets::TicketState;

	bool TestOpenAssignResolve()
	{
		GmTicketSystem s;
		const auto id = s.Open(1, "stuck under map", 100);
		if (id == 0) return false;
		auto t = s.Find(id);
		if (!t || t->state != TicketState::Open) return false;
		if (!s.Assign(id, 999)) return false;
		t = s.Find(id);
		if (t->state != TicketState::Assigned || t->assignedGm != 999) return false;
		if (!s.Resolve(id, 200)) return false;
		t = s.Find(id);
		if (t->state != TicketState::Resolved || t->resolvedTsMs != 200) return false;
		LOG_INFO(Core, "[GmTicketSystemTests] open/assign/resolve OK");
		return true;
	}

	bool TestQueue()
	{
		GmTicketSystem s;
		const auto a = s.Open(1, "a", 100);
		const auto b = s.Open(2, "b", 200);
		const auto c = s.Open(3, "c", 300);
		s.Assign(a, 999);  // a out of queue
		auto q = s.OpenQueue();
		if (q.size() != 2) return false;
		LOG_INFO(Core, "[GmTicketSystemTests] queue OK");
		(void)b; (void)c;
		return true;
	}

	bool TestCancel()
	{
		GmTicketSystem s;
		const auto id = s.Open(1, "wrong", 100);
		if (!s.Cancel(id)) return false;
		if (s.Find(id)->state != TicketState::Cancelled) return false;
		// Cannot resolve a cancelled ticket.
		if (s.Resolve(id, 200)) return false;
		LOG_INFO(Core, "[GmTicketSystemTests] cancel OK");
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

	const bool ok = TestOpenAssignResolve() && TestQueue() && TestCancel();
	if (ok) LOG_INFO(Core, "[GmTicketSystemTests] ALL OK");
	else LOG_ERROR(Core, "[GmTicketSystemTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
