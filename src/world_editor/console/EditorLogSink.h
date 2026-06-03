#pragma once

#include "src/shared/core/Log.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace engine::editor::world::console
{
	/// Une ligne de log capturée pour la Console in-app (sous-projet 1, bloc E).
	struct LogEntry
	{
		engine::core::LogLevel level = engine::core::LogLevel::Info;
		std::string subsystem;
		std::string message;
	};

	/// Tampon circulaire **thread-safe** des dernières lignes de log, pour la
	/// Console de l'éditeur monde. Singleton à l'échelle du processus : alimenté
	/// par le sink global (`Log::SetSink`, posé au boot éditeur), lu par le
	/// `ConsolePanel`. Volontairement découplé de la durée de vie du Shell
	/// (aucun dangling possible si le Shell est détruit avant le dernier log).
	class EditorLogSink
	{
	public:
		/// Instance unique du processus.
		static EditorLogSink& Instance();

		/// Ajoute une ligne. Appelé depuis n'importe quel thread (via le sink
		/// Log) — protégé par mutex. Évince les plus anciennes au-delà de la
		/// capacité.
		void Push(engine::core::LogLevel level, const char* subsystem, std::string_view message);

		/// Copie les lignes de niveau >= `minLevel`, ordre chronologique (plus
		/// ancienne en premier). Thread-safe.
		std::vector<LogEntry> Snapshot(engine::core::LogLevel minLevel) const;

		/// Vide le tampon. Thread-safe.
		void Clear();

		/// Nombre courant de lignes (tests / diagnostic). Thread-safe.
		size_t Size() const;

		/// Capacité max (lignes ; min 1). Évince immédiatement le surplus.
		void SetCapacity(size_t cap);

	private:
		EditorLogSink() = default;

		mutable std::mutex   m_mutex;
		std::deque<LogEntry> m_entries;
		size_t               m_capacity = 1000;
	};
}
