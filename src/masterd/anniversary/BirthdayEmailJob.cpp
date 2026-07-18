// BirthdayEmailJob — implémentation. Voir le header (envoi à partir de 7 h
// locales, fuseau approximé par pays).

#include "src/masterd/anniversary/BirthdayEmailJob.h"

#include "src/masterd/anniversary/CountryUtcOffset.h"
#include "src/masterd/email/LocalizedEmail.h"
#include "src/masterd/email/SmtpMailer.h"
#include "src/masterd/exploits/MysqlExploitStore.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

#include <cstdio>
#include <ctime>
#include <string>
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

		/// "mm-dd" zero-paddé.
		std::string MonthDayKey(int month, int day)
		{
			char buf[8];
			std::snprintf(buf, sizeof(buf), "%02d-%02d", month, day);
			return std::string(buf);
		}

		/// Ajoute à \p keys les clés "mm-dd" fêtées le jour civil \p day :
		/// la date elle-même, plus "02-29" si \p day est un 28/02 d'année non
		/// bissextile (les natifs du 29/02 y sont fêtés). Dédoublonné.
		void AppendCelebratedKeys(const engine::anniversary::YmdDate& day,
			std::vector<std::string>& keys)
		{
			auto push = [&keys](std::string k)
			{
				for (const std::string& existing : keys)
					if (existing == k) return;
				keys.push_back(std::move(k));
			};
			push(MonthDayKey(day.month, day.day));
			if (day.month == 2 && day.day == 28 && !engine::anniversary::IsLeapYear(day.year))
				push(MonthDayKey(2, 29));
		}
	}

	void BirthdayEmailJob::Tick()
	{
		const size_t planned = RunAtInstant(static_cast<int64_t>(std::time(nullptr)));
		if (planned > 0)
		{
			LOG_INFO(Db, "[BirthdayEmailJob] {} e-mail(s) d'anniversaire planifié(s) (>= {} h locales)",
				planned, kSendFromLocalHour);
		}
	}

	size_t BirthdayEmailJob::RunAtInstant(int64_t utcEpochSeconds)
	{
		if (m_pool == nullptr || m_exploits == nullptr || !m_exploits->IsAvailable())
			return 0; // mode no-DB : silencieux.
		if (m_smtp == nullptr)
			return 0;

		// Fenêtre candidate : la date locale d'un compte est la date UTC ± 1
		// jour selon son fuseau (UTC-12..UTC+14). On sélectionne donc les
		// jours/mois fêtés sur J-1, J et J+1 UTC, puis on tranche PAR COMPTE
		// en heure locale.
		engine::anniversary::YmdDate utcDate{};
		int utcHour = 0;
		engine::anniversary::LocalCivilFromUtc(utcEpochSeconds, 0, utcDate, utcHour);
		std::vector<std::string> keys;
		AppendCelebratedKeys(engine::anniversary::AddDays(utcDate, -1), keys);
		AppendCelebratedKeys(utcDate, keys);
		AppendCelebratedKeys(engine::anniversary::AddDays(utcDate, 1), keys);
		while (keys.size() < 4) keys.push_back(keys.front()); // 4 binds fixes

		// Sélection des comptes candidats : e-mail VÉRIFIÉ + birth_date dans
		// la fenêtre. birth_date est "yyyy-mm-dd" strict (validée à
		// l'inscription) : SUBSTR(...,6,5) = "mm-dd".
		struct Row
		{
			uint64_t accountId; std::string email; std::string firstName;
			uint32_t locale; std::string countryCode; std::string birthDate;
		};
		std::vector<Row> rows;
		{
			auto guard = m_pool->Acquire();
			MYSQL* mysql = guard.get();
			auto* cache = guard.cache();
			if (!mysql || !cache) return 0;
			auto* stmt = cache->Acquire(mysql,
				"SELECT id, email, COALESCE(first_name, ''), email_locale, "
				"COALESCE(country_code, ''), birth_date FROM accounts "
				"WHERE email_verified = 1 "
				"  AND birth_date IS NOT NULL AND birth_date != '' "
				"  AND SUBSTR(birth_date, 6, 5) IN (?, ?, ?, ?)");
			if (!stmt
				|| !stmt->Bind(0, std::string_view(keys[0]))
				|| !stmt->Bind(1, std::string_view(keys[1]))
				|| !stmt->Bind(2, std::string_view(keys[2]))
				|| !stmt->Bind(3, std::string_view(keys[3]))
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
				r.countryCode = stmt->GetString(4);
				r.birthDate = stmt->GetString(5);
				if (!r.email.empty())
					rows.push_back(std::move(r));
			}
		}
		if (rows.empty())
			return 0;

		// Tranche PAR COMPTE en heure locale : jour J local atteint ET
		// horloge locale >= 7 h. Garde annuelle sur l'ANNÉE LOCALE (le jour J
		// d'un joueur UTC+12 peut commencer la veille en UTC).
		std::vector<PlannedEmail> toSend;
		for (const Row& r : rows)
		{
			engine::anniversary::YmdDate birth{};
			if (!engine::anniversary::ParseYmd(r.birthDate, birth))
				continue;
			const int offsetMin = UtcOffsetMinutesForCountry(r.countryCode);
			engine::anniversary::YmdDate localDate{};
			int localHour = 0;
			engine::anniversary::LocalCivilFromUtc(utcEpochSeconds, offsetMin, localDate, localHour);
			if (!engine::anniversary::IsAnniversaryDay(birth, localDate))
				continue;
			if (localHour < kSendFromLocalHour)
				continue; // trop tôt chez lui — repassera au prochain Tick.
			if (!m_exploits->TryClaimAnnualReward(r.accountId,
					static_cast<uint16_t>(localDate.year), "birthday_email"))
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
