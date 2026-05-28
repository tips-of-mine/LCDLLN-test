#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <algorithm>
#include <cassert>
#include <cstring>

// Phase 1a code review #4 : MYSQL_BIND::is_null veut un bool* sur libmysqlclient
// récent. On stocke en std::vector<char> pour stabilité ABI ; ce static_assert
// documente que la conversion reinterpret_cast<bool*> est safe (1 byte both).
static_assert(sizeof(bool) == sizeof(char),
	"SqlPreparedStatement requires sizeof(bool) == sizeof(char) for is_null bridge");

namespace engine::server::db
{
	namespace
	{
		MYSQL_STMT* PrepareStatement(MYSQL* mysql, std::string_view sql)
		{
			MYSQL_STMT* stmt = mysql_stmt_init(mysql);
			if (!stmt)
				return nullptr;
			if (mysql_stmt_prepare(stmt, sql.data(), static_cast<unsigned long>(sql.size())) != 0)
			{
				mysql_stmt_close(stmt);
				return nullptr;
			}
			return stmt;
		}
	}

	SqlPreparedStatement::SqlPreparedStatement(MYSQL_STMT* stmt, size_t paramCount, size_t resultColumnCount)
		: m_stmt(stmt)
		, m_paramCount(paramCount)
		, m_resultColumnCount(resultColumnCount)
	{
		if (paramCount > 0)
		{
			m_paramBuffers.resize(paramCount);
			m_paramLengths.assign(paramCount, 0);
			m_paramIsNull.assign(paramCount, 0);
			m_paramTypes.assign(paramCount, MYSQL_TYPE_NULL);
			m_paramIsUnsigned.assign(paramCount, 0);
		}
		if (resultColumnCount > 0)
		{
			m_resultBuffers.assign(resultColumnCount, std::vector<uint8_t>(256));
			m_resultLengths.assign(resultColumnCount, 0);
			m_resultIsNull.assign(resultColumnCount, 0);
		}
	}

	SqlPreparedStatement::~SqlPreparedStatement()
	{
		if (m_stmt)
		{
			mysql_stmt_close(m_stmt);
			m_stmt = nullptr;
		}
	}

