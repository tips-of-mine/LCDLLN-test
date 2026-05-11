// Wave 12 — PacketLog : ring buffer thread-safe de paquets recents (RX/TX)
// pour analyse post-mortem cote serveur. Capture un snapshot leger (header
// + premiers octets du payload) pour pouvoir dumper le contexte lorsqu'un
// handler journalise une erreur. Opt-in : pas wire dans NetServer dans cette
// PR (foundation only). Une PR ulterieure branchera Append() dans les chemins
// RX/TX et DumpRecent() dans les handlers d'erreur.
//
// Conception :
//   - capacite fixee a la construction (typiquement 256..1024)
//   - Append : O(1), overwrite quand plein, thread-safe (mutex court)
//   - Drain  : copie complete du buffer en ordre chronologique
//   - payload preview tronque a kPreviewBytes (64) pour ne pas exploser la
//     RAM en cas de stream lourd
//   - aucune persistance disque, aucun throw explicite

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace engine::server::netdebug
{
	/// Direction du paquet : RX (recu du client) ou TX (envoye au client).
	enum class PacketDirection : uint8_t
	{
		RX = 0,
		TX = 1,
	};

	/// Nombre d'octets de payload conserves en preview dans chaque entry du
	/// ring buffer. On NE STOCKE PAS le payload complet pour eviter une
	/// explosion memoire (cas streams gros). 64 octets suffisent pour les
	/// debug habituels (header opcode + premiers champs).
	inline constexpr size_t kPreviewBytes = 64;

	/// Snapshot d'un paquet capture en RAM dans le ring buffer.
	/// Contient : metadata (timestamp, conn, opcode, sessionId, direction)
	/// + un preview tronque du payload pour hex-dump.
	struct PacketLogEntry
	{
		/// Timestamp Unix en millisecondes (UTC) au moment du capture.
		uint64_t        timestampUnixMs = 0;
		/// Identifiant de la connexion source/destination (slot NetServer).
		uint32_t        connId          = 0;
		/// Opcode applicatif (header v1).
		uint16_t        opcode          = 0;
		/// requestId du header (corrélation request/response).
		uint32_t        requestId       = 0;
		/// sessionId du header (lien session post-AUTH ; 0 si pre-auth).
		uint64_t        sessionId       = 0;
		/// Sens du paquet (depuis le point de vue du serveur).
		PacketDirection direction       = PacketDirection::RX;
		/// Taille reelle du payload (non tronquee).
		size_t          payloadSize     = 0;
		/// Premiers kPreviewBytes octets du payload (hex-dump friendly).
		/// Les octets non remplis (si payloadSize < kPreviewBytes) sont
		/// zero-fill.
		std::array<uint8_t, kPreviewBytes> payloadPreview{};
	};

	/// Ring buffer thread-safe (un seul mutex couvre Append + Drain) de
	/// paquets recents. Capacite immuable apres construction. Append est
	/// toujours O(1) (overwrite quand plein). Drain renvoie une copie
	/// complete du buffer en ordre chronologique (plus vieux en [0]).
	///
	/// Performance : acceptable car PacketLog est opt-in (active uniquement
	/// en build dev/debug via flag config dans une PR ulterieure).
	///
	/// Thread safety : toutes les methodes publiques sont thread-safe.
	class PacketLog final
	{
	public:
		/// Cree un ring buffer de capacite \p capacity. Si capacity == 0,
		/// le log est inerte (Append no-op, Drain renvoie {}).
		explicit PacketLog(size_t capacity);

		/// Capture un paquet dans le ring buffer. Best-effort : ne throw
		/// jamais (en dehors d'une allocation OS catastrophique du vector
		/// initial, mais le buffer est dimensionne a la construction).
		/// Copie au max kPreviewBytes du payload, zero-fill le reste.
		/// Thread-safe (verrou court).
		///
		/// \param dir          Sens du paquet (RX ou TX).
		/// \param connId       Slot de connexion NetServer.
		/// \param opcode       Opcode applicatif (header v1).
		/// \param requestId    requestId du header.
		/// \param sessionId    sessionId du header (0 si pre-auth).
		/// \param payload      Pointeur sur le payload (peut etre nullptr
		///                     si payloadSize == 0).
		/// \param payloadSize  Taille reelle du payload (octets).
		/// \param nowUnixMs    Timestamp Unix en ms (UTC). Le caller le
		///                     fournit pour decoupler des appels systeme.
		void Append(PacketDirection dir,
		            uint32_t connId,
		            uint16_t opcode,
		            uint32_t requestId,
		            uint64_t sessionId,
		            const uint8_t* payload,
		            size_t payloadSize,
		            uint64_t nowUnixMs);

		/// Snapshot complet du buffer en ordre chronologique (entry la plus
		/// ancienne en [0], la plus recente en [size-1]). Renvoie un vector
		/// vide si jamais Append n'a ete appele. Thread-safe (verrou court,
		/// copie sous mutex).
		std::vector<PacketLogEntry> Drain() const;

		/// Identique a Drain mais filtre pour ne garder que les entries
		/// dont connId == \p connId. Utile pour dumper le contexte d'une
		/// connexion specifique en cas d'erreur.
		std::vector<PacketLogEntry> DrainForConn(uint32_t connId) const;

		/// Capacite maximale (immuable apres construction).
		size_t Capacity() const noexcept { return m_capacity; }

		/// Nombre courant d'entries dans le buffer (<= Capacity()).
		size_t Size() const;

	private:
		const size_t                 m_capacity;
		mutable std::mutex           m_mu;
		std::vector<PacketLogEntry>  m_ring;
		size_t                       m_writeIdx = 0;
		bool                         m_wrapped  = false;
	};

	/// Formatte une liste d'entries en texte multilignes pour log/dump.
	/// Chaque entry occupe une ligne au format :
	///
	///   [YYYY-MM-DD HH:MM:SS.mmm] DIR connId=X opcode=Y reqId=Z sessId=W
	///   size=N preview=AA BB CC ...
	///
	/// La conversion du timestamp utilise gmtime (UTC). Pas de localisation.
	/// Si \p entries est vide, retourne une chaine vide.
	std::string FormatEntries(const std::vector<PacketLogEntry>& entries);
}
