#pragma once
// BirthdayEmailJob — envoi d'un e-mail RÉEL (SMTP) de vœux personnalisés à
// chaque compte dont c'est l'anniversaire, À PARTIR DE 7 H DU MATIN DANS LE
// FUSEAU DU JOUEUR (spec 2026-07-18, extension e-mail). Contrairement aux
// récompenses in-game (EnterWorld requis), cet e-mail part SANS connexion :
// c'est de la ré-invitation (« un cadeau t'attend en jeu aujourd'hui »).
//
// Fuseau du joueur : approximation par PAYS (accounts.country_code, cf.
// CountryUtcOffset — fuseau représentatif, heure standard ; pays inconnu =
// UTC). L'heure locale du compte est recalculée à chaque passage : l'envoi
// part au premier Tick où son horloge locale du jour J atteint 07:00.
//
// Fonctionnement : Tick() est appelé périodiquement (boucle principale du
// master, ~10 min) et évalue CHAQUE compte candidat dans SA date locale
// (les dates UTC J-1/J/J+1 couvrent tous les fuseaux). Cibles : e-mail
// VÉRIFIÉ + birth_date du jour local (règle 29/02 → 28/02 hors bissextile).
// Idempotence inter-redémarrages : garde account_anniversary_rewards
// kind='birthday_email' (année LOCALE du joueur) — aucun double envoi.
//
// Les envois SMTP (réseau, potentiellement lents) partent sur un thread
// détaché ; les décisions (SELECT + garde) restent sur le thread appelant.

#include "src/shared/anniversary/AnniversaryMath.h"

#include <cstdint>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server
{
	class MysqlExploitStore;
	struct SmtpConfig;

	class BirthdayEmailJob
	{
	public:
		/// Heure locale (0..23) à partir de laquelle l'e-mail peut partir.
		static constexpr int kSendFromLocalHour = 7;

		/// \param pool  accès DB `accounts` (lecture seule ici).
		/// \param store garde annuelle TryClaimAnnualReward (kind birthday_email).
		/// \param smtp  config SMTP (host vide = job désactivé, mode dev).
		void SetDependencies(engine::server::db::ConnectionPool* pool,
			MysqlExploitStore* store, const SmtpConfig* smtp)
		{
			m_pool = pool; m_exploits = store; m_smtp = smtp;
		}

		/// À appeler périodiquement (main loop master, ex. toutes les 10 min).
		/// Chaque passage réévalue l'heure locale des comptes fêtés — la
		/// garde annuelle rend l'ensemble idempotent.
		/// Effets de bord : SELECT DB, INSERT garde, envois SMTP (thread
		/// détaché), logs.
		void Tick();

		/// Cœur testable : traite l'instant UTC \p utcEpochSeconds
		/// (indépendant de l'horloge). \return nombre d'e-mails PLANIFIÉS.
		size_t RunAtInstant(int64_t utcEpochSeconds);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
		MysqlExploitStore* m_exploits = nullptr;
		const SmtpConfig* m_smtp = nullptr;
	};
}
