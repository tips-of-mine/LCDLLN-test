#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace engine::server
{
	/// TA.3 — Registre des personnages admis sur le shard.
	///
	/// Au handshake TCP, `ShardTicketValidator` accepte un ticket signé par le master
	/// qui porte le `character_id` (couvert par le HMAC, donc non falsifiable). Le shard
	/// inscrit alors ce personnage ici. Le gate UDP (`ServerApp::HandleHello`) n'accepte
	/// un `Hello` (dont le nonce = character_id) que si ce character_id figure dans ce
	/// registre — sinon un client pourrait usurper le personnage d'autrui en envoyant un
	/// nonce arbitraire.
	///
	/// Thread-safe : écrit depuis la stack TCP (handshake) et lu depuis la boucle UDP
	/// gameplay (threads distincts). Les horloges sont passées par l'appelant (ms,
	/// monotone) pour rendre la classe déterministe et testable.
	class AdmittedCharacterRegistry
	{
	public:
		AdmittedCharacterRegistry() = default;

		/// Admet (ou rafraîchit l'horodatage d')un personnage. `characterId == 0` est ignoré
		/// (sentinelle « aucun personnage » : un ticket émis avant l'EnterWorld porte 0).
		/// \param nowMs horloge monotone en millisecondes (point de départ du TTL).
		void Admit(uint64_t characterId, uint64_t accountId, uint64_t nowMs);

		/// Retire un personnage (ex. déconnexion). No-op si absent.
		void Revoke(uint64_t characterId);

		/// True si `characterId` est admis et non expiré (TTL). `characterId == 0` → false.
		/// \param nowMs horloge monotone en millisecondes (même base que Admit).
		bool IsAdmitted(uint64_t characterId, uint64_t nowMs) const;

		/// account_id associé à un personnage admis et non expiré, 0 sinon.
		uint64_t AdmittedAccountId(uint64_t characterId, uint64_t nowMs) const;

		/// Nombre d'entrées (admises ou non, sans purge). Utile aux tests / diagnostics.
		size_t Count() const;

		/// Durée de vie d'une admission en ms (défaut 5 min). Borne haute : le client doit
		/// présenter son Hello UDP peu après le handshake TCP.
		void SetTtlMs(uint64_t ttlMs) { m_ttlMs = ttlMs; }

	private:
		struct Entry
		{
			uint64_t accountId = 0;
			uint64_t admittedAtMs = 0;
		};

		mutable std::mutex m_mutex;
		std::unordered_map<uint64_t, Entry> m_admitted;
		uint64_t m_ttlMs = 300000u; ///< 5 minutes.
	};
}
