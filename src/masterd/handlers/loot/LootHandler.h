#pragma once
// CMANGOS.17 (Phase 3.17 step 3+4 Loot) - LootHandler : dispatch des opcodes
// Loot cote joueur (183 Choice, 186 SimulateRoll) et registry in-memory V1
// des rolls actives.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// 2 requests opcodes. Les responses 184/187 sont emises avec le meme
// requestId / sessionId que la request recue. Les push notifications 182
// (RollNotification) et 185 (RollResultNotification) sont emises par le
// handler aux clients eligibles.
//
// V1 simulation simple :
//   - SimulateRollRequest cree une nouvelle ActiveRoll avec creator comme
//     SEUL eligible (pas de groupe). itemTemplateId aleatoire dans 1..5,
//     count=1, durationSec=30. Push immediatement RollNotification au creator.
//     Reponse Ok + rollId.
//   - ChoiceRequest : verifie roll existe + non resolved + creator eligible.
//     Set choice. Comme un seul eligible, resout immediatement : random
//     uint8 0..100, regle Need > Greed > Pass + plus haut roll dans la meme
//     categorie. Tous Pass => personne ne gagne (winnerName="" + roll=0).
//     Push RollResultNotification au creator. Marque resolved=true.
//   - Pas de Tick periodique pour timeout : a chaque HandleChoice on scan
//     les rolls expirees pour les resolve.
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// status=Unauthorized (code 1) dans la reponse type-specific.
//
// Items hardcode V1 :
//   1 = Iron Ore
//   2 = Linen Cloth
//   3 = Mageweave
//   4 = Health Potion
//   5 = Mana Potion
//
// V1 limitations :
//   - SimulateRoll = creator-only eligible (pas de groupe). Future PR :
//     integration avec Group/Party (CMANGOS.15).
//   - Items hardcode (5 entries V1). Future PR : integration LootTable +
//     ItemTemplate (engine::server::loot::LootTable).
//   - Pas de timeout tick periodique : scan a chaque HandleChoice.
//   - Pas de SyncLoot RPC entre master et shardd (master autoritaire V1).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Choix d'un joueur pour une roll de loot. Wire format uint8.
	enum class LootChoice : uint8_t
	{
		Pass  = 0,
		Greed = 1,
		Need  = 2,
	};

	/// Roll active in-memory V1.
	/// resolved=false tant que toutes les choices ne sont pas recues
	/// OU que endsAt n'est pas atteint. Une fois resolved=true, plus aucune
	/// nouvelle choice n'est acceptee (RollEnded).
	struct ActiveRoll
	{
		uint64_t                                       rollId         = 0;
		uint32_t                                       itemTemplateId = 0;
		std::string                                    itemName;
		uint32_t                                       count          = 0;
		std::chrono::steady_clock::time_point          endsAt;
		std::vector<uint64_t>                          eligibleAccountIds;
		std::unordered_map<uint64_t /*accountId*/, LootChoice> choices;
		/// Pour pouvoir push le resultat aux eligibles, on memorise leur
		/// connId associe au moment de la creation. Parallele a
		/// eligibleAccountIds (meme indexation).
		std::vector<uint32_t>                          eligibleConnIds;
		bool                                           resolved       = false;
	};

	/// Dispatcher Loot cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class LootHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes Loot.
		/// Dispatch vers HandleChoice / HandleSimulateRoll selon l'opcode.
		/// Si l'opcode n'est pas un opcode Loot, ignore silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (183/186).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push LootRollNotification (opcode 182)
		/// au client identifie par \p connId. Utilise par le handler en interne
		/// mais accessible egalement depuis l'exterieur (tests, hooks, future
		/// integration shardd quand un mob drop un item group-roll).
		///
		/// \param connId          identifiant de connexion TCP cible (0 = no-op).
		/// \param rollId          identifiant unique de la roll.
		/// \param itemTemplateId  identifiant template de l'item.
		/// \param itemName        nom resolu de l'item.
		/// \param count           nombre d'items dropes.
		/// \param durationSec     duree en secondes avant timeout.
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushRollNotification(uint32_t connId, uint64_t rollId, uint32_t itemTemplateId,
			const std::string& itemName, uint32_t count, uint32_t durationSec);

		/// API publique : pousse une push LootRollResultNotification (opcode 185)
		/// au client identifie par \p connId.
		///
		/// \param connId          identifiant de connexion TCP cible (0 = no-op).
		/// \param rollId          identifiant unique de la roll.
		/// \param winnerName      nom du gagnant (vide si tous Pass).
		/// \param winnerChoice    choix du gagnant (0=Pass / 1=Greed / 2=Need).
		/// \param winnerRoll      roll du gagnant (0..100, 0 si tous Pass).
		/// \param itemTemplateId  identifiant template de l'item.
		/// \param itemName        nom resolu de l'item.
		/// \param count           nombre d'items dropes.
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushRollResult(uint32_t connId, uint64_t rollId, const std::string& winnerName,
			uint8_t winnerChoice, uint8_t winnerRoll, uint32_t itemTemplateId,
			const std::string& itemName, uint32_t count);

		/// Helper static : retourne le nom de l'item hardcode V1 pour un
		/// itemTemplateId donne (1..5). Pour un id hors plage, retourne
		/// un fallback "Item #<id>" (ASCII safe pour MSVC).
		static std::string ResolveItemName(uint32_t itemTemplateId);

	private:
		/// Traite LOOT_ROLL_CHOICE_REQUEST : valide rollId + non-resolved
		/// + creator eligible, set choice, et resout la roll si toutes
		/// les choices recues OU endsAt depasse.
		void HandleChoice(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite LOOT_SIMULATE_ROLL_REQUEST : cree une nouvelle ActiveRoll
		/// avec le creator comme seul eligible, push RollNotification, et
		/// retourne Ok + rollId.
		void HandleSimulateRoll(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// Pioche un random uint8 dans [0, 100]. Utilise mt19937 + uniform.
		/// Doit etre appele sous m_mutex (m_rng accede partage).
		uint8_t RollDie0to100Locked();

		/// Pioche un itemTemplateId dans [1, 5] (V1 hardcode 5 entries).
		/// Doit etre appele sous m_mutex.
		uint32_t PickRandomItemIdLocked();

		/// Resout la roll \p r : pour chaque non-Pass choice, pioche un random
		/// 0..100. Need > Greed > Pass. Tie sur meme categorie : plus haut
		/// roll gagne. Tous Pass => winnerName="" + winnerRoll=0.
		/// Push RollResultNotification a chaque eligible, set resolved=true.
		/// Doit etre appele sous m_mutex.
		void ResolveRollLocked(ActiveRoll& r);

		/// Scan toutes les rolls non-resolved pour resolve celles dont
		/// endsAt est depassee. Appele en debut de HandleChoice.
		/// Doit etre appele sous m_mutex.
		void ScanExpiredRollsLocked();

		NetServer*                                       m_server     = nullptr;
		SessionManager*                                  m_sessionMgr = nullptr;
		ConnectionSessionMap*                            m_connMap    = nullptr;

		/// Mutex protegeant m_rolls + m_nextRollId + m_rng.
		mutable std::mutex                               m_mutex;

		/// Registry des rolls actives. Cle = rollId.
		std::unordered_map<uint64_t, ActiveRoll>         m_rolls;

		/// Compteur monotone pour generer les rollId. Incremente atomicly.
		std::atomic<uint64_t>                            m_nextRollId{1};

		/// PRNG mt19937 seede sur steady_clock::now() au premier usage.
		/// Protege par m_mutex.
		std::mt19937                                     m_rng;
		bool                                             m_rngSeeded = false;
	};
}
