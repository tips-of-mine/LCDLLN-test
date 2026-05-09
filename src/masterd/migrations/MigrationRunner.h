#pragma once

namespace engine::core
{
	class Config;
}

namespace engine::server
{
	/// Runs pending DB migrations at Master startup (connect, advisory lock, read schema_version, apply pending, update).
	/// Returns true on success or when DB is not configured; false on failure (checksum mismatch, apply error, lock timeout).
	/// Only built and used on UNIX (Linux) server; requires MySQL client library.
	bool MigrationRunnerRun(const engine::core::Config& config);
}
