#pragma once
// AnniversaryService — récompenses d'anniversaire (spec docs/superpowers/
// specs/2026-07-18-anniversary-rewards-design.md). Déclenché par le master à
// chaque EnterWorld réussi (point d'accroche unique : compte + personnage
// destinataire + connexion chat disponibles) :
//
//   * Inscription (accounts.created_at, IMMUABLE) : débloque tous les paliers
//     signup_anniv_N manquants (N = années révolues, plafond data 10).
//     « Jamais perdu » : rattrapage automatique au prochain EnterWorld.
//   * Naissance (accounts.birth_date, IMMUABLE) : si aujourd'hui (UTC) est le
//     jour J (29/02 → 28/02 les années non bissextiles), débloque birthday_N
//     et dépose un courrier cadeau (50 or + 50 argent + 50 bronze, 1 gâteau
//     parmi 10, 1 souvenir millésimé). Jour strict : pas d'EnterWorld le jour
//     J = pas de cadeau. Idempotence par account_anniversary_rewards.
//
// Anti-triche : les deux dates sont lues en DB côté master, jamais fournies
// par le client ; tous les octrois sont idempotents par clés primaires.

#include "src/shared/anniversary/AnniversaryMath.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::db { class ConnectionPool; }
namespace engine::server::mail { class IMailStore; }

namespace engine::server
{
	class MysqlExploitStore;

	class AnniversaryService
	{
	public:
		/// Résultat d'un passage : libellés FR des exploits NOUVELLEMENT
		/// débloqués (pour la notification chat « système ») et indicateur
		/// d'envoi du courrier cadeau.
		struct Result
		{
			std::vector<std::string> unlockedTitles;
			bool birthdayGiftMailed = false;
		};

		/// \param pool  accès DB `accounts` (created_at / birth_date).
		/// \param store store exploits + garde annuelle (peut être no-op si
		///        DB indisponible — le service ne fait alors rien).
		/// \param mail  store de courrier (production MySQL ; in-memory en
		///        mode no-DB).
		void SetDependencies(engine::server::db::ConnectionPool* pool,
			MysqlExploitStore* store, engine::server::mail::IMailStore* mail)
		{
			m_pool = pool; m_exploits = store; m_mail = mail;
		}

		/// À appeler après un EnterWorld VALIDÉ. Lit les dates du compte,
		/// débloque les paliers manquants et, le jour J de naissance, dépose
		/// le courrier cadeau. \param todayUtc injecté pour les tests
		/// (production : anniversary::TodayUtc()).
		/// Effets de bord : écritures DB (unlocks, garde, mail) + logs.
		Result OnEnterWorld(uint64_t accountId, const engine::anniversary::YmdDate& todayUtc);

		/// Plafond des paliers seedés par la migration 0074 (signup et
		/// birthday). Au-delà, aucun code d'exploit n'existe : on borne.
		static constexpr int kMaxTier = 10;

		/// Montant du cadeau : 50 or + 50 argent + 50 bronze, exprimé dans
		/// l'unité de base (bronze) de l'affichage client (100:1:1, cf.
		/// CurrencyFormat.h) — le champ mail `copper_gold` est réinterprété
		/// en bronze par le client.
		static constexpr uint64_t kGiftBronze = 50ull * 10000ull + 50ull * 100ull + 50ull;

		/// Plage d'ids des gâteaux d'anniversaire dans items.json (10
		/// variantes, buffs distincts — SP3 branche l'activation).
		static constexpr uint32_t kFirstCakeItemId = 5101u;
		static constexpr uint32_t kCakeVariantCount = 10u;

		/// Plage d'ids des souvenirs millésimés (An 1..10, slots tournants).
		static constexpr uint32_t kFirstKeepsakeItemId = 5121u;

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
		MysqlExploitStore*                  m_exploits = nullptr;
		engine::server::mail::IMailStore*   m_mail = nullptr;
	};
}
