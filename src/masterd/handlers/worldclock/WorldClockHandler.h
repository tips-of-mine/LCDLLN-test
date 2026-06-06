#pragma once
// WorldClockHandler : etat horloge monde authoritative master + push broadcast
// sur changement admin. Contrairement au LunarHandler (etat purement stateless,
// recalcule a chaque appel depuis l'epoch), l'horloge monde a un etat MUTABLE
// (offset, pause, time scale) modifiable au runtime via les commandes admin
// (/settime, /pausetime, /timescale). Toute mutation declenche un broadcast 205.
//
// Le handler est instancie dans main_linux.cpp au boot, cable via
// SetServer/SetSessionManager/SetConnectionSessionMap, configure une fois via
// Configure() (params boot lus depuis config.json), puis enregistre dans le
// dispatch du NetServer pour l'opcode 203. La response 204 est emise avec le
// meme requestId/sessionId que la request recue. Le push 205 est broadcast a
// tous les clients connectes (pas de subscribe explicite : l'horloge est
// globale et permanente, comme la lune).
//
// La formule de calcul des secondes de jeu vit dans WorldClock.h (engine::world,
// pure et deterministe) : master, shard et client utilisent la MEME fonction
// pour rester synchronises. Le handler ne fait que detenir les params courants
// et les muter.
//
// V1 limitations :
//   - SetTimeOfDay decale aussi la phase lunaire (offset partage). OK v1.
//   - Pas de persistance de l'offset/pause (cf. WorldClockParams : offsetGameSec
//     non persiste). Un reboot master retombe sur les params config.
//   - Pas de Tick periodique : l'horloge ne change qu'a la demande admin
//     (mutateurs). La derive temporelle est implicite dans GameSeconds().

#include "src/shared/world/WorldClock.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace engine::server
{
	class NetServer;
	class SessionManager;
	class ConnectionSessionMap;
}

namespace engine::server
{
	/// Dispatcher WorldClock cote master + etat mutable + broadcast admin.
	/// Doit etre configure via Set*() avant tout HandlePacket / mutateur.
	class WorldClockHandler
	{
	public:
		/// Branche le NetServer pour pouvoir envoyer les reponses + push notifications.
		void SetServer(NetServer* s) { m_server = s; }
		/// Branche le SessionManager pour resoudre sessionId -> accountId (validation auth).
		void SetSessionManager(SessionManager* mgr) { m_sessionMgr = mgr; }
		/// Branche la map connId -> sessionId pour valider les requests + iterer pour le push.
		void SetConnectionSessionMap(ConnectionSessionMap* m) { m_connMap = m; }

		/// Fixe les parametres boot de l'horloge. Appele une fois au boot avant
		/// tout HandlePacket / mutateur. Ecrit sous mutex.
		/// \param p Parametres initiaux (epoch, time scale, periode lunaire...).
		void Configure(const engine::world::WorldClockParams& p);

		/// Dispatch packet : opcode 203 (WorldClockStateRequest) -> response 204.
		/// Si l'opcode n'est pas WorldClock, ignore silencieusement.
		///
		/// \param connId           identifiant de connexion TCP (pour Send response).
		/// \param opcode           opcode du paquet entrant (203).
		/// \param requestId        request_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param sessionIdHeader  session_id du paquet entrant ; renvoye tel quel dans la reponse.
		/// \param payload          pointeur sur le payload (sans header).
		/// \param payloadSize      taille du payload en octets.
		void HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		                  uint64_t sessionIdHeader, const uint8_t* payload, size_t payloadSize);

		/// Cale l'heure du jour a `hours` (commande admin /settime). Decale
		/// l'offset de jeu pour que TimeOfDayHours == hours immediatement.
		/// Effet de bord : decale aussi la phase lunaire (offset partage, OK v1).
		/// Broadcast 205 si succes.
		/// \param hours Heure cible dans [0,24). Hors borne -> false, pas de mutation.
		/// \return true si applique et broadcaste, false si hors borne.
		bool SetTimeOfDay(float hours);

		/// Met en pause ou reprend l'horloge (commande admin /pausetime).
		/// A la pause : fige pausedAtGameSec a la valeur courante. A la reprise :
		/// recale l'offset pour la continuite (pas de saut temporel). Broadcast 205.
		/// \param paused true pour figer, false pour reprendre.
		/// \return true (toujours applique).
		bool SetPaused(bool paused);

		/// Change la vitesse d'ecoulement du temps (commande admin /timescale),
		/// en minutes REELLES par jour de jeu. Recale l'offset pour la continuite
		/// (l'instant de bascule garde la meme heure de jeu). Si l'horloge est en
		/// pause, l'offset n'est PAS touche (la valeur figee pausedAtGameSec prime).
		/// Broadcast 205 si succes.
		/// \param realMinPerDay Minutes reelles par jour de jeu, borne [1,1440].
		///                      Hors borne -> false, pas de mutation.
		/// \return true si applique et broadcaste, false si hors borne.
		bool SetTimeScale(float realMinPerDay);

		/// Copie des parametres courants sous mutex (pour le futur lunaire/shard).
		engine::world::WorldClockParams GetParams() const;

	private:
		/// Push une WorldClockChangeNotification (opcode 205, push) a tous les
		/// clients connectes (snapshot de ConnectionSessionMap). A appeler hors
		/// mutex (prend un instantane des params via GetParams()).
		void BroadcastChange();

		/// Timestamp Unix courant en ms (system_clock).
		static uint64_t NowMs();

		/// Secondes reelles ecoulees depuis l'epoch de reference. Suppose le
		/// mutex deja pris (lit m_params.epochRefUnixMs).
		/// \param now Timestamp Unix courant en ms.
		double RealSecSinceEpoch(uint64_t now) const;

		NetServer*            m_server     = nullptr;
		SessionManager*       m_sessionMgr = nullptr;
		ConnectionSessionMap* m_connMap    = nullptr;

		/// Mutex protegeant m_params (etat mutable de l'horloge).
		mutable std::mutex                  m_mutex;
		/// Etat courant de l'horloge (defaults WorldClockParams jusqu'a Configure()).
		engine::world::WorldClockParams     m_params;
	};
}
