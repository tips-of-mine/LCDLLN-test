// BirthdayEmailJob — implémentation. Voir le header.

#include "src/masterd/anniversary/BirthdayEmailJob.h"

#include "src/masterd/email/LocalizedEmail.h"
#include "src/masterd/email/SmtpMailer.h"
#include "src/masterd/exploits/MysqlExploitStore.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

#include <cstdio>
#include <thread>
#include <vector>

namespace engine::server
{
	namespace
	{
		/// Un envoi planifié (données copiées : le thread SMTP survit au scope).
		struct PlannedEmail
		{
			std::string to;
			std::string subject;
			std::string body;
			bool isHtml = false;
		};

		/// "mm-dd" zero-paddé du jour fêté.
		std::string MonthDayKey(int month, int day)
		{
			char buf[8];
			std::snprintf(buf, sizeof(buf), "%02d-%02d", month, day);
			return std::string(buf);
		}
	}

	void BirthdayEmailJob::Tick()
	{
		const engine::anniversary::YmdDate today = engine::anniversary::TodayUtc();
		char dayKey[16];
		std::snprintf(dayKey, sizeof(dayKey), "%04d-%02d-%02d", today.year, today.month, today.day);
		if (m_lastProcessedDay == dayKey)
			return;
		m_lastProcessedDay = dayKey;
		const size_t planned = RunForDay(today);
		LOG_INFO(Db, "[BirthdayEmailJob] jour {} traité : {} e-mail(s) d'anniversaire planifié(s)",
			dayKey, planned);
	}

	size_t BirthdayEmailJob::RunForDay(const engine::anniversary::YmdDate& todayUtc)
	{
		if (m_pool == nullptr || m_exploits == nullptr || !m_exploits->IsAvailable())
			return 0; // mode no-DB : silencieux.
		if (m_smtp == nullptr)
			return 0;

		// Clés jour/mois fêtées aujourd'hui : la date du jour, plus le 29/02
		// quand nous sommes le 28/02 d'une année non bissextile.
		const std::string key1 = MonthDayKey(todayUtc.month, todayUtc.day);
		std::string key2 = key1;
		if (todayUtc.month == 2 && todayUtc.day == 28
			&& !engine::anniversary::IsLeapYear(todayUtc.year))
		{
			key2 = MonthDayKey(2, 29);
		}

		// Sélection des comptes fêtés : e-mail VÉRIFIÉ + birth_date du jour.
		// birth_date est "yyyy-mm-dd" strict (validée à l'inscription) :
		// SUBSTR(...,6,5) = "mm-dd". L'année de naissance == année courante
		// est exclue (inscription du jour, pas un anniversaire).
		struct Row { uint64_t accountId; std::string email; std::string firstName; uint32_t locale; };
		std::vector<Row> rows;
		{
			auto guard = m_pool->Acquire();
			MYSQL* mysql = guard.get();
			auto* cache = guard.cache();
			if (!mysql || !cache) return 0;
			auto* stmt = cache->Acquire(mysql,
				"SELECT id, email, COALESCE(first_name, ''), email_locale FROM accounts "
				"WHERE email_verified = 1 "
				"  AND birth_date IS NOT NULL AND birth_date != '' "
				"  AND (SUBSTR(birth_date, 6, 5) = ? OR SUBSTR(birth_date, 6, 5) = ?) "
				"  AND SUBSTR(birth_date, 1, 4) != ?");
			char yearStr[8];
			std::snprintf(yearStr, sizeof(yearStr), "%04d", todayUtc.year);
			if (!stmt
				|| !stmt->Bind(0, std::string_view(key1))
				|| !stmt->Bind(1, std::string_view(key2))
				|| !stmt->Bind(2, std::string_view(yearStr))
				|| !stmt->Execute())
			{
				LOG_WARN(Db, "[BirthdayEmailJob] SELECT anniversaires échoué : {}", mysql_error(mysql));
				return 0;
			}
			while (stmt->FetchRow())
			{
				Row r;
				r.accountId = stmt->GetUInt64(0);
				r.email = stmt->GetString(1);
				r.firstName = stmt->GetString(2);
				r.locale = static_cast<uint32_t>(stmt->GetUInt64(3));
				if (!r.email.empty())
					rows.push_back(std::move(r));
			}
		}
		if (rows.empty())
			return 0;

		// Garde annuelle par compte (idempotente inter-reboots) puis
		// construction du message personnalisé (prénom + langue du compte).
		std::vector<PlannedEmail> toSend;
		toSend.reserve(rows.size());
		const uint16_t year = static_cast<uint16_t>(todayUtc.year);
		for (const Row& r : rows)
		{
			if (!m_exploits->TryClaimAnnualReward(r.accountId, year, "birthday_email"))
				continue; // déjà envoyé cette année.
			AccountEmailLocale loc = AccountEmailLocale::English;
			if (r.locale <= static_cast<uint32_t>(AccountEmailLocale::Italian))
				loc = static_cast<AccountEmailLocale>(r.locale);
			PlannedEmail mail;
			mail.to = r.email;
			BuildBirthdayEmail(loc, r.firstName, mail.subject, mail.body, mail.isHtml);
			toSend.push_back(std::move(mail));
		}
		if (toSend.empty())
			return 0;

		// SMTP indisponible (dev local sans smtp.local.json) : les gardes sont
		// prises (pas de rattrapage) mais on le dit clairement dans les logs.
		if (m_smtp->host.empty() || m_smtp->port == 0)
		{
			LOG_WARN(Db, "[BirthdayEmailJob] SMTP non configuré : {} e-mail(s) d'anniversaire NON envoyés",
				toSend.size());
			return 0;
		}

		// Envois sur un thread détaché : SmtpMailer::Send est thread-safe
		// (connexion par appel) et le réseau ne doit pas bloquer la boucle
		// principale du master. Données copiées (SmtpConfig inclus).
		const SmtpConfig smtpCopy = *m_smtp;
		std::thread([smtpCopy, batch = std::move(toSend)]()
		{
			size_t ok = 0;
			for (const PlannedEmail& m : batch)
			{
				if (SmtpMailer::Send(smtpCopy, m.to, m.subject, m.body, m.isHtml))
					++ok;
			}
			LOG_INFO(Db, "[BirthdayEmailJob] envois SMTP terminés : {}/{} réussis", ok, batch.size());
		}).detach();
		return toSend.size();
	}
}
