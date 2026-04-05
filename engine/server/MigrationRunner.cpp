// M21.3 — Master applies pending migrations on startup.
// Connect to MySQL, take advisory lock, read schema_version, verify checksums, apply pending migrations, update schema_version.

#include "engine/server/MigrationRunner.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <openssl/sha.h>

#include <mysql.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	const char* kLockName = "lcdlln_master_migrations";
	const int kLockTimeoutSec = 10;

	/// Computes SHA-256 hex (64 chars) of file content. Returns empty string on read error.
	std::string Sha256HexFromFile(const fs::path& path)
	{
		std::string content = engine::platform::FileSystem::ReadAllText(path);
		if (content.empty())
			return {};
		SHA256_CTX ctx;
		SHA256_Init(&ctx);
		SHA256_Update(&ctx, content.data(), content.size());
		unsigned char hash[SHA256_DIGEST_LENGTH];
		SHA256_Final(hash, &ctx);
		std::ostringstream os;
		os << std::hex << std::setfill('0');
		for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
			os << std::setw(2) << static_cast<unsigned>(hash[i]);
		return os.str();
	}

	/// Extracts version number from filename NNNN_name.sql. Returns -1 if invalid.
	int VersionFromFilename(const std::string& name)
	{
		if (name.size() < 5 || name[4] != '_')
			return -1;
		int v = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (name[i] < '0' || name[i] > '9')
				return -1;
			v = v * 10 + (name[i] - '0');
		}
		return v;
	}

	/// Drains all result sets after a multi-statement query (required by MySQL C API).
	bool DrainResults(MYSQL* mysql)
	{
		for (;;)
		{
			MYSQL_RES* res = mysql_store_result(mysql);
			if (res)
				mysql_free_result(res);
			if (mysql_errno(mysql) != 0)
				return false;
			if (!mysql_more_results(mysql))
				break;
			if (mysql_next_result(mysql) != 0)
				return false;
		}
		return true;
	}
}

namespace engine::server
{
	bool MigrationRunnerRun(const engine::core::Config& config)
	{
		std::string host = config.GetString("db.host", "");
		if (host.empty())
		{
			LOG_WARN(Core, "[MigrationRunner] DB not configured (db.host empty), skipping migrations");
			return true;
		}

		unsigned int port = static_cast<unsigned int>(config.GetInt("db.port", 3306));
		std::string user = config.GetString("db.user", "");
		std::string password = config.GetString("db.password", "");
		std::string database = config.GetString("db.database", "lcdlln_master");
		std::string migrationsPath = config.GetString("db.migrations_path", "db/migrations");

		MYSQL* mysql = mysql_init(nullptr);
		if (!mysql)
		{
			LOG_ERROR(Core, "[MigrationRunner] mysql_init failed");
			return false;
		}

		bool reconnect = false;
		mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);

		if (!mysql_real_connect(mysql, host.c_str(), user.c_str(), password.empty() ? nullptr : password.c_str(),
				database.c_str(), port, nullptr, CLIENT_MULTI_STATEMENTS))
		{
			LOG_ERROR(Core, "[MigrationRunner] Connect failed: {}", mysql_error(mysql));
			mysql_close(mysql);
			return false;
		}
		LOG_INFO(Core, "[MigrationRunner] Connected to {}:{}/{}", host, port, database);

		// Advisory lock
		std::string lockQuery = "SELECT GET_LOCK('";
		lockQuery += kLockName;
		lockQuery += "', ";
		lockQuery += std::to_string(kLockTimeoutSec);
		lockQuery += ")";
		if (mysql_real_query(mysql, lockQuery.c_str(), static_cast<unsigned long>(lockQuery.size())) != 0)
		{
			LOG_ERROR(Core, "[MigrationRunner] GET_LOCK query failed: {}", mysql_error(mysql));
			mysql_close(mysql);
			return false;
		}
		MYSQL_RES* lockRes = mysql_store_result(mysql);
		if (!lockRes)
		{
			LOG_ERROR(Core, "[MigrationRunner] GET_LOCK store result failed: {}", mysql_error(mysql));
			mysql_close(mysql);
			return false;
		}
		MYSQL_ROW lockRow = mysql_fetch_row(lockRes);
		mysql_free_result(lockRes);
		if (!lockRow || !lockRow[0] || (lockRow[0][0] != '1'))
		{
			LOG_ERROR(Core, "[MigrationRunner] GET_LOCK failed (timeout or error), another Master may be running migrations");
			mysql_close(mysql);
			return false;
		}
		LOG_INFO(Core, "[MigrationRunner] Advisory lock acquired");

