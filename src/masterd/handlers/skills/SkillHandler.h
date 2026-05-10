#pragma once
// CMANGOS.39 (Phase 4.39 step 3+4) — SkillHandler : dispatch des opcodes
// Skills cote joueur (113/115/117) et appel des methodes correspondantes
// d'un store in-memory de SkillBook par account.
//
// Le handler est instancie dans main_linux.cpp au boot du master, cable via
// SetXxx(...), puis enregistre dans le packetHandler du NetServer pour les
// opcodes 113/115/117 (les requests). Les responses 114/116/118 sont emises
// avec le meme requestId / sessionId que la request recue. La push
// notification 119 (SkillUpgradeNotification) est emise par PushSkillUpgrade,
// API publique pour le futur CraftingSystem ou autres handlers (Quest reward,
// etc.).
//
// Validation session : chaque opcode exige une session authentifiee. Le
// handler resout connId -> sessionId via ConnectionSessionMap, puis sessionId
// -> accountId via SessionManager. Si l'un echoue, on repond avec
// error=Unauthorized (code 6) dans la reponse type-specific.
//
// Store in-memory V1 : map account_id -> map skillId -> SkillBookEntry. Au
// premier acces (List ou Learn ou Use), un starter set hardcode est seede
// (Cooking=1, Herbalism=2, Mining=3, FirstAid=4, Lockpicking=5, tous
// value=1 ou 0, cap=75). Pas de persistance DB en V1 — toutes les progressions
// sont perdues au reboot, ce qui est acceptable pour un V1 (sub-PR future
// pour la persistance avec migration MysqlSkillStore).

#include "src/shared/network/SkillPayloads.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>
#include <unordered_map>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Dispatcher Skills cote joueur. Doit etre configure via Set*() avant
	/// tout HandlePacket.
	class SkillHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId.
		void SetSessionManager(SessionManager* sm) { m_sessionMgr = sm; }
		/// Branche la map connId -> sessionId.
		void SetConnectionSessionMap(ConnectionSessionMap* cm) { m_connMap = cm; }

		/// Point d'entree appele par NetServer pour les opcodes Skills.
		/// Dispatch vers HandleListRequest / HandleLearnRequest / HandleUseRequest
		/// selon l'opcode. Si l'opcode n'est pas un opcode Skills, ignore
		/// silencieusement.
		///
		/// \param connId          identifiant de connexion TCP (pour Send response).
		/// \param opcode          opcode du paquet entrant (113/115/117).
		/// \param requestId       request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload         pointeur sur le payload (sans header).
		/// \param payloadSize     taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader,
		                  const uint8_t* payload, size_t payloadSize);

		/// API publique : pousse une push SkillUpgradeNotification (opcode 119)
		/// au client identifie par \p connId. Utilise par les autres handlers
		/// (CraftingSystem futur, QuestHandler reward, etc.) pour signaler un
		/// gain de skill ou un changement de cap.
		///
		/// \param connId    identifiant de connexion TCP cible (0 = no-op).
		/// \param skillId   skill modifie.
		/// \param newValue  nouvelle valeur courante du skill.
		/// \param newCap    nouveau cap (peut etre identique a l'ancien).
		/// \param delta     variation appliquee (signed ; positif = gain).
		/// \return true si le packet a ete envoye, false si connId invalide ou server null.
		bool PushSkillUpgrade(uint32_t connId, uint16_t skillId,
		                       uint16_t newValue, uint16_t newCap, int16_t delta);

	private:
		/// Traite SKILLS_LIST_REQUEST : enumere la skill book de l'account
		/// (seede au premier acces avec un starter set hardcode V1).
		void HandleListRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite SKILL_LEARN_REQUEST : verifie skillId valide (1..5 V1) et pas
		/// deja appris. Si OK, ajoute au store avec value=0, cap=75 et push
		/// une UpgradeNotification (delta=0) au meme client.
		void HandleLearnRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Traite SKILL_USE_REQUEST : verifie skill appris. V1 : RNG 70% success,
		/// si Success applique +1 (clamp cap). Si gain effectif > 0, push une
		/// UpgradeNotification au meme client.
		void HandleUseRequest(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
			uint64_t accountId, const uint8_t* payload, size_t payloadSize);

		/// Recherche le sessionIdHeader actif pour un connId donne. Retourne 0
		/// si la connexion n'a pas de session ou si la map n'est pas branchee.
		/// Utilise par PushSkillUpgrade pour fabriquer le sessionId du header.
		uint64_t FindSessionIdForConn(uint32_t connId) const;

		/// Seede le starter set V1 pour un account si l'entree n'existe pas.
		/// Pas de mutex ici : appele uniquement sous m_mutex deja verrouille.
		void SeedStarterSetIfNeeded(uint64_t accountId);

		/// V1 : Cooking, Herbalism, Mining, FirstAid, Lockpicking.
		static constexpr uint16_t kStarterSkillCount = 5u;
		/// V1 : skillId max valide pour Learn (1..kStarterSkillCount).
		static constexpr uint16_t kMaxValidSkillId = 5u;

		NetServer*                                                                          m_server     = nullptr;
		SessionManager*                                                                     m_sessionMgr = nullptr;
		ConnectionSessionMap*                                                               m_connMap    = nullptr;

		/// Store in-memory : account_id -> (skillId -> SkillBookEntry).
		/// Protege par m_mutex (HandlePacket peut etre appele depuis le thread
		/// reseau, PushSkillUpgrade peut etre appele depuis n'importe ou).
		std::mutex                                                                          m_mutex;
		std::unordered_map<uint64_t,
			std::unordered_map<uint16_t, engine::network::SkillBookEntry>>                  m_skillsByAccount;

		/// RNG pour Use 70% success. Seede au premier usage (lazy).
		std::mt19937                                                                        m_rng;
		bool                                                                                m_rngSeeded = false;
	};
}
