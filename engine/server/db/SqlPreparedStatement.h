#pragma once
// CMANGOS.13 (Phase 1a) — SqlPreparedStatement : wrapper C++20 autour de
// MYSQL_STMT avec binding type-safe et cache LRU par connexion.

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct MYSQL;
struct MYSQL_STMT;

namespace engine::server::db
{
	/// Wrapper RAII autour de `MYSQL_STMT`. Ne pas instancier directement —
	/// utiliser `SqlPreparedStatementCache::Acquire(mysql, sql)`.
	class SqlPreparedStatement
	{
	public:
		~SqlPreparedStatement();
		SqlPreparedStatement(const SqlPreparedStatement&) = delete;
		SqlPreparedStatement& operator=(const SqlPreparedStatement&) = delete;

		/// Bind un argument à la position \p pos (0-indexé). Surcharges pour les
		/// types courants. Doit être appelé avant Execute().
		bool Bind(size_t pos, int32_t value);
		bool Bind(size_t pos, int64_t value);
		bool Bind(size_t pos, uint32_t value);
		bool Bind(size_t pos, uint64_t value);
		bool Bind(size_t pos, double value);
		bool Bind(size_t pos, std::string_view value);
		/// Bind blob brut (la mémoire pointée doit rester valide jusqu'à Execute()).
		bool BindBlob(size_t pos, const void* data, size_t size);

		/// Exécute le statement. Retourne true si OK. Pour SELECT, voir FetchRow.
		bool Execute();

		/// Pour SELECT : récupère la prochaine ligne. Retourne false si plus de lignes
		/// (ou erreur). Les valeurs sont accessibles via GetInt/GetString après chaque
		/// FetchRow réussi.
		bool FetchRow();

		/// Lecteurs de colonne après FetchRow (0-indexés sur les colonnes du SELECT).
		int32_t  GetInt32(size_t col, int32_t fallback = 0) const;
		uint64_t GetUInt64(size_t col, uint64_t fallback = 0) const;
		std::string GetString(size_t col) const;

		/// Réinitialise les bindings et l'état pour une nouvelle exécution.
		/// Le statement reste valide, on peut Bind+Execute à nouveau.
		bool Reset();

		/// Accès au handle brut MYSQL_STMT (pour cas très spécifiques uniquement).
		MYSQL_STMT* Handle() const { return m_stmt; }

	private:
		// Construction via SqlPreparedStatementCache uniquement.
		friend class SqlPreparedStatementCache;
		SqlPreparedStatement(MYSQL_STMT* stmt, size_t paramCount, size_t resultColumnCount);

		MYSQL_STMT* m_stmt = nullptr;
		size_t m_paramCount = 0;
		size_t m_resultColumnCount = 0;
		// Buffers pour les bindings d'entrée (longueurs, types, données).
		// Stockés ici pour rester en vie jusqu'à Execute().
		std::vector<std::vector<uint8_t>> m_paramBuffers;
		std::vector<unsigned long> m_paramLengths;
		std::vector<char> m_paramIsNull;  // char car MYSQL_BIND::is_null veut un my_bool*
		// Type MYSQL pour chaque param (MYSQL_TYPE_LONG/LONGLONG/DOUBLE/STRING/BLOB).
		// Stocké comme int pour éviter d'inclure <mysql.h> dans le header public.
		std::vector<int> m_paramTypes;
		// Drapeau "non signé" pour les types entiers (MYSQL_BIND::is_unsigned).
		std::vector<char> m_paramIsUnsigned;
		// Buffers pour les colonnes de résultat (alloués au premier Execute).
		std::vector<std::vector<uint8_t>> m_resultBuffers;
		std::vector<unsigned long> m_resultLengths;
		std::vector<char> m_resultIsNull;
	};

	/// Cache LRU de SqlPreparedStatement par connexion MYSQL*.
	/// Thread-safety : un cache appartient à un seul thread (typiquement,
	/// un cache par worker DB). Pas de mutex interne.
	class SqlPreparedStatementCache
	{
	public:
		explicit SqlPreparedStatementCache(size_t maxEntries);
		~SqlPreparedStatementCache();
		SqlPreparedStatementCache(const SqlPreparedStatementCache&) = delete;
		SqlPreparedStatementCache& operator=(const SqlPreparedStatementCache&) = delete;

		/// Acquiert (ou crée) un statement pour \p sql sur \p mysql. Retourne
		/// nullptr si la préparation échoue. Le pointeur reste valide jusqu'à
		/// l'éviction LRU ou la destruction du cache.
		SqlPreparedStatement* Acquire(MYSQL* mysql, std::string_view sql);

		/// Nombre d'entrées actuellement dans le cache.
		size_t Size() const { return m_lru.size(); }

	private:
		struct Entry
		{
			std::string sql;
			std::unique_ptr<SqlPreparedStatement> stmt;
		};

		size_t m_maxEntries;
		std::list<Entry> m_lru;  // front = most recent, back = least recent
		std::unordered_map<std::string, std::list<Entry>::iterator> m_index;
	};
}