		// Read schema_version (if table exists)
		std::vector<std::pair<int, std::string>> appliedVersions;
		const char* selectVersion = "SELECT version, checksum FROM schema_version ORDER BY version";
		if (mysql_real_query(mysql, selectVersion, static_cast<unsigned long>(strlen(selectVersion))) != 0)
		{
			unsigned int err = mysql_errno(mysql);
			if (err == 1146) // ER_NO_SUCH_TABLE
			{
				LOG_INFO(Core, "[MigrationRunner] schema_version table missing, current version=0");
			}
			else
			{
				LOG_ERROR(Core, "[MigrationRunner] SELECT schema_version failed: {} ({})", mysql_error(mysql), err);
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
		}
		else
		{
			MYSQL_RES* res = mysql_store_result(mysql);
			if (res)
			{
				MYSQL_ROW row;
				while ((row = mysql_fetch_row(res)))
				{
					if (row[0] && row[1])
					{
						int ver = std::atoi(row[0]);
						appliedVersions.emplace_back(ver, row[1]);
					}
				}
				mysql_free_result(res);
				LOG_INFO(Core, "[MigrationRunner] Current schema version: {} applied", appliedVersions.empty() ? 0 : appliedVersions.back().first);
			}
		}

		// List migration files (relative path from CWD)
		fs::path migrationsDir(migrationsPath);
		if (!fs::is_directory(migrationsDir))
		{
			LOG_ERROR(Core, "[MigrationRunner] Migrations directory not found: {}", migrationsPath);
			mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
			mysql_close(mysql);
			return false;
		}
		std::vector<std::pair<int, fs::path>> files;
		for (const auto& e : fs::directory_iterator(migrationsDir))
		{
			if (e.path().extension() != ".sql")
				continue;
			std::string name = e.path().filename().string();
			int version = VersionFromFilename(name);
			if (version >= 0)
				files.emplace_back(version, e.path());
		}
		std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

		// Verify checksums for all applied versions
		for (const auto& [version, expectedChecksum] : appliedVersions)
		{
			auto it = std::find_if(files.begin(), files.end(), [version](const auto& p) { return p.first == version; });
			if (it == files.end())
			{
				LOG_ERROR(Core, "[MigrationRunner] Applied version {} has no matching file in {}", version, migrationsPath);
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
			std::string computed = Sha256HexFromFile(it->second);
			if (computed.empty())
			{
				LOG_ERROR(Core, "[MigrationRunner] Failed to read migration file: {}", it->second.generic_string());
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
			if (computed != expectedChecksum)
			{
				// Anciens schema.sql Docker / init inséraient ce checksum factice pour la v1 ; le volume MySQL
				// ne repasse pas par docker-entrypoint-initdb.d. On aligne une seule fois sur le fichier actuel.
				static constexpr std::string_view kLegacyV1Placeholder =
					"0000000000000000000000000000000000000000000000000000000000000001";
				if (version == 1 && expectedChecksum == kLegacyV1Placeholder)
				{
					char escaped[160];
					mysql_real_escape_string(mysql, escaped, computed.c_str(), static_cast<unsigned long>(computed.size()));
					std::string fixSql = "UPDATE schema_version SET checksum='";
					fixSql += escaped;
					fixSql += "' WHERE version=1";
					if (mysql_real_query(mysql, fixSql.c_str(), static_cast<unsigned long>(fixSql.size())) != 0)
					{
						LOG_ERROR(Core, "[MigrationRunner] Échec réparation checksum v1 : {}", mysql_error(mysql));
						mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
						mysql_close(mysql);
						return false;
					}
					if (!DrainResults(mysql))
					{
						LOG_ERROR(Core, "[MigrationRunner] Drain après réparation checksum v1 : {}", mysql_error(mysql));
						mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
						mysql_close(mysql);
						return false;
					}
					LOG_WARN(Core,
						"[MigrationRunner] Checksum v1 corrigé (placeholder historique → SHA-256 actuel de {})",
						it->second.filename().string());
					continue;
				}
				LOG_ERROR(Core,
					"[MigrationRunner] Checksum mismatch for version {} (file {}): en base='{}', fichier='{}'. "
					"(voir deploy/docker/repair-schema-checksum.sh ou README si migration légitime)",
					version,
					it->second.generic_string(),
					expectedChecksum,
					computed);
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
		}
		int maxApplied = appliedVersions.empty() ? 0 : appliedVersions.back().first;

		// Apply pending migrations
		for (const auto& [version, path] : files)
		{
			if (version <= maxApplied)
				continue;
			std::string sql = engine::platform::FileSystem::ReadAllText(path);
			if (sql.empty())
			{
				LOG_ERROR(Core, "[MigrationRunner] Empty or unreadable file: {}", path.generic_string());
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
			if (mysql_real_query(mysql, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
			{
				LOG_ERROR(Core, "[MigrationRunner] Execute migration {} failed: {}", version, mysql_error(mysql));
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
			if (!DrainResults(mysql))
			{
				LOG_ERROR(Core, "[MigrationRunner] Drain results after migration {} failed: {}", version, mysql_error(mysql));
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
			std::string checksum = Sha256HexFromFile(path);
			if (checksum.empty())
			{
				LOG_ERROR(Core, "[MigrationRunner] Sha256 for version {} failed", version);
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
			char checksumEscaped[128];
			mysql_real_escape_string(mysql, checksumEscaped, checksum.c_str(), static_cast<unsigned long>(checksum.size()));
			std::string insertSql = "INSERT INTO schema_version (version, applied_at, checksum) VALUES (";
			insertSql += std::to_string(version);
			insertSql += ", CURRENT_TIMESTAMP, '";
			insertSql += checksumEscaped;
			insertSql += "')";
			if (mysql_real_query(mysql, insertSql.c_str(), static_cast<unsigned long>(insertSql.size())) != 0)
			{
				LOG_ERROR(Core, "[MigrationRunner] INSERT schema_version for {} failed: {}", version, mysql_error(mysql));
				mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
				mysql_close(mysql);
				return false;
			}
			DrainResults(mysql);
			LOG_INFO(Core, "[MigrationRunner] Applied migration {} ({})", version, path.filename().string());
		}

		mysql_query(mysql, "SELECT RELEASE_LOCK('lcdlln_master_migrations')");
		mysql_close(mysql);
		LOG_INFO(Core, "[MigrationRunner] Migrations complete, lock released");
		return true;
	}
}
