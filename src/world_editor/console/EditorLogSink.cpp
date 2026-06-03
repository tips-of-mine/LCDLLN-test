#include "src/world_editor/console/EditorLogSink.h"

namespace engine::editor::world::console
{
	EditorLogSink& EditorLogSink::Instance()
	{
		static EditorLogSink s;
		return s;
	}

	void EditorLogSink::Push(engine::core::LogLevel level, const char* subsystem, std::string_view message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_entries.push_back(LogEntry{
			level,
			subsystem ? std::string(subsystem) : std::string("?"),
			std::string(message) });
		while (m_entries.size() > m_capacity)
		{
			m_entries.pop_front();
		}
	}

	std::vector<LogEntry> EditorLogSink::Snapshot(engine::core::LogLevel minLevel) const
	{
		std::vector<LogEntry> out;
		std::lock_guard<std::mutex> lock(m_mutex);
		out.reserve(m_entries.size());
		for (const LogEntry& e : m_entries)
		{
			if (e.level >= minLevel)
			{
				out.push_back(e);
			}
		}
		return out;
	}

	void EditorLogSink::Clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_entries.clear();
	}

	size_t EditorLogSink::Size() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_entries.size();
	}

	void EditorLogSink::SetCapacity(size_t cap)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_capacity = (cap == 0u) ? 1u : cap;
		while (m_entries.size() > m_capacity)
		{
			m_entries.pop_front();
		}
	}
}
