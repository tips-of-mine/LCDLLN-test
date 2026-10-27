#pragma once
// ETAPE3.4 (chantier 4.4) — canal fiable minimal sur l'UDP de jeu.
//
// Le flux UDP reste non fiable (snapshots, entrées, combat) ; certains messages ne
// doivent pourtant jamais se perdre : butin, inventaire, échange, enchère, courrier,
// chat, quêtes, équipement, demandes du joueur… Ils passent par ce canal :
//   - l'émetteur numérote chaque message (séquence 32 bits par sens) et l'emballe dans
//     un MessageKind::Reliable ; il le garde jusqu'à son accusé et le renvoie après un
//     délai qui suit le temps d'aller-retour mesuré (250 ms au départ, doublé à chaque
//     renvoi, 2 s au plus) ;
//   - le récepteur rend les messages DANS L'ORDRE, sans doublon ; il en garde au plus
//     kMaxOutOfOrder en avance d'un trou ; il accuse réception par lots
//     (MessageKind::ReliableAck : « tout jusqu'à N » + 64 bits pour les suivants),
//     au plus une fois toutes les kAckIntervalMs ;
//   - un message jamais accusé après kGiveUpMs (30 s), ou plus de kMaxInFlight messages en
//     attente d'accusé, met le canal en échec : l'appelant coupe la session.
//
// Aucun fragment : un message fiable tient dans un datagramme (le réassemblage, que
// City of Heroes signale lui-même comme exploitable, n'existe pas ici).
//
// Pas thread-safe (un canal par session, utilisé depuis un seul thread). Aucune
// dépendance au réseau : testable seul (cf. reliable_channel_tests).

#include "src/shared/network/ServerProtocol.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <vector>

namespace engine::network::reliable
{
	/// Vrai si un message de ce type doit passer par le canal fiable. Liste fermée,
	/// partagée par le client et le shard (cf. ReliableChannel.cpp).
	bool IsReliableKind(engine::server::MessageKind kind);

	/// Un canal fiable (les deux sens) entre le client et le shard.
	class ReliableChannel
	{
	public:
		/// Messages en attente d'accusé au plus. Au-delà : échec (pair muet ou saturé).
		static constexpr size_t kMaxInFlight = 512u;
		/// Messages gardés en avance d'un trou côté réception.
		static constexpr uint32_t kMaxOutOfOrder = 256u;
		/// Délai de renvoi initial, minimal et maximal (ms).
		static constexpr uint64_t kInitialRtoMs = 250u;
		static constexpr uint64_t kMinRtoMs = 100u;
		static constexpr uint64_t kMaxRtoMs = 2000u;
		/// Un message sans accusé depuis ce délai met le canal en échec (ms). Aligné sur
		/// l'éviction des clients UDP inactifs du shard (kUdpClientIdleTimeoutMs, 30 s) :
		/// un chargement long côté client (changement de zone) ne coupe pas plus tôt.
		static constexpr uint64_t kGiveUpMs = 30000u;
		/// Intervalle minimal entre deux accusés (ms) : les accusés sont groupés.
		static constexpr uint64_t kAckIntervalMs = 40u;
		/// Taille maximale d'un message fiable (paquet du protocole emballé) : le paquet
		/// Reliable (en-tête 8 + seq 4 + message) scellé (29 octets, ETAPE3.1) tient dans
		/// le tampon de réception du client (2048 octets). Au-delà de 1431 octets le
		/// datagramme dépasse 1472 et compte sur la fragmentation IP (comme avant ce
		/// canal pour les gros états shard → client : annuaire de guilde, liste d'amis…).
		/// Le shard ne reçoit que 1472 octets : les demandes du client restent petites.
		/// Un message plus gros est REFUSÉ (jamais envoyé hors canal : il doublerait
		/// les messages en attente de renvoi et casserait l'ordre).
		static constexpr size_t kMaxMessageSize = 2000u;
		/// Taille maximale d'un message fiable client → shard : l'enveloppe scellée tient
		/// dans le datagramme de 1472 octets que le shard reçoit (1472 - 29 - 12).
		static constexpr size_t kMaxClientToShardMessageSize = 1431u;

		/// Emballe \p packet (paquet complet du protocole de jeu) avec le numéro suivant
		/// et le garde jusqu'à son accusé. \p outDatagram reçoit le paquet Reliable à
		/// envoyer maintenant.
		/// \return false si le canal est en échec, plein, ou si le paquet est trop gros
		///         (rien n'est gardé ; l'appelant journalise et, si plein, coupe).
		bool Queue(std::span<const std::byte> packet, uint64_t nowMs, std::vector<std::byte>& outDatagram);

		/// Traite un paquet Reliable reçu. Les messages devenus livrables (dans l'ordre)
		/// sont AJOUTÉS à \p outDelivered (paquets du protocole tels qu'émis).
		/// \return false si le paquet est mal formé.
		bool OnReliable(std::span<const std::byte> packet, uint64_t nowMs,
			std::vector<std::vector<std::byte>>& outDelivered);

		/// Traite un paquet ReliableAck reçu. \return false s'il est mal formé.
		bool OnAck(std::span<const std::byte> packet, uint64_t nowMs);

		/// Renvois dus et accusé en attente : les paquets à envoyer sont AJOUTÉS à
		/// \p outDatagrams. À appeler régulièrement (chaque tick / chaque image).
		void Update(uint64_t nowMs, std::vector<std::vector<std::byte>>& outDatagrams);

		/// Vrai si le canal a abandonné (message sans accusé depuis kGiveUpMs, ou trop
		/// de messages en attente). La session doit être coupée.
		bool Failed() const { return m_failed; }

		/// Oublie tout (nouvelle session).
		void Reset();

		/// Messages en attente d'accusé (diagnostic).
		size_t InFlightCount() const { return m_inFlight.size(); }
		/// Renvois effectués (diagnostic).
		uint64_t RetransmitCount() const { return m_retransmits; }
		/// Temps d'aller-retour lissé (ms, 0 = pas encore mesuré).
		uint64_t SmoothedRttMs() const { return m_srttMs; }

	private:
		struct InFlight
		{
			uint32_t seq = 0;
			std::vector<std::byte> datagram; ///< paquet Reliable complet (prêt à renvoyer)
			uint64_t firstSentMs = 0;
			uint64_t lastSentMs = 0;
			uint64_t rtoMs = kInitialRtoMs;
			bool retransmitted = false;
		};

		void Acknowledge(uint32_t seq, uint64_t nowMs);
		uint64_t CurrentRto() const;

		// Émission.
		uint32_t m_nextSeq = 1;
		std::deque<InFlight> m_inFlight; ///< trié par seq croissante
		uint64_t m_srttMs = 0;
		uint64_t m_retransmits = 0;
		bool m_failed = false;

		// Réception.
		uint32_t m_delivered = 0; ///< tout jusqu'à ce numéro a été livré
		std::map<uint32_t, std::vector<std::byte>> m_outOfOrder;
		bool m_ackPending = false;
		uint64_t m_lastAckSentMs = 0;
	};
}
