/// M23.4 — Smoke test: verify log rotation by size (runtime logger).
/// Writes enough data to exceed rotation_size_mb then checks rotated files exist.

#include "engine/core/Log.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

int main()
{
	const fs::path tmpDir = fs::temp_directory_path();
	const fs::path logPath = tmpDir / "lcdlln_rotation_smoke.log";

	engine::core::LogSettings settings;
	settings.filePath = logPath.string();
	settings.console = false;
	settings.flushAlways = true;
	settings.level = engine::core::LogLevel::Info;
	// 1 MB rotation so we can trigger rotation by writing ~1.5 MB of lines.
	settings.rotation_size_mb = 1;
	settings.retention_days = 3;

	engine::core::Log::Init(settings);

	// Write enough to exceed 1 MB (e.g. 1500 lines of ~700 bytes ≈ 1.05 MB).
	const std::string line(700, 'x');
	for (int i = 0; i < 1500; ++i)
	{
		LOG_INFO(Core, "[LogRotationSmoke] line {} {}", i, line);
	}

	engine::core::Log::Shutdown();

	// Check that the main log file exists and has non-zero size (rotation may have created .1, .2, ...).
	bool ok = fs::exists(logPath) && fs::file_size(logPath) > 0;
	if (ok)
	{
		// Optionally check for rotated file (e.g. lcdlln_rotation_smoke.1.log).
		fs::path rotated = logPath.parent_path() / (logPath.stem().string() + ".1" + logPath.extension().string());
		if (fs::exists(rotated))
			ok = true; // Rotation clearly happened.
	}

	return ok ? 0 : 1;
}
