#pragma once
// BirthdayEmailJob — envoi d'un e-mail RÉEL (SMTP) de vœux personnalisés à
// chaque compte dont c'est l'anniversaire (spec 2026-07-18, extension
// e-mail). Contrairement aux récompenses in-game (EnterWorld requis), cet
// e-mail part SANS connexion du joueur : c'est de la ré-invitation (« un
// cadeau t'attend en jeu aujourd'hui »).
//
// Fonctionnement : Tick() est appelé périodiquement par la boucle principale
// du master ; le travail réel ne tourne qu'une fois par jour UTC (premier
// Tick du process, puis à chaque changement de date). Cibles : comptes avec
// e-mail VÉRIFIÉ et birth_date du jour (règle 29/02 → fêté le 28/02 les
// années non bissextiles). Idempotence inter-redémarrages : garde
// account_anniversary_rewards kind='birthday_email' (migration 0074) — un
// reboot du master le jour J ne double aucun envoi.
//
// Les envois SMTP (réseau, potentiellement lents) partent sur un thread
// détaché ; les décisions (SELECT + garde) restent sur le thread appelant.

#include "src/shared/anniversary/AnniversaryMath.h"

#include <string>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server
{
	class MysqlExploitStore;
	struct SmtpConfig;

	class BirthdayEmailJob
	{
	public:
		/// \param pool  accès DB `accounts` (lecture seule ici).
		/// \param store garde annuelle TryClaimAnnualReward (kind birthday_email).
		/// \param smtp  config SMTP (peut pointer une config vide : le job se
		///        désactive si host/port absents — mode dev sans SMTP).
		void SetDependencies(engine::server::db::ConnectionPool* pool,
			MysqlExploitStore* store, const SmtpConfig* smtp)
		{
			m_pool = pool; m_exploits = store; m_smtp = smtp;
		}

		/// À appeler périodiquement (main loop master, ex. toutes les 10 min).
		/// No-op si le jour UTC courant a déjà été traité par ce process.
		/// Effets de bord : SELECT DB, INSERT garde, envois SMTP (thread
		/// détaché), logs.
		void Tick();

		/// Cœur testable : traite la date \p todayUtc (indépendant de
		/// l'horloge). Retourne le nombre d'e-mails PLANIFIÉS (garde prise).
		size_t RunForDay(const engine::anniversary::YmdDate& todayUtc);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
		MysqlExploitStore* m_exploits = nullptr;
		const SmtpConfig* m_smtp = nullptr;
		/// Dernier jour UTC traité par ce process ("yyyy-mm-dd", vide au boot).
		std::string m_lastProcessedDay;
	};
}
