#pragma once
// CMANGOS.01 (Phase 2.01a) — ChatGate : moteur de décision avant relai d'un
// message chat. Vérifie l'état du compte (banni / mute persistant en DB) et
// applique un anti-flood en mémoire (sliding window par account).
//
// Sépare la sanitization (ChatSanitizer.h) de la décision d'autorisation.
// Pure logique : la sanitization ne modifie JAMAIS l'autorisation, et
// l'autorisation ne modifie JAMAIS le texte. Les deux étapes sont
// orthogonales et appelables en série depuis ChatRelayHandler.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server
{
	class AccountStore;
}

namespace engine::server::chat
{
	/// Information sur un mute persistant en DB (table `chat_mutes`).
	struct ChatMute
	{
		/// Epoch ms UTC ; 0 = mute permanent (jamais expiré).
		uint64_t untilTsMs = 0;
		/// Raison libre (audit, affichée à l'utilisateur).
		std::string reason;
	};

	/// Décision finale du gate.
	enum class ChatGateDecision : uint8_t
	{
		/// Autorisé — l'appelant peut router le message.
		Allowed = 0,
		/// Compte banni (`AccountStatus::Locked`). Pas de message à l'utilisateur.
		Banned = 1,
		/// Mute actif (DB). `reason` est rempli avec le motif.
		Muted = 2,
		/// Anti-flood déclenché : trop de messages dans la fenêtre.
		Flooding = 3,
	};

	struct ChatGateResult
	{
		ChatGateDecision decision = ChatGateDecision::Allowed;
		/// Si `Muted`, la raison telle que stockée en DB. Sinon vide.
		std::string reason;
		/// Si `Muted`, l'epoch ms d'expiration (0 = permanent). Sinon 0.
		uint64_t untilTsMs = 0;
	};

	/// Configuration runtime du gate.
	struct ChatGateConfig
	{
		/// Fenêtre de surveillance anti-flood (ms).
		uint64_t floodWindowMs = 5000;
		/// Nombre max de messages **dans** la fenêtre. Le N+1 est rejeté.
		size_t floodMaxMessages = 5;
		/// Capacité max de la table interne de fenêtres par account
		/// (protection mémoire si beaucoup d'accounts spamment). Au-delà,
		/// les entrées les plus anciennes (purgées car hors fenêtre) sont
		/// laissées tomber. 0 = illimité.
		size_t maxTrackedAccounts = 4096;
	};

	/// Callback pour récupérer un mute. Retourne `nullopt` si pas mute
	/// actif. Injectable (production = SQL via ConnectionPool ; tests =
	/// mock RAM). Doit être thread-safe.
	using ChatMuteLookupFn = std::function<std::optional<ChatMute>(uint64_t accountId)>;

	/// Callback pour vérifier le statut "banni". Retourne true si le compte
	/// est verrouillé (`AccountStatus::Locked`). Injectable.
	using AccountBannedFn = std::function<bool(uint64_t accountId)>;

	/// Gate thread-safe : appelable concurremment depuis plusieurs threads
	/// du master. Tient une fenêtre coulissante par account_id.
	///
	/// Cycle de vie typique :
	///   1. Construire avec une config.
	///   2. Câbler les callbacks (production : SqlChatMuteSource + AccountStore).
	///   3. Pour chaque message : `Decide(accountId, nowMs)`.
	///   4. Si `Allowed`, le caller appelle `RecordSent(accountId, nowMs)`
	///      (séparé pour permettre des dry-runs ; en pratique
	///      `DecideAndRecord` est l'API simple).
	class ChatGate
	{
	public:
		explicit ChatGate(ChatGateConfig cfg = {});

		/// Câble le lookup de mute. Si non câblé, `Decide` retourne
		/// toujours `Allowed` pour la part mute (autres checks restent
		/// actifs). Idempotent.
		void SetMuteLookup(ChatMuteLookupFn fn);

		/// Câble le check de ban. Si non câblé, `Decide` retourne toujours
		/// `Allowed` pour la part ban.
		void SetBannedCheck(AccountBannedFn fn);

		/// Décide sans modifier l'état (utile pour preview / debug).
		ChatGateResult Decide(uint64_t accountId, uint64_t nowMs) const;

		/// Décide ET enregistre le tick si `Allowed`. C'est l'API à
		/// utiliser dans le hot path. Si la décision est `Allowed`, l'état
		/// interne est mis à jour ; sinon non.
		ChatGateResult DecideAndRecord(uint64_t accountId, uint64_t nowMs);

		/// Helper de configuration : remplace les callbacks par des
		/// implémentations basées sur ConnectionPool (mute) + AccountStore
		/// (ban). Pratique pour le câblage en production.
		void WireProduction(engine::server::db::ConnectionPool* pool, AccountStore* accounts);

		/// Reset complet de l'état runtime (anti-flood). Utile en tests.
		void ResetState();

	private:
		struct WindowState
		{
			/// Timestamps (ms UTC) des messages envoyés ; tronqués à
			/// `floodWindowMs` avant chaque check.
			std::deque<uint64_t> tsMs;
		};

		/// Purge les timestamps hors fenêtre. Caller doit tenir le lock.
		void PurgeWindow(WindowState& w, uint64_t nowMs) const;

		ChatGateConfig                           m_cfg;
		mutable std::mutex                       m_mutex;
		std::unordered_map<uint64_t, WindowState> m_windows;
		ChatMuteLookupFn                         m_muteLookup;
		AccountBannedFn                          m_bannedCheck;
	};

	/// Helper : crée une `ChatMuteLookupFn` qui interroge `chat_mutes` via
	/// le pool. Mute permanent (`until_ts=0`) toujours actif. Mute expiré
	/// (`until_ts < nowMs`) ignoré (l'appelant compare nowMs lui-même via
	/// `Decide`). Le lookup retourne UN mute (le plus récent) — la table
	/// a une PK sur account_id donc au plus une ligne.
	ChatMuteLookupFn MakeSqlMuteLookup(engine::server::db::ConnectionPool* pool);

	/// Helper : crée un `AccountBannedFn` qui interroge un AccountStore.
	AccountBannedFn MakeAccountStoreBannedCheck(AccountStore* accounts);
}