	bool SqlPreparedStatement::Bind(size_t pos, int32_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(int32_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(int32_t));
		m_paramLengths[pos] = sizeof(int32_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONG;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, int64_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(int64_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(int64_t));
		m_paramLengths[pos] = sizeof(int64_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONGLONG;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, uint32_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(uint32_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(uint32_t));
		m_paramLengths[pos] = sizeof(uint32_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONG;
		m_paramIsUnsigned[pos] = 1;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, uint64_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(uint64_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(uint64_t));
		m_paramLengths[pos] = sizeof(uint64_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONGLONG;
		m_paramIsUnsigned[pos] = 1;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, double value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(double));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(double));
		m_paramLengths[pos] = sizeof(double);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_DOUBLE;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, std::string_view value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].assign(value.begin(), value.end());
		m_paramLengths[pos] = static_cast<unsigned long>(value.size());
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_STRING;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::BindBlob(size_t pos, const void* data, size_t size)
	{
		if (pos >= m_paramCount)
			return false;
		const auto* p = static_cast<const uint8_t*>(data);
		m_paramBuffers[pos].assign(p, p + size);
		m_paramLengths[pos] = static_cast<unsigned long>(size);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_BLOB;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Execute()
	{
		if (!m_stmt)
			return false;

		// Bind input parameters — utilise m_paramTypes (set par chaque Bind*) pour
		// éviter l'heuristique sur la taille (qui confondait double/int64 sur 8 bytes).
		if (m_paramCount > 0)
		{
			std::vector<MYSQL_BIND> binds(m_paramCount);
			std::memset(binds.data(), 0, sizeof(MYSQL_BIND) * m_paramCount);
			for (size_t i = 0; i < m_paramCount; ++i)
			{
				binds[i].buffer = m_paramBuffers[i].data();
				binds[i].buffer_length = static_cast<unsigned long>(m_paramBuffers[i].size());
				binds[i].length = &m_paramLengths[i];
				binds[i].buffer_type = static_cast<enum_field_types>(m_paramTypes[i]);
				binds[i].is_unsigned = m_paramIsUnsigned[i] != 0;
				binds[i].is_null = reinterpret_cast<bool*>(&m_paramIsNull[i]);
			}
			if (mysql_stmt_bind_param(m_stmt, binds.data()) != 0)
				return false;
		}

		// Bind output columns (pour SELECT).
		if (m_resultColumnCount > 0)
		{
			std::vector<MYSQL_BIND> rbinds(m_resultColumnCount);
			std::memset(rbinds.data(), 0, sizeof(MYSQL_BIND) * m_resultColumnCount);
			for (size_t i = 0; i < m_resultColumnCount; ++i)
			{
				rbinds[i].buffer = m_resultBuffers[i].data();
				rbinds[i].buffer_length = static_cast<unsigned long>(m_resultBuffers[i].size());
				rbinds[i].length = &m_resultLengths[i];
				rbinds[i].is_null = reinterpret_cast<bool*>(&m_resultIsNull[i]);
				rbinds[i].buffer_type = MYSQL_TYPE_STRING;  // simpliste, on lit en string
			}
			if (mysql_stmt_bind_result(m_stmt, rbinds.data()) != 0)
				return false;
		}

		return mysql_stmt_execute(m_stmt) == 0;
	}

	bool SqlPreparedStatement::FetchRow()
	{
		if (!m_stmt)
			return false;
		const int rc = mysql_stmt_fetch(m_stmt);
		return rc == 0;  // 0 = OK, MYSQL_NO_DATA = fin, autres = erreur
	}

	int32_t SqlPreparedStatement::GetInt32(size_t col, int32_t fallback) const
	{
		if (col >= m_resultColumnCount || m_resultIsNull[col])
			return fallback;
		// Le buffer contient une string ASCII de l'entier (MYSQL_TYPE_STRING).
		const auto& buf = m_resultBuffers[col];
		const unsigned long len = std::min(m_resultLengths[col], static_cast<unsigned long>(buf.size()));
		const std::string s(reinterpret_cast<const char*>(buf.data()), len);
		return std::atoi(s.c_str());
	}

	uint64_t SqlPreparedStatement::GetUInt64(size_t col, uint64_t fallback) const
	{
		if (col >= m_resultColumnCount || m_resultIsNull[col])
			return fallback;
		const auto& buf = m_resultBuffers[col];
		const unsigned long len = std::min(m_resultLengths[col], static_cast<unsigned long>(buf.size()));
		const std::string s(reinterpret_cast<const char*>(buf.data()), len);
		return std::strtoull(s.c_str(), nullptr, 10);
	}

	std::string SqlPreparedStatement::GetString(size_t col) const
	{
		if (col >= m_resultColumnCount || m_resultIsNull[col])
			return {};
		const auto& buf = m_resultBuffers[col];
		const unsigned long len = std::min(m_resultLengths[col], static_cast<unsigned long>(buf.size()));
		return std::string(reinterpret_cast<const char*>(buf.data()), len);
	}

	bool SqlPreparedStatement::Reset()
	{
		if (!m_stmt)
			return false;
		// Vide le résultat en cours (si SELECT en boucle).
		mysql_stmt_free_result(m_stmt);
		return mysql_stmt_reset(m_stmt) == 0;
	}

	// ─── Cache ───

	SqlPreparedStatementCache::SqlPreparedStatementCache(size_t maxEntries)
		: m_maxEntries(maxEntries)
	{
		// CMANGOS.13 review #11 : maxEntries=0 ferait croître le cache sans borne.
		assert(maxEntries > 0 && "SqlPreparedStatementCache requires maxEntries >= 1");
	}

	SqlPreparedStatementCache::~SqlPreparedStatementCache() = default;

	SqlPreparedStatement* SqlPreparedStatementCache::Acquire(MYSQL* mysql, std::string_view sql)
	{
		const std::string key(sql);

		// Hit : déplacer en tête (most recent) et retourner.
		auto it = m_index.find(key);
		if (it != m_index.end())
		{
			m_lru.splice(m_lru.begin(), m_lru, it->second);
			it->second = m_lru.begin();
			// Reset() avant retour : sans ça, une 2e exécution du même stmt après
			// un FetchRow() consommé peut échouer (libmysql conserve l'état du
			// résultat précédent jusqu'à mysql_stmt_free_result/mysql_stmt_reset).
			// Reset() est no-op sur un stmt fraîchement préparé jamais exécuté.
			// Si Reset() échoue (connexion morte / stmt corrompu), on retourne
			// nullptr — le call site sait que l'acquisition a échoué.
			if (!it->second->stmt->Reset())
				return nullptr;
			return it->second->stmt.get();
		}

		// Miss : préparer un nouveau statement.
		MYSQL_STMT* stmt = PrepareStatement(mysql, sql);
		if (!stmt)
			return nullptr;
		const size_t paramCount = mysql_stmt_param_count(stmt);
		MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
		const size_t resultColumnCount = meta ? mysql_num_fields(meta) : 0;
		if (meta)
			mysql_free_result(meta);

		// Eviction LRU si plein.
		if (m_lru.size() >= m_maxEntries && !m_lru.empty())
		{
			m_index.erase(m_lru.back().sql);
			m_lru.pop_back();
		}

		Entry entry{key, std::unique_ptr<SqlPreparedStatement>(
			new SqlPreparedStatement(stmt, paramCount, resultColumnCount))};
		m_lru.push_front(std::move(entry));
		m_index[key] = m_lru.begin();
		return m_lru.begin()->stmt.get();
	}
}
