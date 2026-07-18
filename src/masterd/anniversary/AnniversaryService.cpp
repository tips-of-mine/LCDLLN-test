// AnniversaryService — implémentation. Voir le header pour les règles.

#include "src/masterd/anniversary/AnniversaryService.h"

#include "src/masterd/exploits/MysqlExploitStore.h"
#include "src/masterd/mail/MailManager.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"
#include "src/shared/core/Log.h"

#include <mysql.h>

#include <chrono>
#include <cstdio>

namespace engine::server
{
	namespace
	{
		using engine::anniversary::YmdDate;

		/// Libellé FR du palier fidélité N (aligné sur le seed 0074).
		std::string SignupTitle(int n)
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "Fidèle depuis %d an%s", n, n > 1 ? "s" : "");
			return std::string(buf);
		}

		/// Libellé FR du palier anniversaire N (aligné sur le seed 0074).
		std::string BirthdayTitle(int n)
		{
			if (n <= 1) return std::string("Premier anniversaire fêté");
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%d anniversaires fêtés", n);
			return std::string(buf);
		}
	}

	AnniversaryService::Result AnniversaryService::OnEnterWorld(uint64_t accountId,
		const engine::anniversary::YmdDate& todayUtc)
	{
		Result result;
		if (m_pool == nullptr || m_exploits == nullptr || !m_exploits->IsAvailable())
			return result; // mode no-DB : silencieux (dev local / tests).

		// Lecture ciblée des deux dates IMMUABLES du compte. created_at est un
		// TIMESTAMP : formaté en date civile par MySQL (session serveur = UTC,
		// convention déploiement Docker).
		std::string createdStr;
		std::string birthStr;
		{
			auto guard = m_pool->Acquire();
			MYSQL* mysql = guard.get();
			auto* cache = guard.cache();
			if (!mysql || !cache) return result;
			auto* stmt = cache->Acquire(mysql,
				"SELECT DATE_FORMAT(created_at, '%Y-%m-%d'), COALESCE(birth_date, '') "
				"FROM accounts WHERE id = ?");
			if (stmt && stmt->Bind(0, accountId) && stmt->Execute() && stmt->FetchRow())
			{
				createdStr = stmt->GetString(0);
				birthStr   = stmt->GetString(1);
			}
			else
			{
				LOG_WARN(Db, "[AnniversaryService] lecture dates compte {} échouée", accountId);
				return result;
			}
		}

		// ① Fidélité (inscription) — rattrapage de tous les paliers manquants.
		YmdDate created{};
		if (engine::anniversary::ParseYmd(createdStr, created))
		{
			const int years = engine::anniversary::YearsElapsed(created, todayUtc);
			const int cap = years < kMaxTier ? years : kMaxTier;
			for (int n = 1; n <= cap; ++n)
			{
				char code[32];
				std::snprintf(code, sizeof(code), "signup_anniv_%d", n);
				if (m_exploits->UnlockByCode(accountId, code, "anniversary_service") == 1)
				{
					result.unlockedTitles.push_back(SignupTitle(n));
					LOG_INFO(Db, "[AnniversaryService] compte {} : exploit {} débloqué", accountId, code);
				}
			}
		}

		// ② Naissance — jour J strict uniquement.
		YmdDate birth{};
		if (!engine::anniversary::ParseYmd(birthStr, birth)
			|| !engine::anniversary::IsAnniversaryDay(birth, todayUtc))
		{
			return result;
		}
		const uint16_t year = static_cast<uint16_t>(todayUtc.year);

		// Exploit annuel (garde intra-jour : reconnexions multiples = 1 seul).
		if (m_exploits->TryClaimAnnualReward(accountId, year, "birthday_exploit"))
		{
			const uint32_t already = m_exploits->CountUnlockedByMetric(accountId, "birthday_years");
			const int n = static_cast<int>(already) + 1;
			if (n <= kMaxTier)
			{
				char code[32];
				std::snprintf(code, sizeof(code), "birthday_%d", n);
				if (m_exploits->UnlockByCode(accountId, code, "anniversary_service") == 1)
				{
					result.unlockedTitles.push_back(BirthdayTitle(n));
					LOG_INFO(Db, "[AnniversaryService] compte {} : exploit {} débloqué", accountId, code);
				}
			}
		}

		// Courrier cadeau (garde dédiée : découplé de l'exploit pour rester
		// robuste si l'un des deux échoue). La garde est prise AVANT l'envoi :
		// aucun double-envoi possible ; un échec du mail est loggué en ERROR
		// (compensation manuelle possible via la table de garde).
		if (m_mail != nullptr && m_exploits->TryClaimAnnualReward(accountId, year, "birthday_gift"))
		{
			const uint32_t celebrated = m_exploits->CountUnlockedByMetric(accountId, "birthday_years");
			const uint32_t tier = celebrated == 0u ? 1u
				: (celebrated > static_cast<uint32_t>(kMaxTier) ? static_cast<uint32_t>(kMaxTier) : celebrated);

			// Gâteau : variante déterministe par (compte, année) — stable si
			// l'envoi devait être rejoué, et répartie entre les 10 buffs.
			const uint32_t cakeId = kFirstCakeItemId
				+ static_cast<uint32_t>((accountId + static_cast<uint64_t>(year)) % kCakeVariantCount);
			const uint32_t keepsakeId = kFirstKeepsakeItemId + (tier - 1u);

			mail::Mail gift;
			gift.senderAccountId   = 0u; // 0 = système
			gift.receiverAccountId = accountId;
			gift.subject = "Joyeux anniversaire !";
			gift.body =
				"Toute l'équipe vous souhaite un joyeux anniversaire !\n\n"
				"Vous trouverez ci-joint un gâteau à partager avec votre groupe "
				"ou votre guilde (aujourd'hui seulement), un souvenir de fidélité "
				"et une bourse de 50 or, 50 argent et 50 bronze.\n\n"
				"Bonne fête et bon jeu !";
			gift.copperGold = kGiftBronze;
			gift.items.push_back(mail::MailItemAttachment{ cakeId, 1u });
			gift.items.push_back(mail::MailItemAttachment{ keepsakeId, 1u });
			gift.sentTsMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			gift.expiresTsMs = 0u; // le courrier n'expire pas (le gâteau, si — SP3).

			if (m_mail->Insert(gift) != 0u)
			{
				result.birthdayGiftMailed = true;
				LOG_INFO(Db, "[AnniversaryService] compte {} : courrier cadeau anniversaire envoyé "
					"(mailId={}, gâteau={}, souvenir={})", accountId, gift.mailId, cakeId, keepsakeId);
			}
			else
			{
				LOG_ERROR(Db, "[AnniversaryService] compte {} : ÉCHEC envoi courrier cadeau "
					"(garde {} déjà prise — compensation manuelle requise)", accountId, year);
			}
		}

		return result;
	}
}
